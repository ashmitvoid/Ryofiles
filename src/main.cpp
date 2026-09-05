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
#include "picker/PortalPickerContext.hpp"
#include "picker/PortalWindowParent.hpp"
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
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QUrl>
#include <QWindow>
#include <QtQml>

namespace {

constexpr auto kPortalService = "org.freedesktop.impl.portal.desktop.ryofiles";
constexpr auto kPortalObjectPath = "/org/freedesktop/portal/desktop";
constexpr qsizetype kMaximumPortalContextBytes = 1024 * 1024;

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

bool readPortalPickerContext(QJsonObject* context, QString* error) {
    if (error)
        error->clear();
    if (!context)
        return false;

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read portal picker context from stdin");
        return false;
    }

    const QByteArray bytes = input.read(kMaximumPortalContextBytes + 1);
    if (bytes.size() > kMaximumPortalContextBytes) {
        if (error)
            *error = QStringLiteral("Portal picker context exceeded the size limit");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Portal picker context is malformed JSON");
        return false;
    }

    *context = document.object();
    return true;
}

int runPicker(
    QGuiApplication& app,
    const QString& mode,
    bool multiple,
    const QString& initialDirectory,
    const QStringList& mimeTypes,
    const QString& suggestedName,
    const QString& dialogTitle,
    const QString& acceptLabel,
    const QJsonObject& portalContextObject,
    bool structuredPortalResult) {
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

    PortalPickerContext portalContext;
    if (structuredPortalResult
        && !portalContext.configure(portalContextObject, &configurationError)) {
        QTextStream(stderr)
            << "ryofiles: " << configurationError << Qt::endl;
        return 2;
    }

    QGuiApplication::setApplicationName(QStringLiteral("Ryofiles Picker"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ryofiles-picker"));

    RyokuIntegration ryoku;

    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Ryoku", &ryoku);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "Picker", &picker);
    qmlRegisterSingletonInstance("Ryofiles.Core", 1, 0, "PortalContext", &portalContext);
    registerNavigationTypes();

    QQmlApplicationEngine engine;
    engine.setInitialProperties({
        {QStringLiteral("dialogTitle"), dialogTitle},
        {QStringLiteral("customAcceptLabel"), acceptLabel},
    });
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
        [&app, &portalContext, structuredPortalResult](const QStringList& paths) {
            QStringList uris;
            uris.reserve(paths.size());
            for (const QString& path : paths) {
                uris.push_back(
                    QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
            }

            QTextStream output(stdout);
            if (structuredPortalResult) {
                output << QString::fromUtf8(
                    QJsonDocument(portalContext.resultObject(uris))
                        .toJson(QJsonDocument::Compact))
                       << Qt::endl;
            } else {
                for (const QString& uri : uris)
                    output << uri << Qt::endl;
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

    PortalWindowParent portalWindowParent;
    if (structuredPortalResult && !engine.rootObjects().isEmpty()) {
        QWindow* pickerWindow = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
        if (pickerWindow) {
            const bool modal = qEnvironmentVariable("RYOFILES_PORTAL_MODAL") != QStringLiteral("0");
            pickerWindow->setModality(modal ? Qt::WindowModal : Qt::NonModal);

            const PortalParentWindow parent = PortalParentWindow::parse(
                qEnvironmentVariable("RYOFILES_PORTAL_PARENT_WINDOW"));
            if (!parent.isEmpty())
                portalWindowParent.attach(pickerWindow, parent, modal);
        }
    }

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
    const QCommandLineOption pickerTitleOption(
        QStringList{QStringLiteral("picker-title")},
        QStringLiteral("Window title for picker mode."),
        QStringLiteral("title"));
    const QCommandLineOption acceptLabelOption(
        QStringList{QStringLiteral("accept-label")},
        QStringLiteral("Custom accept-button label for picker mode."),
        QStringLiteral("label"));
    const QCommandLineOption portalContextStdinOption(
        QStringList{QStringLiteral("portal-context-stdin")},
        QStringLiteral("Read internal FileChooser picker context from stdin."));
    const QCommandLineOption fileChooserPortalOption(
        QStringList{QStringLiteral("filechooser-portal")},
        QStringLiteral("Run the XDG FileChooser portal backend service."));

    parser.addOption(pickerOption);
    parser.addOption(multipleOption);
    parser.addOption(initialDirectoryOption);
    parser.addOption(mimeOption);
    parser.addOption(suggestedNameOption);
    parser.addOption(pickerTitleOption);
    parser.addOption(acceptLabelOption);
    parser.addOption(portalContextStdinOption);
    parser.addOption(fileChooserPortalOption);
    parser.process(app);

    if (parser.isSet(fileChooserPortalOption)) {
        if (parser.isSet(pickerOption)
            || parser.isSet(multipleOption)
            || parser.isSet(initialDirectoryOption)
            || parser.isSet(mimeOption)
            || parser.isSet(suggestedNameOption)
            || parser.isSet(pickerTitleOption)
            || parser.isSet(acceptLabelOption)
            || parser.isSet(portalContextStdinOption)) {
            QTextStream(stderr)
                << "ryofiles: --filechooser-portal cannot be combined with picker options\n";
            return 2;
        }
        return runFileChooserPortal(app);
    }

    if (parser.isSet(pickerOption)) {
        QJsonObject portalContext;
        const bool structuredPortalResult = parser.isSet(portalContextStdinOption);
        if (structuredPortalResult) {
            QString contextError;
            if (!readPortalPickerContext(&portalContext, &contextError)) {
                QTextStream(stderr)
                    << "ryofiles: " << contextError << Qt::endl;
                return 2;
            }
        }

        return runPicker(
            app,
            parser.value(pickerOption),
            parser.isSet(multipleOption),
            parser.value(initialDirectoryOption),
            parser.values(mimeOption),
            parser.value(suggestedNameOption),
            parser.value(pickerTitleOption),
            parser.value(acceptLabelOption),
            portalContext,
            structuredPortalResult);
    }

    if (parser.isSet(multipleOption)
        || parser.isSet(initialDirectoryOption)
        || parser.isSet(mimeOption)
        || parser.isSet(suggestedNameOption)
        || parser.isSet(pickerTitleOption)
        || parser.isSet(acceptLabelOption)
        || parser.isSet(portalContextStdinOption)) {
        QTextStream(stderr)
            << "ryofiles: picker-only options require --picker"
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
