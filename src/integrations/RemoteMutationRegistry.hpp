// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QtTypes>

#include <functional>
#include <utility>

class RemoteMutationRegistry final {
public:
    using Callback = std::function<void()>;

    static RemoteMutationRegistry& instance() {
        static RemoteMutationRegistry registry;
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

    void notifyChanged() {
        // Copy before dispatch so callbacks may destroy/unsubscribe managers safely.
        const auto callbacks = m_callbacks.values();
        for (const Callback& callback : callbacks) {
            if (callback)
                callback();
        }
    }

private:
    RemoteMutationRegistry() = default;

    QHash<quint64, Callback> m_callbacks;
    quint64 m_nextId = 0;
};
