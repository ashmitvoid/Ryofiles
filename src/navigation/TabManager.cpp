// SPDX-License-Identifier: GPL-3.0-only

#include "TabManager.hpp"

#include <QDir>

TabManager::TabManager(QObject* parent)
    : QAbstractListModel(parent) {
    newTab(QDir::homePath());
}

int TabManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_tabs.size();
}

QVariant TabManager::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tabs.size())
        return {};

    DirectorySession* session = m_tabs.at(index.row());
    switch (role) {
    case TitleRole:
        return session->title();
    case PathRole:
        return session->path();
    case SessionRole:
        return QVariant::fromValue(session);
    case ActiveRole:
        return index.row() == m_currentIndex;
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

DirectorySession* TabManager::currentSession() const {
    if (m_currentIndex < 0 || m_currentIndex >= m_tabs.size())
        return nullptr;
    return m_tabs.at(m_currentIndex);
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
    emit currentSessionChanged();
}

void TabManager::attachSession(DirectorySession* session) {
    connect(session, &DirectorySession::titleChanged, this, [this, session] {
        emitTabChanged(session, {TitleRole});
    });
    connect(session, &DirectorySession::pathChanged, this, [this, session] {
        emitTabChanged(session, {PathRole, TitleRole});
    });
}

void TabManager::emitTabChanged(DirectorySession* session, const QList<int>& roles) {
    const qsizetype idx = m_tabs.indexOf(session);
    if (idx < 0)
        return;
    emit dataChanged(index(static_cast<int>(idx)), index(static_cast<int>(idx)), roles);
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
        DirectorySession* only = m_tabs.front();
        if (only->path() != QDir::homePath())
            only->navigate(QDir::homePath());
        return;
    }

    DirectorySession* session = m_tabs.at(indexValue);
    m_closedTabs.push_back({session->path()});
    while (m_closedTabs.size() > 10)
        m_closedTabs.removeFirst();

    const int previousCurrent = m_currentIndex;
    beginRemoveRows({}, indexValue, indexValue);
    m_tabs.removeAt(indexValue);
    endRemoveRows();
    session->deleteLater();

    if (indexValue < previousCurrent)
        m_currentIndex = previousCurrent - 1;
    else if (indexValue == previousCurrent)
        m_currentIndex = qMin(indexValue, m_tabs.size() - 1);

    emit countChanged();
    emit currentIndexChanged();
    emit currentSessionChanged();

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
    newTab(tab.path);
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
