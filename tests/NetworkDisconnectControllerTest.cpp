// SPDX-License-Identifier: GPL-3.0-only

#include "locations/NetworkDisconnectController.hpp"

#include <QtTest>

class NetworkDisconnectControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void canonicalizesSupportedNetworkRoots() {
        const NetworkDisconnectTarget target =
            NetworkDisconnectController::targetFromInput(
                QStringLiteral(" SFTP://alice@example.com:2222/home/alice/../projects "));

        QVERIFY(target.valid);
        QCOMPARE(target.rootUri, QStringLiteral("sftp://alice@example.com:2222/home/projects"));
        QVERIFY(target.error.isEmpty());
    }

    void rejectsLocalLocations() {
        const NetworkDisconnectTarget target =
            NetworkDisconnectController::targetFromInput(QStringLiteral("/tmp"));

        QVERIFY(!target.valid);
        QVERIFY(target.rootUri.isEmpty());
        QVERIFY(!target.error.isEmpty());
    }

    void rejectsPasswordBearingLocationsWithoutEchoingSecret() {
        const QString secret = QStringLiteral("super-secret-password");
        const NetworkDisconnectTarget target =
            NetworkDisconnectController::targetFromInput(
                QStringLiteral("sftp://alice:%1@example.com/home/alice").arg(secret));

        QVERIFY(!target.valid);
        QVERIFY(!target.error.isEmpty());
        QVERIFY(!target.error.contains(secret));
    }

    void rootMatchingIsCanonicalAndExact() {
        QVERIFY(NetworkDisconnectController::rootMatches(
            QStringLiteral("SFTP://alice@example.com/share/./team"),
            QStringLiteral("sftp://alice@example.com/share/team")));
        QVERIFY(!NetworkDisconnectController::rootMatches(
            QStringLiteral("sftp://alice@example.com/share/team/projects"),
            QStringLiteral("sftp://alice@example.com/share/team")));
        QVERIFY(!NetworkDisconnectController::rootMatches(
            QStringLiteral("sftp://bob@example.com/share/team"),
            QStringLiteral("sftp://alice@example.com/share/team")));
        QVERIFY(!NetworkDisconnectController::rootMatches(
            QStringLiteral("smb://example.com/share/team"),
            QStringLiteral("sftp://alice@example.com/share/team")));
    }

    void invalidDisconnectDoesNotStartAsyncOperation() {
        NetworkDisconnectController controller;
        QSignalSpy stateSpy(&controller, &NetworkDisconnectController::stateChanged);

        QVERIFY(!controller.disconnectFrom(QStringLiteral("file:///tmp")));
        QVERIFY(!controller.busy());
        QVERIFY(controller.targetRootUri().isEmpty());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(stateSpy.count() >= 1);
    }

    void idleCancellationIsNoOp() {
        NetworkDisconnectController controller;
        controller.cancel();

        QVERIFY(!controller.busy());
        QVERIFY(controller.targetRootUri().isEmpty());
        QVERIFY(controller.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(NetworkDisconnectControllerTest)
#include "NetworkDisconnectControllerTest.moc"
