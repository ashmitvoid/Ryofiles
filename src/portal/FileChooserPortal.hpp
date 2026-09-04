// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "portal/PortalPickerRequest.hpp"

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QVariantMap>

class PendingPortalRequest;

class FileChooserPortal final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
    Q_CLASSINFO(
        "D-Bus Introspection",
        "<interface name=\"org.freedesktop.impl.portal.FileChooser\">"
        "<method name=\"OpenFile\">"
        "<arg type=\"o\" name=\"handle\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"title\" direction=\"in\"/>"
        "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>"
        "<arg type=\"u\" name=\"response\" direction=\"out\"/>"
        "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>"
        "</method>"
        "<method name=\"SaveFile\">"
        "<arg type=\"o\" name=\"handle\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"title\" direction=\"in\"/>"
        "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>"
        "<arg type=\"u\" name=\"response\" direction=\"out\"/>"
        "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>"
        "</method>"
        "<method name=\"SaveFiles\">"
        "<arg type=\"o\" name=\"handle\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/>"
        "<arg type=\"s\" name=\"title\" direction=\"in\"/>"
        "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>"
        "<arg type=\"u\" name=\"response\" direction=\"out\"/>"
        "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>"
        "</method>"
        "</interface>")

public:
    explicit FileChooserPortal(QObject* parent = nullptr);

public slots:
    void OpenFile(
        const QDBusObjectPath& handle,
        const QString& appId,
        const QString& parentWindow,
        const QString& title,
        const QVariantMap& options);

    void SaveFile(
        const QDBusObjectPath& handle,
        const QString& appId,
        const QString& parentWindow,
        const QString& title,
        const QVariantMap& options);

    void SaveFiles(
        const QDBusObjectPath& handle,
        const QString& appId,
        const QString& parentWindow,
        const QString& title,
        const QVariantMap& options);

private:
    void startRequest(
        const QDBusObjectPath& handle,
        PortalPickerRequest request);
    void finishImmediate(quint32 response, const QVariantMap& results = {});

    QHash<QString, PendingPortalRequest*> m_pending;
};
