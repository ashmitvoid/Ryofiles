// SPDX-License-Identifier: GPL-3.0-only

#include "ArchivePreviewStore.hpp"
#include "locations/LocalPathGuard.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr quint64 kHardMaximumArchiveBytes = 256ULL * 1024ULL * 1024ULL;
constexpr quint64 kHardMaximumLogicalBytes = 1024ULL * 1024ULL * 1024ULL;
constexpr quint64 kHardMaximumEntries = 1024;
constexpr quint64 kHardMaximumPathBytes = 64ULL * 1024ULL;
constexpr size_t kReadBlockBytes = 64 * 1024;

class FileDescriptor final {
public:
    explicit FileDescriptor(int fd = -1) : m_fd(fd) {}
    ~FileDescriptor() {
        if (m_fd >= 0)
            ::close(m_fd);
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const { return m_fd; }
    bool valid() const { return m_fd >= 0; }

private:
    int m_fd = -1;
};

struct BoundedArchiveInput {
    int fd = -1;
    quint64 snapshotBytes = 0;
    quint64 offset = 0;
    const std::atomic_bool* cancelRequested = nullptr;
    std::array<char, kReadBlockBytes> buffer{};
};

QString systemError(const QString& action, const QString& path) {
    return QStringLiteral("%1 '%2': %3")
        .arg(action, path, QString::fromLocal8Bit(std::strerror(errno)));
}

QString archiveFailure(struct archive* reader, const QString& action) {
    const char* raw = archive_error_string(reader);
    return raw && *raw
        ? QStringLiteral("%1: %2").arg(action, QString::fromLocal8Bit(raw))
        : action;
}

bool validLimits(const ArchivePreviewLimits& limits, QString* error) {
    if (limits.maximumArchiveBytes == 0
        || limits.maximumArchiveBytes > kHardMaximumArchiveBytes
        || limits.maximumLogicalBytes == 0
        || limits.maximumLogicalBytes > kHardMaximumLogicalBytes
        || limits.maximumEntries == 0
        || limits.maximumEntries > kHardMaximumEntries
        || limits.maximumPathBytes == 0
        || limits.maximumPathBytes > kHardMaximumPathBytes) {
        if (error)
            *error = QStringLiteral("Archive preview limits are outside the supported bounded range");
        return false;
    }
    return true;
}

int boundedOpen(struct archive* archiveHandle, void* clientData) {
    auto* input = static_cast<BoundedArchiveInput*>(clientData);
    if (!input || input->fd < 0) {
        archive_set_error(archiveHandle, EBADF, "%s", "Archive preview input is invalid");
        return ARCHIVE_FATAL;
    }

    if (::lseek(input->fd, 0, SEEK_SET) < 0) {
        archive_set_error(archiveHandle, errno, "%s", "Could not seek archive preview input");
        return ARCHIVE_FATAL;
    }

    input->offset = 0;
    return ARCHIVE_OK;
}

la_ssize_t boundedRead(
    struct archive* archiveHandle,
    void* clientData,
    const void** buffer) {
    auto* input = static_cast<BoundedArchiveInput*>(clientData);
    if (!input || !buffer)
        return -1;

    if (input->cancelRequested
        && input->cancelRequested->load(std::memory_order_relaxed)) {
        archive_set_error(archiveHandle, ECANCELED, "%s", "Archive preview cancelled");
        return -1;
    }

    if (input->offset >= input->snapshotBytes) {
        *buffer = nullptr;
        return 0;
    }

    const quint64 remaining = input->snapshotBytes - input->offset;
    const size_t requested = static_cast<size_t>(
        std::min<quint64>(remaining, input->buffer.size()));

    ssize_t bytesRead = -1;
    do {
        bytesRead = ::read(input->fd, input->buffer.data(), requested);
    } while (bytesRead < 0 && errno == EINTR
        && !(input->cancelRequested
            && input->cancelRequested->load(std::memory_order_relaxed)));

    if (bytesRead < 0) {
        if (input->cancelRequested
            && input->cancelRequested->load(std::memory_order_relaxed)) {
            archive_set_error(archiveHandle, ECANCELED, "%s", "Archive preview cancelled");
        } else {
            archive_set_error(archiveHandle, errno, "%s", "Could not read archive preview input");
        }
        return -1;
    }

    if (bytesRead == 0) {
        *buffer = nullptr;
        return 0;
    }

    input->offset += static_cast<quint64>(bytesRead);
    *buffer = input->buffer.data();
    return static_cast<la_ssize_t>(bytesRead);
}

la_int64_t boundedSkip(
    struct archive* archiveHandle,
    void* clientData,
    la_int64_t request) {
    auto* input = static_cast<BoundedArchiveInput*>(clientData);
    if (!input || request <= 0)
        return 0;

    if (input->cancelRequested
        && input->cancelRequested->load(std::memory_order_relaxed)) {
        archive_set_error(archiveHandle, ECANCELED, "%s", "Archive preview cancelled");
        return -1;
    }

    if (input->offset >= input->snapshotBytes)
        return 0;

    const quint64 remaining = input->snapshotBytes - input->offset;
    const quint64 requested = static_cast<quint64>(request);
    const quint64 amount = std::min(remaining, requested);
    const off_t skipAmount = static_cast<off_t>(amount);

    const off_t before = ::lseek(input->fd, 0, SEEK_CUR);
    if (before < 0)
        return 0;

    const off_t after = ::lseek(input->fd, skipAmount, SEEK_CUR);
    if (after < 0)
        return 0;

    const off_t moved = after - before;
    if (moved <= 0)
        return 0;

    input->offset += static_cast<quint64>(moved);
    return static_cast<la_int64_t>(moved);
}

int boundedClose(struct archive*, void*) {
    return ARCHIVE_OK;
}

bool decodeBoundedUtf8(
    const char* raw,
    quint64 maximumBytes,
    QString* decoded,
    QString* error) {
    if (!raw || !decoded) {
        if (error)
            *error = QStringLiteral("Archive entry path is missing");
        return false;
    }

    const size_t limit = static_cast<size_t>(maximumBytes);
    size_t length = 0;
    while (length <= limit && raw[length] != '\0')
        ++length;

    if (length > limit) {
        if (error)
            *error = QStringLiteral("Archive entry path exceeds the preview metadata limit");
        return false;
    }

    const QByteArray bytes(raw, static_cast<qsizetype>(length));
    const QString value = QString::fromUtf8(bytes);
    if (value.toUtf8() != bytes) {
        if (error)
            *error = QStringLiteral("Archive entry path is not valid UTF-8");
        return false;
    }

    *decoded = value;
    return true;
}

ArchivePreviewEntryKind entryKind(const struct archive_entry* entry) {
    if (archive_entry_hardlink(entry))
        return ArchivePreviewEntryKind::Hardlink;

    switch (archive_entry_filetype(entry)) {
    case AE_IFREG:
        return ArchivePreviewEntryKind::RegularFile;
    case AE_IFDIR:
        return ArchivePreviewEntryKind::Directory;
    case AE_IFLNK:
        return ArchivePreviewEntryKind::Symlink;
    default:
        return ArchivePreviewEntryKind::Other;
    }
}

} // namespace

bool ArchivePreviewStore::isCandidatePath(const QString& path) {
    if (path.trimmed().isEmpty() || LocalPathGuard::isUriLike(path))
        return false;

    const QString name = QFileInfo(path).fileName().toLower();
    static const QStringList suffixes = {
        QStringLiteral(".tar.gz"),
        QStringLiteral(".tar.xz"),
        QStringLiteral(".tar.zst"),
        QStringLiteral(".tgz"),
        QStringLiteral(".tar"),
        QStringLiteral(".zip"),
        QStringLiteral(".7z"),
    };

    for (const QString& suffix : suffixes) {
        if (name.endsWith(suffix))
            return true;
    }
    return false;
}

ArchivePreviewResult ArchivePreviewStore::inspect(
    const QString& path,
    const std::atomic_bool& cancelRequested,
    const ProgressCallback& progress,
    const ArchivePreviewLimits& limits) {
    ArchivePreviewResult result;

    if (!validLimits(limits, &result.error))
        return result;
    if (!isCandidatePath(path)) {
        result.error = QStringLiteral("Archive preview supports only tested local archive formats");
        return result;
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        result.status = ArchivePreviewStatus::Cancelled;
        return result;
    }

    const QByteArray encodedPath = QFile::encodeName(path);
    FileDescriptor inputFd(::open(
        encodedPath.constData(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!inputFd.valid()) {
        result.error = systemError(
            QStringLiteral("Could not open archive preview input without following symlinks"),
            path);
        return result;
    }

    struct stat inputStat {};
    if (::fstat(inputFd.get(), &inputStat) < 0) {
        result.error = systemError(QStringLiteral("Could not inspect archive preview input"), path);
        return result;
    }
    if (!S_ISREG(inputStat.st_mode) || inputStat.st_size < 0) {
        result.error = QStringLiteral("Archive preview input is not a regular local file");
        return result;
    }

    const quint64 snapshotBytes = static_cast<quint64>(inputStat.st_size);
    if (snapshotBytes > limits.maximumArchiveBytes) {
        result.error = QStringLiteral("Archive exceeds the preview input-byte limit");
        return result;
    }

    struct archive* reader = archive_read_new();
    if (!reader) {
        result.error = QStringLiteral("Could not allocate libarchive preview reader");
        return result;
    }

    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    BoundedArchiveInput input;
    input.fd = inputFd.get();
    input.snapshotBytes = snapshotBytes;
    input.cancelRequested = &cancelRequested;

    const int openStatus = archive_read_open2(
        reader,
        &input,
        boundedOpen,
        boundedRead,
        boundedSkip,
        boundedClose);
    if (openStatus != ARCHIVE_OK) {
        result.error = archiveFailure(reader, QStringLiteral("Could not open archive preview"));
        archive_read_free(reader);
        return result;
    }

    quint64 logicalBytes = 0;

    auto finish = [&](ArchivePreviewStatus status, const QString& error = QString()) {
        result.status = status;
        result.error = error;
        result.archiveBytesConsumed = input.offset;
        archive_read_close(reader);
        archive_read_free(reader);
        return result;
    };

    for (;;) {
        if (cancelRequested.load(std::memory_order_relaxed))
            return finish(ArchivePreviewStatus::Cancelled);

        struct archive_entry* entry = nullptr;
        const int nextStatus = archive_read_next_header(reader, &entry);
        if (nextStatus == ARCHIVE_EOF)
            break;
        if (nextStatus != ARCHIVE_OK || !entry) {
            if (cancelRequested.load(std::memory_order_relaxed))
                return finish(ArchivePreviewStatus::Cancelled);
            return finish(
                ArchivePreviewStatus::Failed,
                archiveFailure(reader, QStringLiteral("Could not read archive preview entry")));
        }

        QString pathText;
        QString pathError;
        if (!decodeBoundedUtf8(
                archive_entry_pathname(entry),
                limits.maximumPathBytes,
                &pathText,
                &pathError)) {
            return finish(ArchivePreviewStatus::Failed, pathError);
        }

        ArchivePreviewEntry previewEntry;
        previewEntry.path = pathText;
        previewEntry.kind = entryKind(entry);

        const bool sizeKnown = archive_entry_size_is_set(entry) != 0;
        const la_int64_t rawSize = sizeKnown ? archive_entry_size(entry) : 0;
        if (sizeKnown && rawSize < 0) {
            return finish(
                ArchivePreviewStatus::Failed,
                QStringLiteral("Archive entry has an invalid negative size: %1").arg(pathText));
        }

        if (sizeKnown) {
            previewEntry.sizeKnown = true;
            previewEntry.size = static_cast<quint64>(rawSize);
        }

        result.entries.push_back(previewEntry);
        ++result.entriesSeen;

        if (result.formatName.isEmpty()) {
            const char* format = archive_format_name(reader);
            if (format && *format)
                result.formatName = QString::fromLatin1(format);
        }

        if (progress) {
            progress({pathText, result.entriesSeen, input.offset});
            if (cancelRequested.load(std::memory_order_relaxed))
                return finish(ArchivePreviewStatus::Cancelled);
        }

        if (result.entries.size() >= static_cast<qsizetype>(limits.maximumEntries)) {
            result.truncated = true;
            break;
        }

        if (previewEntry.kind == ArchivePreviewEntryKind::RegularFile) {
            if (!previewEntry.sizeKnown
                || logicalBytes > limits.maximumLogicalBytes
                || previewEntry.size > limits.maximumLogicalBytes - logicalBytes) {
                result.truncated = true;
                break;
            }
            logicalBytes += previewEntry.size;
        }

        const int skipStatus = archive_read_data_skip(reader);
        if (skipStatus != ARCHIVE_OK && skipStatus != ARCHIVE_EOF) {
            if (cancelRequested.load(std::memory_order_relaxed))
                return finish(ArchivePreviewStatus::Cancelled);
            return finish(
                ArchivePreviewStatus::Failed,
                archiveFailure(reader, QStringLiteral("Could not skip archive preview payload")));
        }
    }

    return finish(ArchivePreviewStatus::Success);
}
