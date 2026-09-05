// SPDX-License-Identifier: GPL-3.0-only

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QtTest>

namespace {

constexpr auto kPortalService = "org.freedesktop.impl.portal.desktop.ryofiles";
constexpr auto kPortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr auto kPortalInterface = "org.freedesktop.impl.portal.FileChooser";
constexpr auto kRequestInterface = "org.freedesktop.impl.portal.Request";

bool hasPickerArgument(const QStringList& arguments) {
    for (const QString& argument : arguments) {
        if (argument == QStringLiteral("--picker")
            || argument.startsWith(QStringLiteral("--picker="))) {
            return true;
        }
    }
    return false;
}

} // namespace

class FileChooserPortalSmokeTest final : public QObject {
    Q_OBJECT

public:
    explicit FileChooserPortalSmokeTest(QString portalExecutable)
        : m_portalExecutable(std::move(portalExecutable)) {
    }

private:
    bool serviceRegistered() const {
        QDBusConnectionInterface* interface =
            QDBusConnection::sessionBus().interface();
        if (!interface)
            return false;
        const QDBusReply<bool> reply =
            interface->isServiceRegistered(QString::fromLatin1(kPortalService));
        return reply.isValid() && reply.value();
    }

    bool waitForService(bool expected, int timeoutMs = 5000) const {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (serviceRegistered() == expected)
                return true;
            QTest::qWait(25);
        }
        return serviceRegistered() == expected;
    }

    bool startPortal(
        QProcess& process,
        const QString& pickerMode,
        const QString& pickerUri = QString()) const {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(
            QStringLiteral("QT_QPA_PLATFORM"),
            QStringLiteral("offscreen"));
        environment.insert(
            QStringLiteral("RYOFILES_PICKER_EXECUTABLE"),
            QCoreApplication::applicationFilePath());
        environment.insert(
            QStringLiteral("RYOFILES_SMOKE_PICKER_MODE"),
            pickerMode);
        environment.insert(
            QStringLiteral("RYOFILES_SMOKE_FILE_URI"),
            pickerUri);

        process.setProcessEnvironment(environment);
        process.setProgram(m_portalExecutable);
        process.setArguments({QStringLiteral("--filechooser-portal")});
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (!process.waitForStarted(5000))
            return false;
        return waitForService(true);
    }

    void stopPortal(QProcess& process) const {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(1500)) {
                process.kill();
                process.waitForFinished(1500);
            }
        }
        waitForService(false, 3000);
    }

    QDBusPendingCall openFile(
        const QString& handlePath,
        const QString& title = QStringLiteral("Ryofiles smoke")) const {
        QDBusInterface portal(
            QString::fromLatin1(kPortalService),
            QString::fromLatin1(kPortalObjectPath),
            QString::fromLatin1(kPortalInterface),
            QDBusConnection::sessionBus());
        if (!portal.isValid())
            return QDBusPendingCall::fromError(portal.lastError());

        return portal.asyncCall(
            QStringLiteral("OpenFile"),
            QVariant::fromValue(QDBusObjectPath(handlePath)),
            QStringLiteral("org.ryoku.RyofilesSmoke"),
            QString(),
            title,
            QVariantMap{});
    }

    bool closeRequest(const QString& handlePath) const {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService),
            handlePath,
            QString::fromLatin1(kRequestInterface),
            QStringLiteral("Close"));
        const QDBusMessage reply = QDBusConnection::sessionBus().call(
            message,
            QDBus::Block,
            400);
        return reply.type() == QDBusMessage::ReplyMessage;
    }

private slots:
    void init() {
        QVERIFY2(
            QDBusConnection::sessionBus().isConnected(),
            "FileChooser smoke must run inside a private session D-Bus");
        QVERIFY2(
            !serviceRegistered(),
            "Private session bus unexpectedly already owns the Ryofiles portal name");
    }

    void openFileReturnsValidatedUri() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString selectedPath = temp.filePath(QStringLiteral("selected file.txt"));
        QFile selected(selectedPath);
        QVERIFY(selected.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(selected.write("smoke", 5), 5LL);
        selected.close();

        const QString expectedUri =
            QUrl::fromLocalFile(selectedPath).toString(QUrl::FullyEncoded);

        QProcess portalProcess;
        QVERIFY2(
            startPortal(portalProcess, QStringLiteral("success"), expectedUri),
            qPrintable(QString::fromUtf8(portalProcess.readAllStandardError())));

        const QString handlePath =
            QStringLiteral("/org/freedesktop/portal/desktop/request/ryofiles_smoke/success");
        const QDBusPendingCall pending = openFile(handlePath);
        QDBusPendingCallWatcher watcher(pending);
        QSignalSpy finishedSpy(&watcher, &QDBusPendingCallWatcher::finished);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);

        const QDBusPendingReply<quint32, QVariantMap> reply(pending);
        QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
        QCOMPARE(reply.argumentAt<0>(), quint32(0));

        const QVariantMap results = reply.argumentAt<1>();
        QCOMPARE(
            results.value(QStringLiteral("uris")).toStringList(),
            QStringList({expectedUri}));

        QVERIFY(serviceRegistered());
        stopPortal(portalProcess);
        QVERIFY(!serviceRegistered());
    }

    void requestCloseCancelsBlockingPicker() {
        QProcess portalProcess;
        QVERIFY2(
            startPortal(portalProcess, QStringLiteral("block")),
            qPrintable(QString::fromUtf8(portalProcess.readAllStandardError())));

        const QString handlePath =
            QStringLiteral("/org/freedesktop/portal/desktop/request/ryofiles_smoke/cancel");
        const QDBusPendingCall pending = openFile(handlePath, QStringLiteral("Cancel smoke"));
        QDBusPendingCallWatcher watcher(pending);
        QSignalSpy finishedSpy(&watcher, &QDBusPendingCallWatcher::finished);

        QTRY_VERIFY_WITH_TIMEOUT(closeRequest(handlePath), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);

        const QDBusPendingReply<quint32, QVariantMap> reply(pending);
        QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
        QCOMPARE(reply.argumentAt<0>(), quint32(2));
        QVERIFY(reply.argumentAt<1>().isEmpty());

        QVERIFY(serviceRegistered());
        stopPortal(portalProcess);
        QVERIFY(!serviceRegistered());
    }

private:
    QString m_portalExecutable;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments();

    if (hasPickerArgument(arguments)) {
        const QString mode = qEnvironmentVariable("RYOFILES_SMOKE_PICKER_MODE");
        if (mode == QStringLiteral("success")) {
            const QString uri = qEnvironmentVariable("RYOFILES_SMOKE_FILE_URI");
            if (uri.isEmpty())
                return 2;
            QTextStream(stdout) << uri << Qt::endl;
            return 0;
        }
        if (mode == QStringLiteral("block"))
            return app.exec();
        return 2;
    }

    const QString portalExecutable =
        qEnvironmentVariable("RYOFILES_SMOKE_PORTAL_EXECUTABLE");
    if (portalExecutable.isEmpty()) {
        QTextStream(stderr)
            << "RYOFILES_SMOKE_PORTAL_EXECUTABLE is required" << Qt::endl;
        return 2;
    }

    FileChooserPortalSmokeTest test(portalExecutable);
    return QTest::qExec(&test, argc, argv);
}

#include "FileChooserPortalSmokeTest.moc"
