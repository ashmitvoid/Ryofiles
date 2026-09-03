// SPDX-License-Identifier: GPL-3.0-only

#include "TrashManager.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <QSet>

namespace {

QFileDevice::Permissions privateDirectoryPermissions() {
    return QFileDevice::ReadOwner
        | QFileDevice::WriteOwner
        | QFileDevice::ExeOwner;
}

QString entryId(const QString& trashRoot, const QString& name) {
    return trashRoot + QLatin1Char('\n') + name;
}

} // namespace

TrashManager::TrashManager(QObject* parent)
    : QAbstractListModel(parent) {
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(120);

    connect(&m_refreshDebounce, &QTimer::timeout, this, &TrashManager::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_refreshDebounce.start();
    });

    refresh();
}

TrashManager::~TrashManager() {
    m_stopping.store(true, std::memory_order_relaxed);
    for (auto& future : m_futures) {
        if (future.isRunning())
            future.waitForFinished();
    }
}

int TrashManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant TrashManager::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry& entry = m_entries.at(index.row());
    switch (role) {
    case IdRole: return entry.id;
    case NameRole: return entry.name;
    case OriginalPathRole: return entry.originalPath;
    case TrashedPathRole: return entry.trashedPath;
    case DeletionDateRole: return entry.deletionDate;
    case TrashRootRole: return entry.trashRoot;
    case OrphanedRole: return entry.orphaned;
    default: return {};
    }
}

QHash<int, QByteArray> TrashManager::roleNames() const {
    return {
        {IdRole, "itemId"},
        {NameRole, "name"},
        {OriginalPathRole, "originalPath"},
        {TrashedPathRole, "trashedPath"},
        {DeletionDateRole, "deletionDate"},
        {TrashRootRole, "trashRoot"},
        {OrphanedRole, "orphaned"},
    };
}

QString TrashManager::dataHome() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return path.isEmpty()
        ? QDir::home().filePath(QStringLiteral(".local/share"))
        : path;
}

QString TrashManager::homeTrashRoot() {
    return QDir(dataHome()).filePath(QStringLiteral("Trash"));
}

QString TrashManager::percentEncodePath(const QString& path) {
    return QString::fromLatin1(QUrl::toPercentEncoding(path, QByteArrayLiteral("/")));
}

QString TrashManager::percentDecodePath(const QString& value) {
    return QUrl::fromPercentEncoding(value.toUtf8());
}

bool TrashManager::pathExists(const QString& path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool TrashManager::ensureDirectory(
    const QString& path,
    QFileDevice::Permissions permissions) {
    if (!QDir().mkpath(path))
        return false;
    return QFile::setPermissions(path, permissions);
}

bool TrashManager::ensureTrashLayout(const QString& root, QString* error) {
    if (!ensureDirectory(root, privateDirectoryPermissions())
        || !ensureDirectory(QDir(root).filePath(QStringLiteral("files")), privateDirectoryPermissions())
        || !ensureDirectory(QDir(root).filePath(QStringLiteral("info")), privateDirectoryPermissions())) {
        if (error)
            *error = QObject::tr("Could not create trash directory: %1").arg(root);
        return false;
    }
    return true;
}

bool TrashManager::isSecureSharedTrash(const QString& path) {
    struct stat st {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &st) != 0)
        return false;

    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return false;

    return (st.st_mode & S_ISVTX) != 0;
}

TrashManager::TrashLocation TrashManager::locationForSource(
    const QString& source,
    QString* error) {
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() && !sourceInfo.isSymLink()) {
        if (error)
            *error = QObject::tr("Source does not exist: %1").arg(source);
        return {};
    }

    QStorageInfo sourceStorage(sourceInfo.absoluteFilePath());
    const QString userDataHome = dataHome();
    QDir().mkpath(userDataHome);
    QStorageInfo homeStorage(userDataHome);

    if (!sourceStorage.isValid() || !sourceStorage.isReady()) {
        if (error)
            *error = QObject::tr("Could not resolve filesystem for: %1").arg(source);
        return {};
    }

    if (homeStorage.isValid()
        && homeStorage.isReady()
        && sourceStorage.device() == homeStorage.device()) {
        TrashLocation location {
            homeTrashRoot(),
            QString(),
            true,
        };
        if (!ensureTrashLayout(location.root, error))
            return {};
        return location;
    }

    const QString top = sourceStorage.rootPath();
    if (top.isEmpty()) {
        if (error)
            *error = QObject::tr("Could not resolve mount root for: %1").arg(source);
        return {};
    }

    const uid_t uid = ::getuid();
    const QString shared = QDir(top).filePath(QStringLiteral(".Trash"));

    if (QFileInfo::exists(shared) && isSecureSharedTrash(shared)) {
        const QString userRoot = QDir(shared).filePath(QString::number(uid));
        if (ensureTrashLayout(userRoot, error))
            return {userRoot, top, false};
    }

    const QString privateRoot =
        QDir(top).filePath(QStringLiteral(".Trash-%1").arg(uid));
    if (ensureTrashLayout(privateRoot, error))
        return {privateRoot, top, false};

    if (error && error->isEmpty())
        *error = QObject::tr("Trash is unavailable on filesystem: %1").arg(top);
    return {};
}

QList<TrashManager::TrashLocation> TrashManager::discoverTrashLocations() {
    QList<TrashLocation> locations;

    const QString homeRoot = homeTrashRoot();
    if (QDir(homeRoot).exists())
        locations.push_back({homeRoot, QString(), true});

    const uid_t uid = ::getuid();
    const auto volumes = QStorageInfo::mountedVolumes();

    for (const QStorageInfo& volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;

        const QString top = volume.rootPath();
        if (top.isEmpty())
            continue;

        const QString sharedParent =
            QDir(top).filePath(QStringLiteral(".Trash"));
        const QString shared =
            QDir(sharedParent).filePath(QString::number(uid));
        if (QDir(shared).exists() && isSecureSharedTrash(sharedParent))
            locations.push_back({shared, top, false});

        const QString privateRoot =
            QDir(top).filePath(QStringLiteral(".Trash-%1").arg(uid));
        if (QDir(privateRoot).exists())
            locations.push_back({privateRoot, top, false});
    }

    QSet<QString> seen;
    QList<TrashLocation> unique;
    for (const auto& location : locations) {
        const QString clean = QDir::cleanPath(location.root);
        if (seen.contains(clean))
            continue;
        seen.insert(clean);
        unique.push_back(location);
    }
    return unique;
}

QString TrashManager::uniqueName(
    const QString& filesDir,
    const QString& preferred) {
    const QString base = preferred.isEmpty()
        ? QStringLiteral("item")
        : preferred;

    for (int n = 0; n < 100000; ++n) {
        const QString candidate = n == 0
            ? base
            : QStringLiteral("%1.%2").arg(base).arg(n);

        if (!pathExists(QDir(filesDir).filePath(candidate)))
            return candidate;
    }

    return {};
}

bool TrashManager::createTrashInfo(
    const TrashLocation& location,
    const QString& source,
    QString* itemName,
    QString* infoPath,
    QString* error) {
    const QString filesDir =
        QDir(location.root).filePath(QStringLiteral("files"));
    const QString infoDir =
        QDir(location.root).filePath(QStringLiteral("info"));
    const QString preferred = QFileInfo(source).fileName();

    for (int attempt = 0; attempt < 100000; ++attempt) {
        const QString candidate = attempt == 0
            ? preferred
            : QStringLiteral("%1.%2").arg(preferred).arg(attempt);

        if (pathExists(QDir(filesDir).filePath(candidate)))
            continue;

        const QString candidateInfo =
            QDir(infoDir).filePath(candidate + QStringLiteral(".trashinfo"));

        QFile infoFile(candidateInfo);
        if (!infoFile.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            continue;

        QString storedPath;
        if (location.home) {
            storedPath = QFileInfo(source).absoluteFilePath();
        } else {
            storedPath = QDir(location.relativeBase).relativeFilePath(
                QFileInfo(source).absoluteFilePath());
            if (storedPath.startsWith(QStringLiteral("../"))
                || storedPath == QStringLiteral("..")) {
                infoFile.close();
                QFile::remove(candidateInfo);
                if (error)
                    *error = QObject::tr("Could not encode trash origin safely: %1").arg(source);
                return false;
            }
        }

        const QByteArray payload =
            QByteArrayLiteral("[Trash Info]\nPath=")
            + percentEncodePath(storedPath).toUtf8()
            + QByteArrayLiteral("\nDeletionDate=")
            + QDateTime::currentDateTime()
                  .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"))
                  .toUtf8()
            + QByteArrayLiteral("\n");

        if (infoFile.write(payload) != payload.size()) {
            infoFile.close();
            QFile::remove(candidateInfo);
            if (error)
                *error = QObject::tr("Could not write trash metadata for: %1").arg(source);
            return false;
        }

        if (!infoFile.flush()) {
            infoFile.close();
            QFile::remove(candidateInfo);
            if (error)
                *error = QObject::tr("Could not flush trash metadata for: %1").arg(source);
            return false;
        }

        infoFile.close();
        *itemName = candidate;
        *infoPath = candidateInfo;
        return true;
    }

    if (error)
        *error = QObject::tr("Could not allocate a unique trash name for: %1").arg(source);
    return false;
}

bool TrashManager::moveToTrash(
    const TrashLocation& location,
    const QString& source,
    QString* error) {
    QString itemName;
    QString infoPath;
    if (!createTrashInfo(location, source, &itemName, &infoPath, error))
        return false;

    const QString destination =
        QDir(location.root).filePath(QStringLiteral("files/%1").arg(itemName));

    if (!QDir().rename(source, destination)) {
        QFile::remove(infoPath);
        if (error)
            *error = QObject::tr("Could not move item into Trash: %1").arg(source);
        return false;
    }

    return true;
}

bool TrashManager::parseTrashInfo(
    const QString& infoPath,
    const TrashLocation& location,
    QString* originalPath,
    QString* deletionDate) {
    QFile file(infoPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QList<QByteArray> lines = file.readAll().split('\n');
    if (lines.isEmpty() || lines.first().trimmed() != QByteArrayLiteral("[Trash Info]"))
        return false;

    QString pathValue;
    QString dateValue;

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i);
        if (pathValue.isEmpty() && line.startsWith("Path="))
            pathValue = QString::fromUtf8(line.mid(5));
        else if (dateValue.isEmpty() && line.startsWith("DeletionDate="))
            dateValue = QString::fromUtf8(line.mid(13));
    }

    if (pathValue.isEmpty())
        return false;

    const QString decoded = percentDecodePath(pathValue);
    if (QDir::isAbsolutePath(decoded)) {
        if (!location.home)
            return false;
        *originalPath = QDir::cleanPath(decoded);
    } else {
        if (location.relativeBase.isEmpty()
            || decoded == QStringLiteral("..")
            || decoded.startsWith(QStringLiteral("../"))) {
            return false;
        }
        *originalPath =
            QDir::cleanPath(QDir(location.relativeBase).filePath(decoded));
    }

    *deletionDate = dateValue;
    return true;
}

QVector<TrashManager::Entry> TrashManager::scanTrash(QString* error) {
    Q_UNUSED(error)

    QVector<Entry> entries;
    for (const TrashLocation& location : discoverTrashLocations()) {
        const QDir filesDir(QDir(location.root).filePath(QStringLiteral("files")));
        const QDir infoDir(QDir(location.root).filePath(QStringLiteral("info")));

        if (!filesDir.exists())
            continue;

        const QFileInfoList files = filesDir.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Time);

        for (const QFileInfo& trashed : files) {
            Entry entry;
            entry.name = trashed.fileName();
            entry.trashedPath = trashed.absoluteFilePath();
            entry.infoPath =
                infoDir.filePath(entry.name + QStringLiteral(".trashinfo"));
            entry.trashRoot = location.root;
            entry.relativeBase = location.relativeBase;
            entry.id = entryId(entry.trashRoot, entry.name);

            if (!parseTrashInfo(
                    entry.infoPath,
                    location,
                    &entry.originalPath,
                    &entry.deletionDate)) {
                entry.orphaned = true;
            }

            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

void TrashManager::setBusy(bool busyValue) {
    if (m_busy == busyValue)
        return;
    m_busy = busyValue;
    emit busyChanged();
}

void TrashManager::rebuildWatches() {
    const QStringList current =
        m_watcher.directories() + m_watcher.files();
    if (!current.isEmpty())
        m_watcher.removePaths(current);

    QStringList paths;
    for (const TrashLocation& location : discoverTrashLocations()) {
        for (const QString& suffix : {
                 QStringLiteral("files"),
                 QStringLiteral("info")}) {
            const QString path = QDir(location.root).filePath(suffix);
            if (QDir(path).exists())
                paths.push_back(path);
        }
    }

    paths.removeDuplicates();
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

void TrashManager::pruneFutures() {
    m_futures.erase(
        std::remove_if(
            m_futures.begin(),
            m_futures.end(),
            [](const QFuture<void>& future) {
                return future.isFinished();
            }),
        m_futures.end());
}

void TrashManager::refresh() {
    if (m_stopping.load(std::memory_order_relaxed))
        return;

    pruneFutures();
    setBusy(true);
    const quint64 generation =
        m_refreshGeneration.fetch_add(1, std::memory_order_relaxed) + 1;

    QFuture<void> future = QtConcurrent::run([this, generation] {
        QString error;
        const QVector<Entry> scanned = scanTrash(&error);

        if (m_stopping.load(std::memory_order_relaxed))
            return;

        QMetaObject::invokeMethod(this, [this, scanned, error, generation] {
            if (generation != m_refreshGeneration.load(std::memory_order_relaxed))
                return;

            beginResetModel();
            m_entries = scanned;
            endResetModel();
            emit countChanged();
            setBusy(false);
            rebuildWatches();

            if (!error.isEmpty()) {
                emit operationFinished(
                    QStringLiteral("refresh"),
                    false,
                    error);
            }
        }, Qt::QueuedConnection);
    });
    m_futures.push_back(future);
}

void TrashManager::startOperation(
    const QString& operationId,
    const std::function<TrashResult()>& work) {
    pruneFutures();
    setBusy(true);

    QFuture<void> future = QtConcurrent::run([this, operationId, work] {
        const TrashResult result = work();

        if (m_stopping.load(std::memory_order_relaxed))
            return;

        QMetaObject::invokeMethod(this, [this, operationId, result] {
            emit operationFinished(operationId, result.success, result.error);
            setBusy(false);
            refresh();
        }, Qt::QueuedConnection);
    });

    m_futures.push_back(future);
}

QString TrashManager::trash(const QStringList& requestedPaths) {
    QStringList paths;
    for (const QString& requested : requestedPaths) {
        const QFileInfo info(requested);
        if (info.exists() || info.isSymLink())
            paths.push_back(info.absoluteFilePath());
    }

    paths.removeDuplicates();
    if (paths.isEmpty())
        return {};

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    startOperation(id, [paths] {
        for (const QString& path : paths) {
            QString error;
            const TrashLocation location = locationForSource(path, &error);
            if (location.root.isEmpty())
                return TrashResult{false, error};

            if (!moveToTrash(location, path, &error))
                return TrashResult{false, error};
        }

        return TrashResult{true, QString()};
    });

    return id;
}

QString TrashManager::uniqueSiblingPath(const QString& desiredPath) {
    if (!pathExists(desiredPath))
        return desiredPath;

    const QFileInfo info(desiredPath);
    const QDir parent = info.dir();
    const QString name = info.fileName();

    QString stem = name;
    QString extension;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && !info.isDir()) {
        stem = name.left(dot);
        extension = name.mid(dot);
    }

    for (int n = 1; n < 100000; ++n) {
        const QString suffix = n == 1
            ? QStringLiteral(" (restored)")
            : QStringLiteral(" (restored %1)").arg(n);
        const QString candidate = parent.filePath(stem + suffix + extension);
        if (!pathExists(candidate))
            return candidate;
    }

    return {};
}

QString TrashManager::backupSiblingPath(const QString& desiredPath) {
    const QFileInfo info(desiredPath);
    return info.dir().filePath(
        QStringLiteral(".%1.ryofiles-restore-backup-%2")
            .arg(info.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

bool TrashManager::movePath(
    const QString& source,
    const QString& destination) {
    return QDir().rename(source, destination);
}

bool TrashManager::removePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;

    if (info.isFile() || info.isSymLink())
        return QFile::remove(path);

    return QDir(path).removeRecursively();
}

TrashManager::TrashResult TrashManager::restoreEntry(
    const Entry& entry,
    RestoreDecision decision) {
    if (entry.orphaned || entry.originalPath.isEmpty())
        return {false, QObject::tr("Trash metadata is missing or invalid for: %1").arg(entry.name)};

    const QFileInfo originalInfo(entry.originalPath);
    const QString parentPath = originalInfo.absolutePath();

    if (!QDir(parentPath).exists()) {
        return {
            false,
            QObject::tr("Original folder no longer exists: %1").arg(parentPath),
        };
    }

    QString destination = entry.originalPath;
    QString backup;

    if (pathExists(destination)) {
        if (decision == RestoreSkip) {
            return {
                false,
                QObject::tr("Restore destination already exists: %1").arg(destination),
            };
        }

        if (decision == RestoreKeepBoth) {
            destination = uniqueSiblingPath(destination);
            if (destination.isEmpty())
                return {false, QObject::tr("Could not create a unique restore name")};
        } else if (decision == RestoreReplace) {
            backup = backupSiblingPath(destination);
            if (!movePath(destination, backup)) {
                return {
                    false,
                    QObject::tr("Could not protect existing destination before restore: %1")
                        .arg(destination),
                };
            }
        }
    }

    if (!movePath(entry.trashedPath, destination)) {
        if (!backup.isEmpty())
            movePath(backup, entry.originalPath);

        return {
            false,
            QObject::tr("Could not restore trashed item to: %1").arg(destination),
        };
    }

    if (!QFile::remove(entry.infoPath)) {
        if (!movePath(destination, entry.trashedPath)) {
            return {
                false,
                QObject::tr("Item restored, but Trash metadata cleanup failed: %1")
                    .arg(entry.infoPath),
            };
        }

        if (!backup.isEmpty())
            movePath(backup, entry.originalPath);

        return {
            false,
            QObject::tr("Could not remove Trash metadata: %1").arg(entry.infoPath),
        };
    }

    if (!backup.isEmpty() && !removePath(backup)) {
        return {
            false,
            QObject::tr("Restore succeeded but old destination cleanup failed: %1").arg(backup),
        };
    }

    return {true, QString()};
}

QString TrashManager::restore(
    const QString& itemId,
    int decisionValue) {
    if (decisionValue < RestoreSkip || decisionValue > RestoreReplace)
        return {};

    auto it = std::find_if(
        m_entries.cbegin(),
        m_entries.cend(),
        [&itemId](const Entry& entry) {
            return entry.id == itemId;
        });

    if (it == m_entries.cend())
        return {};

    const Entry entry = *it;

    if (pathExists(entry.originalPath)
        && decisionValue == RestoreSkip) {
        emit restoreConflict(itemId, entry.originalPath);
        return {};
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const RestoreDecision decision =
        static_cast<RestoreDecision>(decisionValue);

    startOperation(id, [entry, decision] {
        return restoreEntry(entry, decision);
    });

    return id;
}
