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
    quint64 size = 0,
    const QString& siblingId = {}) {
    return {
        {QStringLiteral("Vendor"), vendor},
        {QStringLiteral("Model"), model},
        {QStringLiteral("ConnectionBus"), bus},
        {QStringLiteral("Removable"), removable},
        {QStringLiteral("MediaRemovable"), mediaRemovable},
        {QStringLiteral("CanPowerOff"), canPowerOff},
        {QStringLiteral("Size"), QVariant::fromValue(size)},
        {QStringLiteral("SiblingId"), siblingId},
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
    const QString& fsType = QStringLiteral("ext4"),
    const QString& cryptoBackingDevice = {}) {
    UDisksPropertyMap properties {
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
    if (!cryptoBackingDevice.isEmpty()) {
        properties.insert(
            QStringLiteral("CryptoBackingDevice"),
            QVariant::fromValue(QDBusObjectPath(cryptoBackingDevice)));
    }
    return properties;
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
        QVERIFY(!item.canPowerOffNow);
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

    void allowsPowerOffForUnmountedCapableDrive() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/usb_power");
        const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdc1");
        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("USB"), QStringLiteral("usb"), false, true, true));
        addVolume(objects, blockPath,
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdc1")),
            filesystemProperties());

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, blockPath);
        QVERIFY2(plan.allowed, qPrintable(plan.reason));
        QCOMPARE(plan.driveObjectPath, drivePath);
        QCOMPARE(plan.affectedDrivePaths, QSet<QString>{drivePath});

        const QVector<DriveItem> volumes = DriveModel::volumesFromManagedObjects(objects);
        QCOMPARE(volumes.size(), 1);
        QVERIFY(volumes.front().canPowerOff);
        QVERIFY(volumes.front().canPowerOffNow);
    }

    void blocksPowerOffWhileTargetFilesystemIsMounted() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/usb_power");
        const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdc1");
        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("USB"), QStringLiteral("usb"), false, true, true));
        addVolume(objects, blockPath,
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdc1")),
            filesystemProperties({QByteArrayLiteral("/run/media/test/USB")}));

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, blockPath);
        QVERIFY(!plan.allowed);
        QVERIFY(!plan.reason.isEmpty());
    }

    void blocksPowerOffWhenAnotherPartitionOnSameDriveIsMounted() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/usb_power");
        const QString targetPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdc1");
        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("USB"), QStringLiteral("usb"), false, true, true));
        addVolume(objects, targetPath,
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdc1")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdc2"),
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdc2")),
            filesystemProperties({QByteArrayLiteral("/run/media/test/DATA")}));

        QVERIFY(!DriveModel::powerOffPlan(objects, targetPath).allowed);
    }

    void blocksPowerOffWhenSiblingDriveIsMounted() {
        UDisksManagedObjectMap objects;
        const QString targetDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/card_reader_a");
        const QString siblingDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/card_reader_b");
        const QString targetPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/mmc_a1");
        const QString siblingId = QStringLiteral("reader-42");

        addDrive(objects, targetDrive,
            driveProperties({}, QStringLiteral("Reader A"), QStringLiteral("usb"), false, true, true, 0, siblingId));
        addDrive(objects, siblingDrive,
            driveProperties({}, QStringLiteral("Reader B"), QStringLiteral("usb"), false, true, true, 0, siblingId));
        addVolume(objects, targetPath,
            blockProperties(targetDrive, QByteArrayLiteral("/dev/mmc-a1")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/mmc_b1"),
            blockProperties(siblingDrive, QByteArrayLiteral("/dev/mmc-b1")),
            filesystemProperties({QByteArrayLiteral("/run/media/test/CARD")}));

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, targetPath);
        QVERIFY(!plan.allowed);
        QVERIFY(plan.affectedDrivePaths.contains(targetDrive));
        QVERIFY(plan.affectedDrivePaths.contains(siblingDrive));
    }

    void ignoresMountedUnrelatedDriveDuringPowerOffPreflight() {
        UDisksManagedObjectMap objects;
        const QString targetDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/target");
        const QString otherDrive = QStringLiteral("/org/freedesktop/UDisks2/drives/other");
        const QString targetPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdd1");

        addDrive(objects, targetDrive,
            driveProperties({}, QStringLiteral("Target"), QStringLiteral("usb"), false, true, true));
        addDrive(objects, otherDrive,
            driveProperties({}, QStringLiteral("Other"), QStringLiteral("usb"), false, true, true));
        addVolume(objects, targetPath,
            blockProperties(targetDrive, QByteArrayLiteral("/dev/sdd1")),
            filesystemProperties());
        addVolume(objects,
            QStringLiteral("/org/freedesktop/UDisks2/block_devices/sde1"),
            blockProperties(otherDrive, QByteArrayLiteral("/dev/sde1")),
            filesystemProperties({QByteArrayLiteral("/run/media/test/OTHER")}));

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, targetPath);
        QVERIFY2(plan.allowed, qPrintable(plan.reason));
        QCOMPARE(plan.affectedDrivePaths, QSet<QString>{targetDrive});
    }

    void blocksPowerOffWhenDriveDoesNotSupportIt() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/no_poweroff");
        const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdf1");
        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("Disk"), QStringLiteral("usb"), false, true, false));
        addVolume(objects, blockPath,
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdf1")),
            filesystemProperties());

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, blockPath);
        QVERIFY(!plan.allowed);
        QVERIFY(plan.driveObjectPath.isEmpty());
    }

    void blocksPowerOffWithoutPhysicalDrive() {
        UDisksManagedObjectMap objects;
        const QString blockPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/virtual1");
        addVolume(objects, blockPath,
            blockProperties(QStringLiteral("/"), QByteArrayLiteral("/dev/virtual1")),
            filesystemProperties());

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, blockPath);
        QVERIFY(!plan.allowed);
        QVERIFY(plan.affectedDrivePaths.isEmpty());
    }

    void blocksPowerOffForMountedEncryptedCleartextDescendant() {
        UDisksManagedObjectMap objects;
        const QString drivePath = QStringLiteral("/org/freedesktop/UDisks2/drives/encrypted_usb");
        const QString backingPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdg1");
        const QString cleartextPath = QStringLiteral("/org/freedesktop/UDisks2/block_devices/dm_0");

        addDrive(objects, drivePath,
            driveProperties({}, QStringLiteral("Encrypted USB"), QStringLiteral("usb"), false, true, true));
        addVolume(objects, backingPath,
            blockProperties(drivePath, QByteArrayLiteral("/dev/sdg1"), {}, {}, false, false, false, 0, QStringLiteral("crypto_LUKS")),
            filesystemProperties());
        addVolume(objects, cleartextPath,
            blockProperties(
                QStringLiteral("/"),
                QByteArrayLiteral("/dev/dm-0"),
                {},
                {},
                false,
                false,
                false,
                0,
                QStringLiteral("ext4"),
                backingPath),
            filesystemProperties({QByteArrayLiteral("/run/media/test/SECRET")}));

        const DrivePowerOffPlan plan = DriveModel::powerOffPlan(objects, backingPath);
        QVERIFY(!plan.allowed);
        QVERIFY(plan.affectedDrivePaths.contains(drivePath));
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
