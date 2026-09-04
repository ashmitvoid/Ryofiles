// SPDX-License-Identifier: GPL-3.0-only

#include "locations/SessionFileModel.hpp"

#include "fs/DirectoryModel.hpp"
#include "locations/RemoteDirectoryModel.hpp"

#include <utility>

static_assert(DirectoryModel::NameRole == RemoteDirectoryModel::NameRole);
static_assert(DirectoryModel::PathRole == RemoteDirectoryModel::UriRole);
static_assert(DirectoryModel::DirectoryRole == RemoteDirectoryModel::DirectoryRole);
static_assert(DirectoryModel::SizeTextRole == RemoteDirectoryModel::SizeTextRole);
static_assert(DirectoryModel::ModifiedTextRole == RemoteDirectoryModel::ModifiedTextRole);
static_assert(DirectoryModel::HiddenRole == RemoteDirectoryModel::HiddenRole);

SessionFileModel::SessionFileModel(QObject* parent)
    : QIdentityProxyModel(parent) {
    connect(this, &QAbstractItemModel::modelReset, this, &SessionFileModel::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, [this] { emit countChanged(); });
    connect(this, &QAbstractItemModel::rowsRemoved, this, [this] { emit countChanged(); });
}

QVariant SessionFileModel::data(const QModelIndex& index, int role) const {
    if (m_remoteActive && role == ThumbnailCandidateRole)
        return false;
    return QIdentityProxyModel::data(index, role);
}

QHash<int, QByteArray> SessionFileModel::roleNames() const {
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

void SessionFileModel::useLocal(DirectoryModel* model) {
    if (!model)
        return;
    m_local = model;
    connectLocal(model);
    setBackend(model, false);
}

void SessionFileModel::useRemote(RemoteDirectoryModel* model) {
    if (!model)
        return;
    m_remote = model;
    connectRemote(model);
    setBackend(model, true);
}

bool SessionFileModel::loading() const {
    if (m_remoteActive)
        return m_remote ? m_remote->loading() : false;
    return m_local ? m_local->loading() : false;
}

bool SessionFileModel::showHidden() const {
    if (m_remoteActive)
        return m_remote ? m_remote->showHidden() : false;
    return m_local ? m_local->showHidden() : false;
}

void SessionFileModel::setShowHidden(bool show) {
    if (m_remoteActive) {
        if (m_remote)
            m_remote->setShowHidden(show);
        return;
    }
    if (m_local)
        m_local->setShowHidden(show);
}

QString SessionFileModel::filterQuery() const {
    if (m_remoteActive)
        return m_remote ? m_remote->filterQuery() : QString();
    return m_local ? m_local->filterQuery() : QString();
}

void SessionFileModel::setFilterQuery(const QString& query) {
    if (m_remoteActive) {
        if (m_remote)
            m_remote->setFilterQuery(query);
        return;
    }
    if (m_local)
        m_local->setFilterQuery(query);
}

void SessionFileModel::refresh() {
    if (m_remoteActive) {
        if (m_remote)
            m_remote->refresh();
        return;
    }
    if (m_local)
        m_local->refresh();
}

QString SessionFileModel::pathAt(int index) const {
    if (index < 0 || index >= rowCount())
        return {};
    return data(this->index(index, 0), PathRole).toString();
}

bool SessionFileModel::isDirectoryAt(int index) const {
    if (index < 0 || index >= rowCount())
        return false;
    return data(this->index(index, 0), DirectoryRole).toBool();
}

int SessionFileModel::indexOfPath(const QString& path) const {
    if (path.isEmpty())
        return -1;

    for (int index = 0; index < rowCount(); ++index) {
        if (pathAt(index) == path)
            return index;
    }
    return -1;
}

void SessionFileModel::setBackend(QAbstractItemModel* model, bool remote) {
    if (sourceModel() == model && m_remoteActive == remote)
        return;

    const bool kindChanged = m_remoteActive != remote;
    m_remoteActive = remote;
    setSourceModel(model);

    if (kindChanged)
        emit sourceKindChanged();
    emit loadingChanged();
    emit showHiddenChanged();
    emit filterQueryChanged();
    emit countChanged();
}

void SessionFileModel::clearBackendConnections() {
    for (const QMetaObject::Connection& connection : std::as_const(m_backendConnections))
        QObject::disconnect(connection);
    m_backendConnections.clear();
}

void SessionFileModel::connectLocal(DirectoryModel* model) {
    clearBackendConnections();
    m_backendConnections.push_back(connect(
        model, &DirectoryModel::loadingChanged,
        this, &SessionFileModel::loadingChanged));
    m_backendConnections.push_back(connect(
        model, &DirectoryModel::showHiddenChanged,
        this, &SessionFileModel::showHiddenChanged));
    m_backendConnections.push_back(connect(
        model, &DirectoryModel::filterQueryChanged,
        this, &SessionFileModel::filterQueryChanged));
}

void SessionFileModel::connectRemote(RemoteDirectoryModel* model) {
    clearBackendConnections();
    m_backendConnections.push_back(connect(
        model, &RemoteDirectoryModel::loadingChanged,
        this, &SessionFileModel::loadingChanged));
    m_backendConnections.push_back(connect(
        model, &RemoteDirectoryModel::showHiddenChanged,
        this, &SessionFileModel::showHiddenChanged));
    m_backendConnections.push_back(connect(
        model, &RemoteDirectoryModel::filterQueryChanged,
        this, &SessionFileModel::filterQueryChanged));
}
