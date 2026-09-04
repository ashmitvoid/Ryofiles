// SPDX-License-Identifier: GPL-3.0-only

#include "locations/LocationSpec.hpp"

#include <QDir>
#include <QtTest>

class LocationSpecTest final : public QObject {
    Q_OBJECT

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
};

QTEST_GUILESS_MAIN(LocationSpecTest)
#include "LocationSpecTest.moc"
