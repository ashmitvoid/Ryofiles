// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QString>
#include <QtTypes>

#include <functional>
#include <utility>

class NetworkMountRecoveryRegistry final {
public:
    using Callback = std::function<void(const QString& rootUri)>;

    static NetworkMountRecoveryRegistry& instance() {
        static NetworkMountRecoveryRegistry registry;
        return registry;
    }

    quint64 subscribe(Callback callback) {
        if (!callback)
            return 0;

        const quint64 id = ++m_nextId;
        m_callbacks.insert(id, std::move(callback));
        return id;
    }

    void unsubscribe(quint64 id) {
        if (id != 0)
            m_callbacks.remove(id);
    }

    void notifyUnmounted(const QString& rootUri) {
        if (rootUri.trimmed().isEmpty())
            return;

        // Copy before dispatch so callbacks may destroy/unsubscribe managers safely.
        const auto callbacks = m_callbacks.values();
        for (const Callback& callback : callbacks) {
            if (callback)
                callback(rootUri);
        }
    }

private:
    NetworkMountRecoveryRegistry() = default;

    QHash<quint64, Callback> m_callbacks;
    quint64 m_nextId = 0;
};
