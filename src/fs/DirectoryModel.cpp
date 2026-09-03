// SPDX-License-Identifier: GPL-3.0-only

#include "DirectoryModel.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

DirectoryModel::DirectoryModel(QObject* parent)
    : QAbstractListModel(parent) {
    m_refreshDebounce.setSingleShot(true);
    m_refreshDebounce.setInterval(120);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &DirectoryModel::scan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_refreshDebounce.start();
    });

    m_path = home();
    watchCurrentDirectory();
    scan();
}

int DirectoryModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_visibleIndexes.size();
}

const DirectoryModel::Entry* DirectoryModel::entryForRow(int row) const {
    if (row < 0 || row >= m_visibleIndexes.size())
        return nullptr;

    const int sourceIndex = m_visibleIndexes.at(row);
    if (sourceIndex < 0 || sourceIndex >= m_allEntries.size())
        return nullptr;

    return &m_allEntries.at(sourceIndex);
}

QVariant DirectoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};

    const Entry* entry = entryForRow(index.row());
    if (!entry)
        return {};

    switch (role) {
    case NameRole: return entry->name;
    case PathRole: return entry->path;
    case DirectoryRole: return entry->directory;
    case SizeTextRole: return entry->sizeText;
    case ModifiedTextRole: return entry->modifiedText;
    case HiddenRole: return entry->hidden;
    case ThumbnailCandidateRole: return entry->thumbnailCandidate;
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

    const QDir directory(QDir::cleanPath(requestedPath));
    if (!directory.exists()) {
        emit errorOccurred(tr("Folder does not exist: %1").arg(requestedPath));
        return;
    }

    const QString absolute = directory.absolutePath();
    if (absolute == m_path)
        return;

    m_path = absolute;
    emit pathChanged();
    watchCurrentDirectory();
    scan();
}

void DirectoryModel::setShowHidden(bool show) {
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    emit showHiddenChanged();
    scan();
}

void DirectoryModel::setFilterText(const QString& text) {
    const QString normalized = text.trimmed();
    if (m_filterText == normalized)
        return;

    m_filterText = normalized;

    beginResetModel();
    rebuildFilterIndexes();
    endResetModel();

    emit filterTextChanged();
    emit countChanged();
}

void DirectoryModel::clearFilter() {
    setFilterText(QString());
}

void DirectoryModel::rebuildFilterIndexes() {
    m_visibleIndexes.clear();
    m_visibleIndexes.reserve(m_allEntries.size());

    const bool filter = !m_filterText.isEmpty();
    for (int i = 0; i < m_allEntries.size(); ++i) {
        const Entry& entry = m_allEntries.at(i);
        if (!filter || entry.name.contains(m_filterText, Qt::CaseInsensitive))
            m_visibleIndexes.push_back(i);
    }
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
    scan();
}

void DirectoryModel::goUp() {
    QDir directory(m_path);
    if (directory.cdUp())
        setPath(directory.absolutePath());
}

QString DirectoryModel::pathAt(int index) const {
    const Entry* entry = entryForRow(index);
    return entry ? entry->path : QString();
}

bool DirectoryModel::isDirectoryAt(int index) const {
    const Entry* entry = entryForRow(index);
    return entry ? entry->directory : false;
}

int DirectoryModel::indexOfPath(const QString& targetPath) const {
    if (targetPath.isEmpty())
        return -1;

    for (int row = 0; row < m_visibleIndexes.size(); ++row) {
        const Entry* entry = entryForRow(row);
        if (entry && entry->path == targetPath)
            return row;
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

void DirectoryModel::watchCurrentDirectory() {
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);

    if (QDir(m_path).exists())
        m_watcher.addPath(m_path);
}

void DirectoryModel::scan() {
    const quint64 generation = ++m_generation;
    const QString scanPath = m_path;
    const bool scanHidden = m_showHidden;
    setLoading(true);

    auto* watcher = new QFutureWatcher<QPair<QList<Entry>, QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, scanPath] {
        const auto [entries, error] = watcher->result();
        watcher->deleteLater();

        if (generation != m_generation || scanPath != m_path)
            return;

        beginResetModel();
        m_allEntries = entries;
        rebuildFilterIndexes();
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
