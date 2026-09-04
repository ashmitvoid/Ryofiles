// SPDX-License-Identifier: GPL-3.0-only
#include "portal/PortalRoutingManager.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

namespace {

int printFailure(const QString& message) {
    QTextStream(stderr) << "ryofiles-portalctl: " << message << Qt::endl;
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ryofiles-portalctl"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Safely enable, disable, or inspect Ryofiles XDG FileChooser routing."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("action"),
        QStringLiteral("One of: status, enable, disable."));
    parser.process(app);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 1)
        parser.showHelp(2);

    PortalRoutingManager manager;
    const QString action = arguments.constFirst().trimmed().toLower();

    if (action == QStringLiteral("status")) {
        const PortalRoutingManager::Status status = manager.status();
        if (!status.ok)
            return printFailure(status.message);

        QTextStream out(stdout);
        out << "enabled=" << (status.enabled ? "yes" : "no") << '\n';
        out << "managed=" << (status.managed ? "yes" : "no") << '\n';
        out << "backends=" << status.backendList << '\n';
        out << status.message << Qt::endl;
        return 0;
    }

    PortalRoutingManager::Result result;
    if (action == QStringLiteral("enable")) {
        result = manager.enable();
    } else if (action == QStringLiteral("disable")) {
        result = manager.disable();
    } else {
        return printFailure(QStringLiteral("Unknown action '%1'; use status, enable, or disable")
                                .arg(action));
    }

    if (!result.ok)
        return printFailure(result.message);

    QTextStream(stdout) << result.message << Qt::endl;
    if (result.changed) {
        QTextStream(stdout)
            << "Restart xdg-desktop-portal (or log out and back in) before testing the new route."
            << Qt::endl;
    }
    return 0;
}
