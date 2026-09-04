// SPDX-License-Identifier: GPL-3.0-only

#include "DirectoryModel.hpp"
#include "locations/LocalPathGuard.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <utility>

DirectoryModel::DirectoryModel(QObject* parent)
    : DirectoryModel(true, parent) {
}

DirectoryModel::DirectoryModel(bool startActive, QObject* parent)
    : QAbstractListModel(parent)
    , m_active(startActive) {
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(120);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &DirectoryModel::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (m_active)
            m_refreshDebounce.start();
    });

    m_path = home();
    if (m_active) {
        watchCurrentDirectory();
        scan();
    }
}

int DirectoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DirectoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry& entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case PathRole: return entry.path;
    case DirectoryRole: return entry.directory;
    case SizeTextRole: return entry.sizeText;
    case ModifiedTextRole: return entry.modifiedText;
    case HiddenRole: return entry.hidden;
    case ThumbnailCandidateRole: return entry.thumbnailCandidate;
    default: return {};
    }
}

QHash<int, QByteArray> DirectoryModel::roleNames() const {
    return {
        {NameRole, "name"},
        {PathRole, "filePath"},
        {DirectoryRole, "isDir"},
        {SizeTextRole, "sizeText"},
        {ModifiedTextRole, "modifiedText"},
        {HiddenRole, "isHidden"},
        {ThumbnailCandidateRole, "thumbnailCandidate"},
    };
}

void DirectoryModel::setPath(const QString& requestedPath) {
    if (requestedPath.trimmed().isEmpty())
        return;

    if (LocalPathGuard::isUriLike(requestedPath)) {
        emit errorOccurred(tr("Remote URI is not a local folder: %1").arg(requestedPath));
        return;
    }

    const QDir directory(QDir::cleanPath(requestedPath));
    if (!directory.exists()) {
        emit errorOccurred(tr("Folder does not exist: %1").arg(requestedPath));
        return;
    }

    const QString absolute = directory.absolutePath();
    if (absolute == m_path)
        return;

    if (!m_filterQuery.isEmpty()) {
        m_filterQuery.clear();
        emit filterQueryChanged();
    }

    m_path = absolute;
    emit pathChanged();

    if (m_active) {
        watchCurrentDirectory();
        scan();
    }
}

void DirectoryModel::setActive(bool active) {
    if (m_active == active)
        return;

    m_active = active;
    emit activeChanged();

    if (!m_active) {
        m_refreshDebounce.stop();
        ++m_generation;
        clearWatchers();
        setLoading(false);
        return;
    }

    watchCurrentDirectory();
    scan();
}

void DirectoryModel::setShowHidden(bool show) {
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    if (m_active)
        scan();
}

void DirectoryModel::setFilterQuery(const QString& query) {
    const QString normalized = query.trimmed();
    if (m_filterQuery == normalized)
        return;

    m_filterQuery = normalized;
    emit filterQueryChanged();
    rebuildFilteredEntries();
}

QString DirectoryModel::standardPath(int location) {
    const QString value =
        QStandardPaths::writableLocation(static_cast<QStandardPaths::StandardLocation>(location));
    return value.isEmpty() ? QDir::homePath() : value;
}

QString DirectoryModel::home() const { return QDir::homePath(); }
QString DirectoryModel::desktop() const { return standardPath(QStandardPaths::DesktopLocation); }
QString DirectoryModel::documents() const { return standardPath(QStandardPaths::DocumentsLocation); }
QString DirectoryModel::downloads() const { return standardPath(QStandardPaths::DownloadLocation); }
QString DirectoryModel::pictures() const { return standardPath(QStandardPaths::PicturesLocation); }
QString DirectoryModel::music() const { return standardPath(QStandardPaths::MusicLocation); }
QString DirectoryModel::videos() const { return standardPath(QStandardPaths::MoviesLocation); }

void DirectoryModel::refresh() {
    if (m_active)
        scan();
}

void DirectoryModel::goUp() {
    QDir directory(m_path);
    if (directory.cdUp())
        setPath(directory.absolutePath());
}

QString DirectoryModel::pathAt(int index) const {
    if (index < 0 || index >= m_entries.size())
        return {};
    return m_entries.at(index).path;
}

bool DirectoryModel::isDirectoryAt(int index) const {
    if (index < 0 || index >= m_entries.size())
        return false;
    return m_entries.at(index).directory;
}

int DirectoryModel::indexOfPath(const QString& targetPath) const {
    if (targetPath.isEmpty())
        return -1;

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == targetPath)
            return i;
    }
    return -1;
}

void DirectoryModel::activate(int index) {
    const QString target = pathAt(index);
    if (target.isEmpty())
        return;

    if (isDirectoryAt(index)) {
        setPath(target);
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

void DirectoryModel::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void DirectoryModel::clearWatchers() {
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
}

void DirectoryModel::watchCurrentDirectory() {
    clearWatchers();
    if (!m_active)
        return;

    if (QDir(m_path).exists())
        m_watcher.addPath(m_path);
}

QList<DirectoryModel::Entry> DirectoryModel::filterEntries(
    const QList<Entry>& entries,
    const QString& query) {
    if (query.isEmpty())
        return entries;

    QList<Entry> filtered;
    filtered.reserve(entries.size());
    for (const Entry& entry : entries) {
        if (entry.name.contains(query, Qt::CaseInsensitive))
            filtered.push_back(entry);
    }
    return filtered;
}

void DirectoryModel::rebuildFilteredEntries() {
    QList<Entry> filtered = filterEntries(m_allEntries, m_filterQuery);

    beginResetModel();
    m_entries = std::move(filtered);
    endResetModel();
    emit countChanged();
}

void DirectoryModel::scan() {
    if (!m_active) {
        setLoading(false);
        return;
    }

    const quint64 generation = ++m_generation;
    const QString scanPath = m_path;
    const bool scanHidden = m_showHidden;
    setLoading(true);

    auto* watcher = new QFutureWatcher<QPair<QList<Entry>, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, scanPath] {
        auto [entries, error] = watcher->result();
        watcher->deleteLater();

        if (!m_active || generation != m_generation || scanPath != m_path)
            return;

        QList<Entry> filtered = filterEntries(entries, m_filterQuery);

        beginResetModel();
        m_allEntries = std::move(entries);
        m_entries = std::move(filtered);
        endResetModel();
        setLoading(false);
        emit countChanged();

        if (!error.isEmpty())
            emit errorOccurred(error);
    });

    watcher->setFuture(QtConcurrent::run([scanPath, scanHidden] {
        QString error;
        QList<Entry> entries = scanDirectory(scanPath, scanHidden, &error);
        return qMakePair(entries, error);
    }));
}

bool DirectoryModel::thumbnailCandidateFor(const QFileInfo& info) {
    if (!info.isFile() || info.isSymLink())
        return false;

    static const QSet<QString> suffixes = {
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

    return suffixes.contains(info.suffix().toLower());
}

QList<DirectoryModel::Entry> DirectoryModel::scanDirectory(
    const QString& path,
    bool showHidden,
    QString* error) {
    QDir directory(path);
    if (!directory.exists()) {
        if (error)
            *error = QObject::tr("Folder disappeared while reading: %1").arg(path);
        return {};
    }

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
    if (showHidden)
        filters |= QDir::Hidden;

    const QFileInfoList infos = directory.entryInfoList(
        filters,
        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);

    QList<Entry> entries;
    entries.reserve(infos.size());
    for (const QFileInfo& info : infos) {
        Entry entry;
        entry.name = info.fileName();
        entry.path = info.absoluteFilePath();
        entry.directory = info.isDir();
        entry.hidden = info.isHidden() || entry.name.startsWith(QLatin1Char('.'));
        entry.thumbnailCandidate = thumbnailCandidateFor(info);
        entry.sizeText = entry.directory ? QString() : formatSize(info.size());
        entry.modifiedText = info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        entries.push_back(std::move(entry));
    }
    return entries;
}

QString DirectoryModel::formatSize(qint64 bytes) {
    constexpr qreal k = 1024.0;
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(QString::number(bytes / k, 'f', 1));
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (k * k), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (k * k * k), 'f', 1));
}
