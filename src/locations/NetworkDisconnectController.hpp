// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

struct NetworkDisconnectTarget {
    bool valid = false;
    QString rootUri;
    QString error;
};

class NetworkDisconnectController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString targetRootUri READ targetRootUri NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit NetworkDisconnectController(QObject* parent = nullptr);
    ~NetworkDisconnectController() override;

    bool busy() const { return m_busy; }
    QString targetRootUri() const { return m_targetRootUri; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool disconnectFrom(const QString& rootUri);
    Q_INVOKABLE void cancel();

    static NetworkDisconnectTarget targetFromInput(const QString& rootUri);
    static bool rootMatches(const QString& mountedRootUri, const QString& targetRootUri);

signals:
    void stateChanged();
    void disconnected(const QString& rootUri);
    void disconnectFailed(const QString& rootUri, const QString& error);
    void disconnectCancelled(const QString& rootUri);

private:
    struct RequestContext;

    void finishRequest(
        RequestContext* context,
        bool success,
        bool cancelled,
        const QString& error);
    void setImmediateError(const QString& error);

    RequestContext* m_request = nullptr;
    bool m_busy = false;
    QString m_targetRootUri;
    QString m_lastError;
};
