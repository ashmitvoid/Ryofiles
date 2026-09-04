// SPDX-License-Identifier: GPL-3.0-only

#include "GitActionController.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

namespace {
constexpr int kMaxActionPaths = 512;

bool outsideRoot(const QString& relativePath) {
    return relativePath == QStringLiteral("..")
        || relativePath.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(relativePath);
}
} // namespace

GitActionController::GitActionController(QObject* parent)
    : QObject(parent) {}

GitActionController::~GitActionController() {
    m_stopping.store(true, std::memory_order_relaxed);
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    if (m_future.isRunning())
        m_future.waitForFinished();
}

void GitActionController::setImmediateError(const QString& message) {
    m_error = message;
    emit stateChanged();
}

bool GitActionController::validatedRelativePaths(
    const QString& repositoryRoot,
    const QStringList& requestedPaths,
    QStringList* relativePaths,
    QString* error) {
    if (!relativePaths)
        return false;

    relativePaths->clear();
    if (error)
        error->clear();

    const QDir root(repositoryRoot);
    if (repositoryRoot.trimmed().isEmpty() || !root.exists()) {
        if (error)
            *error = tr("Git repository root is unavailable");
        return false;
    }

    if (requestedPaths.isEmpty()) {
        if (error)
            *error = tr("No files selected");
        return false;
    }

    if (requestedPaths.size() > kMaxActionPaths) {
        if (error)
            *error = tr("Too many files selected for one Git action");
        return false;
    }

    const QString absoluteRoot = QDir(repositoryRoot).absolutePath();
    const QDir normalizedRoot(absoluteRoot);

    for (const QString& requested : requestedPaths) {
        if (requested.trimmed().isEmpty())
            continue;

        const QString absolute = QFileInfo(requested).absoluteFilePath();
        const QString relative = QDir::cleanPath(normalizedRoot.relativeFilePath(absolute));

        if (relative.isEmpty() || relative == QStringLiteral(".") || outsideRoot(relative)) {
            if (error)
                *error = tr("Refusing Git action outside the repository: %1").arg(requested);
            relativePaths->clear();
            return false;
        }

        relativePaths->push_back(relative);
    }

    relativePaths->removeDuplicates();
    if (relativePaths->isEmpty()) {
        if (error)
            *error = tr("No valid Git paths selected");
        return false;
    }

    return true;
}

GitActionController::ProcessResult GitActionController::runProcess(
    const QString& repositoryRoot,
    const QStringList& arguments,
    qsizetype outputLimit,
    const std::atomic_bool& cancelled,
    const std::atomic_bool& stopping,
    bool truncateIsSuccess) {
    ProcessResult result;

    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty()) {
        result.error = tr("Git executable not found");
        return result;
    }

    QProcess process;
    process.setWorkingDirectory(repositoryRoot);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(git, arguments, QIODevice::ReadOnly);

    if (!process.waitForStarted(1000)) {
        result.error = tr("Could not start Git command");
        return result;
    }

    QElapsedTimer elapsed;
    elapsed.start();
    bool stoppedForOutputLimit = false;

    const auto appendBounded = [](QByteArray* target, const QByteArray& chunk, qsizetype limit) {
        if (!target || chunk.isEmpty())
            return true;
        const qsizetype remaining = qMax<qsizetype>(0, limit - target->size());
        if (remaining > 0)
            target->append(chunk.left(remaining));
        return chunk.size() <= remaining;
    };

    while (process.state() != QProcess::NotRunning) {
        if (cancelled.load(std::memory_order_relaxed)
            || stopping.load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(200);
            result.cancelled = true;
            return result;
        }

        process.waitForReadyRead(40);
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();

        if (!appendBounded(&result.standardOutput, out, outputLimit)) {
            result.truncated = true;
            process.kill();
            process.waitForFinished(200);
            if (truncateIsSuccess) {
                stoppedForOutputLimit = true;
                break;
            }
            result.error = tr("Git command output limit reached");
            return result;
        }

        if (!appendBounded(&result.standardError, err, MaxErrorBytes)) {
            result.truncated = true;
            process.kill();
            process.waitForFinished(200);
            result.error = tr("Git command error output limit reached");
            return result;
        }

        if (elapsed.elapsed() > ProcessTimeoutMs) {
            process.kill();
            process.waitForFinished(200);
            result.error = tr("Git command timed out");
            return result;
        }
    }

    if (!stoppedForOutputLimit) {
        const QByteArray remainingOut = process.readAllStandardOutput();
        const QByteArray remainingErr = process.readAllStandardError();
        if (!appendBounded(&result.standardOutput, remainingOut, outputLimit)) {
            result.truncated = true;
            if (!truncateIsSuccess) {
                result.error = tr("Git command output limit reached");
                return result;
            }
            stoppedForOutputLimit = true;
        }
        if (!appendBounded(&result.standardError, remainingErr, MaxErrorBytes)) {
            result.truncated = true;
            result.error = tr("Git command error output limit reached");
            return result;
        }
    }

    if (stoppedForOutputLimit && truncateIsSuccess) {
        result.success = true;
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString message = QString::fromUtf8(result.standardError).trimmed();
        result.error = message.isEmpty() ? tr("Git command failed") : message;
        return result;
    }

    result.success = true;
    return result;
}

bool GitActionController::hasHead(
    const QString& repositoryRoot,
    const std::atomic_bool& cancelled,
    const std::atomic_bool& stopping) {
    const ProcessResult result = runProcess(
        repositoryRoot,
        {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("HEAD")},
        4096,
        cancelled,
        stopping);
    return result.success;
}

QString GitActionController::startMutation(
    Kind kind,
    const QString& repositoryRoot,
    const QStringList& paths) {
    QStringList relativePaths;
    QString validationError;
    if (!validatedRelativePaths(repositoryRoot, paths, &relativePaths, &validationError)) {
        setImmediateError(validationError);
        return {};
    }

    Request request;
    request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.kind = kind;
    request.repositoryRoot = QDir(repositoryRoot).absolutePath();
    request.relativePaths = relativePaths;
    return startRequest(std::move(request));
}

QString GitActionController::stage(
    const QString& repositoryRoot,
    const QStringList& paths) {
    return startMutation(Kind::Stage, repositoryRoot, paths);
}

QString GitActionController::unstage(
    const QString& repositoryRoot,
    const QStringList& paths) {
    return startMutation(Kind::Unstage, repositoryRoot, paths);
}

QString GitActionController::requestDiff(
    const QString& repositoryRoot,
    const QString& path,
    bool staged) {
    QStringList relativePaths;
    QString validationError;
    if (!validatedRelativePaths(repositoryRoot, {path}, &relativePaths, &validationError)) {
        setImmediateError(validationError);
        return {};
    }

    Request request;
    request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.kind = Kind::Diff;
    request.repositoryRoot = QDir(repositoryRoot).absolutePath();
    request.relativePaths = relativePaths;
    request.stagedDiff = staged;
    return startRequest(std::move(request));
}

QString GitActionController::startRequest(Request request) {
    if (m_busy) {
        setImmediateError(tr("Another Git action is already running"));
        return {};
    }

    ++m_generation;
    const quint64 generation = m_generation;
    const QString requestId = request.id;
    auto token = std::make_shared<std::atomic_bool>(false);
    m_cancelToken = token;
    m_busy = true;
    m_error.clear();

    if (request.kind == Kind::Diff) {
        m_diffText.clear();
        m_diffPath = QDir(request.repositoryRoot).absoluteFilePath(request.relativePaths.constFirst());
        m_diffStaged = request.stagedDiff;
        m_diffTruncated = false;
        emit diffChanged();
    }

    emit stateChanged();

    m_future = QtConcurrent::run([this, request = std::move(request), token, generation] {
        runRequest(request, token, generation);
    });
    return requestId;
}

void GitActionController::runRequest(
    Request request,
    const std::shared_ptr<std::atomic_bool>& token,
    quint64 generation) {
    ProcessResult result;

    if (request.kind == Kind::Stage) {
        QStringList arguments{QStringLiteral("add"), QStringLiteral("--")};
        arguments.append(request.relativePaths);
        result = runProcess(
            request.repositoryRoot,
            arguments,
            MaxCommandOutputBytes,
            *token,
            m_stopping);
    } else if (request.kind == Kind::Unstage) {
        QStringList arguments;
        if (hasHead(request.repositoryRoot, *token, m_stopping)) {
            arguments = {
                QStringLiteral("restore"),
                QStringLiteral("--staged"),
                QStringLiteral("--"),
            };
        } else {
            arguments = {
                QStringLiteral("rm"),
                QStringLiteral("--cached"),
                QStringLiteral("-r"),
                QStringLiteral("--ignore-unmatch"),
                QStringLiteral("--"),
            };
        }
        arguments.append(request.relativePaths);
        result = runProcess(
            request.repositoryRoot,
            arguments,
            MaxCommandOutputBytes,
            *token,
            m_stopping);
    } else {
        QStringList arguments{
            QStringLiteral("--no-pager"),
            QStringLiteral("diff"),
            QStringLiteral("--no-ext-diff"),
            QStringLiteral("--no-textconv"),
            QStringLiteral("--no-color"),
            QStringLiteral("--unified=3"),
        };
        if (request.stagedDiff)
            arguments.push_back(QStringLiteral("--cached"));
        arguments.push_back(QStringLiteral("--"));
        arguments.append(request.relativePaths);
        result = runProcess(
            request.repositoryRoot,
            arguments,
            MaxDiffBytes,
            *token,
            m_stopping,
            true);
    }

    if (m_stopping.load(std::memory_order_relaxed))
        return;

    QMetaObject::invokeMethod(
        this,
        [this, request = std::move(request), generation, result = std::move(result)]() mutable {
            applyResult(request, generation, std::move(result));
        },
        Qt::QueuedConnection);
}

void GitActionController::applyResult(
    const Request& request,
    quint64 generation,
    ProcessResult result) {
    if (generation != m_generation)
        return;

    m_cancelToken.reset();
    m_busy = false;
    m_error = result.cancelled ? QString() : result.error;

    if (request.kind == Kind::Diff && result.success) {
        m_diffText = QString::fromUtf8(result.standardOutput);
        m_diffPath = QDir(request.repositoryRoot).absoluteFilePath(request.relativePaths.constFirst());
        m_diffStaged = request.stagedDiff;
        m_diffTruncated = result.truncated;
        emit diffChanged();
    }

    emit stateChanged();
    emit operationFinished(request.id, result.success, m_error);
}

void GitActionController::cancel() {
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
}

bool GitActionController::openTerminal(const QString& path) const {
    const QFileInfo info(path);
    QString directory;
    if (info.isDir())
        directory = info.absoluteFilePath();
    else
        directory = info.absolutePath();

    if (directory.isEmpty() || !QDir(directory).exists())
        return false;

    const QString ryokuApp = QStandardPaths::findExecutable(QStringLiteral("ryoku-app"));
    if (!ryokuApp.isEmpty())
        return QProcess::startDetached(ryokuApp, {QStringLiteral("terminal")}, directory);

    const QString xdgTerminal = QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec"));
    if (!xdgTerminal.isEmpty())
        return QProcess::startDetached(xdgTerminal, {}, directory);

    const QStringList fallbacks{
        QStringLiteral("kitty"),
        QStringLiteral("foot"),
        QStringLiteral("alacritty"),
        QStringLiteral("wezterm"),
        QStringLiteral("ghostty"),
        QStringLiteral("konsole"),
    };
    for (const QString& candidate : fallbacks) {
        const QString executable = QStandardPaths::findExecutable(candidate);
        if (!executable.isEmpty())
            return QProcess::startDetached(executable, {}, directory);
    }

    return false;
}
