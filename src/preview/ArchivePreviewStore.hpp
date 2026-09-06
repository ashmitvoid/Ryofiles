// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

enum class ArchivePreviewStatus {
    Success,
    Cancelled,
    Failed,
};

enum class ArchivePreviewEntryKind {
    RegularFile,
    Directory,
    Symlink,
    Hardlink,
    Other,
};

struct ArchivePreviewEntry {
    QString path;
    ArchivePreviewEntryKind kind = ArchivePreviewEntryKind::Other;
    quint64 size = 0;
    bool sizeKnown = false;
};

struct ArchivePreviewLimits {
    quint64 maximumArchiveBytes = 32ULL * 1024ULL * 1024ULL;
    quint64 maximumLogicalBytes = 128ULL * 1024ULL * 1024ULL;
    quint64 maximumEntries = 128;
    quint64 maximumPathBytes = 4096;
};

struct ArchivePreviewProgress {
    QString currentPath;
    quint64 entriesSeen = 0;
    quint64 archiveBytesConsumed = 0;
};

struct ArchivePreviewResult {
    ArchivePreviewStatus status = ArchivePreviewStatus::Failed;
    QString error;
    QString formatName;
    QVector<ArchivePreviewEntry> entries;
    bool truncated = false;
    quint64 entriesSeen = 0;
    quint64 archiveBytesConsumed = 0;

    bool succeeded() const {
        return status == ArchivePreviewStatus::Success;
    }
};

class ArchivePreviewStore final {
public:
    using ProgressCallback = std::function<void(const ArchivePreviewProgress&)>;

    static bool isCandidatePath(const QString& path);

    static ArchivePreviewResult inspect(
        const QString& path,
        const std::atomic_bool& cancelRequested,
        const ProgressCallback& progress = {},
        const ArchivePreviewLimits& limits = {});
};
