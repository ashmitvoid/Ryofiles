// SPDX-License-Identifier: GPL-3.0-only

#include "locations/LocationSpec.hpp"
#include "picker/PickerController.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class LocationSpecTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& content = QByteArrayLiteral("x")) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

private slots:
    void rejectsEmptyLocation() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("   "));
        QVERIFY(!spec.isValid());
        QVERIFY(!spec.error.isEmpty());
    }

    void parsesAbsoluteLocalPathWithoutRequiringExistence() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("/tmp/ryofiles/../network-test"));
        QVERIFY(spec.isValid());
        QVERIFY(spec.isLocal());
        QCOMPARE(spec.localPath, QStringLiteral("/tmp/network-test"));
        QCOMPARE(spec.scheme, QStringLiteral("file"));
        QVERIFY(spec.canonical.startsWith(QStringLiteral("file:///")));
    }

    void expandsCurrentUsersHomeShortcut() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("~/Documents"));
        QVERIFY(spec.isLocal());
        QCOMPARE(spec.localPath, QDir::cleanPath(QDir::homePath() + QStringLiteral("/Documents")));
    }

    void rejectsOtherUsersTildeShortcut() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("~someone/Documents"));
        QVERIFY(!spec.isValid());
    }

    void parsesFileUriAsLocal() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("file:///tmp/Ryofiles%20Files"));
        QVERIFY(spec.isLocal());
        QCOMPARE(spec.localPath, QStringLiteral("/tmp/Ryofiles Files"));
    }

    void parsesSftpUriWithoutTurningItIntoALocalPath() {
        const LocationSpec spec = LocationSpec::parse(
            QStringLiteral("sftp://ashmit@example.com:2222/home/ashmit/projects"));
        QVERIFY(spec.isValid());
        QVERIFY(spec.isNetwork());
        QVERIFY(spec.localPath.isEmpty());
        QCOMPARE(spec.scheme, QStringLiteral("sftp"));
        QCOMPARE(spec.host, QStringLiteral("example.com"));
        QCOMPARE(spec.userName, QStringLiteral("ashmit"));
        QCOMPARE(spec.displayName, QStringLiteral("ashmit@example.com:2222"));
        QCOMPARE(spec.canonical,
            QStringLiteral("sftp://ashmit@example.com:2222/home/ashmit/projects"));
    }

    void normalizesNetworkSchemeAndPathSegments() {
        const LocationSpec spec = LocationSpec::parse(
            QStringLiteral("SMB://fileserver/team/../shared"));
        QVERIFY(spec.isNetwork());
        QCOMPARE(spec.scheme, QStringLiteral("smb"));
        QCOMPARE(spec.host, QStringLiteral("fileserver"));
        QCOMPARE(spec.canonical, QStringLiteral("smb://fileserver/shared"));
    }

    void supportsInitialGvfsProtocols() {
        QVERIFY(LocationSpec::isSupportedNetworkScheme(QStringLiteral("sftp")));
        QVERIFY(LocationSpec::isSupportedNetworkScheme(QStringLiteral("SMB")));
        QVERIFY(LocationSpec::isSupportedNetworkScheme(QStringLiteral("dav")));
        QVERIFY(LocationSpec::isSupportedNetworkScheme(QStringLiteral("davs")));
        QVERIFY(LocationSpec::isSupportedNetworkScheme(QStringLiteral("ftp")));
        QVERIFY(!LocationSpec::isSupportedNetworkScheme(QStringLiteral("https")));
        QVERIFY(!LocationSpec::isSupportedNetworkScheme(QStringLiteral("ssh")));
    }

    void rejectsEmbeddedNetworkPassword() {
        const LocationSpec spec = LocationSpec::parse(
            QStringLiteral("sftp://user:secret@example.com/home/user"));
        QVERIFY(!spec.isValid());
        QVERIFY(spec.error.contains(QStringLiteral("password"), Qt::CaseInsensitive));
        QVERIFY(!spec.canonical.contains(QStringLiteral("secret")));
    }

    void rejectsUnsupportedNetworkProtocol() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("https://example.com/files"));
        QVERIFY(!spec.isValid());
        QVERIFY(spec.error.contains(QStringLiteral("Unsupported")));
    }

    void rejectsNetworkUriWithoutHost() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("sftp:///home/user"));
        QVERIFY(!spec.isValid());
    }

    void rejectsQueryAndFragmentForNetworkLocations() {
        QVERIFY(!LocationSpec::parse(QStringLiteral("smb://server/share?token=abc")).isValid());
        QVERIFY(!LocationSpec::parse(QStringLiteral("davs://server/share#section")).isValid());
    }

    void defaultsNetworkPathToRoot() {
        const LocationSpec spec = LocationSpec::parse(QStringLiteral("sftp://example.com"));
        QVERIFY(spec.isNetwork());
        QCOMPARE(spec.canonical, QStringLiteral("sftp://example.com/"));
    }

    void treatsGvfsFuseMountAsLocalOnlyWhenGivenAsAPath() {
        const LocationSpec spec = LocationSpec::parse(
            QStringLiteral("/run/user/1000/gvfs/sftp:host=example.com,user=ashmit"));
        QVERIFY(spec.isLocal());
        QVERIFY(!spec.isNetwork());
    }

    void pickerRejectsInvalidConfiguration() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const PickerContract invalidMode =
            PickerContract::parse("save", false, temp.path(), {});
        QVERIFY(!invalidMode.valid);
        QVERIFY(invalidMode.error.contains(QStringLiteral("open")));

        const PickerContract multipleFolder =
            PickerContract::parse("folder", true, temp.path(), {});
        QVERIFY(!multipleFolder.valid);
        QVERIFY(multipleFolder.error.contains(QStringLiteral("multiple"), Qt::CaseInsensitive));

        const PickerContract filteredFolder = PickerContract::parse(
            "folder", false, temp.path(), {QStringLiteral("image/*")});
        QVERIFY(!filteredFolder.valid);
        QVERIFY(filteredFolder.error.contains(QStringLiteral("MIME"), Qt::CaseInsensitive));

        const PickerContract remoteInitial = PickerContract::parse(
            "open", false, QStringLiteral("sftp://example.invalid/home"), {});
        QVERIFY(!remoteInitial.valid);
        QVERIFY(remoteInitial.error.contains(QStringLiteral("local"), Qt::CaseInsensitive));

        const PickerContract missingInitial = PickerContract::parse(
            "open", false, temp.filePath("missing"), {});
        QVERIFY(!missingInitial.valid);
    }

    void pickerOpenModeValidatesSelectionAndMultiplicity() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString first = temp.filePath("first.txt");
        const QString second = temp.filePath("second.txt");
        const QString folder = temp.filePath("folder");
        writeFile(first);
        writeFile(second);
        QVERIFY(QDir().mkpath(folder));

        const PickerContract single = PickerContract::parse("open", false, temp.path(), {});
        QVERIFY(single.valid);
        QCOMPARE(single.modeName(), QStringLiteral("open"));
        QVERIFY(!single.folderMode);
        QVERIFY(!single.multiple);
        QCOMPARE(single.initialDirectory, QDir::cleanPath(temp.path()));
        QVERIFY(single.canAccept({first}, temp.path()));
        QVERIFY(!single.canAccept({first, second}, temp.path()));
        QVERIFY(!single.canAccept({folder}, temp.path()));
        QVERIFY(!single.canAccept({QStringLiteral("smb://server.invalid/file.txt")}, temp.path()));
        QCOMPARE(single.acceptedPaths({first}, temp.path()), QStringList({first}));

        const PickerContract multiple = PickerContract::parse("open", true, temp.path(), {});
        QVERIFY(multiple.valid);
        QVERIFY(multiple.multiple);
        QVERIFY(multiple.canAccept({first, second}, temp.path()));
        QCOMPARE(
            multiple.acceptedPaths({first, second, first}, temp.path()),
            QStringList({first, second}));
    }

    void pickerOpenModeHonorsMimeFilters() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString image = temp.filePath("photo.png");
        const QString text = temp.filePath("notes.txt");
        writeFile(image, QByteArrayLiteral("not-decoded-by-picker"));
        writeFile(text, QByteArrayLiteral("plain text"));

        const PickerContract images = PickerContract::parse(
            "open",
            false,
            temp.path(),
            {QStringLiteral("image/*"), QStringLiteral(" image/png ")});
        QVERIFY(images.valid);
        QCOMPARE(images.mimeTypes, QStringList({QStringLiteral("image/*"), QStringLiteral("image/png")}));
        QVERIFY(images.canAccept({image}, temp.path()));
        QVERIFY(!images.canAccept({text}, temp.path()));

        const PickerContract textFiles = PickerContract::parse(
            "open", false, temp.path(), {QStringLiteral("text/plain")});
        QVERIFY(textFiles.valid);
        QVERIFY(textFiles.canAccept({text}, temp.path()));
        QVERIFY(!textFiles.canAccept({image}, temp.path()));

        const PickerContract invalidFilter = PickerContract::parse(
            "open", false, temp.path(), {QStringLiteral("image")});
        QVERIFY(!invalidFilter.valid);
        QVERIFY(invalidFilter.error.contains(QStringLiteral("MIME"), Qt::CaseInsensitive));
    }

    void pickerFolderModeReturnsCurrentDirectoryOnly() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString child = temp.filePath("child");
        const QString file = temp.filePath("ignored.txt");
        QVERIFY(QDir().mkpath(child));
        writeFile(file);

        const PickerContract picker = PickerContract::parse("folder", false, temp.path(), {});
        QVERIFY(picker.valid);
        QVERIFY(picker.folderMode);
        QVERIFY(picker.canAccept({}, child));
        QVERIFY(picker.canAccept({file}, child));
        QVERIFY(!picker.canAccept({}, temp.filePath("missing")));
        QVERIFY(!picker.canAccept({}, QStringLiteral("sftp://example.invalid/home")));
        QCOMPARE(
            picker.acceptedPaths({file}, child),
            QStringList({QDir::cleanPath(child)}));
    }
};

QTEST_GUILESS_MAIN(LocationSpecTest)
#include "LocationSpecTest.moc"
