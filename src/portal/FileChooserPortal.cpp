// SPDX-License-Identifier: GPL-3.0-only

#include "FileChooserPortal.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace {

constexpr quint32 kSuccess = 0;
constexpr quint32 kCancelled = 1;
constexpr quint32 kOther = 2;

class PortalRequestObject final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    explicit PortalRequestObject(QObject* parent = nullptr)
        : QObject(parent) {
    }

public slots:
    void Close() {
        if (m_closed)
            return;
        m_closed = true;
        emit closeRequested();
    }

signals:
    void closeRequested();

private:
    bool m_closed = false;
};

} // namespace

class PendingPortalRequest final : public QObject {
    Q_OBJECT

public:
    PendingPortalRequest(
        QDBusConnection bus,
        QDBusMessage invocation,
        QDBusObjectPath handle,
        PortalPickerRequest request,
        QObject* parent = nullptr)
        : QObject(parent)
        , m_bus(std::move(bus))
        , m_invocation(std::move(invocation))
        , m_handle(std::move(handle))
        , m_request(std::move(request)) {
        m_requestObject = new PortalRequestObject(this);
        connect(
            m_requestObject,
            &PortalRequestObject::closeRequested,
            this,
            &PendingPortalRequest::cancelFromPortal,
            Qt::QueuedConnection);

        connect(
            &m_process,
            &QProcess::started,
            this,
            [this] {
                const QByteArray context =
                    QJsonDocument(m_request.pickerContextJson())
                        .toJson(QJsonDocument::Compact);
                m_process.write(context);
                m_process.closeWriteChannel();
            });

        connect(
            &m_process,
            &QProcess::finished,
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_finished)
                    return;
                if (exitStatus != QProcess::NormalExit) {
                    complete(kOther, {});
                    return;
                }
                if (exitCode == 1) {
                    complete(kCancelled, {});
                    return;
                }
                if (exitCode != 0) {
                    complete(kOther, {});
                    return;
                }

                const PortalPickerResult pickerResult =
                    PortalPickerResult::fromPickerStdout(
                        m_request,
                        m_process.readAllStandardOutput());
                if (!pickerResult.valid) {
                    complete(kOther, {});
                    return;
                }

                QVariantMap results;
                results.insert(QStringLiteral("uris"), pickerResult.uris);

                if (pickerResult.selectedFilterIndex >= 0
                    && pickerResult.selectedFilterIndex < m_request.filters.size()) {
                    results.insert(
                        QStringLiteral("current_filter"),
                        QVariant::fromValue(
                            m_request.filters.at(pickerResult.selectedFilterIndex)));
                }

                if (!m_request.choices.isEmpty()) {
                    PortalChoiceSelectionList selections;
                    selections.values.reserve(m_request.choices.size());
                    for (const PortalChoice& choice : m_request.choices) {
                        selections.values.push_back({
                            choice.id,
                            pickerResult.choiceSelections.value(choice.id),
                        });
                    }
                    results.insert(
                        QStringLiteral("choices"),
                        QVariant::fromValue(selections));
                }

                complete(kSuccess, results);
            });

        connect(
            &m_process,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError error) {
                if (m_finished)
                    return;
                if (error == QProcess::FailedToStart)
                    complete(kOther, {});
            });
    }

    bool start() {
        if (!m_request.valid) {
            complete(kOther, {});
            return false;
        }

        if (m_handle.path().isEmpty()
            || !m_bus.registerObject(
                m_handle.path(),
                m_requestObject,
                QDBusConnection::ExportAllSlots)) {
            complete(kOther, {});
            return false;
        }
        m_requestRegistered = true;

        QString pickerProgram = qEnvironmentVariable("RYOFILES_PICKER_EXECUTABLE");
        if (pickerProgram.isEmpty())
            pickerProgram = QStandardPaths::findExecutable(QStringLiteral("ryofiles"));
        if (pickerProgram.isEmpty()) {
            complete(kOther, {});
            return false;
        }

        m_process.setProgram(pickerProgram);
        m_process.setArguments(m_request.pickerProcessArguments());
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        return true;
    }

    QString handlePath() const { return m_handle.path(); }

signals:
    void completed(const QString& handlePath);

private slots:
    void cancelFromPortal() {
        if (m_finished)
            return;
        if (m_process.state() != QProcess::NotRunning)
            m_process.kill();
        complete(kOther, {});
    }

private:
    void complete(quint32 response, const QVariantMap& results) {
        if (m_finished)
            return;
        m_finished = true;

        if (m_requestRegistered) {
            m_bus.unregisterObject(m_handle.path());
            m_requestRegistered = false;
        }

        const QVariantList replyArguments = {
            QVariant::fromValue(response),
            QVariant::fromValue(results),
        };
        m_bus.send(m_invocation.createReply(replyArguments));

        emit completed(m_handle.path());
        deleteLater();
    }

    QDBusConnection m_bus;
    QDBusMessage m_invocation;
    QDBusObjectPath m_handle;
    PortalPickerRequest m_request;
    PortalRequestObject* m_requestObject = nullptr;
    QProcess m_process;
    bool m_requestRegistered = false;
    bool m_finished = false;
};

FileChooserPortal::FileChooserPortal(QObject* parent)
    : QObject(parent) {
    registerPortalDbusTypes();
}

void FileChooserPortal::OpenFile(
    const QDBusObjectPath& handle,
    const QString& appId,
    const QString& parentWindow,
    const QString& title,
    const QVariantMap& options) {
    Q_UNUSED(appId)
    Q_UNUSED(parentWindow)
    startRequest(handle, PortalPickerRequest::openFile(title, options));
}

void FileChooserPortal::SaveFile(
    const QDBusObjectPath& handle,
    const QString& appId,
    const QString& parentWindow,
    const QString& title,
    const QVariantMap& options) {
    Q_UNUSED(appId)
    Q_UNUSED(parentWindow)
    startRequest(handle, PortalPickerRequest::saveFile(title, options));
}

void FileChooserPortal::SaveFiles(
    const QDBusObjectPath& handle,
    const QString& appId,
    const QString& parentWindow,
    const QString& title,
    const QVariantMap& options) {
    Q_UNUSED(appId)
    Q_UNUSED(parentWindow)
    startRequest(handle, PortalPickerRequest::saveFiles(title, options));
}

void FileChooserPortal::startRequest(
    const QDBusObjectPath& handle,
    PortalPickerRequest request) {
    if (!calledFromDBus())
        return;

    setDelayedReply(true);
    const QDBusMessage invocation = message();
    const QDBusConnection bus = connection();

    if (!request.valid || m_pending.contains(handle.path())) {
        const QVariantList replyArguments = {
            QVariant::fromValue(kOther),
            QVariant::fromValue(QVariantMap{}),
        };
        bus.send(invocation.createReply(replyArguments));
        return;
    }

    auto* pending = new PendingPortalRequest(
        bus,
        invocation,
        handle,
        std::move(request),
        this);
    m_pending.insert(handle.path(), pending);

    connect(
        pending,
        &PendingPortalRequest::completed,
        this,
        [this, pending](const QString& handlePath) {
            if (m_pending.value(handlePath) == pending)
                m_pending.remove(handlePath);
        });

    pending->start();
}

void FileChooserPortal::finishImmediate(
    quint32 response,
    const QVariantMap& results) {
    if (!calledFromDBus())
        return;
    setDelayedReply(true);
    const QVariantList replyArguments = {
        QVariant::fromValue(response),
        QVariant::fromValue(results),
    };
    connection().send(message().createReply(replyArguments));
}

#include "FileChooserPortal.moc"
