// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

#include <atomic>

struct TextPreviewResult {
    bool supported = false;
    bool truncated = false;
    QString text;
    QString error;
};

class TextPreviewStore final {
public:
    static bool isCandidatePath(const QString& path);
    static TextPreviewResult load(
        const QString& path,
        const std::atomic_bool& cancelled);
};
