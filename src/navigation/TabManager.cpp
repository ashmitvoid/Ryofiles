// SPDX-License-Identifier: GPL-3.0-only

#include "TabManager.hpp"

#include "../integrations/MountRecoveryRegistry.hpp"

#include <QDir>

#include <utility>

TabManager::TabManager(QObject* parent)
    : QAbstractListModel(parent) {
    m_mountRecoverySubscription = MountRecoveryRegistry::instance().subscribe(
        [this](const QString& mountRoot) {
            recoverUnmountedMount(mountRoot);
        });
    newTab(QDir::homePath());
}

TabManager::~TabManager() {
    MountRecoveryRegistry::instance().unsubscribe(m_mountRecoverySubscription);
}

int TabManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_tabs.size();
}

QVariant TabManager::data(const QModelIndex& indexValue, int role) const {
    if (!indexValue.isValid() || indexValue.row() < 0 || indexValue.row() >= m_tabs.size())
        return {};

    const int row = indexValue.row();
    DirectorySession* session = m_tabs.at(row);
    DirectorySession* secondary = m_splitStates.at(row).secondary;

    switch (role) {
    case TitleRole:
        if (secondary)
            return session->title() + QStringLiteral("  |  ") + secondary->title();
        return session->title();
    case PathRole:
        return session->path();
    case SessionRole:
        return QVariant::fromValue(session);
    case ActiveRole:
        return row == m_currentIndex;
    default:
        return {};
    }
}

QHash<int, QByteArray> TabManager::roleNames() const {
    return {
        {TitleRole, "title"},
        {PathRole, "path"},
        {SessionRole, "session"},
        {ActiveRole, "active"},
    };
}

DirectorySession* TabManager::primarySession() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_tabs.size())
        return nullptr;
    return m_tabs.at(m_currentIndex);
}

DirectorySession* TabManager::secondarySession() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_splitStates.size())
        return nullptr;
    return m_splitStates.at(m_currentIndex).secondary;
}

DirectorySession* TabManager::currentSession() const {
    if (activePane() == 1) {
        if (auto* secondary = secondarySession())
            return secondary;
    }
    return primarySession();
}

bool TabManager::split() const {
    return secondarySession() != nullptr;
}

int TabManager::activePane() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_splitStates.size())
        return 0;
    if (!m_splitStates.at(m_currentIndex).secondary)
        return 0;
    return m_splitStates.at(m_currentIndex).activePane;
}

void TabManager::setActivePane(int pane) {
    if (m_currentIndex < 0 || m_currentIndex >= m_splitStates.size())
        return;

    const int bounded = (pane == 1 && m_splitStates.at(m_currentIndex).secondary) ? 1 : 0;
    auto& state = m_splitStates[m_currentIndex];
    if (state.activePane == bounded)
        return;

    state.activePane = bounded;
    emit activePaneChanged();
    emit currentSessionChanged();
}

void TabManager::setCurrentIndex(int indexValue) {
    if (indexValue < 0 || indexValue >= m_tabs.size() || indexValue == m_currentIndex)
        return;

    const int previous = m_currentIndex;
    m_currentIndex = indexValue;

    if (previous >= 0 && previous < m_tabs.size())
        emit dataChanged(index(previous), index(previous), {ActiveRole});
    emit dataChanged(index(m_currentIndex), index(m_currentIndex), {ActiveRole});

    emit currentIndexChanged();
    emitCurrentSplitStateChanged();
}

void TabManager::attachSession(DirectorySession* session) {
    connect(session, &DirectorySession::titleChanged, this, [this, session] {
        emitTabChanged(session, {TitleRole});
    });
    connect(session, &DirectorySession::pathChanged, this, [this, session] {
        emitTabChanged(session, {PathRole, TitleRole});
    });
}

int TabManager::tabIndexForSession(DirectorySession* session) const {
    const qsizetype primaryIndex = m_tabs.indexOf(session);
    if (primaryIndex >= 0)
        return static_cast<int>(primaryIndex);

    for (int i = 0; i < m_splitStates.size(); ++i) {
        if (m_splitStates.at(i).secondary == session)
            return i;
    }
    return -1;
}

void TabManager::emitTabChanged(DirectorySession* session, const QList<int>& roles) {
    const int idx = tabIndexForSession(session);
    if (idx < 0)
        return;
    emit dataChanged(index(idx), index(idx), roles);
}

void TabManager::emitCurrentSplitStateChanged() {
    emit splitChanged();
    emit activePaneChanged();
    emit currentSessionChanged();
}

void TabManager::newTab(const QString& requestedPath) {
    QString path = requestedPath;
    if (path.isEmpty()) {
        if (auto* current = currentSession())
            path = current->path();
        else
            path = QDir::homePath();
    }

    const int insertAt = m_tabs.size();
    beginInsertRows({}, insertAt, insertAt);
    auto* session = new DirectorySession(path, this);
    attachSession(session);
    m_tabs.push_back(session);
    m_splitStates.push_back({});
    endInsertRows();

    emit countChanged();
    setCurrentIndex(insertAt);
}

void TabManager::duplicateCurrentTab() {
    if (auto* current = currentSession())
        newTab(current->path());
}

void TabManager::closeTab(int indexValue) {
    if (indexValue < 0 || indexValue >= m_tabs.size())
        return;

    if (m_tabs.size() == 1) {
        if (m_splitStates.front().secondary)
            closeSplit();
        DirectorySession* only = m_tabs.front();
        if (only->path() != QDir::homePath())
            only->navigate(QDir::homePath());
        return;
    }

    DirectorySession* primary = m_tabs.at(indexValue);
    DirectorySession* secondary = m_splitStates.at(indexValue).secondary;
    m_closedTabs.push_back({
        primary->path(),
        secondary ? secondary->path() : QString(),
        secondary != nullptr,
        m_splitStates.at(indexValue).activePane,
    });
    while (m_closedTabs.size() > 10)
        m_closedTabs.removeFirst();

    const int previousCurrent = m_currentIndex;
    beginRemoveRows({}, indexValue, indexValue);
    m_tabs.removeAt(indexValue);
    m_splitStates.removeAt(indexValue);
    endRemoveRows();

    primary->deleteLater();
    if (secondary)
        secondary->deleteLater();

    if (indexValue < previousCurrent)
        m_currentIndex = previousCurrent - 1;
    else if (indexValue == previousCurrent)
        m_currentIndex = qMin(indexValue, m_tabs.size() - 1);

    emit countChanged();
    emit currentIndexChanged();
    emitCurrentSplitStateChanged();

    if (m_currentIndex >= 0)
        emit dataChanged(index(m_currentIndex), index(m_currentIndex), {ActiveRole});
}

void TabManager::closeCurrentTab() {
    closeTab(m_currentIndex);
}

void TabManager::reopenClosedTab() {
    if (m_closedTabs.isEmpty())
        return;

    const ClosedTab tab = m_closedTabs.takeLast();
    newTab(tab.primaryPath);
    if (tab.split) {
        openSplit(tab.secondaryPath);
        setActivePane(tab.activePane);
    }
}

void TabManager::nextTab() {
    if (m_tabs.size() < 2)
        return;
    setCurrentIndex((m_currentIndex + 1) % m_tabs.size());
}

void TabManager::previousTab() {
    if (m_tabs.size() < 2)
        return;
    setCurrentIndex((m_currentIndex - 1 + m_tabs.size()) % m_tabs.size());
}

void TabManager::toggleSplitView() {
    if (split())
        closeSplit();
    else
        openSplit();
}

void TabManager::openSplit(const QString& requestedPath) {
    if (m_currentIndex < 0 || m_currentIndex >= m_tabs.size())
        return;

    auto& state = m_splitStates[m_currentIndex];
    QString path = requestedPath;
    if (path.isEmpty())
        path = primarySession() ? primarySession()->path() : QDir::homePath();

    if (state.secondary) {
        state.secondary->navigate(path);
        return;
    }

    state.secondary = new DirectorySession(path, this);
    attachSession(state.secondary);
    state.activePane = 0;

    emit dataChanged(index(m_currentIndex), index(m_currentIndex), {TitleRole});
    emit splitChanged();
}

void TabManager::closeSplit() {
    if (m_currentIndex < 0 || m_currentIndex >= m_splitStates.size())
        return;

    auto& state = m_splitStates[m_currentIndex];
    DirectorySession* secondary = state.secondary;
    if (!secondary)
        return;

    const bool currentChanged = state.activePane == 1;
    state.secondary = nullptr;
    state.activePane = 0;
    secondary->deleteLater();

    emit dataChanged(index(m_currentIndex), index(m_currentIndex), {TitleRole});
    emit splitChanged();
    emit activePaneChanged();
    if (currentChanged)
        emit currentSessionChanged();
}

void TabManager::swapPanes() {
    if (m_currentIndex < 0 || m_currentIndex >= m_splitStates.size())
        return;

    auto& state = m_splitStates[m_currentIndex];
    if (!state.secondary)
        return;

    std::swap(m_tabs[m_currentIndex], state.secondary);
    emit dataChanged(
        index(m_currentIndex),
        index(m_currentIndex),
        {TitleRole, PathRole, SessionRole});
    emit splitChanged();
    emit currentSessionChanged();
}

int TabManager::recoverUnmountedMount(
    const QString& mountRoot,
    const QString& preferredFallback) {
    if (mountRoot.trimmed().isEmpty())
        return 0;

    const QString fallback = DirectorySession::recoveryPathForUnmount(
        mountRoot,
        preferredFallback);
    int recoveredSessions = 0;

    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs.at(i)->recoverFromUnmount(mountRoot, fallback))
            ++recoveredSessions;

        DirectorySession* secondary = m_splitStates.at(i).secondary;
        if (secondary && secondary->recoverFromUnmount(mountRoot, fallback))
            ++recoveredSessions;
    }

    for (ClosedTab& tab : m_closedTabs) {
        if (DirectorySession::pathInsideRoot(tab.primaryPath, mountRoot))
            tab.primaryPath = fallback;
        if (tab.split && DirectorySession::pathInsideRoot(tab.secondaryPath, mountRoot))
            tab.secondaryPath = fallback;
    }

    return recoveredSessions;
}
