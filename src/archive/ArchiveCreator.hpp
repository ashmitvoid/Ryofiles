// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

struct ArchiveCreationProgress {
    QString sourcePath;
    QString entryPath;
    quint64 entriesProcessed = 0;
    quint64 bytesRead = 0;
};

enum class ArchiveCreationStatus {
    Success,
    Cancelled,
    Failed,
};

struct ArchiveCreationResult {
    ArchiveCreationStatus status = ArchiveCreationStatus::Failed;
    QString error;
    quint64 entriesWritten = 0;
    quint64 bytesRead = 0;

    bool succeeded() const { return status == ArchiveCreationStatus::Success; }
};

class ArchiveCreator final {
public:
    using ProgressCallback = std::function<void(const ArchiveCreationProgress&)>;

    static ArchiveCreationResult create(
        const QStringList& sources,
        const QString& archivePath,
        const std::atomic_bool& cancelRequested,
        const ProgressCallback& progress = {});
};
