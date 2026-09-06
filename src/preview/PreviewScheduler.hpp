// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

struct PreviewResult {
    bool ok = false;
    QString error;
    QJsonObject payload;
};

class PreviewScheduler final : public QObject {
public:
    enum class Lane {
        InteractivePreview,
        ExplicitLongWork,
    };

    using Callback = std::function<void(PreviewResult)>;

    static constexpr int kMaxPending = 8;
    static constexpr int kMaxLongPending = 4;
    static constexpr int kMaxActiveHelpers = 1;
    static constexpr int kMaxLongWorkHelpers = 1;
    static constexpr int kMaxTotalHelpers = 2;
    static constexpr int kIdleExitMs = 10'000;
    static constexpr int kInteractiveTimeoutMs = 15'000;
    static constexpr int kLongWorkTimeoutMs = 5 * 60 * 1000;

    explicit PreviewScheduler(QString helperPath = {}, QObject* parent = nullptr);
    ~PreviewScheduler() override;

    static PreviewScheduler& instance();

    bool submit(
        Lane lane,
        QObject* owner,
        const QJsonObject& request,
        Callback callback);
    void cancelOwner(QObject* owner);

    int pendingCount(Lane lane) const;
    bool helperRunning(Lane lane) const;
    QString helperPath() const { return m_helperPath; }

private:
    struct Request {
        quint64 id = 0;
        QObject* ownerKey = nullptr;
        QPointer<QObject> owner;
        QJsonObject request;
        Callback callback;
        int retries = 0;
    };

    struct LaneState {
        QQueue<Request> pending;
        std::optional<Request> active;
        QProcess process;
        QTimer idleTimer;
        QTimer requestTimer;
        QByteArray readBuffer;
        int capacity = 0;
        int timeoutMs = 0;
        bool stopping = false;
        bool intentionalStop = false;
    };

    static QString defaultHelperPath();

    LaneState& stateFor(Lane lane);
    const LaneState& stateFor(Lane lane) const;
    void configureLane(LaneState& state, int capacity, int timeoutMs);
    void ensureProcess(LaneState& state);
    void dispatchNext(LaneState& state);
    void handleReadyRead(LaneState& state);
    void handleProcessFinished(LaneState& state);
    void handleFailedToStart(LaneState& state);
    void handleRequestTimeout(LaneState& state);
    void completeActive(LaneState& state, PreviewResult result);
    void failActiveAndRestart(LaneState& state, const QString& error, bool allowRetry);
    void stopProcess(LaneState& state, bool intentional);
    void scheduleIdleExit(LaneState& state);
    void failAllPending(LaneState& state, const QString& error);

    QString m_helperPath;
    quint64 m_nextRequestId = 1;
    LaneState m_interactive;
    LaneState m_longWork;
};

#include "PreviewSchedulerImplementation.hpp"
