// SPDX-License-Identifier: GPL-3.0-only

#include "navigation/DirectorySession.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DirectorySessionLocationTest final : public QObject {
    Q_OBJECT

private slots:
    void localSessionKeepsFilesystemPathContract() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DirectorySession session(temp.path());
        QCOMPARE(session.path(), QDir(temp.path()).absolutePath());
        QVERIFY(!session.remote());
        QVERIFY(!session.model()->remote());
        QVERIFY(session.localBackendActive());
    }

    void localModelExposesNamesAndKeepsAnchoredSelection() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QStringList names{
            QStringLiteral("alpha.txt"),
            QStringLiteral("bravo.txt"),
            QStringLiteral("charlie.txt"),
        };
        for (const QString& name : names) {
            QFile file(QDir(temp.path()).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(name.toUtf8()) > 0);
        }

        DirectorySession session(temp.path());
        QTRY_VERIFY_WITH_TIMEOUT(!session.model()->loading(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.model()->rowCount(), static_cast<int>(names.size()), 5000);

        const QString alphaPath = QDir(temp.path()).filePath(QStringLiteral("alpha.txt"));
        const QString bravoPath = QDir(temp.path()).filePath(QStringLiteral("bravo.txt"));
        const QString charliePath = QDir(temp.path()).filePath(QStringLiteral("charlie.txt"));
        const int alpha = session.model()->indexOfPath(alphaPath);
        const int bravo = session.model()->indexOfPath(bravoPath);
        const int charlie = session.model()->indexOfPath(charliePath);
        QVERIFY(alpha >= 0);
        QVERIFY(bravo >= 0);
        QVERIFY(charlie >= 0);

        QCOMPARE(session.model()->nameAt(alpha), QStringLiteral("alpha.txt"));
        QCOMPARE(session.model()->nameAt(bravo), QStringLiteral("bravo.txt"));
        QCOMPARE(session.model()->nameAt(charlie), QStringLiteral("charlie.txt"));
        QVERIFY(session.model()->nameAt(-1).isEmpty());
        QVERIFY(session.model()->nameAt(session.model()->rowCount()).isEmpty());

        session.selectSingle(alpha);
        QCOMPARE(session.selectedPath(), alphaPath);
        QCOMPARE(session.selectionCount(), 1);

        session.selectRange(charlie);
        QCOMPARE(session.selectedPath(), charliePath);
        QCOMPARE(session.selectionCount(), 3);
        QVERIFY(session.isSelectedPath(alphaPath));
        QVERIFY(session.isSelectedPath(bravoPath));
        QVERIFY(session.isSelectedPath(charliePath));

        session.toggleSelection(bravo);
        QCOMPARE(session.selectionCount(), 2);
        QVERIFY(session.isSelectedPath(alphaPath));
        QVERIFY(!session.isSelectedPath(bravoPath));
        QVERIFY(session.isSelectedPath(charliePath));
    }

    void remoteHistorySwitchesBackendsWithoutLosingLocalState() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString localPath = QDir(temp.path()).absolutePath();

        DirectorySession session(localPath);
        QVERIFY(session.navigate(QStringLiteral("sftp://alice@example.invalid/projects")));
        QCOMPARE(session.path(), QStringLiteral("sftp://alice@example.invalid/projects"));
        QVERIFY(session.remote());
        QVERIFY(session.model()->remote());
        QVERIFY(!session.localBackendActive());
        QVERIFY(session.canGoBack());

        session.goBack();
        QCOMPARE(session.path(), localPath);
        QVERIFY(!session.remote());
        QVERIFY(session.localBackendActive());
        QVERIFY(session.canGoForward());

        session.goForward();
        QCOMPARE(session.path(), QStringLiteral("sftp://alice@example.invalid/projects"));
        QVERIFY(session.remote());
        QVERIFY(!session.localBackendActive());
    }

    void remoteGoUpPreservesAuthorityAndUser() {
        DirectorySession session;
        QVERIFY(session.navigate(QStringLiteral("sftp://alice@example.invalid/home/alice/projects/")));
        session.goUp();
        QCOMPARE(session.path(), QStringLiteral("sftp://alice@example.invalid/home/alice"));
        QCOMPARE(session.title(), QStringLiteral("alice"));

        session.goUp();
        QCOMPARE(session.path(), QStringLiteral("sftp://alice@example.invalid/home"));
        session.goUp();
        QCOMPARE(session.path(), QStringLiteral("sftp://alice@example.invalid/"));

        const QString root = session.path();
        session.goUp();
        QCOMPARE(session.path(), root);
    }

    void remoteNavigationDisablesLocalOnlySessionWork() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DirectorySession session(temp.path());
        session.setPreviewVisible(true);
        session.deepSearch()->start(temp.path(), QStringLiteral("needle"));
        QVERIFY(session.deepSearch()->active());

        QVERIFY(session.navigate(QStringLiteral("smb://nas.invalid/share")));
        QVERIFY(session.remote());
        QVERIFY(!session.previewVisible());
        QVERIFY(!session.deepSearch()->active());
    }

    void remoteFileLaunchTargetIsCanonicalAndSecretSafe() {
        QCOMPARE(
            DirectorySession::normalizeRemoteFileLaunchUri(
                QStringLiteral("SFTP://alice@example.invalid/share/report%20final.txt")),
            QStringLiteral("sftp://alice@example.invalid/share/report%20final.txt"));
        QVERIFY(DirectorySession::normalizeRemoteFileLaunchUri(
            QStringLiteral("/tmp/report.txt")).isEmpty());
        QVERIFY(DirectorySession::normalizeRemoteFileLaunchUri(
            QStringLiteral("file:///tmp/report.txt")).isEmpty());
        QVERIFY(DirectorySession::normalizeRemoteFileLaunchUri(
            QStringLiteral("sftp://alice:secret@example.invalid/share/report.txt")).isEmpty());
        QVERIFY(DirectorySession::normalizeRemoteFileLaunchUri(
            QStringLiteral("sftp://alice@example.invalid/share/report.txt?download=1")).isEmpty());
    }

    void unmountRecoveryNeverTreatsRemoteUriAsLocalPath() {
        DirectorySession session;
        QVERIFY(session.navigate(QStringLiteral("sftp://alice@example.invalid/mnt/usb/work")));
        const QString remotePath = session.path();

        QVERIFY(!DirectorySession::pathInsideRoot(remotePath, QStringLiteral("/mnt/usb")));
        QVERIFY(!session.recoverFromUnmount(QStringLiteral("/mnt/usb"), QDir::homePath()));
        QCOMPARE(session.path(), remotePath);
        QVERIFY(session.remote());
    }

    void networkRootMatchingUsesAuthorityAndPathBoundaries() {
        const QString root = QStringLiteral("sftp://alice@example.invalid/share");
        QVERIFY(DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share"), root));
        QVERIFY(DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share/projects"), root));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@example.invalid/share-backup"), root));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://bob@example.invalid/share/projects"), root));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("sftp://alice@other.invalid/share/projects"), root));
        QVERIFY(!DirectorySession::locationInsideRoot(
            QStringLiteral("/share/projects"), root));
    }

    void networkUnmountRecoversCurrentSessionAndPrunesHistory() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString fallback = QDir(temp.path()).absolutePath();
        const QString root = QStringLiteral("sftp://alice@example.invalid/share");

        DirectorySession session(fallback);
        QVERIFY(session.navigate(root));
        QVERIFY(session.navigate(root + QStringLiteral("/projects")));
        session.setSelectedPath(root + QStringLiteral("/projects/file.txt"));
        QVERIFY(session.remote());

        QVERIFY(session.recoverFromNetworkUnmount(root, fallback));
        QCOMPARE(session.path(), fallback);
        QCOMPARE(session.selectionCount(), 0);
        QVERIFY(!session.remote());
        QVERIFY(!session.canGoForward());

        if (session.canGoBack()) {
            session.goBack();
            QVERIFY(!DirectorySession::locationInsideRoot(session.path(), root));
        }
    }

    void networkUnmountPrunesInactiveHistoryWithoutMovingUnrelatedRemote() {
        const QString removedRoot = QStringLiteral("sftp://alice@example.invalid/share");
        const QString current = QStringLiteral("sftp://alice@other.invalid/work");

        DirectorySession session;
        QVERIFY(session.navigate(removedRoot + QStringLiteral("/old")));
        QVERIFY(session.navigate(current));
        QCOMPARE(session.path(), current);

        QVERIFY(session.recoverFromNetworkUnmount(removedRoot, QDir::homePath()));
        QCOMPARE(session.path(), current);
        QVERIFY(session.remote());

        session.goBack();
        QVERIFY(!DirectorySession::locationInsideRoot(session.path(), removedRoot));
    }

    void invalidLocalNavigationDoesNotChangeSession() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        DirectorySession session(temp.path());
        const QString before = session.path();
        QVERIFY(!session.navigate(QDir(temp.path()).filePath(QStringLiteral("missing"))));
        QCOMPARE(session.path(), before);
        QVERIFY(!session.remote());
    }
};

QTEST_GUILESS_MAIN(DirectorySessionLocationTest)
#include "DirectorySessionLocationTest.moc"
