// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "portal/PortalParentWindow.hpp"

#include <QGuiApplication>
#include <QWindow>

#include <algorithm>
#include <cstring>
#include <memory>

#include <dlfcn.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

class PortalWindowParent final {
public:
    PortalWindowParent() = default;
    PortalWindowParent(const PortalWindowParent&) = delete;
    PortalWindowParent& operator=(const PortalWindowParent&) = delete;

    ~PortalWindowParent() {
        resetWayland();
    }

    bool attach(
        QWindow* window,
        const PortalParentWindow& parent,
        bool modal) {
        if (!window || parent.isEmpty())
            return false;

        window->setModality(modal ? Qt::WindowModal : Qt::NonModal);

        if (parent.kind == PortalParentWindowKind::X11)
            return attachX11(window, parent.handle);
        if (parent.kind == PortalParentWindowKind::Wayland)
            return attachWayland(window, parent.handle);
        return false;
    }

private:
    using MarshalFlags = wl_proxy* (*)(
        wl_proxy*, uint32_t, const wl_interface*, uint32_t, uint32_t, ...);
    using AddListener = int (*)(wl_proxy*, void (**)(void), void*);
    using GetVersion = uint32_t (*)(wl_proxy*);
    using ProxyDestroy = void (*)(wl_proxy*);
    using DisplayRoundtrip = int (*)(wl_display*);
    using DisplayFlush = int (*)(wl_display*);

    struct ImportedListener {
        void (*destroyed)(void*, wl_proxy*);
    };

    bool attachX11(QWindow* window, const QString& handle) {
        if (QGuiApplication::platformName() != QStringLiteral("xcb"))
            return false;

        bool ok = false;
        const qulonglong xid = handle.toULongLong(&ok, 16);
        if (!ok || xid == 0)
            return false;

        m_x11Parent.reset(QWindow::fromWinId(static_cast<WId>(xid)));
        if (!m_x11Parent)
            return false;
        window->setTransientParent(m_x11Parent.get());
        return true;
    }

    void initializeProtocolInterfaces(const wl_interface* surfaceInterface) {
        m_importTypes[0] = &m_importedInterface;
        m_importTypes[1] = nullptr;
        m_setParentTypes[0] = surfaceInterface;

        m_importerMethods[0] = {"destroy", "", nullptr};
        m_importerMethods[1] = {"import_toplevel", "ns", m_importTypes};
        m_importedMethods[0] = {"destroy", "", nullptr};
        m_importedMethods[1] = {"set_parent_of", "o", m_setParentTypes};
        m_importedEvents[0] = {"destroyed", "", nullptr};

        m_importedInterface = {
            "zxdg_imported_v2",
            1,
            2,
            m_importedMethods,
            1,
            m_importedEvents,
        };
        m_importerInterface = {
            "zxdg_importer_v2",
            1,
            2,
            m_importerMethods,
            0,
            nullptr,
        };
    }

    template <typename T>
    bool resolveFunction(T* target, const char* name) {
        if (!target || !m_waylandLibrary)
            return false;
        *target = reinterpret_cast<T>(dlsym(m_waylandLibrary, name));
        return *target != nullptr;
    }

    bool loadWaylandClient() {
        if (m_waylandLibrary)
            return true;

        m_waylandLibrary = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!m_waylandLibrary)
            return false;

        const auto* registryInterface = reinterpret_cast<const wl_interface*>(
            dlsym(m_waylandLibrary, "wl_registry_interface"));
        const auto* surfaceInterface = reinterpret_cast<const wl_interface*>(
            dlsym(m_waylandLibrary, "wl_surface_interface"));
        if (!registryInterface || !surfaceInterface
            || !resolveFunction(&m_marshal, "wl_proxy_marshal_flags")
            || !resolveFunction(&m_addListener, "wl_proxy_add_listener")
            || !resolveFunction(&m_getVersion, "wl_proxy_get_version")
            || !resolveFunction(&m_proxyDestroy, "wl_proxy_destroy")
            || !resolveFunction(&m_roundtrip, "wl_display_roundtrip")
            || !resolveFunction(&m_flush, "wl_display_flush")) {
            resetWayland();
            return false;
        }

        m_registryInterface = registryInterface;
        initializeProtocolInterfaces(surfaceInterface);
        return true;
    }

    static void registryGlobal(
        void* data,
        wl_registry* registry,
        uint32_t name,
        const char* interface,
        uint32_t version) {
        auto* self = static_cast<PortalWindowParent*>(data);
        if (!self || self->m_importer || !interface
            || std::strcmp(interface, "zxdg_importer_v2") != 0) {
            return;
        }

        const uint32_t bindVersion = std::min<uint32_t>(version, 1);
        self->m_importer = self->m_marshal(
            reinterpret_cast<wl_proxy*>(registry),
            0,
            &self->m_importerInterface,
            bindVersion,
            0,
            name,
            self->m_importerInterface.name,
            bindVersion,
            nullptr);
    }

    static void registryGlobalRemove(void*, wl_registry*, uint32_t) {
    }

    static void importedDestroyed(void*, wl_proxy*) {
        // The foreign parent disappeared. The relationship is already invalid;
        // the proxy itself remains client-owned until normal teardown.
    }

    bool attachWayland(QWindow* window, const QString& handle) {
#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
        Q_UNUSED(window)
        Q_UNUSED(handle)
        return false;
#else
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
            return false;

        auto* native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!native || !native->display() || !loadWaylandClient())
            return false;

        window->create();
        auto* childSurface = reinterpret_cast<wl_surface*>(window->winId());
        if (!childSurface)
            return false;

        wl_display* display = native->display();
        m_registry = m_marshal(
            reinterpret_cast<wl_proxy*>(display),
            1,
            m_registryInterface,
            m_getVersion(reinterpret_cast<wl_proxy*>(display)),
            0,
            nullptr);
        if (!m_registry)
            return false;

        static const wl_registry_listener registryListener = {
            &PortalWindowParent::registryGlobal,
            &PortalWindowParent::registryGlobalRemove,
        };
        if (m_addListener(
                m_registry,
                reinterpret_cast<void (**)(void)>(
                    const_cast<wl_registry_listener*>(&registryListener)),
                this) != 0
            || m_roundtrip(display) < 0
            || !m_importer) {
            resetWayland();
            return false;
        }

        const QByteArray encodedHandle = handle.toUtf8();
        m_imported = m_marshal(
            m_importer,
            1,
            &m_importedInterface,
            m_getVersion(m_importer),
            0,
            encodedHandle.constData(),
            nullptr);
        if (!m_imported) {
            resetWayland();
            return false;
        }

        static const ImportedListener importedListener = {
            &PortalWindowParent::importedDestroyed,
        };
        if (m_addListener(
                m_imported,
                reinterpret_cast<void (**)(void)>(
                    const_cast<ImportedListener*>(&importedListener)),
                this) != 0) {
            resetWayland();
            return false;
        }

        m_marshal(
            m_imported,
            1,
            nullptr,
            m_getVersion(m_imported),
            0,
            childSurface);
        m_flush(display);
        return true;
#endif
    }

    void destroyProtocolObject(wl_proxy*& proxy) {
        if (!proxy || !m_marshal || !m_getVersion)
            return;
        m_marshal(
            proxy,
            0,
            nullptr,
            m_getVersion(proxy),
            WL_MARSHAL_FLAG_DESTROY);
        proxy = nullptr;
    }

    void resetWayland() {
        destroyProtocolObject(m_imported);
        destroyProtocolObject(m_importer);
        if (m_registry && m_proxyDestroy) {
            m_proxyDestroy(m_registry);
            m_registry = nullptr;
        }

        m_marshal = nullptr;
        m_addListener = nullptr;
        m_getVersion = nullptr;
        m_proxyDestroy = nullptr;
        m_roundtrip = nullptr;
        m_flush = nullptr;
        m_registryInterface = nullptr;

        if (m_waylandLibrary) {
            dlclose(m_waylandLibrary);
            m_waylandLibrary = nullptr;
        }
    }

    std::unique_ptr<QWindow> m_x11Parent;

    void* m_waylandLibrary = nullptr;
    MarshalFlags m_marshal = nullptr;
    AddListener m_addListener = nullptr;
    GetVersion m_getVersion = nullptr;
    ProxyDestroy m_proxyDestroy = nullptr;
    DisplayRoundtrip m_roundtrip = nullptr;
    DisplayFlush m_flush = nullptr;
    const wl_interface* m_registryInterface = nullptr;
    wl_proxy* m_registry = nullptr;
    wl_proxy* m_importer = nullptr;
    wl_proxy* m_imported = nullptr;

    const wl_interface* m_importTypes[2] = {};
    const wl_interface* m_setParentTypes[1] = {};
    wl_message m_importerMethods[2] = {};
    wl_message m_importedMethods[2] = {};
    wl_message m_importedEvents[1] = {};
    wl_interface m_importerInterface = {};
    wl_interface m_importedInterface = {};
};
