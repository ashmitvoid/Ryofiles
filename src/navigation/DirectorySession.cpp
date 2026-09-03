// SPDX-License-Identifier: GPL-3.0-only

#include "DirectorySession.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <utility>

DirectorySession::DirectorySession(const QString& initialPath, QObject* parent)
    : QObject(parent)
    , m_model(nullptr) {
    connect(&m_model, &DirectoryModel::errorOccurred, this, &DirectorySession::errorOccurred);

    QString start = normalizeDirectoryPath(initialPath);
    if (start.isEmpty() || !QDir(start).exists())
        start = QDir::homePath();

    m_history.push_back({start, {}, QString(), QString(), 0.0});
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
    return entry ? entry->primarySelectedPath : QString();
}

int DirectorySession::selectionCount() const {
    const auto* entry = currentEntry();
    return entry ? entry->selectedPaths.size() : 0;
}

QStringList DirectorySession::selectedPaths() const {
    const auto* entry = currentEntry();
    if (!entry)
        return {};

    QStringList paths;
    paths.reserve(entry->selectedPaths.size());
    for (const QString& pathValue : entry->selectedPaths)
        paths.push_back(pathValue);
    return paths;
}

bool DirectorySession::isSelectedPath(const QString& pathValue) const {
    const auto* entry = currentEntry();
    return entry && entry->selectedPaths.contains(pathValue);
}

void DirectorySession::emitSelectionChanged() {
    ++m_selectionRevision;
    emit selectionChanged();
}

void DirectorySession::setSelectedPath(const QString& pathValue) {
    auto* entry = currentEntry();
    if (!entry)
        return;

    QSet<QString> next;
    if (!pathValue.isEmpty())
        next.insert(pathValue);

    if (entry->selectedPaths == next && entry->primarySelectedPath == pathValue)
        return;

    entry->selectedPaths = std::move(next);
    entry->primarySelectedPath = pathValue;
    entry->anchorPath = pathValue;
    emitSelectionChanged();
}

void DirectorySession::selectSingle(int index) {
    const QString target = m_model.pathAt(index);
    if (target.isEmpty())
        return;
    setSelectedPath(target);
}

void DirectorySession::toggleSelection(int index) {
    const QString target = m_model.pathAt(index);
    if (target.isEmpty())
        return;

    auto* entry = currentEntry();
    if (!entry)
        return;

    if (entry->selectedPaths.contains(target)) {
        entry->selectedPaths.remove(target);
        if (entry->primarySelectedPath == target) {
            entry->primarySelectedPath =
                entry->selectedPaths.isEmpty() ? QString() : *entry->selectedPaths.constBegin();
        }
    } else {
        entry->selectedPaths.insert(target);
        entry->primarySelectedPath = target;
    }

    entry->anchorPath = target;
    emitSelectionChanged();
}

void DirectorySession::selectRange(int index) {
    const QString target = m_model.pathAt(index);
    if (target.isEmpty())
        return;

    auto* entry = currentEntry();
    if (!entry)
        return;

    int anchorIndex = m_model.indexOfPath(entry->anchorPath);
    if (anchorIndex < 0)
        anchorIndex = m_model.indexOfPath(entry->primarySelectedPath);
    if (anchorIndex < 0)
        anchorIndex = index;

    const int first = qMin(anchorIndex, index);
    const int last = qMax(anchorIndex, index);

    QSet<QString> range;
    range.reserve(last - first + 1);
    for (int i = first; i <= last; ++i) {
        const QString pathValue = m_model.pathAt(i);
        if (!pathValue.isEmpty())
            range.insert(pathValue);
    }

    entry->selectedPaths = std::move(range);
    entry->primarySelectedPath = target;
    if (entry->anchorPath.isEmpty())
        entry->anchorPath = m_model.pathAt(anchorIndex);
    emitSelectionChanged();
}

void DirectorySession::selectAll() {
    auto* entry = currentEntry();
    if (!entry)
        return;

    QSet<QString> all;
    all.reserve(m_model.rowCount());
    for (int i = 0; i < m_model.rowCount(); ++i) {
        const QString pathValue = m_model.pathAt(i);
        if (!pathValue.isEmpty())
            all.insert(pathValue);
    }

    if (entry->selectedPaths == all)
        return;

    entry->selectedPaths = std::move(all);
    if (!entry->primarySelectedPath.isEmpty() &&
        !entry->selectedPaths.contains(entry->primarySelectedPath)) {
        entry->primarySelectedPath.clear();
    }
    if (entry->primarySelectedPath.isEmpty() && m_model.rowCount() > 0)
        entry->primarySelectedPath = m_model.pathAt(0);
    if (entry->anchorPath.isEmpty())
        entry->anchorPath = entry->primarySelectedPath;
    emitSelectionChanged();
}

void DirectorySession::clearSelection() {
    auto* entry = currentEntry();
    if (!entry || (entry->selectedPaths.isEmpty() &&
                   entry->primarySelectedPath.isEmpty() &&
                   entry->anchorPath.isEmpty()))
        return;

    entry->selectedPaths.clear();
    entry->primarySelectedPath.clear();
    entry->anchorPath.clear();
    emitSelectionChanged();
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

void DirectorySession::setViewMode(int mode) {
    const int bounded = qBound(
        static_cast<int>(CompactView),
        mode,
        static_cast<int>(DetailsView));

    if (m_viewMode == bounded)
        return;

    m_viewMode = bounded;
    emit viewModeChanged();
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

    m_history.push_back({target, {}, QString(), QString(), 0.0});
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
    emitSelectionChanged();
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
