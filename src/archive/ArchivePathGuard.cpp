// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchivePathGuard.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace {

constexpr qsizetype kMaximumArchivePathLength = 32 * 1024;

ArchivePathResult rejected(const QString& error) {
    return {false, {}, error};
}

ArchivePathResult accepted(const QString& normalizedPath) {
    return {true, normalizedPath, {}};
}

bool hasInvalidMetadata(const QString& value) {
    return value.size() > kMaximumArchivePathLength
        || value.contains(QChar::Null);
}

bool isRootedPath(const QString& value) {
    if (value.startsWith(QLatin1Char('/'))
        || value.startsWith(QLatin1Char('\\'))) {
        return true;
    }

    static const QRegularExpression windowsDriveRoot(
        QStringLiteral("^[A-Za-z]:[\\\\/].*"));
    return windowsDriveRoot.match(value).hasMatch();
}

QStringList securityComponents(QString value) {
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return value.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool hasTraversalComponent(const QString& value) {
    const QStringList components = securityComponents(value);
    for (const QString& component : components) {
        if (component == QStringLiteral(".."))
            return true;
    }
    return false;
}

QString normalizeRelativeEntry(const QString& value) {
    QStringList normalized;
    const QStringList components = value.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    normalized.reserve(components.size());
    for (const QString& component : components) {
        if (component == QStringLiteral("."))
            continue;
        normalized.push_back(component);
    }
    return normalized.join(QLatin1Char('/'));
}

QString normalizeRelativeLinkTarget(const QString& value) {
    QStringList normalized;
    const QStringList components = value.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    normalized.reserve(components.size());
    for (const QString& component : components) {
        if (component == QStringLiteral("."))
            continue;
        normalized.push_back(component);
    }
    if (normalized.isEmpty())
        return QStringLiteral(".");
    return normalized.join(QLatin1Char('/'));
}

ArchivePathResult validateArchiveRootRelativePath(
    const QString& value,
    const QString& kind) {
    if (value.isEmpty())
        return rejected(QStringLiteral("Archive %1 path is empty").arg(kind));
    if (hasInvalidMetadata(value))
        return rejected(QStringLiteral("Archive %1 path metadata is invalid").arg(kind));
    if (isRootedPath(value))
        return rejected(QStringLiteral("Archive %1 path is absolute or rooted").arg(kind));
    if (hasTraversalComponent(value))
        return rejected(QStringLiteral("Archive %1 path contains traversal").arg(kind));

    const QString normalized = normalizeRelativeEntry(value);
    if (normalized.isEmpty())
        return rejected(QStringLiteral("Archive %1 path resolves to the extraction root").arg(kind));
    return accepted(normalized);
}

} // namespace

ArchivePathResult ArchivePathGuard::validateEntryPath(const QString& path) {
    return validateArchiveRootRelativePath(path, QStringLiteral("entry"));
}

ArchivePathResult ArchivePathGuard::validateHardlinkTarget(const QString& target) {
    return validateArchiveRootRelativePath(target, QStringLiteral("hardlink target"));
}

ArchivePathResult ArchivePathGuard::validateSymlinkTarget(
    const QString& normalizedEntryPath,
    const QString& target) {
    const ArchivePathResult entry = validateEntryPath(normalizedEntryPath);
    if (!entry.safe)
        return rejected(QStringLiteral("Symlink entry path is unsafe: %1").arg(entry.error));

    if (target.isEmpty())
        return rejected(QStringLiteral("Archive symlink target is empty"));
    if (hasInvalidMetadata(target))
        return rejected(QStringLiteral("Archive symlink target metadata is invalid"));
    if (isRootedPath(target))
        return rejected(QStringLiteral("Archive symlink target is absolute or rooted"));

    QString entryForSecurity = entry.normalizedPath;
    entryForSecurity.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QStringList resolved = entryForSecurity.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!resolved.isEmpty())
        resolved.removeLast();

    const QStringList targetComponents = securityComponents(target);
    for (const QString& component : targetComponents) {
        if (component == QStringLiteral("."))
            continue;
        if (component == QStringLiteral("..")) {
            if (resolved.isEmpty())
                return rejected(QStringLiteral("Archive symlink target escapes extraction root"));
            resolved.removeLast();
            continue;
        }
        resolved.push_back(component);
    }

    return accepted(normalizeRelativeLinkTarget(target));
}
