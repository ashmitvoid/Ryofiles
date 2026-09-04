// SPDX-License-Identifier: GPL-3.0-only

#include "DriveModel.hpp"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QMetaType>

#include <algorithm>
#include <utility>

namespace {
QString textProperty(const UDisksPropertyMap& properties, const char* key) {
    const QVariant value = properties.value(QString::fromLatin1(key));
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant().toString().trimmed();
    return value.toString().trimmed();
}

bool boolProperty(const UDisksPropertyMap& properties, const char* key) {
    const QVariant value = properties.value(QString::fromLatin1(key));
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant().toBool();
    return value.toBool();
}

quint64 uint64Property(const UDisksPropertyMap& properties, const char* key) {
    const QVariant value = properties.value(QString::fromLatin1(key));
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant().toULongLong();
    return value.toULongLong();
}
} // namespace

DriveModel::DriveModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_bus(QDBusConnection::systemBus())
    , m_serviceWatcher(
          QString::fromLatin1(kService),
          m_bus,
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
          this) {
    registerDbusTypes();

    connect(
        &m_serviceWatcher,
        &QDBusServiceWatcher::serviceRegistered,
        this,
        &DriveModel::onServiceRegistered);
    connect(
        &m_serviceWatcher,
        &QDBusServiceWatcher::serviceUnregistered,
        this,
        &DriveModel::onServiceUnregistered);

    if (!m_bus.isConnected()) {
        setLastError(tr("System D-Bus is unavailable"));
        return;
    }

    m_bus.connect(
        QString::fromLatin1(kService),
        QString::fromLatin1(kRootPath),
        QString::fromLatin1(kObjectManagerInterface),
        QStringLiteral("InterfacesAdded"),
        this,
        SLOT(onInterfacesAdded(QDBusObjectPath,UDisksInterfaceMap)));
    m_bus.connect(
        QString::fromLatin1(kService),
        QString::fromLatin1(kRootPath),
        QString::fromLatin1(kObjectManagerInterface),
        QStringLiteral("InterfacesRemoved"),
        this,
        SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    m_bus.connect(
        QString::fromLatin1(kService),
        QString(),
        QString::fromLatin1(kPropertiesInterface),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));

    refresh();
}

int DriveModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DriveModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const DriveItem& item = m_items.at(index.row());
    switch (role) {
    case ObjectPathRole:
        return item.objectPath;
    case DriveObjectPathRole:
        return item.driveObjectPath;
    case NameRole:
        return item.name;
    case DevicePathRole:
        return item.devicePath;
    case MountPointRole:
        return item.mountPoint;
    case FsTypeRole:
        return item.fsType;
    case ConnectionBusRole:
        return item.connectionBus;
    case SizeBytesRole:
        return QVariant::fromValue(item.sizeBytes);
    case SizeTextRole:
        return formatSize(item.sizeBytes);
    case MountedRole:
        return item.mounted;
    case RemovableRole:
        return item.removable;
    case ReadOnlyRole:
        return item.readOnly;
    case CanPowerOffRole:
        return item.canPowerOff;
    case BusyRole:
        return m_busyObjects.contains(item.objectPath);
    case Qt::DisplayRole:
        return item.name;
    default:
        return {};
    }
}

QHash<int, QByteArray> DriveModel::roleNames() const {
    return {
        {ObjectPathRole, "objectPath"},
        {DriveObjectPathRole, "driveObjectPath"},
        {NameRole, "name"},
        {DevicePathRole, "devicePath"},
        {MountPointRole, "mountPoint"},
        {FsTypeRole, "fsType"},
        {ConnectionBusRole, "connectionBus"},
        {SizeBytesRole, "sizeBytes"},
        {SizeTextRole, "sizeText"},
        {MountedRole, "mounted"},
        {RemovableRole, "removable"},
        {ReadOnlyRole, "readOnly"},
        {CanPowerOffRole, "canPowerOff"},
        {BusyRole, "busy"},
    };
}

void DriveModel::registerDbusTypes() {
    static const bool registered = [] {
        qRegisterMetaType<UDisksInterfaceMap>("UDisksInterfaceMap");
        qRegisterMetaType<UDisksManagedObjectMap>("UDisksManagedObjectMap");
        qDBusRegisterMetaType<QVariantMap>();
        qDBusRegisterMetaType<UDisksInterfaceMap>();
        qDBusRegisterMetaType<UDisksManagedObjectMap>();
        return true;
    }();
    Q_UNUSED(registered);
}

QVariant DriveModel::unwrapped(const QVariant& value) {
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QString DriveModel::bytePath(const QVariant& rawValue) {
    const QVariant value = unwrapped(rawValue);
    QByteArray bytes;

    if (value.metaType() == QMetaType::fromType<QByteArray>())
        bytes = value.toByteArray();
    else if (value.canConvert<QByteArray>())
        bytes = value.toByteArray();

    while (!bytes.isEmpty() && bytes.endsWith('\0'))
        bytes.chop(1);
    return bytes.isEmpty() ? QString() : QFile::decodeName(bytes);
}

QString DriveModel::firstMountPoint(const QVariant& rawValue) {
    const QVariant value = unwrapped(rawValue);

    if (value.metaType() == QMetaType::fromType<QList<QByteArray>>()) {
        const auto paths = value.value<QList<QByteArray>>();
        for (QByteArray bytes : paths) {
            while (!bytes.isEmpty() && bytes.endsWith('\0'))
                bytes.chop(1);
            if (!bytes.isEmpty())
                return QFile::decodeName(bytes);
        }
        return {};
    }

    if (value.metaType() == QMetaType::fromType<QVariantList>()) {
        const QVariantList paths = value.toList();
        for (const QVariant& pathValue : paths) {
            const QString path = bytePath(pathValue);
            if (!path.isEmpty())
                return path;
        }
        return {};
    }

    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd()) {
            QByteArray bytes;
            argument >> bytes;
            while (!bytes.isEmpty() && bytes.endsWith('\0'))
                bytes.chop(1);
            if (!bytes.isEmpty()) {
                argument.endArray();
                return QFile::decodeName(bytes);
            }
        }
        argument.endArray();
    }

    return {};
}

QString DriveModel::objectPath(const QVariant& rawValue) {
    const QVariant value = unwrapped(rawValue);
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}

QString DriveModel::displayName(
    const UDisksPropertyMap& block,
    const UDisksPropertyMap& drive,
    const QString& devicePath) {
    QString name = textProperty(block, "HintName");
    if (name.isEmpty())
        name = textProperty(block, "IdLabel");

    if (name.isEmpty()) {
        const QString vendor = textProperty(drive, "Vendor");
        const QString model = textProperty(drive, "Model");
        name = (vendor + QStringLiteral(" ") + model).trimmed();
    }

    if (name.isEmpty())
        name = QFileInfo(devicePath).fileName();
    if (name.isEmpty())
        name = tr("Storage");
    return name;
}

QVector<DriveItem> DriveModel::volumesFromManagedObjects(const UDisksManagedObjectMap& objects) {
    QVector<DriveItem> items;

    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const UDisksInterfaceMap& interfaces = it.value();
        if (!interfaces.contains(QString::fromLatin1(kBlockInterface)) ||
            !interfaces.contains(QString::fromLatin1(kFilesystemInterface)) ||
            interfaces.contains(QString::fromLatin1(kLoopInterface))) {
            continue;
        }

        const UDisksPropertyMap& block = interfaces.value(QString::fromLatin1(kBlockInterface));
        const UDisksPropertyMap& filesystem = interfaces.value(QString::fromLatin1(kFilesystemInterface));

        if (boolProperty(block, "HintIgnore") || boolProperty(block, "HintSystem"))
            continue;

        QString devicePath = bytePath(block.value(QStringLiteral("PreferredDevice")));
        if (devicePath.isEmpty())
            devicePath = bytePath(block.value(QStringLiteral("Device")));

        const QString drivePath = objectPath(block.value(QStringLiteral("Drive")));
        UDisksPropertyMap drive;
        if (!drivePath.isEmpty() && drivePath != QStringLiteral("/")) {
            const auto driveIt = objects.constFind(QDBusObjectPath(drivePath));
            if (driveIt != objects.cend())
                drive = driveIt.value().value(QString::fromLatin1(kDriveInterface));
        }

        const QString connectionBus = textProperty(drive, "ConnectionBus").toLower();
        const bool removable =
            boolProperty(drive, "Removable") ||
            boolProperty(drive, "MediaRemovable") ||
            connectionBus == QStringLiteral("usb") ||
            connectionBus == QStringLiteral("ieee1394") ||
            connectionBus == QStringLiteral("sdio");

        const QString mountPoint = firstMountPoint(filesystem.value(QStringLiteral("MountPoints")));
        quint64 sizeBytes = uint64Property(block, "Size");
        if (sizeBytes == 0)
            sizeBytes = uint64Property(drive, "Size");

        items.push_back({
            it.key().path(),
            drivePath,
            displayName(block, drive, devicePath),
            devicePath,
            mountPoint,
            textProperty(block, "IdType"),
            connectionBus,
            sizeBytes,
            !mountPoint.isEmpty(),
            removable,
            boolProperty(block, "ReadOnly"),
            boolProperty(drive, "CanPowerOff"),
        });
    }

    std::sort(items.begin(), items.end(), [](const DriveItem& left, const DriveItem& right) {
        if (left.removable != right.removable)
            return left.removable > right.removable;
        const int byName = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        return left.devicePath < right.devicePath;
    });

    return items;
}

QString DriveModel::formatSize(quint64 bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }

    const int decimals = unit == 0 ? 0 : (value < 10.0 ? 1 : 0);
    return QStringLiteral("%1 %2")
        .arg(QString::number(value, 'f', decimals), QString::fromLatin1(units[unit]));
}

void DriveModel::refresh() {
    if (!m_bus.isConnected()) {
        setAvailable(false);
        setLastError(tr("System D-Bus is unavailable"));
        return;
    }

    if (m_loading) {
        m_refreshPending = true;
        return;
    }

    setLoading(true);

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService),
        QString::fromLatin1(kRootPath),
        QString::fromLatin1(kObjectManagerInterface),
        QStringLiteral("GetManagedObjects"));

    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        QDBusPendingReply<UDisksManagedObjectMap> reply = *call;
        call->deleteLater();
        setLoading(false);

        if (reply.isError()) {
            setAvailable(false);
            replaceItems({});
            setLastError(reply.error().message().isEmpty()
                ? tr("UDisks2 is unavailable")
                : reply.error().message());
        } else {
            setAvailable(true);
            setLastError({});
            replaceItems(volumesFromManagedObjects(reply.value()));
        }

        if (std::exchange(m_refreshPending, false))
            refresh();
    });
}

void DriveModel::mount(const QString& targetObjectPath) {
    const int row = indexForObjectPath(targetObjectPath);
    if (row < 0) {
        const QString message = tr("Storage volume is no longer available");
        setLastError(message);
        emit operationFailed(targetObjectPath, message);
        return;
    }

    const DriveItem item = m_items.at(row);
    if (m_busyObjects.contains(targetObjectPath))
        return;
    if (item.mounted) {
        emit mounted(targetObjectPath, item.mountPoint);
        return;
    }

    setBusy(targetObjectPath, true);
    setLastError({});

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService),
        targetObjectPath,
        QString::fromLatin1(kFilesystemInterface),
        QStringLiteral("Mount"));
    message << QVariantMap{};

    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, targetObjectPath](QDBusPendingCallWatcher* call) {
            QDBusPendingReply<QString> reply = *call;
            call->deleteLater();
            setBusy(targetObjectPath, false);

            if (reply.isError()) {
                const QString error = reply.error().message().isEmpty()
                    ? tr("Could not mount storage volume")
                    : reply.error().message();
                setLastError(error);
                emit operationFailed(targetObjectPath, error);
                requestRefreshAfterCurrent();
                return;
            }

            setLastError({});
            emit mounted(targetObjectPath, reply.value());
            requestRefreshAfterCurrent();
        });
}

void DriveModel::unmount(const QString& targetObjectPath) {
    const int row = indexForObjectPath(targetObjectPath);
    if (row < 0) {
        const QString message = tr("Storage volume is no longer available");
        setLastError(message);
        emit operationFailed(targetObjectPath, message);
        return;
    }

    const DriveItem item = m_items.at(row);
    if (m_busyObjects.contains(targetObjectPath))
        return;
    if (!item.mounted) {
        emit unmounted(targetObjectPath);
        return;
    }

    setBusy(targetObjectPath, true);
    setLastError({});

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService),
        targetObjectPath,
        QString::fromLatin1(kFilesystemInterface),
        QStringLiteral("Unmount"));
    message << QVariantMap{};

    auto* watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
        [this, targetObjectPath](QDBusPendingCallWatcher* call) {
            QDBusPendingReply<> reply = *call;
            call->deleteLater();
            setBusy(targetObjectPath, false);

            if (reply.isError()) {
                const QString error = reply.error().message().isEmpty()
                    ? tr("Could not unmount storage volume")
                    : reply.error().message();
                setLastError(error);
                emit operationFailed(targetObjectPath, error);
                requestRefreshAfterCurrent();
                return;
            }

            setLastError({});
            emit unmounted(targetObjectPath);
            requestRefreshAfterCurrent();
        });
}

void DriveModel::onInterfacesAdded(
    const QDBusObjectPath& objectPathValue,
    const UDisksInterfaceMap& interfaces) {
    Q_UNUSED(objectPathValue);
    Q_UNUSED(interfaces);
    requestRefreshAfterCurrent();
}

void DriveModel::onInterfacesRemoved(
    const QDBusObjectPath& objectPathValue,
    const QStringList& interfaces) {
    Q_UNUSED(objectPathValue);
    Q_UNUSED(interfaces);
    requestRefreshAfterCurrent();
}

void DriveModel::onPropertiesChanged(
    const QString& interfaceName,
    const QVariantMap& changedProperties,
    const QStringList& invalidatedProperties) {
    Q_UNUSED(changedProperties);
    Q_UNUSED(invalidatedProperties);

    if (interfaceName == QString::fromLatin1(kBlockInterface) ||
        interfaceName == QString::fromLatin1(kFilesystemInterface) ||
        interfaceName == QString::fromLatin1(kDriveInterface)) {
        requestRefreshAfterCurrent();
    }
}

void DriveModel::onServiceRegistered(const QString& serviceName) {
    if (serviceName != QString::fromLatin1(kService))
        return;
    setLastError({});
    refresh();
}

void DriveModel::onServiceUnregistered(const QString& serviceName) {
    if (serviceName != QString::fromLatin1(kService))
        return;
    m_refreshPending = false;
    setAvailable(false);
    replaceItems({});
    setLastError(tr("UDisks2 service stopped"));
}

int DriveModel::indexForObjectPath(const QString& targetObjectPath) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).objectPath == targetObjectPath)
            return i;
    }
    return -1;
}

void DriveModel::replaceItems(QVector<DriveItem> items) {
    const int oldCount = m_items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (oldCount != m_items.size())
        emit countChanged();
}

void DriveModel::setAvailable(bool availableValue) {
    if (m_available == availableValue)
        return;
    m_available = availableValue;
    emit availableChanged();
}

void DriveModel::setLoading(bool loadingValue) {
    if (m_loading == loadingValue)
        return;
    m_loading = loadingValue;
    emit loadingChanged();
}

void DriveModel::setLastError(const QString& error) {
    if (m_lastError == error)
        return;
    m_lastError = error;
    emit lastErrorChanged();
}

void DriveModel::setBusy(const QString& targetObjectPath, bool busy) {
    const bool changed = busy
        ? m_busyObjects.insert(targetObjectPath).second
        : m_busyObjects.remove(targetObjectPath) > 0;
    if (!changed)
        return;

    const int row = indexForObjectPath(targetObjectPath);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {BusyRole});
}

void DriveModel::requestRefreshAfterCurrent() {
    if (m_loading) {
        m_refreshPending = true;
        return;
    }
    refresh();
}
