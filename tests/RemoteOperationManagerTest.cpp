// SPDX-License-Identifier: GPL-3.0-only

#include "operations/RemoteOperationManager.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class RemoteOperationManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void remoteToLocalTransferPlanIsCanonical() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const RemoteOperationPlan plan = RemoteOperationManager::planTransfer(
            QStringLiteral("SFTP://alice@example.invalid/share/report.txt"),
            temp.path());
        QVERIFY(plan.valid);
        QVERIFY(plan.involvesNetwork);
        QCOMPARE(plan.source, QStringLiteral("sftp://alice@example.invalid/share/report.txt"));
        QCOMPARE(plan.destinationDirectory, QDir(temp.path()).absolutePath());
    }

    void localToRemoteTransferPlanIsCanonical() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("report.txt"));

        const RemoteOperationPlan plan = RemoteOperationManager::planTransfer(
            source,
            QStringLiteral("smb://nas.invalid/team"));
        QVERIFY(plan.valid);
        QVERIFY(plan.involvesNetwork);
        QCOMPARE(plan.source, QDir::cleanPath(source));
        QCOMPARE(plan.destinationDirectory, QStringLiteral("smb://nas.invalid/team"));
    }

    void localOnlyTransferIsRejected() {
        QTemporaryDir source;
        QTemporaryDir destination;
        QVERIFY(source.isValid());
        QVERIFY(destination.isValid());

        const RemoteOperationPlan plan = RemoteOperationManager::planTransfer(
            source.filePath(QStringLiteral("report.txt")),
            destination.path());
        QVERIFY(!plan.valid);
        QVERIFY(!plan.involvesNetwork);
        QVERIFY(plan.error.contains(QStringLiteral("local operation engine"), Qt::CaseInsensitive));
    }

    void passwordBearingUriIsRejectedWithoutEchoingSecret() {
        const RemoteOperationPlan plan = RemoteOperationManager::planRemoteItem(
            QStringLiteral("sftp://alice:super-secret@example.invalid/report.txt"));
        QVERIFY(!plan.valid);
        QVERIFY(!plan.error.contains(QStringLiteral("super-secret")));
    }

    void localRenameTargetIsRejected() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const RemoteOperationPlan plan = RemoteOperationManager::planRemoteItem(
            temp.filePath(QStringLiteral("report.txt")));
        QVERIFY(!plan.valid);
        QVERIFY(plan.error.contains(QStringLiteral("network location"), Qt::CaseInsensitive));
    }

    void createFolderRequiresSafeLeafName() {
        const RemoteOperationPlan valid = RemoteOperationManager::planCreateFolder(
            QStringLiteral("davs://cloud.invalid/files"),
            QStringLiteral("Reports 2026"));
        QVERIFY(valid.valid);
        QCOMPARE(valid.destinationDirectory, QStringLiteral("davs://cloud.invalid/files"));
        QCOMPARE(valid.name, QStringLiteral("Reports 2026"));

        const RemoteOperationPlan invalid = RemoteOperationManager::planCreateFolder(
            QStringLiteral("davs://cloud.invalid/files"),
            QStringLiteral("../escape"));
        QVERIFY(!invalid.valid);
    }

    void queryAndFragmentUrisAreRejected() {
        QVERIFY(!RemoteOperationManager::planRemoteItem(
            QStringLiteral("sftp://alice@example.invalid/report.txt?download=1")).valid);
        QVERIFY(!RemoteOperationManager::planRemoteItem(
            QStringLiteral("sftp://alice@example.invalid/report.txt#part")).valid);
    }

    void invalidCallsDoNotCreateJobs() {
        QTemporaryDir one;
        QTemporaryDir two;
        QVERIFY(one.isValid());
        QVERIFY(two.isValid());

        RemoteOperationManager manager;
        QCOMPARE(manager.rowCount(), 0);
        QVERIFY(manager.copyFile(one.filePath(QStringLiteral("a")), two.path()).isEmpty());
        QVERIFY(manager.moveFile(one.filePath(QStringLiteral("a")), two.path()).isEmpty());
        QVERIFY(manager.rename(one.filePath(QStringLiteral("a")), QStringLiteral("b")).isEmpty());
        QVERIFY(manager.createFolder(one.path(), QStringLiteral("new")).isEmpty());
        QVERIFY(manager.trash(one.filePath(QStringLiteral("a"))).isEmpty());
        QCOMPARE(manager.rowCount(), 0);
        QCOMPARE(manager.activeCount(), 0);
    }

    void keepBothNamesPreserveExtensionsAndStayBounded() {
        QCOMPARE(
            RemoteOperationManager::keepBothName(QStringLiteral("report.txt"), 1),
            QStringLiteral("report (copy).txt"));
        QCOMPARE(
            RemoteOperationManager::keepBothName(QStringLiteral("report.txt"), 2),
            QStringLiteral("report (copy 2).txt"));
        QCOMPARE(
            RemoteOperationManager::keepBothName(QStringLiteral("archive.tar.gz"), 3),
            QStringLiteral("archive.tar (copy 3).gz"));
        QCOMPARE(
            RemoteOperationManager::keepBothName(QStringLiteral(".env"), 1),
            QStringLiteral(".env (copy)"));
        QCOMPARE(
            RemoteOperationManager::keepBothName(QStringLiteral("README"), 0),
            QStringLiteral("README (copy)"));
        QVERIFY(RemoteOperationManager::keepBothName(QString(), 1).isEmpty());
    }

    void remoteConflictPolicyIsNonDestructive() {
        QVERIFY(RemoteOperationManager::nonDestructiveConflictDecision(
            RemoteOperationManager::Skip));
        QVERIFY(RemoteOperationManager::nonDestructiveConflictDecision(
            RemoteOperationManager::KeepBoth));
        QVERIFY(RemoteOperationManager::nonDestructiveConflictDecision(
            RemoteOperationManager::CancelOperation));
        QVERIFY(!RemoteOperationManager::nonDestructiveConflictDecision(
            RemoteOperationManager::Replace));
        QVERIFY(!RemoteOperationManager::nonDestructiveConflictDecision(-1));
        QVERIFY(!RemoteOperationManager::nonDestructiveConflictDecision(99));
    }

    void modelRolesMatchExistingOperationDrawerAndConflictContract() {
        RemoteOperationManager manager;
        const auto roles = manager.roleNames();
        QCOMPARE(roles.value(RemoteOperationManager::IdRole), QByteArray("jobId"));
        QCOMPARE(roles.value(RemoteOperationManager::KindRole), QByteArray("kind"));
        QCOMPARE(roles.value(RemoteOperationManager::StateRole), QByteArray("state"));
        QCOMPARE(roles.value(RemoteOperationManager::CurrentSourceRole), QByteArray("currentSource"));
        QCOMPARE(roles.value(RemoteOperationManager::DestinationRole), QByteArray("destination"));
        QCOMPARE(roles.value(RemoteOperationManager::ProgressRole), QByteArray("progress"));
        QCOMPARE(roles.value(RemoteOperationManager::ErrorRole), QByteArray("errorText"));
        QCOMPARE(roles.value(RemoteOperationManager::ConflictSourceRole), QByteArray("conflictSource"));
        QCOMPARE(roles.value(RemoteOperationManager::ConflictDestinationRole), QByteArray("conflictDestination"));
    }
};

QTEST_GUILESS_MAIN(RemoteOperationManagerTest)
#include "RemoteOperationManagerTest.moc"
