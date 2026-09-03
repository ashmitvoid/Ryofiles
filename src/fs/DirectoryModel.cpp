// SPDX-License-Identifier: GPL-3.0-only

#include "DirectoryModel.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
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
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DirectoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry& entry = m_entries.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case PathRole: return entry.path;
    case DirectoryRole: return entry.directory;
    case SizeTextRole: return entry.sizeText;
    case ModifiedTextRole: return entry.modifiedText;
    case HiddenRole: return entry.hidden;
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

void DirectoryModel::activate(int index) {
    if (index < 0 || index >= m_entries.size())
        return;

    const Entry& entry = m_entries.at(index);
    if (entry.directory) {
        setPath(entry.path);
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(entry.path));
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
        m_entries = entries;
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
