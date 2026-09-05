// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchiveExtractor.hpp"

#include "archive/ArchivePathGuard.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace {

class FileDescriptor final {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) : m_fd(fd) {}
    ~FileDescriptor() {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : m_fd(other.m_fd) {
        other.m_fd = -1;
    }

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this == &other)
            return *this;
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = other.m_fd;
        other.m_fd = -1;
        return *this;
    }

    int get() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }

private:
    int m_fd = -1;
};

struct CreatedPath {
    QString path;
    bool directory = false;
};

struct PendingHardlink {
    QString destination;
    QString target;
};

struct ParentLookup {
    FileDescriptor fd;
    QByteArray leaf;
    QString error;

    bool ok() const { return fd.valid() && !leaf.isEmpty(); }
};

QString systemError(const QString& action, const QString& path) {
    return QStringLiteral("%1 '%2': %3")
        .arg(action, path, QString::fromLocal8Bit(std::strerror(errno)));
}

QString archiveFailure(struct archive* reader, const QString& action) {
    const char* raw = archive_error_string(reader);
    return raw
        ? QStringLiteral("%1: %2").arg(action, QString::fromLocal8Bit(raw))
        : action;
}

bool decodeArchiveText(const char* raw, QString* decoded) {
    if (!raw || !decoded)
        return false;

    const QByteArray bytes(raw);
    const QString value = QString::fromUtf8(bytes);
    if (value.toUtf8() != bytes)
        return false;

    *decoded = value;
    return true;
}

void recordCreated(
    const QString& path,
    bool directory,
    QVector<CreatedPath>* created,
    QSet<QString>* recorded) {
    if (!created || !recorded || recorded->contains(path))
        return;
    recorded->insert(path);
    created->push_back({path, directory});
}

FileDescriptor duplicateDescriptor(int fd) {
    return FileDescriptor(::fcntl(fd, F_DUPFD_CLOEXEC, 3));
}

ParentLookup openParentDirectory(
    int rootFd,
    const QString& normalizedPath,
    bool createParents,
    QVector<CreatedPath>* created,
    QSet<QString>* recorded) {
    ParentLookup result;
    QStringList components = normalizedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.isEmpty()) {
        result.error = QStringLiteral("Archive destination path is empty");
        return result;
    }

    result.leaf = components.takeLast().toUtf8();
    FileDescriptor current = duplicateDescriptor(rootFd);
    if (!current.valid()) {
        result.error = systemError(QStringLiteral("Could not duplicate extraction root descriptor"), normalizedPath);
        return result;
    }

    QString prefix;
    for (const QString& component : components) {
        prefix = prefix.isEmpty() ? component : prefix + QLatin1Char('/') + component;
        const QByteArray encoded = component.toUtf8();

        int nextFd = ::openat(
            current.get(),
            encoded.constData(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

        if (nextFd < 0 && errno == ENOENT && createParents) {
            const bool createdDirectory =
                ::mkdirat(current.get(), encoded.constData(), 0755) == 0;
            if (!createdDirectory && errno != EEXIST) {
                result.error = systemError(QStringLiteral("Could not create extraction directory"), prefix);
                return result;
            }
            if (createdDirectory)
                recordCreated(prefix, true, created, recorded);

            nextFd = ::openat(
                current.get(),
                encoded.constData(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        }

        if (nextFd < 0) {
            result.error = systemError(QStringLiteral("Could not open extraction directory without following symlinks"), prefix);
            return result;
        }
        current = FileDescriptor(nextFd);
    }

    result.fd = std::move(current);
    return result;
}

void rollbackCreated(int rootFd, const QVector<CreatedPath>& created) {
    for (auto it = created.crbegin(); it != created.crend(); ++it) {
        ParentLookup parent = openParentDirectory(rootFd, it->path, false, nullptr, nullptr);
        if (!parent.ok())
            continue;
        ::unlinkat(
            parent.fd.get(),
            parent.leaf.constData(),
            it->directory ? AT_REMOVEDIR : 0);
    }
}

ArchiveExtractionResult failedResult(
    int rootFd,
    const QVector<CreatedPath>& created,
    const ArchiveExtractionResult& partial,
    const QString& error) {
    rollbackCreated(rootFd, created);
    ArchiveExtractionResult result = partial;
    result.status = ArchiveExtractionStatus::Failed;
    result.error = error;
    return result;
}

ArchiveExtractionResult cancelledResult(
    int rootFd,
    const QVector<CreatedPath>& created,
    const ArchiveExtractionResult& partial) {
    rollbackCreated(rootFd, created);
    ArchiveExtractionResult result = partial;
    result.status = ArchiveExtractionStatus::Cancelled;
    result.error.clear();
    return result;
}

bool createDirectoryEntry(
    int rootFd,
    const QString& normalizedPath,
    QVector<CreatedPath>* created,
    QSet<QString>* recorded,
    QString* error) {
    ParentLookup parent = openParentDirectory(rootFd, normalizedPath, true, created, recorded);
    if (!parent.ok()) {
        if (error)
            *error = parent.error;
        return false;
    }

    if (::mkdirat(parent.fd.get(), parent.leaf.constData(), 0755) == 0) {
        recordCreated(normalizedPath, true, created, recorded);
        return true;
    }

    if (errno != EEXIST) {
        if (error)
            *error = systemError(QStringLiteral("Could not create archive directory"), normalizedPath);
        return false;
    }

    struct stat existing {};
    if (::fstatat(parent.fd.get(), parent.leaf.constData(), &existing, AT_SYMLINK_NOFOLLOW) < 0) {
        if (error)
            *error = systemError(QStringLiteral("Could not inspect existing archive destination"), normalizedPath);
        return false;
    }
    if (!S_ISDIR(existing.st_mode)) {
        if (error)
            *error = QStringLiteral("Archive directory would overwrite an existing non-directory: %1")
                         .arg(normalizedPath);
        return false;
    }
    return true;
}

bool createSymlinkEntry(
    int rootFd,
    const QString& normalizedPath,
    const QString& target,
    QVector<CreatedPath>* created,
    QSet<QString>* recorded,
    QString* error) {
    ParentLookup parent = openParentDirectory(rootFd, normalizedPath, true, created, recorded);
    if (!parent.ok()) {
        if (error)
            *error = parent.error;
        return false;
    }

    const QByteArray encodedTarget = target.toUtf8();
    if (::symlinkat(encodedTarget.constData(), parent.fd.get(), parent.leaf.constData()) < 0) {
        if (error)
            *error = systemError(QStringLiteral("Could not create archive symlink without overwrite"), normalizedPath);
        return false;
    }
    recordCreated(normalizedPath, false, created, recorded);
    return true;
}

bool writeAllAt(int fd, const void* buffer, size_t size, off_t offset, QString* error) {
    const char* cursor = static_cast<const char*>(buffer);
    size_t remaining = size;
    off_t currentOffset = offset;

    while (remaining > 0) {
        const ssize_t written = ::pwrite(fd, cursor, remaining, currentOffset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            if (error)
                *error = QStringLiteral("Could not write extracted file: %1")
                             .arg(QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        if (written == 0) {
            if (error)
                *error = QStringLiteral("Could not write extracted file: short write");
            return false;
        }
        cursor += written;
        remaining -= static_cast<size_t>(written);
        currentOffset += written;
    }
    return true;
}

bool createHardlinkEntry(
    int rootFd,
    const QString& destination,
    const QString& target,
    QVector<CreatedPath>* created,
    QSet<QString>* recorded,
    QString* error) {
    ParentLookup sourceParent = openParentDirectory(rootFd, target, false, nullptr, nullptr);
    if (!sourceParent.ok()) {
        if (error)
            *error = QStringLiteral("Archive hardlink target is unavailable: %1").arg(target);
        return false;
    }

    struct stat sourceInfo {};
    if (::fstatat(
            sourceParent.fd.get(),
            sourceParent.leaf.constData(),
            &sourceInfo,
            AT_SYMLINK_NOFOLLOW) < 0
        || !S_ISREG(sourceInfo.st_mode)) {
        if (error)
            *error = QStringLiteral("Archive hardlink target is not a regular extracted file: %1")
                         .arg(target);
        return false;
    }

    ParentLookup destinationParent = openParentDirectory(
        rootFd, destination, true, created, recorded);
    if (!destinationParent.ok()) {
        if (error)
            *error = destinationParent.error;
        return false;
    }

    if (::linkat(
            sourceParent.fd.get(),
            sourceParent.leaf.constData(),
            destinationParent.fd.get(),
            destinationParent.leaf.constData(),
            0) < 0) {
        if (error)
            *error = systemError(QStringLiteral("Could not create archive hardlink without overwrite"), destination);
        return false;
    }

    recordCreated(destination, false, created, recorded);
    return true;
}

} // namespace

ArchiveExtractionResult ArchiveExtractor::extract(
    const QString& archivePath,
    const QString& destinationDirectory,
    const std::atomic_bool& cancelRequested,
    const ProgressCallback& progress,
    const ArchiveExtractionLimits& limits) {
    ArchiveExtractionResult result;

    if (archivePath.trimmed().isEmpty() || destinationDirectory.trimmed().isEmpty()) {
        result.error = QStringLiteral("Archive path and extraction directory are required");
        return result;
    }

    const QFileInfo archiveInfo(archivePath);
    if (!archiveInfo.exists() || !archiveInfo.isFile()) {
        result.error = QStringLiteral("Archive is not an existing local file");
        return result;
    }

    const QFileInfo destinationInfo(destinationDirectory);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        result.error = QStringLiteral("Extraction destination is not an existing directory");
        return result;
    }

    const QByteArray destinationBytes = QFile::encodeName(destinationInfo.absoluteFilePath());
    FileDescriptor rootFd(::open(
        destinationBytes.constData(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!rootFd.valid()) {
        result.error = systemError(
            QStringLiteral("Could not open extraction root without following symlinks"),
            destinationInfo.absoluteFilePath());
        return result;
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        result.status = ArchiveExtractionStatus::Cancelled;
        return result;
    }

    struct archive* reader = archive_read_new();
    if (!reader) {
        result.error = QStringLiteral("Could not allocate libarchive reader");
        return result;
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    const QByteArray archiveBytes = QFile::encodeName(archiveInfo.absoluteFilePath());
    const int openStatus = archive_read_open_filename(reader, archiveBytes.constData(), 64 * 1024);
    if (openStatus != ARCHIVE_OK) {
        result.error = archiveFailure(reader, QStringLiteral("Could not open archive"));
        archive_read_free(reader);
        return result;
    }

    QVector<CreatedPath> created;
    QSet<QString> recordedCreated;
    QSet<QString> extractedRegularFiles;
    QVector<PendingHardlink> pendingHardlinks;
    quint64 headersSeen = 0;
    quint64 logicalExpandedBytes = 0;

    auto fail = [&](const QString& error) {
        archive_read_close(reader);
        archive_read_free(reader);
        return failedResult(rootFd.get(), created, result, error);
    };

    auto cancel = [&] {
        archive_read_close(reader);
        archive_read_free(reader);
        return cancelledResult(rootFd.get(), created, result);
    };

    for (;;) {
        if (cancelRequested.load(std::memory_order_relaxed))
            return cancel();

        struct archive_entry* entry = nullptr;
        const int nextStatus = archive_read_next_header(reader, &entry);
        if (nextStatus == ARCHIVE_EOF)
            break;
        if (nextStatus != ARCHIVE_OK)
            return fail(archiveFailure(reader, QStringLiteral("Could not read archive entry")));
        if (!entry)
            return fail(QStringLiteral("Archive returned an empty entry header"));

        ++headersSeen;
        if (headersSeen > limits.maximumEntries)
            return fail(QStringLiteral("Archive exceeds the configured entry-count limit"));

        QString rawPath;
        if (!decodeArchiveText(archive_entry_pathname(entry), &rawPath))
            return fail(QStringLiteral("Archive entry path is missing or is not valid UTF-8"));

        const ArchivePathResult guardedPath = ArchivePathGuard::validateEntryPath(rawPath);
        if (!guardedPath.safe)
            return fail(guardedPath.error);
        const QString normalizedPath = guardedPath.normalizedPath;

        if (progress) {
            progress({normalizedPath, result.entriesExtracted, result.bytesWritten});
            if (cancelRequested.load(std::memory_order_relaxed))
                return cancel();
        }

        QString hardlinkTarget;
        QString symlinkTarget;
        const char* rawHardlink = archive_entry_hardlink(entry);
        const char* rawSymlink = archive_entry_symlink(entry);

        if (rawHardlink && rawSymlink)
            return fail(QStringLiteral("Archive entry cannot be both a hardlink and a symlink"));

        if (rawHardlink) {
            if (!decodeArchiveText(rawHardlink, &hardlinkTarget))
                return fail(QStringLiteral("Archive hardlink target is not valid UTF-8"));
            const ArchivePathResult guardedTarget = ArchivePathGuard::validateHardlinkTarget(hardlinkTarget);
            if (!guardedTarget.safe)
                return fail(guardedTarget.error);
            pendingHardlinks.push_back({normalizedPath, guardedTarget.normalizedPath});
            const int skipStatus = archive_read_data_skip(reader);
            if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF)
                return fail(archiveFailure(reader, QStringLiteral("Could not skip hardlink payload")));
            continue;
        }

        const mode_t fileType = archive_entry_filetype(entry);
        if (fileType == AE_IFDIR) {
            QString error;
            if (!createDirectoryEntry(
                    rootFd.get(), normalizedPath, &created, &recordedCreated, &error))
                return fail(error);
            ++result.entriesExtracted;
            const int skipStatus = archive_read_data_skip(reader);
            if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF)
                return fail(archiveFailure(reader, QStringLiteral("Could not skip directory payload")));
            continue;
        }

        if (fileType == AE_IFLNK) {
            if (!rawSymlink || !decodeArchiveText(rawSymlink, &symlinkTarget))
                return fail(QStringLiteral("Archive symlink target is missing or is not valid UTF-8"));
            const ArchivePathResult guardedTarget = ArchivePathGuard::validateSymlinkTarget(
                normalizedPath, symlinkTarget);
            if (!guardedTarget.safe)
                return fail(guardedTarget.error);

            QString error;
            if (!createSymlinkEntry(
                    rootFd.get(),
                    normalizedPath,
                    guardedTarget.normalizedPath,
                    &created,
                    &recordedCreated,
                    &error))
                return fail(error);
            ++result.entriesExtracted;
            const int skipStatus = archive_read_data_skip(reader);
            if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF)
                return fail(archiveFailure(reader, QStringLiteral("Could not skip symlink payload")));
            continue;
        }

        if (rawSymlink)
            return fail(QStringLiteral("Archive entry has symlink metadata but is not a symlink: %1")
                            .arg(normalizedPath));

        if (fileType != AE_IFREG)
            return fail(QStringLiteral("Archive contains an unsupported special filesystem entry: %1")
                            .arg(normalizedPath));

        const bool declaredSizeKnown = archive_entry_size_is_set(entry) != 0;
        const la_int64_t declaredSize = declaredSizeKnown ? archive_entry_size(entry) : 0;
        if (declaredSizeKnown && declaredSize < 0)
            return fail(QStringLiteral("Archive file has an invalid negative size: %1")
                            .arg(normalizedPath));

        if (declaredSizeKnown) {
            const quint64 logicalSize = static_cast<quint64>(declaredSize);
            if (logicalExpandedBytes > limits.maximumExpandedBytes
                || logicalSize > limits.maximumExpandedBytes - logicalExpandedBytes)
                return fail(QStringLiteral("Archive exceeds the configured expanded-size limit"));
            logicalExpandedBytes += logicalSize;
        }

        ParentLookup parent = openParentDirectory(
            rootFd.get(), normalizedPath, true, &created, &recordedCreated);
        if (!parent.ok())
            return fail(parent.error);

        mode_t permissions = static_cast<mode_t>(archive_entry_perm(entry) & 0777);
        if (permissions == 0)
            permissions = 0644;

        FileDescriptor output(::openat(
            parent.fd.get(),
            parent.leaf.constData(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            permissions));
        if (!output.valid())
            return fail(systemError(
                QStringLiteral("Could not create extracted file without overwrite"), normalizedPath));
        recordCreated(normalizedPath, false, &created, &recordedCreated);

        quint64 unknownLogicalSize = 0;
        for (;;) {
            if (cancelRequested.load(std::memory_order_relaxed))
                return cancel();

            const void* block = nullptr;
            size_t blockSize = 0;
            la_int64_t blockOffset = 0;
            const int dataStatus = archive_read_data_block(reader, &block, &blockSize, &blockOffset);
            if (dataStatus == ARCHIVE_EOF)
                break;
            if (dataStatus != ARCHIVE_OK)
                return fail(archiveFailure(reader, QStringLiteral("Could not read archive file payload")));
            if (blockOffset < 0
                || static_cast<unsigned long long>(blockOffset)
                    > static_cast<unsigned long long>(std::numeric_limits<off_t>::max()))
                return fail(QStringLiteral("Archive file contains an invalid sparse-data offset"));

            const quint64 logicalOffset = static_cast<quint64>(blockOffset);
            if (blockSize > std::numeric_limits<quint64>::max() - logicalOffset)
                return fail(QStringLiteral("Archive file sparse extent overflows the logical-size counter"));
            const quint64 logicalExtent = logicalOffset + static_cast<quint64>(blockSize);

            if (declaredSizeKnown) {
                if (logicalExtent > static_cast<quint64>(declaredSize))
                    return fail(QStringLiteral("Archive file data exceeds its declared size: %1")
                                    .arg(normalizedPath));
            } else {
                if (logicalExpandedBytes > limits.maximumExpandedBytes
                    || logicalExtent > limits.maximumExpandedBytes - logicalExpandedBytes)
                    return fail(QStringLiteral("Archive exceeds the configured expanded-size limit"));
                unknownLogicalSize = std::max(unknownLogicalSize, logicalExtent);
            }

            if (static_cast<quint64>(blockSize)
                > std::numeric_limits<quint64>::max() - result.bytesWritten)
                return fail(QStringLiteral("Archive written-byte counter overflow"));

            QString writeError;
            if (!writeAllAt(
                    output.get(),
                    block,
                    blockSize,
                    static_cast<off_t>(blockOffset),
                    &writeError))
                return fail(QStringLiteral("%1 (%2)").arg(writeError, normalizedPath));

            result.bytesWritten += static_cast<quint64>(blockSize);
            if (progress)
                progress({normalizedPath, result.entriesExtracted, result.bytesWritten});
        }

        if (!declaredSizeKnown)
            logicalExpandedBytes += unknownLogicalSize;
        else if (::ftruncate(output.get(), static_cast<off_t>(declaredSize)) < 0)
            return fail(systemError(QStringLiteral("Could not finalize extracted file size"), normalizedPath));

        extractedRegularFiles.insert(normalizedPath);
        ++result.entriesExtracted;
    }

    if (cancelRequested.load(std::memory_order_relaxed))
        return cancel();

    QVector<bool> hardlinkDone(pendingHardlinks.size(), false);
    qsizetype remainingHardlinks = pendingHardlinks.size();
    while (remainingHardlinks > 0) {
        bool madeProgress = false;
        for (qsizetype i = 0; i < pendingHardlinks.size(); ++i) {
            if (hardlinkDone[i])
                continue;
            if (cancelRequested.load(std::memory_order_relaxed))
                return cancel();

            const PendingHardlink& pending = pendingHardlinks[i];
            if (!extractedRegularFiles.contains(pending.target))
                continue;

            QString error;
            if (!createHardlinkEntry(
                    rootFd.get(),
                    pending.destination,
                    pending.target,
                    &created,
                    &recordedCreated,
                    &error))
                return fail(error);

            hardlinkDone[i] = true;
            --remainingHardlinks;
            extractedRegularFiles.insert(pending.destination);
            ++result.entriesExtracted;
            madeProgress = true;
            if (progress)
                progress({pending.destination, result.entriesExtracted, result.bytesWritten});
        }

        if (!madeProgress)
            return fail(QStringLiteral("Archive contains a hardlink whose regular-file target was not extracted"));
    }

    archive_read_close(reader);
    archive_read_free(reader);
    result.status = ArchiveExtractionStatus::Success;
    result.error.clear();
    return result;
}
