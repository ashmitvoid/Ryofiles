// SPDX-License-Identifier: GPL-3.0-only

#include <gio/gio.h>

#include "locations/NetworkDisconnectController.hpp"

#include "locations/LocationSpec.hpp"

#include <QPointer>

struct NetworkDisconnectController::RequestContext {
    QPointer<NetworkDisconnectController> owner;
    GMount* mount = nullptr;
    GCancellable* cancellable = nullptr;
    QString rootUri;
    bool userCancelled = false;

    ~RequestContext() {
        if (cancellable)
            g_object_unref(cancellable);
        if (mount)
            g_object_unref(mount);
    }
};

namespace {
QString takeUtf8(gchar* value) {
    if (!value)
        return {};
    const QString result = QString::fromUtf8(value);
    g_free(value);
    return result;
}

QString mountRootUri(GMount* mount) {
    if (!mount)
        return {};

    GFile* root = g_mount_get_root(mount);
    if (!root)
        return {};

    const QString uri = takeUtf8(g_file_get_uri(root));
    g_object_unref(root);
    return uri;
}

GMount* findMountForRoot(const QString& targetRootUri) {
    GVolumeMonitor* monitor = g_volume_monitor_get();
    if (!monitor)
        return nullptr;

    GMount* match = nullptr;
    GList* mounts = g_volume_monitor_get_mounts(monitor);
    for (GList* node = mounts; node; node = node->next) {
        auto* mount = G_MOUNT(node->data);
        if (!match &&
            !g_mount_is_shadowed(mount) &&
            NetworkDisconnectController::rootMatches(mountRootUri(mount), targetRootUri)) {
            match = G_MOUNT(g_object_ref(mount));
        }
        g_object_unref(mount);
    }
    g_list_free(mounts);
    g_object_unref(monitor);
    return match;
}
} // namespace

NetworkDisconnectController::NetworkDisconnectController(QObject* parent)
    : QObject(parent) {
}

NetworkDisconnectController::~NetworkDisconnectController() {
    if (!m_request)
        return;

    RequestContext* context = m_request;
    m_request = nullptr;
    context->owner.clear();
    context->userCancelled = true;

    auto* cancellable = G_CANCELLABLE(g_object_ref(context->cancellable));
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);
}

NetworkDisconnectTarget NetworkDisconnectController::targetFromInput(const QString& rootUri) {
    NetworkDisconnectTarget target;
    const LocationSpec spec = LocationSpec::parse(rootUri);
    if (!spec.isValid()) {
        target.error = spec.error;
        return target;
    }
    if (!spec.isNetwork()) {
        target.error = QStringLiteral("Only supported network locations can be disconnected");
        return target;
    }

    target.valid = true;
    target.rootUri = spec.canonical;
    return target;
}

bool NetworkDisconnectController::rootMatches(
    const QString& mountedRootUri,
    const QString& targetRootUri) {
    const NetworkDisconnectTarget mounted = targetFromInput(mountedRootUri);
    const NetworkDisconnectTarget target = targetFromInput(targetRootUri);
    return mounted.valid && target.valid && mounted.rootUri == target.rootUri;
}

bool NetworkDisconnectController::disconnectFrom(const QString& rootUri) {
    if (m_request) {
        m_lastError = tr("A network disconnect is already in progress");
        emit stateChanged();
        return false;
    }

    const NetworkDisconnectTarget target = targetFromInput(rootUri);
    if (!target.valid) {
        setImmediateError(target.error.isEmpty()
            ? tr("Network mount is invalid")
            : target.error);
        return false;
    }

    GMount* mount = findMountForRoot(target.rootUri);
    if (!mount) {
        setImmediateError(tr("Network mount is no longer available"));
        return false;
    }
    if (!g_mount_can_unmount(mount)) {
        g_object_unref(mount);
        setImmediateError(tr("This network mount cannot be disconnected"));
        return false;
    }

    auto* context = new RequestContext;
    context->owner = this;
    context->mount = mount;
    context->cancellable = g_cancellable_new();
    context->rootUri = target.rootUri;

    if (!context->cancellable) {
        delete context;
        setImmediateError(tr("Could not initialize the network disconnect"));
        return false;
    }

    m_request = context;
    m_busy = true;
    m_targetRootUri = target.rootUri;
    m_lastError.clear();
    emit stateChanged();

    g_mount_unmount_with_operation(
        context->mount,
        G_MOUNT_UNMOUNT_NONE,
        nullptr,
        context->cancellable,
        +[](GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            auto* context = static_cast<RequestContext*>(userData);
            GError* error = nullptr;
            const bool success = g_mount_unmount_with_operation_finish(
                G_MOUNT(sourceObject), result, &error);
            const bool cancelled = context &&
                (context->userCancelled ||
                 (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)));
            const QString errorText = error ? QString::fromUtf8(error->message) : QString();
            if (error)
                g_error_free(error);

            if (!context)
                return;
            if (context->owner)
                context->owner->finishRequest(context, success, cancelled, errorText);
            else
                delete context;
        },
        context);

    return true;
}

void NetworkDisconnectController::cancel() {
    if (!m_request)
        return;

    RequestContext* context = m_request;
    context->userCancelled = true;

    auto* cancellable = G_CANCELLABLE(g_object_ref(context->cancellable));
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);
}

void NetworkDisconnectController::finishRequest(
    RequestContext* context,
    bool success,
    bool cancelled,
    const QString& error) {
    if (context != m_request) {
        delete context;
        return;
    }

    const QString rootUri = context->rootUri;
    m_request = nullptr;
    m_busy = false;
    m_targetRootUri.clear();

    if (cancelled) {
        m_lastError.clear();
        emit stateChanged();
        emit disconnectCancelled(rootUri);
        delete context;
        return;
    }

    if (success) {
        m_lastError.clear();
        emit stateChanged();
        emit disconnected(rootUri);
        delete context;
        return;
    }

    m_lastError = error.isEmpty()
        ? tr("Could not disconnect the network mount")
        : error;
    emit stateChanged();
    emit disconnectFailed(rootUri, m_lastError);
    delete context;
}

void NetworkDisconnectController::setImmediateError(const QString& error) {
    m_lastError = error;
    emit stateChanged();
}
