// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>

struct ArchivePathResult {
    bool safe = false;
    QString normalizedPath;
    QString error;
};

class ArchivePathGuard final {
public:
    static ArchivePathResult validateEntryPath(const QString& path);
    static ArchivePathResult validateHardlinkTarget(const QString& target);
    static ArchivePathResult validateSymlinkTarget(
        const QString& normalizedEntryPath,
        const QString& target);
};
