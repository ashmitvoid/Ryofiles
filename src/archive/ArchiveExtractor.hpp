// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include <atomic>
#include <functional>

struct ArchiveExtractionLimits {
    quint64 maximumEntries = 1000000;
    quint64 maximumExpandedBytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
};

struct ArchiveExtractionProgress {
    QString entryPath;
    quint64 entriesProcessed = 0;
    quint64 bytesWritten = 0;
};

enum class ArchiveExtractionStatus {
    Success,
    Cancelled,
    Failed,
};

struct ArchiveExtractionResult {
    ArchiveExtractionStatus status = ArchiveExtractionStatus::Failed;
    QString error;
    quint64 entriesExtracted = 0;
    quint64 bytesWritten = 0;

    bool succeeded() const { return status == ArchiveExtractionStatus::Success; }
};

class ArchiveExtractor final {
public:
    using ProgressCallback = std::function<void(const ArchiveExtractionProgress&)>;

    static ArchiveExtractionResult extract(
        const QString& archivePath,
        const QString& destinationDirectory,
        const std::atomic_bool& cancelRequested,
        const ProgressCallback& progress = {},
        const ArchiveExtractionLimits& limits = {});
};
