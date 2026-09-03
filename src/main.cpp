// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"
#include "ryoku/RyokuIntegration.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ryofiles"));
    QGuiApplication::setOrganizationName(QStringLiteral("Ryoku"));

    RyokuIntegration ryoku;
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterType<DirectoryModel>("Ryofiles.Core", 1, 0, "DirectoryModel");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    engine.loadFromModule("Ryofiles", "Main");
    return app.exec();
}
