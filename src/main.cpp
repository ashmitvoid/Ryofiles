// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"
#include "git/GitActionController.hpp"
#include "git/GitStatusController.hpp"
#include "integrations/ClipboardController.hpp"
#include "integrations/DesktopIntegration.hpp"
#include "integrations/MountRecoveryRegistry.hpp"
#include "integrations/NetworkMountRecoveryRegistry.hpp"
#include "integrations/RemoteMutationRegistry.hpp"
#include "locations/NetworkConnectionController.hpp"
#include "locations/NetworkDisconnectController.hpp"
#include "locations/NetworkLocationModel.hpp"
#include "navigation/DirectorySession.hpp"
#include "navigation/TabManager.hpp"
#include "operations/OperationManager.hpp"
#include "operations/RemoteOperationManager.hpp"
#include "picker/PickerController.hpp"
#include "portal/FileChooserPortal.hpp"
#include "preview/TextPreviewLoader.hpp"
#include "ryoku/RyokuIntegration.hpp"
#include "search/DeepSearchModel.hpp"
#include "storage/DriveModel.hpp"
#include "thumbnails/ThumbnailController.hpp"
#include "thumbnails/ThumbnailProvider.hpp"
#include "trash/TrashManager.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QUrl>
#include <QtQml>

namespace {

constexpr auto kPortalService = "org.freedesktop.impl.portal.desktop.ryofiles";
constexpr auto kPortalObjectPath = "/org/freedesktop/portal/desktop";

void registerNavigationTypes() {
    qmlRegisterUncreatableType<DirectorySession>(
        "Ryofiles.Core", 1, 0, "DirectorySession",
        QStringLiteral("DirectorySession instances are managed by TabManager"));
    qmlRegisterUncreatableType<DirectoryModel>(
        "Ryofiles.Core", 1, 0, "DirectoryModel",
        QStringLiteral("DirectoryModel instances are owned by DirectorySession"));
    qmlRegisterUncreatableType<DeepSearchModel>(
        "Ryofiles.Core", 1, 0, "DeepSearchModel",
        QStringLiteral("DeepSearchModel instances are owned by DirectorySession"));
    qmlRegisterType<TabManager>("Ryofiles.Core", 1, 0, "TabManager");
}

int runPicker(
    QGuiApplication& app,
    const QString& mode,
    bool multiple,
    const QString& initialDirectory,
    const QStringList& mimeTypes,
    const QString& suggestedName) {
    PickerController picker;
    QString configurationError;
    if (!picker.configure(
            mode,
            multiple,
            initialDirectory,
            mimeTypes,
            suggestedName,
            &configurationError)) {
        QTextStream(stderr)
            << "ryofiles: " << configurationError << Qt::endl;
        return 2;
    }

    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles Picker"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ryofiles-picker"));

    RyokuIntegration ryoku;

    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Picker", &picker);
    registerNavigationTypes();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    QObject::connect(
        &picker,
        &PickerController::acceptedPaths,
        &app,
        [&app](const QStringList& paths) {
            QTextStream output(stdout);
            for (const QString& path : paths) {
                output << QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)
                       << Qt::endl;
            }
            output.flush();
            app.exit(EXIT_SUCCESS);
        });
    QObject::connect(
        &picker,
        &PickerController::cancelled,
        &app,
        [&app] { app.exit(1); });

    engine.loadFromModule("Ryofiles", "Picker");
    return app.exec();
}

int runFileChooserPortal(QGuiApplication& app) {
    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles FileChooser Portal"));

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stderr)
            << "ryofiles: FileChooser portal requires a session D-Bus\n";
        return 1;
    }

    FileChooserPortal portal;
    if (!bus.registerObject(
            QString::fromLatin1(kPortalObjectPath),
            &portal,
            QDBusConnection::ExportAllSlots)) {
        QTextStream(stderr)
            << "ryofiles: failed to export FileChooser portal object\n";
        return 1;
    }

    if (!bus.registerService(QString::fromLatin1(kPortalService))) {
        QTextStream(stderr)
            << "ryofiles: failed to acquire " << kPortalService << '\n';
        return 1;
    }

    return app.exec();
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ryofiles"));
    QGuiApplication::setOrganizationName(QStringLiteral("Ryoku"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Ryoku-native file manager and file picker"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption pickerOption(
        QStringList{QStringLiteral("picker")},
        QStringLiteral("Launch picker mode: open, save, or folder."),
        QStringLiteral("mode"));
    const QCommandLineOption multipleOption(
        QStringList{QStringLiteral("multiple")},
        QStringLiteral("Allow multiple files in --picker open mode."));
    const QCommandLineOption initialDirectoryOption(
        QStringList{QStringLiteral("initial-dir")},
        QStringLiteral("Initial local directory for picker mode."),
        QStringLiteral("path"));
    const QCommandLineOption mimeOption(
        QStringList{QStringLiteral("mime")},
        QStringLiteral("Accepted MIME type; repeat or comma-separate values."),
        QStringLiteral("type"));
    const QCommandLineOption suggestedNameOption(
        QStringList{QStringLiteral("suggest-name")},
        QStringLiteral("Suggested file name for --picker save mode."),
        QStringLiteral("name"));
    const QCommandLineOption fileChooserPortalOption(
        QStringList{QStringLiteral("filechooser-portal")},
        QStringLiteral("Run the XDG FileChooser portal backend service."));

    parser.addOption(pickerOption);
    parser.addOption(multipleOption);
    parser.addOption(initialDirectoryOption);
    parser.addOption(mimeOption);
    parser.addOption(suggestedNameOption);
    parser.addOption(fileChooserPortalOption);
    parser.process(app);

    if (parser.isSet(fileChooserPortalOption)) {
        if (parser.isSet(pickerOption)
            || parser.isSet(multipleOption)
            || parser.isSet(initialDirectoryOption)
            || parser.isSet(mimeOption)
            || parser.isSet(suggestedNameOption)) {
            QTextStream(stderr)
                << "ryofiles: --filechooser-portal cannot be combined with picker options\n";
            return 2;
        }
        return runFileChooserPortal(app);
    }

    if (parser.isSet(pickerOption)) {
        return runPicker(
            app,
            parser.value(pickerOption),
            parser.isSet(multipleOption),
            parser.value(initialDirectoryOption),
            parser.values(mimeOption),
            parser.value(suggestedNameOption));
    }

    if (parser.isSet(multipleOption)
        || parser.isSet(initialDirectoryOption)
        || parser.isSet(mimeOption)
        || parser.isSet(suggestedNameOption)) {
        QTextStream(stderr)
            << "ryofiles: --multiple, --initial-dir, --mime, and --suggest-name require --picker"
            << Qt::endl;
        return 2;
    }

    RyokuIntegration ryoku;
    ClipboardController fileClipboard;
    DesktopIntegration desktop;
    ThumbnailController thumbnails;
    GitStatusController gitStatus;
    GitActionController gitActions;
    DriveModel drives;
    NetworkLocationModel networkLocations;
    NetworkConnectionController networkConnection;
    NetworkDisconnectController networkDisconnect;
    RemoteOperationManager remoteOperations;

    QObject::connect(
        &drives,
        &DriveModel::unmounted,
        &app,
        [&drives](const QString& objectPath) {
            for (int row = 0; row < drives.rowCount(); ++row) {
                const QModelIndex index = drives.index(row, 0);
                if (drives.data(index, DriveModel::ObjectPathRole).toString() != objectPath)
                    continue;

                const QString mountRoot =
                    drives.data(index, DriveModel::MountPointRole).toString();
                if (!mountRoot.isEmpty())
                    MountRecoveryRegistry::instance().notifyUnmounted(mountRoot);
                break;
            }
        });

    QObject::connect(
        &networkConnection,
        &NetworkConnectionController::connected,
        &networkLocations,
        [&networkLocations](const QString&) { networkLocations.refresh(); });

    QObject::connect(
        &networkDisconnect,
        &NetworkDisconnectController::disconnected,
        &networkLocations,
        [&networkLocations](const QString&) { networkLocations.refresh(); });

    QObject::connect(
        &networkLocations,
        &NetworkLocationModel::unmounted,
        &app,
        [](const QString& rootUri) {
            NetworkMountRecoveryRegistry::instance().notifyUnmounted(rootUri);
        });

    QObject::connect(
        &remoteOperations,
        &RemoteOperationManager::jobFinished,
        &app,
        [](const QString&, bool success) {
            if (success)
                RemoteMutationRegistry::instance().notifyChanged();
        });

    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "FileClipboard", &fileClipboard);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Desktop", &desktop);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Thumbnails", &thumbnails);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "GitStatus", &gitStatus);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "GitActions", &gitActions);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Drives", &drives);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "NetworkLocations", &networkLocations);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "NetworkConnection", &networkConnection);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "NetworkDisconnect", &networkDisconnect);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "RemoteOperations", &remoteOperations);

    registerNavigationTypes();
    qmlRegisterType<OperationManager>("Ryofiles.Core", 1, 0, "OperationManager");
    qmlRegisterType<TrashManager>("Ryofiles.Core", 1, 0, "TrashManager");
    qmlRegisterType<TextPreviewLoader>("Ryofiles.Core", 1, 0, "TextPreviewLoader");

    QQmlApplicationEngine engine;
    engine.addImageProvider(
        QStringLiteral("ryofiles-thumb"),
        new ThumbnailImageProvider);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule("Ryofiles", "Main");
    return app.exec();
}
