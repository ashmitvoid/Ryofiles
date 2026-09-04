// SPDX-License-Identifier: GPL-3.0-only

#include "integrations/NetworkMountRecoveryRegistry.hpp"
#include "navigation/TabManager.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class NetworkMountRecoveryTest final : public QObject {
    Q_OBJECT

private:
    static QString localDir(const QString& root, const QString& name) {
        const QString path = QDir(root).filePath(name);
        return QDir().mkpath(path) ? path : QString();
    }

private slots:
    void uriRootMatchingUsesAuthorityAndPathBoundaries() {
        QVERIFY(DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share/projects"),
            QStringLiteral("sftp://alice@example.invalid/share")));
        QVERIFY(DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share"),
            QStringLiteral("sftp://alice@example.invalid/share/")));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share-backup"),
            QStringLiteral("sftp://alice@example.invalid/share")));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://bob@example.invalid/share/projects"),
            QStringLiteral("sftp://alice@example.invalid/share")));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@other.invalid/share/projects"),
            QStringLiteral("sftp://alice@example.invalid/share")));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("/share/projects"),
            QStringLiteral("sftp://alice@example.invalid/share")));
    }

    void inactiveRemoteSplitSessionsRecoverWithoutMovingActiveLocalTab() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString outside = localDir(temp.path(), QStringLiteral("outside"));
        QVERIFY(!outside.isEmpty());

        const QString root = QStringLiteral("sftp://alice@example.invalid/share");
        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(root + QStringLiteral("/left")));
        tabs.openSplit(root + QStringLiteral("/right"));
        tabs.setActivePane(1);
        tabs.newTab(outside);

        QCOMPARE(tabs.currentSession()->path(), outside);
        NetworkMountRecoveryRegistry::instance().notifyUnmounted(root);
        QCOMPARE(tabs.currentSession()->path(), outside);

        tabs.previousTab();
        QVERIFY(tabs.split());
        QCOMPARE(tabs.primarySession()->path(), QDir::homePath());
        QCOMPARE(tabs.secondarySession()->path(), QDir::homePath());
        QVERIFY(!tabs.primarySession()->remote());
        QVERIFY(!tabs.secondarySession()->remote());
        QCOMPARE(tabs.activePane(), 1);
    }

    void closedRemoteSplitRestoreIsSanitized() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString outside = localDir(temp.path(), QStringLiteral("outside"));
        const QString fallback = localDir(temp.path(), QStringLiteral("fallback"));
        QVERIFY(!outside.isEmpty());
        QVERIFY(!fallback.isEmpty());

        const QString root = QStringLiteral("smb://nas.invalid/team");
        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(root + QStringLiteral("/left")));
        tabs.openSplit(root + QStringLiteral("/right"));
        tabs.setActivePane(1);
        tabs.newTab(outside);
        tabs.previousTab();
        tabs.closeCurrentTab();

        QCOMPARE(tabs.rowCount(), 1);
        QCOMPARE(tabs.recoverUnmountedNetwork(root, fallback), 0);

        tabs.reopenClosedTab();
        QVERIFY(tabs.split());
        QCOMPARE(tabs.primarySession()->path(), fallback);
        QCOMPARE(tabs.secondarySession()->path(), fallback);
        QVERIFY(!tabs.primarySession()->remote());
        QVERIFY(!tabs.secondarySession()->remote());
        QCOMPARE(tabs.activePane(), 1);
    }

    void unrelatedRemoteMountDoesNotRecover() {
        const QString current = QStringLiteral("sftp://alice@other.invalid/share/work");
        TabManager tabs;
        QVERIFY(tabs.currentSession()->navigate(current));

        QCOMPARE(
            tabs.recoverUnmountedNetwork(
                QStringLiteral("sftp://alice@example.invalid/share"),
                QDir::homePath()),
            0);
        QCOMPARE(tabs.currentSession()->path(), current);
        QVERIFY(tabs.currentSession()->remote());
    }
};

QTEST_GUILESS_MAIN(NetworkMountRecoveryTest)
#include "NetworkMountRecoveryTest.moc"
