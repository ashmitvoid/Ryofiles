// SPDX-License-Identifier: GPL-3.0-only

#include "locations/NetworkConnectionController.hpp"

#include <QtTest>

class NetworkConnectionControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void canonicalizesSupportedNetworkTargets() {
        const NetworkConnectionTarget target =
            NetworkConnectionController::targetFromInput(
                QStringLiteral(" SFTP://alice@example.com:2222/home/alice/../projects "));

        QVERIFY(target.valid);
        QCOMPARE(target.uri, QStringLiteral("sftp://alice@example.com:2222/home/projects"));
        QCOMPARE(target.suggestedUserName, QStringLiteral("alice"));
        QVERIFY(target.error.isEmpty());
    }

    void rejectsLocalLocations() {
        const NetworkConnectionTarget target =
            NetworkConnectionController::targetFromInput(QStringLiteral("/tmp"));

        QVERIFY(!target.valid);
        QVERIFY(target.uri.isEmpty());
        QVERIFY(!target.error.isEmpty());
    }

    void rejectsPasswordBearingLocationsWithoutEchoingSecret() {
        const QString secret = QStringLiteral("super-secret-password");
        const NetworkConnectionTarget target =
            NetworkConnectionController::targetFromInput(
                QStringLiteral("sftp://alice:%1@example.com/home/alice").arg(secret));

        QVERIFY(!target.valid);
        QVERIFY(!target.error.isEmpty());
        QVERIFY(!target.error.contains(secret));
    }

    void rejectsUnsupportedProtocols() {
        const NetworkConnectionTarget target =
            NetworkConnectionController::targetFromInput(
                QStringLiteral("https://example.com/files"));

        QVERIFY(!target.valid);
        QVERIFY(!target.error.isEmpty());
    }

    void invalidConnectDoesNotStartAsyncOperation() {
        NetworkConnectionController controller;
        QSignalSpy stateSpy(&controller, &NetworkConnectionController::stateChanged);

        QVERIFY(!controller.connectTo(QStringLiteral("file:///tmp")));
        QVERIFY(!controller.busy());
        QVERIFY(controller.targetUri().isEmpty());
        QVERIFY(!controller.lastError().isEmpty());
        QVERIFY(stateSpy.count() >= 1);
    }

    void idleRepliesAndCancellationAreNoOps() {
        NetworkConnectionController controller;

        controller.submitCredentials(
            QStringLiteral("alice"),
            QStringLiteral("temporary-secret"),
            {},
            false);
        controller.submitChoice(0);
        controller.cancel();

        QVERIFY(!controller.busy());
        QVERIFY(!controller.awaitingCredentials());
        QVERIFY(!controller.awaitingChoice());
        QVERIFY(controller.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(NetworkConnectionControllerTest)
#include "NetworkConnectionControllerTest.moc"
