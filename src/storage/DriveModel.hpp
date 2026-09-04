// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVector>

using UDisksPropertyMap = QVariantMap;
using UDisksInterfaceMap = QMap<QString, UDisksPropertyMap>;
using UDisksManagedObjectMap = QMap<QDBusObjectPath, UDisksInterfaceMap>;

Q_DECLARE_METATYPE(UDisksInterfaceMap)
Q_DECLARE_METATYPE(UDisksManagedObjectMap)

struct DrivePowerOffPlan {
    bool allowed = false;
    QString driveObjectPath;
    QSet<QString> affectedDrivePaths;
    QString reason;
};

struct DriveItem {
    QString objectPath;
    QString driveObjectPath;
    QString name;
    QString devicePath;
    QString mountPoint;
    QString fsType;
    QString connectionBus;
    quint64 sizeBytes = 0;
    bool mounted = false;
    bool removable = false;
    bool readOnly = false;
    bool canPowerOff = false;
    bool canPowerOffNow = false;
};

class DriveModel final : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        ObjectPathRole = Qt::UserRole + 1,
        DriveObjectPathRole,
        NameRole,
        DevicePathRole,
        MountPointRole,
        FsTypeRole,
        ConnectionBusRole,
        SizeBytesRole,
        SizeTextRole,
        MountedRole,
        RemovableRole,
        ReadOnlyRole,
        CanPowerOffRole,
        CanPowerOffNowRole,
        BusyRole,
    };
    Q_ENUM(Role)

    explicit DriveModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool available() const { return m_available; }
    bool loading() const { return m_loading; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void mount(const QString& objectPath);
    Q_INVOKABLE void unmount(const QString& objectPath);
    Q_INVOKABLE void powerOff(const QString& objectPath);

    static QVector<DriveItem> volumesFromManagedObjects(const UDisksManagedObjectMap& objects);
    static DrivePowerOffPlan powerOffPlan(
        const UDisksManagedObjectMap& objects,
        const QString& volumeObjectPath);
    static QString formatSize(quint64 bytes);

signals:
    void countChanged();
    void availableChanged();
    void loadingChanged();
    void lastErrorChanged();
    void mounted(const QString& objectPath, const QString& mountPath);
    void unmounted(const QString& objectPath);
    void poweredOff(const QString& driveObjectPath);
    void operationFailed(const QString& objectPath, const QString& message);

private slots:
    void onInterfacesAdded(const QDBusObjectPath& objectPath, const UDisksInterfaceMap& interfaces);
    void onInterfacesRemoved(const QDBusObjectPath& objectPath, const QStringList& interfaces);
    void onPropertiesChanged(const QString& interfaceName, const QVariantMap& changedProperties, const QStringList& invalidatedProperties);
    void onServiceRegistered(const QString& serviceName);
    void onServiceUnregistered(const QString& serviceName);

private:
    static void registerDbusTypes();
    static QVariant unwrapped(const QVariant& value);
    static QString bytePath(const QVariant& value);
    static QString firstMountPoint(const QVariant& value);
    static QString objectPath(const QVariant& value);
    static QString displayName(const UDisksPropertyMap& block, const UDisksPropertyMap& drive, const QString& devicePath);

    int indexForObjectPath(const QString& objectPath) const;
    void replaceItems(QVector<DriveItem> items);
    void setAvailable(bool available);
    void setLoading(bool loading);
    void setLastError(const QString& error);
    void setBusy(const QString& objectPath, bool busy);
    void setDriveBusy(const QSet<QString>& drivePaths, bool busy);
    void requestRefreshAfterCurrent();

    static constexpr auto kService = "org.freedesktop.UDisks2";
    static constexpr auto kRootPath = "/org/freedesktop/UDisks2";
    static constexpr auto kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";
    static constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
    static constexpr auto kBlockInterface = "org.freedesktop.UDisks2.Block";
    static constexpr auto kFilesystemInterface = "org.freedesktop.UDisks2.Filesystem";
    static constexpr auto kDriveInterface = "org.freedesktop.UDisks2.Drive";
    static constexpr auto kLoopInterface = "org.freedesktop.UDisks2.Loop";

    QDBusConnection m_bus;
    QDBusServiceWatcher m_serviceWatcher;
    UDisksManagedObjectMap m_managedObjects;
    QVector<DriveItem> m_items;
    QSet<QString> m_busyObjects;
    QSet<QString> m_busyDriveObjects;
    bool m_available = false;
    bool m_loading = false;
    bool m_refreshPending = false;
    QString m_lastError;
};
