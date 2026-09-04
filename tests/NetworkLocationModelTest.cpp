// SPDX-License-Identifier: GPL-3.0-only

#include "locations/NetworkLocationModel.hpp"

#include <QtTest>

class NetworkLocationModelTest final : public QObject {
    Q_OBJECT

private slots:
    void keepsOnlySupportedNetworkMounts() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                QStringLiteral("Projects"),
                QStringLiteral("sftp://dev@example.com/"),
                QStringLiteral("sftp://dev@example.com/home/dev/projects"),
                true,
                false,
            },
            {
                QStringLiteral("Local USB"),
                QStringLiteral("file:///run/media/test/USB"),
                QStringLiteral("file:///run/media/test/USB"),
                true,
                false,
            },
            {
                QStringLiteral("Web"),
                QStringLiteral("https://example.com/"),
                QStringLiteral("https://example.com/files"),
                false,
                false,
            },
        };

        const QVector<NetworkLocationItem> locations =
            NetworkLocationModel::locationsFromSnapshots(snapshots);
        QCOMPARE(locations.size(), 1);
        QCOMPARE(locations.front().name, QStringLiteral("Projects"));
        QCOMPARE(locations.front().uri,
            QStringLiteral("sftp://dev@example.com/home/dev/projects"));
        QCOMPARE(locations.front().rootUri, QStringLiteral("sftp://dev@example.com/"));
        QCOMPARE(locations.front().scheme, QStringLiteral("sftp"));
        QCOMPARE(locations.front().host, QStringLiteral("example.com"));
        QVERIFY(locations.front().canUnmount);
    }

    void fallsBackToRemoteRootWhenDefaultLocationIsNotRemote() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                {},
                QStringLiteral("smb://nas/shared"),
                QStringLiteral("file:///tmp/not-the-network-entry"),
                false,
                false,
            },
        };

        const auto locations = NetworkLocationModel::locationsFromSnapshots(snapshots);
        QCOMPARE(locations.size(), 1);
        QCOMPARE(locations.front().uri, QStringLiteral("smb://nas/shared"));
        QCOMPARE(locations.front().name, QStringLiteral("nas"));
    }

    void ignoresShadowedMounts() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                QStringLiteral("Hidden"),
                QStringLiteral("sftp://example.com/"),
                QStringLiteral("sftp://example.com/home"),
                true,
                true,
            },
        };

        QVERIFY(NetworkLocationModel::locationsFromSnapshots(snapshots).isEmpty());
    }

    void deduplicatesByRemoteMountRoot() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                QStringLiteral("Server"),
                QStringLiteral("sftp://user@example.com/"),
                QStringLiteral("sftp://user@example.com/home/user"),
                false,
                false,
            },
            {
                QStringLiteral("Server duplicate"),
                QStringLiteral("sftp://user@example.com/"),
                QStringLiteral("sftp://user@example.com/home/user/projects"),
                true,
                false,
            },
        };

        const auto locations = NetworkLocationModel::locationsFromSnapshots(snapshots);
        QCOMPARE(locations.size(), 1);
        QCOMPARE(locations.front().name, QStringLiteral("Server"));
        QVERIFY(locations.front().canUnmount);
    }

    void sortsNetworkLocationsByDisplayName() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                QStringLiteral("Zulu"),
                QStringLiteral("sftp://z.example/"),
                {},
                true,
                false,
            },
            {
                QStringLiteral("alpha"),
                QStringLiteral("smb://a.example/share"),
                {},
                true,
                false,
            },
        };

        const auto locations = NetworkLocationModel::locationsFromSnapshots(snapshots);
        QCOMPARE(locations.size(), 2);
        QCOMPARE(locations.at(0).name, QStringLiteral("alpha"));
        QCOMPARE(locations.at(1).name, QStringLiteral("Zulu"));
    }

    void neverExposesPasswordBearingMountUris() {
        const QVector<NetworkMountSnapshot> snapshots {
            {
                QStringLiteral("Unsafe"),
                QStringLiteral("sftp://user:secret@example.com/"),
                QStringLiteral("sftp://user:secret@example.com/home/user"),
                true,
                false,
            },
        };

        QVERIFY(NetworkLocationModel::locationsFromSnapshots(snapshots).isEmpty());
    }
};

QTEST_GUILESS_MAIN(NetworkLocationModelTest)
#include "NetworkLocationModelTest.moc"
