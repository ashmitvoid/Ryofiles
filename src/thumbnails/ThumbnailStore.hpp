// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QImage>
#include <QString>

#include <atomic>

class ThumbnailStore final {
public:
    static bool isCandidatePath(const QString& path);

    static QImage load(
        const QString& path,
        int targetPixels,
        const std::atomic_bool& cancelled,
        QString* error = nullptr);

    static void clearMemoryCache();

private:
    ThumbnailStore() = delete;
};
