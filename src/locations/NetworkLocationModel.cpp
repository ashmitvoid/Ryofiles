// SPDX-License-Identifier: GPL-3.0-only

#include <gio/gio.h>

#include "locations/NetworkLocationModel.hpp"

#include "locations/LocationSpec.hpp"

#include <QHash>
#include <QMetaObject>

#include <algorithm>
#include <utility>

namespace {
QString takeUtf8(gchar* value) {
    if (!value)
        return {};
    const QString result = QString::fromUtf8(value);
    g_free(value);
    return result;
}

QString fileUri(GFile* file) {
    if (!file)
        return {};
    return takeUtf8(g_file_get_uri(file));
}

NetworkMountSnapshot snapshotForMount(GMount* mount) {
    NetworkMountSnapshot snapshot;
    if (!mount)
        return snapshot;

    snapshot.name = takeUtf8(g_mount_get_name(mount));
    snapshot.canUnmount = g_mount_can_unmount(mount);
    snapshot.shadowed = g_mount_is_shadowed(mount);

    GFile* root = g_mount_get_root(mount);
    snapshot.rootUri = fileUri(root);
    if (root)
        g_object_unref(root);

    GFile* defaultLocation = g_mount_get_default_location(mount);
    snapshot.defaultUri = fileUri(defaultLocation);
    if (defaultLocation)
        g_object_unref(defaultLocation);

    return snapshot;
}

bool itemLessThan(const NetworkLocationItem& lhs, const NetworkLocationItem& rhs) {
    const int nameOrder = QString::compare(lhs.name, rhs.name, Qt::CaseInsensitive);
    if (nameOrder != 0)
        return nameOrder < 0;
    return lhs.uri < rhs.uri;
}
} // namespace

NetworkLocationModel::NetworkLocationModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_monitor(g_volume_monitor_get()) {
    if (!m_monitor)
        return;

    const auto changed = +[](GVolumeMonitor*, GMount*, gpointer userData) {
        auto* self = static_cast<NetworkLocationModel*>(userData);
        if (self)
            self->scheduleRefresh();
    };

    g_signal_connect(m_monitor, "mount-added", G_CALLBACK(changed), this);
    g_signal_connect(m_monitor, "mount-removed", G_CALLBACK(changed), this);
    g_signal_connect(m_monitor, "mount-changed", G_CALLBACK(changed), this);

    rebuildFromMonitor();
}

NetworkLocationModel::~NetworkLocationModel() {
    if (!m_monitor)
        return;
    g_signal_handlers_disconnect_by_data(m_monitor, this);
    g_object_unref(m_monitor);
    m_monitor = nullptr;
}

int NetworkLocationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant NetworkLocationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const NetworkLocationItem& item = m_items.at(index.row());
    switch (role) {
    case NameRole:
        return item.name;
    case UriRole:
        return item.uri;
    case RootUriRole:
        return item.rootUri;
    case SchemeRole:
        return item.scheme;
    case HostRole:
        return item.host;
    case CanUnmountRole:
        return item.canUnmount;
    default:
        return {};
    }
}

QHash<int, QByteArray> NetworkLocationModel::roleNames() const {
    return {
        {NameRole, "name"},
        {UriRole, "uri"},
        {RootUriRole, "rootUri"},
        {SchemeRole, "scheme"},
        {HostRole, "host"},
        {CanUnmountRole, "canUnmount"},
    };
}

QVector<NetworkLocationItem> NetworkLocationModel::locationsFromSnapshots(
    const QVector<NetworkMountSnapshot>& snapshots) {
    QVector<NetworkLocationItem> result;
    QHash<QString, int> indexByRoot;

    for (const NetworkMountSnapshot& snapshot : snapshots) {
        if (snapshot.shadowed)
            continue;

        const LocationSpec root = LocationSpec::parse(snapshot.rootUri);
        if (!root.isNetwork())
            continue;

        LocationSpec entry = LocationSpec::parse(snapshot.defaultUri);
        if (!entry.isNetwork())
            entry = root;

        const QString dedupeKey = root.canonical;
        auto existing = indexByRoot.constFind(dedupeKey);
        if (existing != indexByRoot.constEnd()) {
            NetworkLocationItem& item = result[*existing];
            item.canUnmount = item.canUnmount || snapshot.canUnmount;
            if (item.name.isEmpty() && !snapshot.name.trimmed().isEmpty())
                item.name = snapshot.name.trimmed();
            continue;
        }

        NetworkLocationItem item;
        item.name = snapshot.name.trimmed();
        if (item.name.isEmpty())
            item.name = entry.displayName;
        item.uri = entry.canonical;
        item.rootUri = root.canonical;
        item.scheme = root.scheme;
        item.host = root.host;
        item.canUnmount = snapshot.canUnmount;

        indexByRoot.insert(dedupeKey, result.size());
        result.push_back(std::move(item));
    }

    std::sort(result.begin(), result.end(), itemLessThan);
    return result;
}

void NetworkLocationModel::refresh() {
    scheduleRefresh();
}

void NetworkLocationModel::scheduleRefresh() {
    if (m_refreshScheduled)
        return;
    m_refreshScheduled = true;

    QMetaObject::invokeMethod(
        this,
        [this] {
            m_refreshScheduled = false;
            rebuildFromMonitor();
        },
        Qt::QueuedConnection);
}

void NetworkLocationModel::rebuildFromMonitor() {
    QVector<NetworkMountSnapshot> snapshots;

    if (m_monitor) {
        GList* mounts = g_volume_monitor_get_mounts(m_monitor);
        for (GList* node = mounts; node; node = node->next) {
            auto* mount = G_MOUNT(node->data);
            snapshots.push_back(snapshotForMount(mount));
            g_object_unref(mount);
        }
        g_list_free(mounts);
    }

    QVector<NetworkLocationItem> next = locationsFromSnapshots(snapshots);
    const bool countWillChange = next.size() != m_items.size();

    beginResetModel();
    m_items = std::move(next);
    endResetModel();

    if (countWillChange)
        emit countChanged();
}
