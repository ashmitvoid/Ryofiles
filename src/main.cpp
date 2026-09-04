// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"
#include "git/GitActionController.hpp"
#include "git/GitStatusController.hpp"
#include "integrations/ClipboardController.hpp"
#include "integrations/DesktopIntegration.hpp"
#include "integrations/MountRecoveryRegistry.hpp"
#include "locations/NetworkConnectionController.hpp"
#include "locations/NetworkLocationModel.hpp"
#include "locations/SessionFileModel.hpp"
#include "navigation/DirectorySession.hpp"
#include "navigation/TabManager.hpp"
#include "operations/OperationManager.hpp"
#include "preview/TextPreviewLoader.hpp"
#include "ryoku/RyokuIntegration.hpp"
#include "search/DeepSearchModel.hpp"
#include "storage/DriveModel.hpp"
#include "thumbnails/ThumbnailController.hpp"
#include "thumbnails/ThumbnailProvider.hpp"
#include "trash/TrashManager.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ryofiles"));
    QGuiApplication::setOrganizationName(QStringLiteral("Ryoku"));

    RyokuIntegration ryoku;
    ClipboardController fileClipboard;
    DesktopIntegration desktop;
    ThumbnailController thumbnails;
    GitStatusController gitStatus;
    GitActionController gitActions;
    DriveModel drives;
    NetworkLocationModel networkLocations;
    NetworkConnectionController networkConnection;

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

    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "FileClipboard", &fileClipboard);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Desktop", &desktop);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Thumbnails", &thumbnails);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "GitStatus", &gitStatus);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "GitActions", &gitActions);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Drives", &drives);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "NetworkLocations", &networkLocations);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "NetworkConnection", &networkConnection);

    qmlRegisterUncreatableType<DirectorySession>(
        "Ryofiles.Core", 1, 0, "DirectorySession",
        QStringLiteral("DirectorySession instances are managed by TabManager"));
    qmlRegisterUncreatableType<SessionFileModel>(
        "Ryofiles.Core", 1, 0, "SessionFileModel",
        QStringLiteral("SessionFileModel instances are owned by DirectorySession"));
    qmlRegisterUncreatableType<DirectoryModel>(
        "Ryofiles.Core", 1, 0, "DirectoryModel",
        QStringLiteral("DirectoryModel instances are internal local backends"));
    qmlRegisterUncreatableType<DeepSearchModel>(
        "Ryofiles.Core", 1, 0, "DeepSearchModel",
        QStringLiteral("DeepSearchModel instances are owned by DirectorySession"));
    qmlRegisterType<TabManager>("Ryofiles.Core", 1, 0, "TabManager");
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
