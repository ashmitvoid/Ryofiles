// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"
#include "integrations/ClipboardController.hpp"
#include "integrations/DesktopIntegration.hpp"
#include "navigation/DirectorySession.hpp"
#include "navigation/TabManager.hpp"
#include "operations/OperationManager.hpp"
#include "preview/TextPreviewLoader.hpp"
#include "ryoku/RyokuIntegration.hpp"
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

    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "FileClipboard", &fileClipboard);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Desktop", &desktop);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Thumbnails", &thumbnails);

    qmlRegisterUncreatableType<DirectorySession>(
        "Ryofiles.Core", 1, 0, "DirectorySession",
        QStringLiteral("DirectorySession instances are managed by TabManager"));
    qmlRegisterUncreatableType<DirectoryModel>(
        "Ryofiles.Core", 1, 0, "DirectoryModel",
        QStringLiteral("DirectoryModel instances are owned by DirectorySession"));
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
