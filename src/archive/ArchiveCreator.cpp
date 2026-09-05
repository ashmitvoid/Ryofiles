// SPDX-License-Identifier: GPL-3.0-only

#include "ArchiveCreator.hpp"
#include "locations/LocalPathGuard.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif

namespace {

constexpr qsizetype kReadBufferSize = 128 * 1024;
constexpr quint64 kProgressByteInterval = 4ULL * 1024ULL * 1024ULL;

enum class WriterFormat {
    Tar,
    TarGzip,
    TarXz,
    TarZstd,
    Zip,
    SevenZip,
};

struct CreationContext {
    struct archive* writer = nullptr;
    const std::atomic_bool& cancelRequested;
    const ArchiveCreator::ProgressCallback& progress;
    quint64 entriesWritten = 0;
    quint64 bytesRead = 0;
    quint64 lastProgressBytes = 0;
    bool cancelled = false;
};

QString archiveError(struct archive* archiveHandle, const QString& fallback) {
    if (!archiveHandle)
        return fallback;
    const char* message = archive_error_string(archiveHandle);
    return message && *message
        ? QString::fromLocal8Bit(message)
        : fallback;
}

bool archiveOk(int status) {
    return status >= ARCHIVE_OK;
}

bool cancelled(CreationContext& context, QString* error) {
    if (!context.cancelRequested.load(std::memory_order_relaxed))
        return false;
    context.cancelled = true;
    if (error)
        *error = QStringLiteral("Cancelled");
    return true;
}

void reportProgress(
    CreationContext& context,
    const QString& sourcePath,
    const QString& entryPath,
    bool force) {
    if (!context.progress)
        return;

    if (!force
        && context.bytesRead - context.lastProgressBytes < kProgressByteInterval) {
        return;
    }

    context.lastProgressBytes = context.bytesRead;
    context.progress({
        sourcePath,
        entryPath,
        context.entriesWritten,
        context.bytesRead,
    });
}

bool detectFormat(const QString& archivePath, WriterFormat* format) {
    const QString name = QFileInfo(archivePath).fileName().toLower();
    if (name.endsWith(QStringLiteral(".tar.gz"))
        || name.endsWith(QStringLiteral(".tgz"))) {
        *format = WriterFormat::TarGzip;
        return true;
    }
    if (name.endsWith(QStringLiteral(".tar.xz"))) {
        *format = WriterFormat::TarXz;
        return true;
    }
    if (name.endsWith(QStringLiteral(".tar.zst"))) {
        *format = WriterFormat::TarZstd;
        return true;
    }
    if (name.endsWith(QStringLiteral(".tar"))) {
        *format = WriterFormat::Tar;
        return true;
    }
    if (name.endsWith(QStringLiteral(".zip"))) {
        *format = WriterFormat::Zip;
        return true;
    }
    if (name.endsWith(QStringLiteral(".7z"))) {
        *format = WriterFormat::SevenZip;
        return true;
    }
    return false;
}

bool configureWriter(
    struct archive* writer,
    WriterFormat format,
    QString* error) {
    int status = ARCHIVE_FATAL;
    switch (format) {
    case WriterFormat::Tar:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    case WriterFormat::TarGzip:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_gzip(writer);
        break;
    case WriterFormat::TarXz:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_xz(writer);
        break;
    case WriterFormat::TarZstd:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_zstd(writer);
        break;
    case WriterFormat::Zip:
        status = archive_write_set_format_zip(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    case WriterFormat::SevenZip:
        status = archive_write_set_format_7zip(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    }

    if (archiveOk(status))
        return true;

    if (error)
        *error = archiveError(writer, QStringLiteral("Could not configure archive writer"));
    return false;
}

bool lstatPath(const QString& path, struct stat* info, QString* error) {
    const QByteArray encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), info) == 0)
        return true;

    if (error) {
        *error = QStringLiteral("Could not inspect source %1: %2")
                     .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
    }
    return false;
}

bool readSymlinkTarget(const QString& path, QByteArray* target, QString* error) {
    const QByteArray encoded = QFile::encodeName(path);
    QByteArray buffer(256, '\0');

    while (buffer.size() <= 1024 * 1024) {
        const ssize_t length = ::readlink(
            encoded.constData(),
            buffer.data(),
            static_cast<size_t>(buffer.size()));
        if (length < 0) {
            if (error) {
                *error = QStringLiteral("Could not read symbolic link %1: %2")
                             .arg(path, QString::fromLocal8Bit(std::strerror(errno)));
            }
            return false;
        }
        if (length < buffer.size()) {
            buffer.resize(static_cast<qsizetype>(length));
            *target = buffer;
            return true;
        }
        buffer.resize(buffer.size() * 2);
    }

    if (error)
        *error = QStringLiteral("Symbolic link target is unexpectedly large: %1").arg(path);
    return false;
}

bool writeHeader(
    CreationContext& context,
    const QString& entryPath,
    const struct stat& info,
    const QByteArray& symlinkTarget,
    QString* error) {
    struct archive_entry* entry = archive_entry_new();
    if (!entry) {
        if (error)
            *error = QStringLiteral("Could not allocate archive entry");
        return false;
    }

    archive_entry_copy_stat(entry, &info);
    const QByteArray encodedEntry = entryPath.toUtf8();
    archive_entry_set_pathname(entry, encodedEntry.constData());

    if (S_ISDIR(info.st_mode)) {
        archive_entry_set_size(entry, 0);
    } else if (S_ISLNK(info.st_mode)) {
        archive_entry_set_size(entry, 0);
        archive_entry_set_symlink(entry, symlinkTarget.constData());
    }

    const int status = archive_write_header(context.writer, entry);
    archive_entry_free(entry);
    if (archiveOk(status))
        return true;

    if (error) {
        *error = archiveError(
            context.writer,
            QStringLiteral("Could not write archive entry %1").arg(entryPath));
    }
    return false;
}

bool finishEntry(
    CreationContext& context,
    const QString& sourcePath,
    const QString& entryPath,
    QString* error) {
    const int status = archive_write_finish_entry(context.writer);
    if (!archiveOk(status)) {
        if (error)
            *error = archiveError(
                context.writer,
                QStringLiteral("Could not finish archive entry %1").arg(entryPath));
        return false;
    }

    ++context.entriesWritten;
    reportProgress(context, sourcePath, entryPath, true);
    return true;
}

bool writeRegularFile(
    CreationContext& context,
    const QString& sourcePath,
    const QString& entryPath,
    QString* error) {
    if (cancelled(context, error))
        return false;

    const QByteArray encoded = QFile::encodeName(sourcePath);
    const int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (error) {
            *error = QStringLiteral("Could not open source %1: %2")
                         .arg(sourcePath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    struct stat info {};
    if (::fstat(fd, &info) != 0) {
        const int savedErrno = errno;
        ::close(fd);
        if (error) {
            *error = QStringLiteral("Could not inspect opened source %1: %2")
                         .arg(sourcePath, QString::fromLocal8Bit(std::strerror(savedErrno)));
        }
        return false;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0) {
        ::close(fd);
        if (error)
            *error = QStringLiteral("Source changed while creating archive: %1").arg(sourcePath);
        return false;
    }

    if (!writeHeader(context, entryPath, info, {}, error)) {
        ::close(fd);
        return false;
    }

    QByteArray buffer(kReadBufferSize, '\0');
    qint64 remaining = static_cast<qint64>(info.st_size);

    while (remaining > 0) {
        if (cancelled(context, error)) {
            ::close(fd);
            return false;
        }

        const size_t request = static_cast<size_t>(qMin<qint64>(remaining, buffer.size()));
        ssize_t bytes = -1;
        do {
            bytes = ::read(fd, buffer.data(), request);
        } while (bytes < 0 && errno == EINTR);

        if (bytes <= 0) {
            const int savedErrno = errno;
            ::close(fd);
            if (error) {
                *error = bytes == 0
                    ? QStringLiteral("Source shrank while creating archive: %1").arg(sourcePath)
                    : QStringLiteral("Could not read source %1: %2")
                          .arg(sourcePath, QString::fromLocal8Bit(std::strerror(savedErrno)));
            }
            return false;
        }

        la_ssize_t offset = 0;
        while (offset < bytes) {
            const la_ssize_t written = archive_write_data(
                context.writer,
                buffer.constData() + offset,
                static_cast<size_t>(bytes - offset));
            if (written <= 0) {
                ::close(fd);
                if (error) {
                    *error = archiveError(
                        context.writer,
                        QStringLiteral("Could not write data for %1").arg(entryPath));
                }
                return false;
            }
            offset += written;
        }

        remaining -= bytes;
        context.bytesRead += static_cast<quint64>(bytes);
        reportProgress(context, sourcePath, entryPath, false);
    }

    ::close(fd);
    return finishEntry(context, sourcePath, entryPath, error);
}

bool writePath(
    CreationContext& context,
    const QString& sourcePath,
    const QString& entryPath,
    QString* error) {
    if (cancelled(context, error))
        return false;

    struct stat info {};
    if (!lstatPath(sourcePath, &info, error))
        return false;

    if (S_ISREG(info.st_mode))
        return writeRegularFile(context, sourcePath, entryPath, error);

    if (S_ISLNK(info.st_mode)) {
        QByteArray target;
        if (!readSymlinkTarget(sourcePath, &target, error))
            return false;
        if (!writeHeader(context, entryPath, info, target, error))
            return false;
        return finishEntry(context, sourcePath, entryPath, error);
    }

    if (!S_ISDIR(info.st_mode)) {
        if (error)
            *error = QStringLiteral("Unsupported filesystem entry: %1").arg(sourcePath);
        return false;
    }

    if (!writeHeader(context, entryPath, info, {}, error))
        return false;
    if (!finishEntry(context, sourcePath, entryPath, error))
        return false;

    const QFileInfoList children = QDir(sourcePath).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name | QDir::DirsFirst);

    for (const QFileInfo& child : children) {
        if (cancelled(context, error))
            return false;

        const QString childName = child.fileName();
        if (childName.isEmpty()
            || childName == QStringLiteral(".")
            || childName == QStringLiteral("..")) {
            if (error)
                *error = QStringLiteral("Unsafe filesystem entry below %1").arg(sourcePath);
            return false;
        }

        if (!writePath(
                context,
                QDir(sourcePath).filePath(childName),
                entryPath + QLatin1Char('/') + childName,
                error)) {
            return false;
        }
    }

    return true;
}

bool destinationInsideDirectorySource(
    const QString& sourcePath,
    const QString& finalArchivePath) {
    struct stat sourceInfo {};
    if (!lstatPath(sourcePath, &sourceInfo, nullptr)
        || !S_ISDIR(sourceInfo.st_mode)) {
        return false;
    }

    const QString canonicalSource = QFileInfo(sourcePath).canonicalFilePath();
    const QFileInfo finalInfo(finalArchivePath);
    const QString canonicalParent = QFileInfo(finalInfo.absolutePath()).canonicalFilePath();
    if (canonicalSource.isEmpty() || canonicalParent.isEmpty())
        return true;

    const QString candidate = QDir::cleanPath(
        QDir(canonicalParent).filePath(finalInfo.fileName()));
    return candidate == canonicalSource
        || candidate.startsWith(canonicalSource + QDir::separator());
}

QString uniqueTemporaryPath(const QFileInfo& finalInfo) {
    const QDir parent(finalInfo.absolutePath());
    for (int attempt = 0; attempt < 16; ++attempt) {
        const QString candidate = parent.filePath(
            QStringLiteral(".%1.ryofiles-archive-%2.tmp")
                .arg(
                    finalInfo.fileName(),
                    QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const QFileInfo info(candidate);
        if (!info.exists() && !info.isSymLink())
            return candidate;
    }
    return {};
}

bool publishNoReplace(
    const QString& temporaryPath,
    const QString& finalPath,
    QString* error) {
    const QByteArray temporary = QFile::encodeName(temporaryPath);
    const QByteArray final = QFile::encodeName(finalPath);

#ifdef SYS_renameat2
    if (::syscall(
            SYS_renameat2,
            AT_FDCWD,
            temporary.constData(),
            AT_FDCWD,
            final.constData(),
            RENAME_NOREPLACE) == 0) {
        return true;
    }

    if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) {
        if (error) {
            *error = errno == EEXIST
                ? QStringLiteral("Archive already exists: %1").arg(finalPath)
                : QStringLiteral("Could not publish archive %1: %2")
                      .arg(finalPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
#endif

    if (::link(temporary.constData(), final.constData()) != 0) {
        if (error) {
            *error = errno == EEXIST
                ? QStringLiteral("Archive already exists: %1").arg(finalPath)
                : QStringLiteral("Could not publish archive %1 without overwriting: %2")
                      .arg(finalPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    if (::unlink(temporary.constData()) == 0)
        return true;

    const int savedErrno = errno;
    ::unlink(final.constData());
    if (error) {
        *error = QStringLiteral("Could not finish archive publication %1: %2")
                     .arg(finalPath, QString::fromLocal8Bit(std::strerror(savedErrno)));
    }
    return false;
}

ArchiveCreationResult failedResult(const QString& error) {
    ArchiveCreationResult result;
    result.status = ArchiveCreationStatus::Failed;
    result.error = error;
    return result;
}

} // namespace

ArchiveCreationResult ArchiveCreator::create(
    const QStringList& requestedSources,
    const QString& requestedArchivePath,
    const std::atomic_bool& cancelRequested,
    const ProgressCallback& progress) {
    if (requestedSources.isEmpty())
        return failedResult(QStringLiteral("No archive sources were provided"));
    if (!LocalPathGuard::allLocalPaths(requestedSources)
        || requestedArchivePath.trimmed().isEmpty()
        || LocalPathGuard::isUriLike(requestedArchivePath)) {
        return failedResult(QStringLiteral("Archive creation requires local filesystem paths"));
    }

    const QFileInfo finalInfo(QFileInfo(requestedArchivePath).absoluteFilePath());
    if (finalInfo.fileName().isEmpty())
        return failedResult(QStringLiteral("Archive output name is empty"));

    WriterFormat format;
    if (!detectFormat(finalInfo.absoluteFilePath(), &format))
        return failedResult(QStringLiteral("Unsupported archive format: %1").arg(finalInfo.fileName()));

    const QFileInfo parentInfo(finalInfo.absolutePath());
    if (!parentInfo.exists() || !parentInfo.isDir() || parentInfo.isSymLink())
        return failedResult(QStringLiteral("Archive destination folder is not a safe local directory"));
    if (finalInfo.exists() || finalInfo.isSymLink())
        return failedResult(QStringLiteral("Archive already exists: %1").arg(finalInfo.absoluteFilePath()));

    QStringList sources;
    sources.reserve(requestedSources.size());
    QSet<QString> topLevelNames;

    for (const QString& requestedSource : requestedSources) {
        if (requestedSource.trimmed().isEmpty())
            return failedResult(QStringLiteral("Archive source path is empty"));

        const QFileInfo sourceInfo(requestedSource);
        const QString sourcePath = QDir::cleanPath(sourceInfo.absoluteFilePath());
        struct stat info {};
        QString inspectError;
        if (!lstatPath(sourcePath, &info, &inspectError))
            return failedResult(inspectError);
        if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode))
            return failedResult(QStringLiteral("Unsupported filesystem entry: %1").arg(sourcePath));

        const QString leaf = QFileInfo(sourcePath).fileName();
        if (leaf.isEmpty()
            || leaf == QStringLiteral(".")
            || leaf == QStringLiteral("..")) {
            return failedResult(QStringLiteral("Archive source has no safe top-level name: %1").arg(sourcePath));
        }
        if (topLevelNames.contains(leaf))
            return failedResult(QStringLiteral("Multiple selected sources have the same archive name: %1").arg(leaf));
        topLevelNames.insert(leaf);

        if (S_ISDIR(info.st_mode)
            && destinationInsideDirectorySource(sourcePath, finalInfo.absoluteFilePath())) {
            return failedResult(QStringLiteral("Archive output cannot be inside selected directory: %1").arg(sourcePath));
        }

        sources.push_back(sourcePath);
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        ArchiveCreationResult result;
        result.status = ArchiveCreationStatus::Cancelled;
        return result;
    }

    const QString temporaryPath = uniqueTemporaryPath(finalInfo);
    if (temporaryPath.isEmpty())
        return failedResult(QStringLiteral("Could not allocate a temporary archive path"));

    struct archive* writer = archive_write_new();
    if (!writer)
        return failedResult(QStringLiteral("Could not allocate archive writer"));

    QString error;
    if (!configureWriter(writer, format, &error)) {
        archive_write_free(writer);
        return failedResult(error);
    }

    const QByteArray encodedTemporary = QFile::encodeName(temporaryPath);
    if (!archiveOk(archive_write_open_filename(writer, encodedTemporary.constData()))) {
        error = archiveError(writer, QStringLiteral("Could not create temporary archive"));
        archive_write_free(writer);
        QFile::remove(temporaryPath);
        return failedResult(error);
    }

    CreationContext context {writer, cancelRequested, progress};
    bool success = true;
    for (const QString& sourcePath : sources) {
        if (!writePath(
                context,
                sourcePath,
                QFileInfo(sourcePath).fileName(),
                &error)) {
            success = false;
            break;
        }
    }

    const int closeStatus = archive_write_close(writer);
    if (success && !archiveOk(closeStatus)) {
        success = false;
        error = archiveError(writer, QStringLiteral("Could not finalize archive stream"));
    }
    archive_write_free(writer);

    ArchiveCreationResult result;
    result.entriesWritten = context.entriesWritten;
    result.bytesRead = context.bytesRead;

    if (!success) {
        QFile::remove(temporaryPath);
        result.status = context.cancelled
            ? ArchiveCreationStatus::Cancelled
            : ArchiveCreationStatus::Failed;
        if (!context.cancelled)
            result.error = error;
        return result;
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        QFile::remove(temporaryPath);
        result.status = ArchiveCreationStatus::Cancelled;
        return result;
    }

    if (!publishNoReplace(temporaryPath, finalInfo.absoluteFilePath(), &error)) {
        QFile::remove(temporaryPath);
        result.status = ArchiveCreationStatus::Failed;
        result.error = error;
        return result;
    }

    result.status = ArchiveCreationStatus::Success;
    return result;
}
