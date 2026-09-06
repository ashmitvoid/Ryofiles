// SPDX-License-Identifier: GPL-3.0-only

#include "PreviewProtocol.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSizeF>

#include <poppler-qt6.h>

#include <algorithm>
#include <cmath>
#include <memory>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

QJsonObject response(
    const QString& id,
    bool ok,
    const QString& error = {},
    const QJsonObject& payload = {}) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), id);
    object.insert(QStringLiteral("ok"), ok);
    object.insert(QStringLiteral("error"), error);
    object.insert(QStringLiteral("payload"), payload);
    return object;
}

void writeResponse(QFile& output, const QJsonObject& object) {
    QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() + 1 > PreviewProtocol::kMaxProtocolBufferBytes) {
        const QString id = object.value(QStringLiteral("id")).toString();
        bytes = QJsonDocument(response(
            id,
            false,
            QStringLiteral("Preview response exceeded protocol limit")))
                    .toJson(QJsonDocument::Compact);
    }
    bytes.append('\n');
    output.write(bytes);
    output.flush();
}

#ifdef Q_OS_UNIX
QString errnoText(const char* action) {
    return QStringLiteral("%1: %2")
        .arg(QString::fromLatin1(action), QString::fromLocal8Bit(std::strerror(errno)));
}

QString kindForMode(mode_t mode) {
    if (S_ISREG(mode))
        return QStringLiteral("file");
    if (S_ISDIR(mode))
        return QStringLiteral("directory");
    if (S_ISLNK(mode))
        return QStringLiteral("symlink");
    return QStringLiteral("other");
}

qint64 mtimeNanoseconds(const struct stat& st) {
#ifdef Q_OS_LINUX
    return static_cast<qint64>(st.st_mtim.tv_sec) * 1'000'000'000LL
        + static_cast<qint64>(st.st_mtim.tv_nsec);
#else
    return static_cast<qint64>(st.st_mtime) * 1'000'000'000LL;
#endif
}

bool sameVersion(const struct stat& left, const struct stat& right) {
    return left.st_dev == right.st_dev
        && left.st_ino == right.st_ino
        && left.st_mode == right.st_mode
        && left.st_size == right.st_size
        && mtimeNanoseconds(left) == mtimeNanoseconds(right);
}

std::unique_ptr<QFile> openRegularNoFollow(
    const QString& path,
    struct stat* openedStat,
    QString* error) {
    const QByteArray encoded = QFile::encodeName(path);

    struct stat initial {};
    if (::lstat(encoded.constData(), &initial) != 0) {
        if (error)
            *error = errnoText("Could not inspect preview file");
        return {};
    }
    if (!S_ISREG(initial.st_mode)) {
        if (error)
            *error = QStringLiteral("Preview input is not a regular file");
        return {};
    }

    const int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (error)
            *error = errnoText("Could not open preview file");
        return {};
    }

    struct stat opened {};
    if (::fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        const int savedErrno = errno;
        ::close(fd);
        errno = savedErrno;
        if (error)
            *error = errnoText("Could not inspect opened preview file");
        return {};
    }

    struct stat live {};
    if (::lstat(encoded.constData(), &live) != 0 || !sameVersion(opened, live)) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Preview file changed while it was being opened");
        return {};
    }

    auto file = std::make_unique<QFile>();
    if (!file->open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Could not attach preview file descriptor");
        return {};
    }

    if (openedStat)
        *openedStat = opened;
    return file;
}

QJsonObject statPayload(const QString& path, QString* error) {
    const QByteArray encoded = QFile::encodeName(path);
    struct stat initial {};
    if (::lstat(encoded.constData(), &initial) != 0) {
        if (error)
            *error = errnoText("Could not inspect preview path");
        return {};
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("kind"), kindForMode(initial.st_mode));
    payload.insert(QStringLiteral("device"), QString::number(static_cast<qulonglong>(initial.st_dev)));
    payload.insert(QStringLiteral("inode"), QString::number(static_cast<qulonglong>(initial.st_ino)));
    payload.insert(QStringLiteral("size"), QString::number(static_cast<qlonglong>(initial.st_size)));
    payload.insert(QStringLiteral("mtimeNs"), QString::number(mtimeNanoseconds(initial)));
    payload.insert(QStringLiteral("mode"), static_cast<int>(initial.st_mode & 07777));

    if (S_ISLNK(initial.st_mode) || S_ISDIR(initial.st_mode) || !S_ISREG(initial.st_mode))
        return payload;

    struct stat opened {};
    std::unique_ptr<QFile> file = openRegularNoFollow(path, &opened, error);
    if (!file)
        return {};

    payload.insert(QStringLiteral("device"), QString::number(static_cast<qulonglong>(opened.st_dev)));
    payload.insert(QStringLiteral("inode"), QString::number(static_cast<qulonglong>(opened.st_ino)));
    payload.insert(QStringLiteral("size"), QString::number(static_cast<qlonglong>(opened.st_size)));
    payload.insert(QStringLiteral("mtimeNs"), QString::number(mtimeNanoseconds(opened)));

    QMimeDatabase mimeDatabase;
    payload.insert(
        QStringLiteral("mime"),
        mimeDatabase.mimeTypeForFile(path, QMimeDatabase::MatchExtension).name());
    return payload;
}

QByteArray encodePngBounded(QImage* image, QString* error) {
    if (!image || image->isNull()) {
        if (error)
            *error = QStringLiteral("PDF page did not render an image");
        return {};
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image->save(&buffer, "PNG")) {
            if (error)
                *error = QStringLiteral("Could not encode PDF preview image");
            return {};
        }
        if (encoded.size() <= PreviewProtocol::kMaxEncodedImageBytes)
            return encoded;

        if (image->width() <= 320 || image->height() <= 320)
            break;

        const int nextWidth = std::max(320, qRound(image->width() * 0.80));
        const int nextHeight = std::max(320, qRound(image->height() * 0.80));
        *image = image->scaled(
            nextWidth,
            nextHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    if (error)
        *error = QStringLiteral("PDF preview image exceeded the output limit");
    return {};
}

QJsonObject pdfPagePayload(const QString& path, const QJsonObject& request, QString* error) {
    struct stat opened {};
    std::unique_ptr<QFile> file = openRegularNoFollow(path, &opened, error);
    if (!file)
        return {};
    if (opened.st_size < 0 || opened.st_size > PreviewProtocol::kMaxPdfInputBytes) {
        if (error)
            *error = QStringLiteral("PDF exceeds the preview input limit");
        return {};
    }

    std::unique_ptr<Poppler::Document> document = Poppler::Document::load(file.get());
    if (!document) {
        if (error)
            *error = QStringLiteral("Could not read PDF document");
        return {};
    }
    if (document->isLocked()) {
        if (error)
            *error = QStringLiteral("PDF requires a password");
        return {};
    }

    const int pageCount = document->numPages();
    if (pageCount <= 0) {
        if (error)
            *error = QStringLiteral("PDF has no renderable pages");
        return {};
    }

    const int pageIndex = request.value(QStringLiteral("page")).toInt(0);
    if (pageIndex < 0 || pageIndex >= pageCount) {
        if (error)
            *error = QStringLiteral("Requested PDF page is out of range");
        return {};
    }

    const int maxWidth = std::clamp(
        request.value(QStringLiteral("maxWidth")).toInt(PreviewProtocol::kDefaultPdfRenderWidth),
        128,
        PreviewProtocol::kMaxPdfRenderDimension);
    const int maxHeight = std::clamp(
        request.value(QStringLiteral("maxHeight")).toInt(PreviewProtocol::kDefaultPdfRenderHeight),
        128,
        PreviewProtocol::kMaxPdfRenderDimension);

    std::unique_ptr<Poppler::Page> page = document->page(pageIndex);
    if (!page) {
        if (error)
            *error = QStringLiteral("Could not load PDF page");
        return {};
    }

    const QSizeF points = page->pageSizeF();
    if (points.width() <= 0.0 || points.height() <= 0.0) {
        if (error)
            *error = QStringLiteral("PDF page has invalid dimensions");
        return {};
    }

    document->setPaperColor(Qt::white);
    document->setRenderHint(Poppler::Document::Antialiasing, true);
    document->setRenderHint(Poppler::Document::TextAntialiasing, true);

    const double scale = std::min(
        static_cast<double>(maxWidth) / points.width(),
        static_cast<double>(maxHeight) / points.height());
    const double dpi = std::clamp(72.0 * scale, 36.0, 216.0);

    QImage image = page->renderToImage(dpi, dpi);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("Could not render PDF page");
        return {};
    }
    if (image.width() > maxWidth || image.height() > maxHeight) {
        image = image.scaled(
            maxWidth,
            maxHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }
    if (image.width() > PreviewProtocol::kMaxPublishedImageDimension
        || image.height() > PreviewProtocol::kMaxPublishedImageDimension
        || image.sizeInBytes() > PreviewProtocol::kMaxPublishedImageBytes) {
        if (error)
            *error = QStringLiteral("PDF render exceeded the decoded-image limit");
        return {};
    }

    QByteArray png = encodePngBounded(&image, error);
    if (png.isEmpty())
        return {};

    QJsonObject payload;
    payload.insert(QStringLiteral("page"), pageIndex);
    payload.insert(QStringLiteral("pageCount"), pageCount);
    payload.insert(QStringLiteral("pixelWidth"), image.width());
    payload.insert(QStringLiteral("pixelHeight"), image.height());
    payload.insert(QStringLiteral("imageFormat"), QStringLiteral("png"));
    payload.insert(QStringLiteral("imageBase64"), QString::fromLatin1(png.toBase64()));
    payload.insert(QStringLiteral("title"), document->title());
    payload.insert(QStringLiteral("author"), document->author());
    payload.insert(QStringLiteral("subject"), document->subject());
    payload.insert(QStringLiteral("keywords"), document->keywords());
    payload.insert(QStringLiteral("fileSize"), QString::number(static_cast<qlonglong>(opened.st_size)));
    return payload;
}
#endif

QJsonObject handleRequest(const QJsonObject& request) {
    const QString id = request.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return response({}, false, QStringLiteral("Preview request is missing an id"));

    const QString op = request.value(QStringLiteral("op")).toString();
    const QString path = request.value(QStringLiteral("path")).toString();
    if (path.isEmpty() || path.contains(QChar::Null) || !QFileInfo(path).isAbsolute())
        return response(id, false, QStringLiteral("Preview path must be an absolute local path"));

#ifdef Q_OS_UNIX
    QString error;
    if (op == QStringLiteral("stat")) {
        const QJsonObject payload = statPayload(path, &error);
        if (!error.isEmpty())
            return response(id, false, error);
        return response(id, true, {}, payload);
    }

    if (op == QStringLiteral("pdf-page")) {
        const QJsonObject payload = pdfPagePayload(path, request, &error);
        if (!error.isEmpty())
            return response(id, false, error);
        return response(id, true, {}, payload);
    }

    return response(id, false, QStringLiteral("Unsupported preview operation"));
#else
    Q_UNUSED(path);
    Q_UNUSED(op);
    return response(id, false, QStringLiteral("Preview helper currently requires Unix no-follow file APIs"));
#endif
}

void applyResourceLimits() {
#ifdef Q_OS_UNIX
#ifdef RLIMIT_AS
    const rlim_t addressSpace = static_cast<rlim_t>(768) * 1024 * 1024;
    const struct rlimit addressLimit {addressSpace, addressSpace};
    ::setrlimit(RLIMIT_AS, &addressLimit);
#endif
    const struct rlimit coreLimit {0, 0};
    ::setrlimit(RLIMIT_CORE, &coreLimit);
#endif
}

void drainOversizedLine(QFile& input, QByteArray current) {
    while (!current.endsWith('\n') && !input.atEnd())
        current = input.readLine(PreviewProtocol::kMaxRequestLineBytes + 1);
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ryofiles-preview-helper"));
    applyResourceLimits();

    QFile input;
    QFile output;
    if (!input.open(stdin, QIODevice::ReadOnly) || !output.open(stdout, QIODevice::WriteOnly))
        return 2;

    while (true) {
        QByteArray line = input.readLine(PreviewProtocol::kMaxRequestLineBytes + 1);
        if (line.isEmpty() && input.atEnd())
            break;

        if (!line.endsWith('\n') && line.size() > PreviewProtocol::kMaxRequestLineBytes) {
            drainOversizedLine(input, line);
            writeResponse(output, response({}, false, QStringLiteral("Preview request exceeded protocol limit")));
            continue;
        }

        if (line.endsWith('\n'))
            line.chop(1);
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            writeResponse(output, response({}, false, QStringLiteral("Malformed preview request")));
            continue;
        }

        writeResponse(output, handleRequest(document.object()));
    }

    return 0;
}
