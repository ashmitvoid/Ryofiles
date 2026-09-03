// SPDX-License-Identifier: GPL-3.0-only

#include "DirectorySession.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

DirectorySession::DirectorySession(const QString& initialPath, QObject* parent)
    : QObject(parent)
    , m_model(nullptr) {
    connect(&m_model, &DirectoryModel::errorOccurred, this, &DirectorySession::errorOccurred);

    QString start = normalizeDirectoryPath(initialPath);
    if (start.isEmpty() || !QDir(start).exists())
        start = QDir::homePath();

    m_history.push_back({start, QString(), 0.0});
    m_historyIndex = 0;
    m_model.setPath(start);
}

QString DirectorySession::normalizeDirectoryPath(const QString& input) {
    QString path = input.trimmed();
    if (path.isEmpty())
        return {};

    if (path == QStringLiteral("~"))
        path = QDir::homePath();
    else if (path.startsWith(QStringLiteral("~/")))
        path = QDir::home().filePath(path.mid(2));

    return QDir(path).absolutePath();
}

DirectorySession::HistoryEntry* DirectorySession::currentEntry() {
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size())
        return nullptr;
    return &m_history[m_historyIndex];
}

const DirectorySession::HistoryEntry* DirectorySession::currentEntry() const {
    if (m_historyIndex < 0 || m_historyIndex >= m_history.size())
        return nullptr;
    return &m_history[m_historyIndex];
}

QString DirectorySession::path() const {
    const auto* entry = currentEntry();
    return entry ? entry->path : QDir::homePath();
}

QString DirectorySession::title() const {
    const QString currentPath = path();
    if (currentPath == QDir::homePath())
        return tr("Home");
    if (currentPath == QStringLiteral("/"))
        return tr("Root");

    const QString name = QFileInfo(currentPath).fileName();
    return name.isEmpty() ? currentPath : name;
}

bool DirectorySession::canGoBack() const {
    return m_historyIndex > 0;
}

bool DirectorySession::canGoForward() const {
    return m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size();
}

QString DirectorySession::selectedPath() const {
    const auto* entry = currentEntry();
    return entry ? entry->selectedPath : QString();
}

void DirectorySession::setSelectedPath(const QString& pathValue) {
    auto* entry = currentEntry();
    if (!entry || entry->selectedPath == pathValue)
        return;
    entry->selectedPath = pathValue;
    emit selectedPathChanged();
}

qreal DirectorySession::scrollPosition() const {
    const auto* entry = currentEntry();
    return entry ? entry->scrollPosition : 0.0;
}

void DirectorySession::setScrollPosition(qreal position) {
    auto* entry = currentEntry();
    if (!entry)
        return;

    const qreal bounded = qMax<qreal>(0.0, position);
    if (qFuzzyCompare(entry->scrollPosition + 1.0, bounded + 1.0))
        return;

    entry->scrollPosition = bounded;
    emit scrollPositionChanged();
}

bool DirectorySession::navigate(const QString& requestedPath) {
    const QString target = normalizeDirectoryPath(requestedPath);
    if (target.isEmpty() || !QDir(target).exists()) {
        emit errorOccurred(tr("Folder does not exist: %1").arg(requestedPath));
        return false;
    }

    if (target == path())
        return true;

    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();

    m_history.push_back({target, QString(), 0.0});
    m_historyIndex = m_history.size() - 1;
    applyHistoryEntry();
    return true;
}

void DirectorySession::applyHistoryEntry() {
    const auto* entry = currentEntry();
    if (!entry)
        return;

    m_model.setPath(entry->path);
    emit pathChanged();
    emit titleChanged();
    emit historyChanged();
    emit selectedPathChanged();
    emit scrollPositionChanged();
}

void DirectorySession::goBack() {
    if (!canGoBack())
        return;
    --m_historyIndex;
    applyHistoryEntry();
}

void DirectorySession::goForward() {
    if (!canGoForward())
        return;
    ++m_historyIndex;
    applyHistoryEntry();
}

void DirectorySession::goUp() {
    QDir directory(path());
    if (directory.cdUp())
        navigate(directory.absolutePath());
}

void DirectorySession::refresh() {
    m_model.refresh();
}

void DirectorySession::activate(int index) {
    const QString target = m_model.pathAt(index);
    if (target.isEmpty())
        return;

    if (m_model.isDirectoryAt(index)) {
        navigate(target);
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}
