// SPDX-License-Identifier: GPL-3.0-only

#include "PreviewProtocol.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>

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

    const int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (error)
            *error = errnoText("Could not open preview file");
        return {};
    }

    struct stat opened {};
    const bool openedOk = ::fstat(fd, &opened) == 0;
    const int savedErrno = errno;
    ::close(fd);
    errno = savedErrno;
    if (!openedOk) {
        if (error)
            *error = errnoText("Could not inspect opened preview file");
        return {};
    }

    struct stat live {};
    if (::lstat(encoded.constData(), &live) != 0 || !sameVersion(opened, live)) {
        if (error)
            *error = QStringLiteral("Preview file changed while it was being inspected");
        return {};
    }

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
#endif

QJsonObject handleRequest(const QJsonObject& request) {
    const QString id = request.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return response({}, false, QStringLiteral("Preview request is missing an id"));

    const QString op = request.value(QStringLiteral("op")).toString();
    if (op != QStringLiteral("stat"))
        return response(id, false, QStringLiteral("Unsupported preview operation"));

    const QString path = request.value(QStringLiteral("path")).toString();
    if (path.isEmpty() || path.contains(QChar::Null) || !QFileInfo(path).isAbsolute())
        return response(id, false, QStringLiteral("Preview path must be an absolute local path"));

#ifdef Q_OS_UNIX
    QString error;
    const QJsonObject payload = statPayload(path, &error);
    if (!error.isEmpty())
        return response(id, false, error);
    return response(id, true, {}, payload);
#else
    Q_UNUSED(path);
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
