// SPDX-License-Identifier: GPL-3.0-only

#include "ThumbnailStore.hpp"

#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {

constexpr int kMemoryCacheKiB = 64 * 1024;
constexpr qint64 kDiskCacheBytes = 256LL * 1024 * 1024;
constexpr qint64 kDiskPruneTargetBytes = 224LL * 1024 * 1024;
constexpr qint64 kMaxSourcePixels = 200LL * 1000 * 1000;
constexpr int kMinTargetPixels = 32;
constexpr int kMaxTargetPixels = 1024;

QCache<QString, QImage> g_memoryCache(kMemoryCacheKiB);
QMutex g_cacheMutex;
qint64 g_lastPruneMs = 0;

QString cacheRoot() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        return {};

    const QString root = QDir(base).filePath(QStringLiteral("thumbnails"));
    if (!QDir().mkpath(root))
        return {};
    return root;
}

QString cacheKey(const QFileInfo& info, int targetPixels) {
    const QByteArray source =
        info.absoluteFilePath().toUtf8()
        + '\n'
        + QByteArray::number(info.size())
        + '\n'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch())
        + '\n'
        + QByteArray::number(targetPixels);

    return QString::fromLatin1(
        QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex());
}

int imageCostKiB(const QImage& image) {
    const qsizetype bytes = image.sizeInBytes();
    return std::max(1, static_cast<int>((bytes + 1023) / 1024));
}

void insertMemory(const QString& key, const QImage& image) {
    if (image.isNull())
        return;

    const int cost = imageCostKiB(image);
    if (cost > kMemoryCacheKiB)
        return;

    QMutexLocker locker(&g_cacheMutex);
    g_memoryCache.insert(key, new QImage(image), cost);
}

QImage memoryLookup(const QString& key) {
    QMutexLocker locker(&g_cacheMutex);
    if (const QImage* image = g_memoryCache.object(key))
        return *image;
    return {};
}

void pruneDiskCache(const QString& root) {
    if (root.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    {
        QMutexLocker locker(&g_cacheMutex);
        if (now - g_lastPruneMs < 30000)
            return;
        g_lastPruneMs = now;
    }

    QDir dir(root);
    QFileInfoList entries = dir.entryInfoList(
        {QStringLiteral("*.png")},
        QDir::Files | QDir::Readable,
        QDir::Time | QDir::Reversed);

    qint64 total = 0;
    for (const QFileInfo& entry : entries)
        total += entry.size();

    if (total <= kDiskCacheBytes)
        return;

    std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.lastModified() < b.lastModified();
    });

    for (const QFileInfo& entry : entries) {
        if (total <= kDiskPruneTargetBytes)
            break;
        const qint64 bytes = entry.size();
        if (QFile::remove(entry.absoluteFilePath()))
            total -= bytes;
    }
}

QString suffixOf(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

} // namespace

bool ThumbnailStore::isCandidatePath(const QString& path) {
    const QString suffix = suffixOf(path);
    static const QSet<QString> candidates = {
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("webp"),
        QStringLiteral("gif"),
        QStringLiteral("bmp"),
        QStringLiteral("tif"),
        QStringLiteral("tiff"),
        QStringLiteral("avif"),
        QStringLiteral("heic"),
        QStringLiteral("heif"),
    };
    return candidates.contains(suffix);
}

QImage ThumbnailStore::load(
    const QString& path,
    int targetPixels,
    const std::atomic_bool& cancelled,
    QString* error) {
    if (cancelled.load(std::memory_order_relaxed))
        return {};

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink())
        return {};
    if (!isCandidatePath(path))
        return {};

    const int target = std::clamp(targetPixels, kMinTargetPixels, kMaxTargetPixels);
    const QString key = cacheKey(info, target);

    if (QImage cached = memoryLookup(key); !cached.isNull())
        return cached;

    const QString root = cacheRoot();
    const QString diskPath = root.isEmpty()
        ? QString()
        : QDir(root).filePath(key + QStringLiteral(".png"));

    if (!diskPath.isEmpty() && QFileInfo::exists(diskPath)) {
        QImageReader cachedReader(diskPath);
        QImage cached = cachedReader.read();
        if (!cached.isNull()) {
            insertMemory(key, cached);
            return cached;
        }
        QFile::remove(diskPath);
    }

    if (cancelled.load(std::memory_order_relaxed))
        return {};

    QImageReader reader(info.absoluteFilePath());
    reader.setAutoTransform(true);

    const QSize sourceSize = reader.size();
    if (!sourceSize.isValid() || sourceSize.width() <= 0 || sourceSize.height() <= 0) {
        if (error)
            *error = QObject::tr("Image dimensions are unavailable");
        return {};
    }

    const qint64 sourcePixels =
        static_cast<qint64>(sourceSize.width()) * static_cast<qint64>(sourceSize.height());
    if (sourcePixels <= 0 || sourcePixels > kMaxSourcePixels) {
        if (error)
            *error = QObject::tr("Image is too large to thumbnail safely");
        return {};
    }

    const QSize scaled = sourceSize.scaled(target, target, Qt::KeepAspectRatio);
    if (scaled.isValid())
        reader.setScaledSize(scaled);

    if (cancelled.load(std::memory_order_relaxed))
        return {};

    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = reader.errorString();
        return {};
    }

    if (cancelled.load(std::memory_order_relaxed))
        return {};

    if (image.width() > target || image.height() > target) {
        image = image.scaled(
            target,
            target,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    insertMemory(key, image);

    if (!diskPath.isEmpty() && !cancelled.load(std::memory_order_relaxed)) {
        const QString temporary = diskPath
            + QStringLiteral(".tmp-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);

        if (image.save(temporary, "PNG")) {
            QFile::remove(diskPath);
            if (!QFile::rename(temporary, diskPath))
                QFile::remove(temporary);
            pruneDiskCache(root);
        } else {
            QFile::remove(temporary);
        }
    }

    return image;
}

void ThumbnailStore::clearMemoryCache() {
    QMutexLocker locker(&g_cacheMutex);
    g_memoryCache.clear();
}
