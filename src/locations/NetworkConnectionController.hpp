// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QStringList>

struct NetworkConnectionTarget {
    bool valid = false;
    QString uri;
    QString suggestedUserName;
    QString error;
};

class NetworkConnectionController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool awaitingCredentials READ awaitingCredentials NOTIFY promptChanged)
    Q_PROPERTY(bool awaitingChoice READ awaitingChoice NOTIFY promptChanged)
    Q_PROPERTY(QString promptMessage READ promptMessage NOTIFY promptChanged)
    Q_PROPERTY(QString suggestedUserName READ suggestedUserName NOTIFY promptChanged)
    Q_PROPERTY(QString suggestedDomain READ suggestedDomain NOTIFY promptChanged)
    Q_PROPERTY(bool needsUserName READ needsUserName NOTIFY promptChanged)
    Q_PROPERTY(bool needsPassword READ needsPassword NOTIFY promptChanged)
    Q_PROPERTY(bool needsDomain READ needsDomain NOTIFY promptChanged)
    Q_PROPERTY(bool anonymousSupported READ anonymousSupported NOTIFY promptChanged)
    Q_PROPERTY(QStringList choices READ choices NOTIFY promptChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString targetUri READ targetUri NOTIFY stateChanged)

public:
    explicit NetworkConnectionController(QObject* parent = nullptr);
    ~NetworkConnectionController() override;

    bool busy() const { return m_busy; }
    bool awaitingCredentials() const { return m_awaitingCredentials; }
    bool awaitingChoice() const { return m_awaitingChoice; }
    QString promptMessage() const { return m_promptMessage; }
    QString suggestedUserName() const { return m_suggestedUserName; }
    QString suggestedDomain() const { return m_suggestedDomain; }
    bool needsUserName() const { return m_needsUserName; }
    bool needsPassword() const { return m_needsPassword; }
    bool needsDomain() const { return m_needsDomain; }
    bool anonymousSupported() const { return m_anonymousSupported; }
    QStringList choices() const { return m_choices; }
    QString lastError() const { return m_lastError; }
    QString targetUri() const { return m_targetUri; }

    Q_INVOKABLE bool connectTo(const QString& location);
    Q_INVOKABLE void submitCredentials(
        const QString& userName,
        const QString& password,
        const QString& domain,
        bool anonymous);
    Q_INVOKABLE void submitChoice(int choice);
    Q_INVOKABLE void cancel();

    static NetworkConnectionTarget targetFromInput(const QString& location);

signals:
    void stateChanged();
    void promptChanged();
    void connected(const QString& uri);
    void connectionFailed(const QString& uri, const QString& error);
    void connectionCancelled(const QString& uri);

private:
    struct RequestContext;

    void resetPrompt();
    void showCredentialsPrompt(
        const QString& message,
        const QString& suggestedUserName,
        const QString& suggestedDomain,
        bool needsUserName,
        bool needsPassword,
        bool needsDomain,
        bool anonymousSupported);
    void showChoicePrompt(const QString& message, const QStringList& choices);
    void finishRequest(
        RequestContext* context,
        bool success,
        bool cancelled,
        const QString& error);
    void setImmediateError(const QString& error);

    RequestContext* m_request = nullptr;
    bool m_busy = false;
    bool m_awaitingCredentials = false;
    bool m_awaitingChoice = false;
    bool m_needsUserName = false;
    bool m_needsPassword = false;
    bool m_needsDomain = false;
    bool m_anonymousSupported = false;
    QString m_promptMessage;
    QString m_suggestedUserName;
    QString m_suggestedDomain;
    QStringList m_choices;
    QString m_lastError;
    QString m_targetUri;
};
