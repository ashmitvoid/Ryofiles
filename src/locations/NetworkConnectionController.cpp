// SPDX-License-Identifier: GPL-3.0-only

#include <gio/gio.h>

#include "locations/NetworkConnectionController.hpp"

#include "locations/LocationSpec.hpp"

#include <QByteArray>
#include <QPointer>

struct NetworkConnectionController::RequestContext {
    QPointer<NetworkConnectionController> owner;
    GFile* file = nullptr;
    GMountOperation* operation = nullptr;
    GCancellable* cancellable = nullptr;
    QString uri;
    QString suggestedUserName;
    bool userCancelled = false;

    ~RequestContext() {
        if (operation) {
            // Do not leave authentication material in the GMountOperation longer than needed.
            g_mount_operation_set_password(operation, nullptr);
            g_object_unref(operation);
        }
        if (cancellable)
            g_object_unref(cancellable);
        if (file)
            g_object_unref(file);
    }
};

namespace {
QString utf8(const gchar* text) {
    return text ? QString::fromUtf8(text) : QString();
}
} // namespace

NetworkConnectionController::NetworkConnectionController(QObject* parent)
    : QObject(parent) {
}

NetworkConnectionController::~NetworkConnectionController() {
    if (!m_request)
        return;

    RequestContext* context = m_request;
    m_request = nullptr;
    context->owner.clear();
    context->userCancelled = true;

    // Keep the cancellable alive even if cancellation completes the async request inline.
    auto* cancellable = G_CANCELLABLE(g_object_ref(context->cancellable));
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);
}

NetworkConnectionTarget NetworkConnectionController::targetFromInput(const QString& location) {
    NetworkConnectionTarget target;
    const LocationSpec spec = LocationSpec::parse(location);
    if (!spec.isValid()) {
        target.error = spec.error;
        return target;
    }
    if (!spec.isNetwork()) {
        target.error = QStringLiteral("Only supported network locations can be connected");
        return target;
    }

    target.valid = true;
    target.uri = spec.canonical;
    target.suggestedUserName = spec.userName;
    return target;
}

bool NetworkConnectionController::connectTo(const QString& location) {
    if (m_request) {
        // Do not disturb an outstanding authentication/question prompt for the active request.
        m_lastError = tr("A network connection is already in progress");
        emit stateChanged();
        return false;
    }

    const NetworkConnectionTarget target = targetFromInput(location);
    if (!target.valid) {
        setImmediateError(target.error.isEmpty()
            ? tr("Network location is invalid")
            : target.error);
        return false;
    }

    auto* context = new RequestContext;
    context->owner = this;
    context->uri = target.uri;
    context->suggestedUserName = target.suggestedUserName;

    const QByteArray uriBytes = target.uri.toUtf8();
    context->file = g_file_new_for_uri(uriBytes.constData());
    context->operation = g_mount_operation_new();
    context->cancellable = g_cancellable_new();

    if (!context->file || !context->operation || !context->cancellable) {
        delete context;
        setImmediateError(tr("Could not initialize the network connection"));
        return false;
    }

    if (!target.suggestedUserName.isEmpty()) {
        const QByteArray userBytes = target.suggestedUserName.toUtf8();
        g_mount_operation_set_username(context->operation, userBytes.constData());
    }
    g_mount_operation_set_password_save(context->operation, G_PASSWORD_SAVE_NEVER);

    const auto askPassword = +[](
        GMountOperation* operation,
        gchar* message,
        gchar* defaultUser,
        gchar* defaultDomain,
        GAskPasswordFlags flags,
        gpointer userData) {
        auto* context = static_cast<RequestContext*>(userData);
        if (!context || !context->owner) {
            g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
            return;
        }

        QString user = utf8(defaultUser);
        if (user.isEmpty())
            user = context->suggestedUserName;

        context->owner->showCredentialsPrompt(
            utf8(message),
            user,
            utf8(defaultDomain),
            flags & G_ASK_PASSWORD_NEED_USERNAME,
            flags & G_ASK_PASSWORD_NEED_PASSWORD,
            flags & G_ASK_PASSWORD_NEED_DOMAIN,
            flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED);
    };

    const auto askQuestion = +[](
        GMountOperation* operation,
        gchar* message,
        gchar** rawChoices,
        gpointer userData) {
        auto* context = static_cast<RequestContext*>(userData);
        if (!context || !context->owner) {
            g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
            return;
        }

        QStringList choices;
        for (gchar** current = rawChoices; current && *current; ++current)
            choices.push_back(QString::fromUtf8(*current));

        if (choices.isEmpty()) {
            g_mount_operation_reply(operation, G_MOUNT_OPERATION_UNHANDLED);
            return;
        }
        context->owner->showChoicePrompt(utf8(message), choices);
    };

    const auto aborted = +[](GMountOperation*, gpointer userData) {
        auto* context = static_cast<RequestContext*>(userData);
        if (context && context->owner)
            context->owner->resetPrompt();
    };

    g_signal_connect(context->operation, "ask-password", G_CALLBACK(askPassword), context);
    g_signal_connect(context->operation, "ask-question", G_CALLBACK(askQuestion), context);
    g_signal_connect(context->operation, "aborted", G_CALLBACK(aborted), context);

    m_request = context;
    m_busy = true;
    m_targetUri = target.uri;
    m_lastError.clear();
    resetPrompt();
    emit stateChanged();

    g_file_mount_enclosing_volume(
        context->file,
        G_MOUNT_MOUNT_NONE,
        context->operation,
        context->cancellable,
        +[](GObject* sourceObject, GAsyncResult* result, gpointer userData) {
            auto* context = static_cast<RequestContext*>(userData);
            GError* error = nullptr;
            bool success = g_file_mount_enclosing_volume_finish(
                G_FILE(sourceObject), result, &error);

            if (error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED))
                success = true;

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

void NetworkConnectionController::submitCredentials(
    const QString& userName,
    const QString& password,
    const QString& domain,
    bool anonymous) {
    if (!m_request || !m_awaitingCredentials)
        return;

    GMountOperation* operation = m_request->operation;
    if (m_anonymousSupported && anonymous) {
        g_mount_operation_set_anonymous(operation, TRUE);
    } else {
        g_mount_operation_set_anonymous(operation, FALSE);

        if (m_needsUserName) {
            const QByteArray bytes = userName.toUtf8();
            g_mount_operation_set_username(operation, bytes.constData());
        }
        if (m_needsDomain) {
            const QByteArray bytes = domain.toUtf8();
            g_mount_operation_set_domain(operation, bytes.constData());
        }
        if (m_needsPassword) {
            QByteArray bytes = password.toUtf8();
            g_mount_operation_set_password(operation, bytes.constData());
            bytes.fill('\0');
        }
    }

    // Ryofiles does not persist credentials in this initial network slice.
    g_mount_operation_set_password_save(operation, G_PASSWORD_SAVE_NEVER);
    resetPrompt();
    g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
}

void NetworkConnectionController::submitChoice(int choice) {
    if (!m_request || !m_awaitingChoice || choice < 0 || choice >= m_choices.size())
        return;

    GMountOperation* operation = m_request->operation;
    g_mount_operation_set_choice(operation, choice);
    resetPrompt();
    g_mount_operation_reply(operation, G_MOUNT_OPERATION_HANDLED);
}

void NetworkConnectionController::cancel() {
    if (!m_request)
        return;

    RequestContext* context = m_request;
    context->userCancelled = true;

    // Hold independent references because replying/cancelling may complete the request inline.
    auto* operation = G_MOUNT_OPERATION(g_object_ref(context->operation));
    auto* cancellable = G_CANCELLABLE(g_object_ref(context->cancellable));
    const bool promptOutstanding = m_awaitingCredentials || m_awaitingChoice;
    resetPrompt();

    if (promptOutstanding)
        g_mount_operation_reply(operation, G_MOUNT_OPERATION_ABORTED);
    g_cancellable_cancel(cancellable);

    g_object_unref(cancellable);
    g_object_unref(operation);
}

void NetworkConnectionController::resetPrompt() {
    const bool changed = m_awaitingCredentials ||
        m_awaitingChoice ||
        !m_promptMessage.isEmpty() ||
        !m_suggestedUserName.isEmpty() ||
        !m_suggestedDomain.isEmpty() ||
        m_needsUserName ||
        m_needsPassword ||
        m_needsDomain ||
        m_anonymousSupported ||
        !m_choices.isEmpty();

    m_awaitingCredentials = false;
    m_awaitingChoice = false;
    m_promptMessage.clear();
    m_suggestedUserName.clear();
    m_suggestedDomain.clear();
    m_needsUserName = false;
    m_needsPassword = false;
    m_needsDomain = false;
    m_anonymousSupported = false;
    m_choices.clear();

    if (changed)
        emit promptChanged();
}

void NetworkConnectionController::showCredentialsPrompt(
    const QString& message,
    const QString& suggestedUserName,
    const QString& suggestedDomain,
    bool needsUserName,
    bool needsPassword,
    bool needsDomain,
    bool anonymousSupported) {
    m_awaitingCredentials = true;
    m_awaitingChoice = false;
    m_promptMessage = message;
    m_suggestedUserName = suggestedUserName;
    m_suggestedDomain = suggestedDomain;
    m_needsUserName = needsUserName;
    m_needsPassword = needsPassword;
    m_needsDomain = needsDomain;
    m_anonymousSupported = anonymousSupported;
    m_choices.clear();
    emit promptChanged();
}

void NetworkConnectionController::showChoicePrompt(
    const QString& message,
    const QStringList& choices) {
    m_awaitingCredentials = false;
    m_awaitingChoice = true;
    m_promptMessage = message;
    m_suggestedUserName.clear();
    m_suggestedDomain.clear();
    m_needsUserName = false;
    m_needsPassword = false;
    m_needsDomain = false;
    m_anonymousSupported = false;
    m_choices = choices;
    emit promptChanged();
}

void NetworkConnectionController::finishRequest(
    RequestContext* context,
    bool success,
    bool cancelled,
    const QString& error) {
    if (context != m_request) {
        delete context;
        return;
    }

    const QString uri = context->uri;
    m_request = nullptr;
    m_busy = false;
    m_targetUri.clear();
    resetPrompt();

    // Clear the password before releasing the operation object.
    g_mount_operation_set_password(context->operation, nullptr);

    if (cancelled) {
        m_lastError.clear();
        emit stateChanged();
        emit connectionCancelled(uri);
        delete context;
        return;
    }

    if (success) {
        m_lastError.clear();
        emit stateChanged();
        emit connected(uri);
        delete context;
        return;
    }

    m_lastError = error.isEmpty()
        ? tr("Could not connect to the network location")
        : error;
    emit stateChanged();
    emit connectionFailed(uri, m_lastError);
    delete context;
}

void NetworkConnectionController::setImmediateError(const QString& error) {
    resetPrompt();
    m_lastError = error;
    emit stateChanged();
}
