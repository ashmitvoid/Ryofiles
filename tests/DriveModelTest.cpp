// SPDX-License-Identifier: GPL-3.0-only

#include "storage/DriveModel.hpp"

#include <QDBusObjectPath>
#include <QtTest>

namespace {
constexpr auto kBlock = "org.freedesktop.UDisks2.Block";
constexpr auto kFilesystem = "org.freedesktop.UDisks2.Filesystem";
constexpr auto kDrive = "org.freedesktop.UDisks2.Drive";
constexpr auto kLoop = "org.freedesktop.UDisks2.Loop";

QVariant byteArrayPath(const QByteArray& path) {
    QByteArray terminated = path;
    terminated.append('\0');
    return QVariant::fromValue(terminated);
}

UDisksPropertyMap driveProperties(
    const QString& vendor,
    const QString& model,
    const QString& bus,
    bool removable = false,
    bool mediaRemovable = false,
    bool canPowerOff = false,
    quint64 size = 0) {
    return {
        {QStringLiteral("Vendor"), vendor},
        {QStringLiteral("Model"), model},
        {QStringLiteral("ConnectionBus"), bus},
        {QStringLiteral("Removable"), removable},
        {QStringLiteral("MediaRemovable"), mediaRemovable},
        {QStringLiteral("CanPowerOff"), canPowerOff},
        {QStringLiteral("Size"), QVariant::fromValue(size)},
    };
}

UDisksPropertyMap blockProperties(
    const QString& drivePath,
    const QByteArray& device,
    const QString& label = {},
    const QString& hintName = {},
    bool hintIgnore = false,
    bool hintSystem = false,
    bool readOnly = false,
    quint64 size = 0,
    const QString& fsType = QStringLiteral("ext4")) {
    return {
        {QStringLiteral("Drive"), QVariant::fromValue(QDBusObjectPath(drivePath))},
        {QStringLiteral("PreferredDevice"), byteArrayPath(device)},
        {QStringLiteral("Device"), byteArrayPath(device)},
        {QStringLiteral("IdLabel"), label},
        {QStringLiteral("HintName"), hintName},
        {QStringLiteral("HintIgnore"), hintIgnore},
        {QStringLiteral("HintSystem"), hintSystem},
        {QStringLiteral("ReadOnly"), readOnly},
        {QStringLiteral("Size"), QVariant::fromValue(size)},
        {QStringLiteral("IdType"), fsType},
    };
}

UDisksPropertyMap filesystemProperties(const QList<QByteArray>& mountPoints = {}) {
    QList<QByteArray> terminated;
    terminated.reserve(mountPoints.size());
    for (QByteArray path : mountPoints) {
        path.append('\0');
        terminated.push_back(path);
    }
    return {{QStringLiteral("MountPoints"), QVariant::fromValue(terminated)}};
}

void addDrive(
    UDisksManagedObjectMap& objects,
    const QString& path,
    const UDisksPropertyMap& properties) {
    objects.insert(
        QDBusObjectPath(path),
        {{QString::fromLatin1(kDrive), properties}});
}

void addVolume(
    UDisksManagedObjectMap& objects,
    const QString& path,
    const UDisksPropertyMap& block,
    const UDisksPropertyMap& filesystem,
    bool loop = false) {
    UDisksInterfaceMap interfaces {
        {QString::fromLatin1(kBlock), block},
        {QString::fromLatin1(kFilesystem), filesystem},
    };
    if (loop)
        interfaces.insert(QString::fromLatin1(kLoop), {});
    objects.insert(QDBusObjectPath(path), interfaces);
}
} // namespace

class DriveModelTest final : public QObject {
    Q_OBJECT

private slots:
    void parsesMountedRemovableVolume() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/usb_test");
        const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb1");

        addDrive(objects, drivePath,
            driveProperties(
                QStringLiteral("SanDisk"),
                QStringLiteral("Ultra"),
                QStringLiteral("usb"),
                false,
                true,
                true,
                64ULL * 1024 * 1024 * 1024));
        addVolume(objects, blockPath,
            blockProperties(
                drivePath,
                QByteArrayLiteral("/dev/sdb1"),
                QStringLiteral("WORK"),
                {},
                false,
                false,
                true,
                32ULL * 1024 * 1024 * 1024,
                QStringLiteral("exfat")),
            filesystemProperties({QByteArrayLiteral("/run/media/test/WORK")}));

        const QVector<DriveItem> volumes = DriveModel::volumesFromManagedObjects(objects);
        QCOMPARE(volumes.size(), 1);

        const DriveItem& item = volumes.front();
        QCOMPARE(item.objectPath, blockPath);
        QCOMPARE(item.driveObjectPath, drivePath);
        QCOMPARE(item.name, QStringLiteral("WORK"));
        QCOMPARE(item.devicePath, QStringLiteral("/dev/sdb1"));
        QCOMPARE(item.mountPoint, QStringLiteral("/run/media/test/WORK"));
        QCOMPARE(item.fsType, QStringLiteral("exfat"));
        QCOMPARE(item.connectionBus, QStringLiteral("usb"));
        QCOMPARE(item.sizeBytes, 32ULL * 1024 * 1024 * 1024);
        QVERIFY(item.mounted);
        QVERIFY(item.removable);
        QVERIFY(item.readOnly);
        QVERIFY(item.canPowerOff);
    }

    void filtersIgnoredSystemAndLoopVolumes() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/test");
        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("Disk"), QStringLiteral("sata")));

        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/system"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/system"), {}, {}, false, true),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/ignored"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/ignored"), {}, {}, true, false),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/loop0"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/loop0")),
            filesystemProperties(),
            true);

        QVERIFY(DriveModel::volumesFromManagedObjects(objects).isEmpty());
    }

    void usesRyofilesNamePrecedenceAndFallbacks() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/name_test");
        addDrive(objects, drivePath,
            driveProperties(QStringLiteral("ACME"), QStringLiteral("Pocket Drive"), QStringLiteral("usb")));

        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/one"),
            blockProperties(
                drivePath,
                QByteArrayLiteral("/dev/one"),
                QStringLiteral("LABEL"),
                QStringLiteral("Friendly Name")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/two"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/two"), QStringLiteral("LABEL")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/three"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/three")),
            filesystemProperties());

        const QVector<DriveItem> volumes = DriveModel::volumesFromManagedObjects(objects);
        QCOMPARE(volumes.size(), 3);

        QMap<QString, QString> names;
        for (const DriveItem& item : volumes)
            names.insert(item.devicePath, item.name);

        QCOMPARE(names.value(QStringLiteral("/dev/one")), QStringLiteral("Friendly Name"));
        QCOMPARE(names.value(QStringLiteral("/dev/two")), QStringLiteral("LABEL"));
        QCOMPARE(names.value(QStringLiteral("/dev/three")), QStringLiteral("ACME Pocket Drive"));
    }

    void fallsBackToDeviceNameWithoutDriveMetadata() {
        UDisksManagedObjectMap objects;
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/mmcblk0p1"),
            blockProperties(QStringLiteral("/"), QByteArrayLiteral("/dev/mmcblk0p1")),
            filesystemProperties());

        const QVector<DriveItem> volumes = DriveModel::volumesFromManagedObjects(objects);
        QCOMPARE(volumes.size(), 1);
        QCOMPARE(volumes.front().name, QStringLiteral("mmcblk0p1"));
    }

    void dropsVolumesWithoutUsableDevicePath() {
        UDisksManagedObjectMap objects;
        const QString path = QStringLiteral("/org/freedesktop/UDisks2/block_devices/invalid");
        UDisksPropertyMap block {
            {QStringLiteral("Drive"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))},
            {QStringLiteral("HintIgnore"), false},
            {QStringLiteral("HintSystem"), false},
        };
        addVolume(objects, path, block, filesystemProperties());

        QVERIFY(DriveModel::volumesFromManagedObjects(objects).isEmpty());
    }

    void sortsRemovableVolumesBeforeFixedVolumes() {
        UDisksManagedObjectMap objects;
        const QString fixedDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/fixed");
        const QString usbDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/usb");
        addDrive(objects, fixedDrive,
            driveProperties({}, QStringLiteral("Alpha Fixed"), QStringLiteral("sata")));
        addDrive(objects, usbDrive,
            driveProperties({}, QStringLiteral("Zulu USB"), QStringLiteral("usb")));

        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/sda1"),
            blockProperties(fixedDrive, QByteArrayLiteral("/dev/sda1")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb1"),
            blockProperties(usbDrive, QByteArrayLiteral("/dev/sdb1")),
            filesystemProperties());

        const QVector<DriveItem> volumes = DriveModel::volumesFromManagedObjects(objects);
        QCOMPARE(volumes.size(), 2);
        QCOMPARE(volumes.at(0).devicePath, QStringLiteral("/dev/sdb1"));
        QVERIFY(volumes.at(0).removable);
        QCOMPARE(volumes.at(1).devicePath, QStringLiteral("/dev/sda1"));
        QVERIFY(!volumes.at(1).removable);
    }

    void formatsBinarySizes() {
        QCOMPARE(DriveModel::formatSize(0), QStringLiteral("0 B"));
        QCOMPARE(DriveModel::formatSize(1023), QStringLiteral("1023 B"));
        QCOMPARE(DriveModel::formatSize(1024), QStringLiteral("1.0 KiB"));
        QCOMPARE(DriveModel::formatSize(10ULL * 1024), QStringLiteral("10 KiB"));
        QCOMPARE(DriveModel::formatSize(1536ULL * 1024 * 1024), QStringLiteral("1.5 GiB"));
    }
};

QTEST_GUILESS_MAIN(DriveModelTest)
#include "DriveModelTest.moc"
