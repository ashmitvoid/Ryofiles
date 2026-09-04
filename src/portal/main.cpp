// SPDX-License-Identifier: GPL-3.0-only

#include "portal/FileChooserPortal.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QTextStream>

namespace {

constexpr auto kPortalService = "org.freedesktop.impl.portal.desktop.ryofiles";
constexpr auto kPortalObjectPath = "/org/freedesktop/portal/desktop";

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Ryofiles FileChooser Portal"));
    QCoreApplication::setOrganizationName(QStringLiteral("Ryoku"));

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stderr) << "ryofiles-filechooser-portal: session D-Bus is unavailable\n";
        return 1;
    }

    FileChooserPortal portal;
    if (!bus.registerObject(
            QString::fromLatin1(kPortalObjectPath),
            &portal,
            QDBusConnection::ExportAllSlots)) {
        QTextStream(stderr)
            << "ryofiles-filechooser-portal: failed to export FileChooser object\n";
        return 1;
    }

    if (!bus.registerService(QString::fromLatin1(kPortalService))) {
        QTextStream(stderr)
            << "ryofiles-filechooser-portal: failed to acquire "
            << kPortalService << '\n';
        return 1;
    }

    return app.exec();
}
