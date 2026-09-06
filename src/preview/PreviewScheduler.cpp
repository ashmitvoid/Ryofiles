// SPDX-License-Identifier: GPL-3.0-only

#include "PreviewScheduler.hpp"
#include "PreviewProtocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <utility>

namespace {

PreviewResult unavailableResult(const QString& detail = {}) {
    PreviewResult result;
    result.ok = false;
    result.error = detail.isEmpty()
        ? QStringLiteral("Preview unavailable")
        : detail;
    return result;
}

} // namespace

PreviewScheduler::PreviewScheduler(QString helperPath, QObject* parent)
    : QObject(parent)
    , m_helperPath(helperPath.isEmpty() ? defaultHelperPath() : std::move(helperPath)) {
    configureLane(m_interactive, kMaxPending, kInteractiveTimeoutMs);
    configureLane(m_longWork, kMaxLongPending, kLongWorkTimeoutMs);
}

PreviewScheduler::~PreviewScheduler() {
    for (LaneState* state : {&m_interactive, &m_longWork}) {
        state->idleTimer.stop();
        state->requestTimer.stop();
        state->pending.clear();
        state->active.reset();
        if (state->process.state() != QProcess::NotRunning) {
            state->process.kill();
            state->process.waitForFinished(1000);
        }
    }
}

PreviewScheduler& PreviewScheduler::instance() {
    static PreviewScheduler scheduler;
    return scheduler;
}

QString PreviewScheduler::defaultHelperPath() {
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("ryofiles-preview-helper"));
}

void PreviewScheduler::configureLane(LaneState& state, int capacity, int timeoutMs) {
    state.capacity = capacity;
    state.timeoutMs = timeoutMs;
    state.idleTimer.setSingleShot(true);
    state.idleTimer.setInterval(kIdleExitMs);
    state.requestTimer.setSingleShot(true);
    state.requestTimer.setInterval(timeoutMs);
    state.process.setProcessChannelMode(QProcess::SeparateChannels);

    connect(&state.process, &QProcess::started, this, [this, &state] {
        state.stopping = false;
        state.intentionalStop = false;
        dispatchNext(state);
    });
    connect(&state.process, &QProcess::readyReadStandardOutput, this, [this, &state] {
        handleReadyRead(state);
    });
    connect(&state.process, &QProcess::readyReadStandardError, this, [&state] {
        state.process.readAllStandardError();
    });
    connect(
        &state.process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, &state](int, QProcess::ExitStatus) {
            handleProcessFinished(state);
        });
    connect(&state.process, &QProcess::errorOccurred, this, [this, &state](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            handleFailedToStart(state);
    });
    connect(&state.idleTimer, &QTimer::timeout, this, [this, &state] {
        if (!state.active && state.pending.isEmpty())
            stopProcess(state, true);
    });
    connect(&state.requestTimer, &QTimer::timeout, this, [this, &state] {
        handleRequestTimeout(state);
    });
}

PreviewScheduler::LaneState& PreviewScheduler::stateFor(Lane lane) {
    return lane == Lane::InteractivePreview ? m_interactive : m_longWork;
}

const PreviewScheduler::LaneState& PreviewScheduler::stateFor(Lane lane) const {
    return lane == Lane::InteractivePreview ? m_interactive : m_longWork;
}

bool PreviewScheduler::submit(
    Lane lane,
    QObject* owner,
    const QJsonObject& request,
    Callback callback) {
    if (!owner || !callback || request.value(QStringLiteral("op")).toString().isEmpty())
        return false;

    LaneState& state = stateFor(lane);
    const int admitted = state.pending.size() + (state.active.has_value() ? 1 : 0);
    if (admitted >= state.capacity)
        return false;

    Request queued;
    queued.id = m_nextRequestId++;
    if (m_nextRequestId == 0)
        m_nextRequestId = 1;
    queued.ownerKey = owner;
    queued.owner = owner;
    queued.request = request;
    queued.callback = std::move(callback);
    state.pending.enqueue(std::move(queued));

    connect(owner, &QObject::destroyed, this, [this, owner] {
        cancelOwner(owner);
    });

    state.idleTimer.stop();
    ensureProcess(state);
    dispatchNext(state);
    return true;
}

void PreviewScheduler::cancelOwner(QObject* owner) {
    if (!owner)
        return;

    for (LaneState* state : {&m_interactive, &m_longWork}) {
        QQueue<Request> retained;
        while (!state->pending.isEmpty()) {
            Request request = state->pending.dequeue();
            if (request.ownerKey != owner)
                retained.enqueue(std::move(request));
        }
        state->pending = std::move(retained);

        if (state->active && state->active->ownerKey == owner) {
            state->requestTimer.stop();
            state->active.reset();
            stopProcess(*state, true);
        } else if (!state->active && state->pending.isEmpty()) {
            scheduleIdleExit(*state);
        }
    }
}

int PreviewScheduler::pendingCount(Lane lane) const {
    const LaneState& state = stateFor(lane);
    return state.pending.size() + (state.active.has_value() ? 1 : 0);
}

bool PreviewScheduler::helperRunning(Lane lane) const {
    return stateFor(lane).process.state() != QProcess::NotRunning;
}

void PreviewScheduler::ensureProcess(LaneState& state) {
    if (state.stopping || state.process.state() != QProcess::NotRunning)
        return;
    if (state.pending.isEmpty())
        return;

    state.readBuffer.clear();
    state.intentionalStop = false;
    state.process.setProgram(m_helperPath);
    state.process.setArguments({});
    state.process.start(QIODevice::ReadWrite);
}

void PreviewScheduler::dispatchNext(LaneState& state) {
    if (state.stopping || state.active || state.pending.isEmpty())
        return;

    if (state.process.state() != QProcess::Running) {
        ensureProcess(state);
        return;
    }

    state.active = state.pending.dequeue();
    if (!state.active->owner) {
        state.active.reset();
        dispatchNext(state);
        return;
    }

    QJsonObject envelope = state.active->request;
    envelope.insert(QStringLiteral("id"), QString::number(state.active->id));
    QByteArray line = QJsonDocument(envelope).toJson(QJsonDocument::Compact);
    if (line.size() > PreviewProtocol::kMaxRequestLineBytes) {
        completeActive(state, unavailableResult(QStringLiteral("Preview request is too large")));
        return;
    }
    line.append('\n');

    if (state.process.write(line) < 0) {
        failActiveAndRestart(state, QStringLiteral("Preview unavailable"), true);
        return;
    }
    state.requestTimer.start(state.timeoutMs);
}

void PreviewScheduler::handleReadyRead(LaneState& state) {
    state.readBuffer.append(state.process.readAllStandardOutput());
    if (state.readBuffer.size() > PreviewProtocol::kMaxProtocolBufferBytes) {
        failActiveAndRestart(state, QStringLiteral("Preview unavailable"), false);
        return;
    }

    while (true) {
        const qsizetype newline = state.readBuffer.indexOf('\n');
        if (newline < 0)
            return;

        const QByteArray line = state.readBuffer.left(newline);
        state.readBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;

        if (!state.active) {
            stopProcess(state, false);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            failActiveAndRestart(state, QStringLiteral("Preview unavailable"), false);
            return;
        }

        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("id")).toString()
            != QString::number(state.active->id)) {
            failActiveAndRestart(state, QStringLiteral("Preview unavailable"), false);
            return;
        }

        PreviewResult result;
        result.ok = object.value(QStringLiteral("ok")).toBool(false);
        result.error = object.value(QStringLiteral("error")).toString();
        result.payload = object.value(QStringLiteral("payload")).toObject();
        if (!result.ok && result.error.isEmpty())
            result.error = QStringLiteral("Preview unavailable");
        completeActive(state, std::move(result));
    }
}

void PreviewScheduler::handleProcessFinished(LaneState& state) {
    state.requestTimer.stop();
    state.readBuffer.clear();

    const bool intentional = state.intentionalStop;
    state.stopping = false;
    state.intentionalStop = false;

    if (state.active) {
        Request failed = *state.active;
        state.active.reset();
        if (!intentional && failed.owner && failed.retries < 1) {
            ++failed.retries;
            state.pending.prepend(std::move(failed));
        } else if (!intentional && failed.owner && failed.callback) {
            failed.callback(unavailableResult());
        }
    }

    if (!state.pending.isEmpty()) {
        ensureProcess(state);
    } else {
        state.idleTimer.stop();
    }
}

void PreviewScheduler::handleFailedToStart(LaneState& state) {
    state.requestTimer.stop();
    state.readBuffer.clear();
    state.stopping = false;
    state.intentionalStop = false;
    if (state.active) {
        Request failed = *state.active;
        state.active.reset();
        if (failed.owner && failed.callback)
            failed.callback(unavailableResult());
    }
    failAllPending(state, QStringLiteral("Preview unavailable"));
}

void PreviewScheduler::handleRequestTimeout(LaneState& state) {
    if (!state.active)
        return;
    failActiveAndRestart(state, QStringLiteral("Preview timed out"), false);
}

void PreviewScheduler::completeActive(LaneState& state, PreviewResult result) {
    state.requestTimer.stop();
    if (!state.active)
        return;

    Request completed = *state.active;
    state.active.reset();
    if (completed.owner && completed.callback)
        completed.callback(std::move(result));

    dispatchNext(state);
    if (!state.active && state.pending.isEmpty())
        scheduleIdleExit(state);
}

void PreviewScheduler::failActiveAndRestart(
    LaneState& state,
    const QString& error,
    bool allowRetry) {
    state.requestTimer.stop();
    if (state.active) {
        Request failed = *state.active;
        state.active.reset();
        if (allowRetry && failed.owner && failed.retries < 1) {
            ++failed.retries;
            state.pending.prepend(std::move(failed));
        } else if (failed.owner && failed.callback) {
            failed.callback(unavailableResult(error));
        }
    }
    stopProcess(state, false);
}

void PreviewScheduler::stopProcess(LaneState& state, bool intentional) {
    state.idleTimer.stop();
    state.requestTimer.stop();
    state.readBuffer.clear();
    state.intentionalStop = intentional;

    if (state.process.state() == QProcess::NotRunning) {
        state.stopping = false;
        state.intentionalStop = false;
        if (!state.pending.isEmpty())
            ensureProcess(state);
        return;
    }

    state.stopping = true;
    state.process.kill();
}

void PreviewScheduler::scheduleIdleExit(LaneState& state) {
    if (state.process.state() == QProcess::Running && !state.active && state.pending.isEmpty())
        state.idleTimer.start(kIdleExitMs);
}

void PreviewScheduler::failAllPending(LaneState& state, const QString& error) {
    while (!state.pending.isEmpty()) {
        Request request = state.pending.dequeue();
        if (request.owner && request.callback)
            request.callback(unavailableResult(error));
    }
}
