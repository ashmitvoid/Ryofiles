// SPDX-License-Identifier: GPL-3.0-only

#include "GitStatusController.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtConcurrent>

GitStatusController::GitStatusController(QObject* parent)
    : QObject(parent) {
    m_refreshDebounce.setSingleShot(true);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &GitStatusController::startRefresh);

    const auto schedule = [this] {
        scheduleRefresh();
    };
    connect(&m_gitWatcher, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&m_gitWatcher, &QFileSystemWatcher::directoryChanged, this, schedule);
}

GitStatusController::~GitStatusController() {
    m_stopping.store(true, std::memory_order_relaxed);
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);

    for (QFuture<void>& future : m_futures)
        future.waitForFinished();
}

void GitStatusController::setPath(const QString& requestedPath) {
    QString clean;
    if (!requestedPath.trimmed().isEmpty())
        clean = QDir(requestedPath).absolutePath();

    if (m_path == clean)
        return;

    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();
    ++m_generation;
    m_refreshDebounce.stop();

    m_path = clean;
    emit pathChanged();
    clearVisibleState();

    if (!m_path.isEmpty())
        scheduleRefresh(0);
}

void GitStatusController::refresh() {
    if (!m_path.isEmpty())
        scheduleRefresh();
}

void GitStatusController::scheduleRefresh(int delayMs) {
    if (m_path.isEmpty())
        return;
    m_refreshDebounce.start(qMax(0, delayMs));
}

void GitStatusController::clearVisibleState() {
    const bool hadState = m_repository || !m_rootPath.isEmpty() || !m_branchName.isEmpty()
        || m_detached || m_loading || m_truncated || m_gitAvailable || !m_error.isEmpty();
    const bool hadStatuses = !m_statuses.isEmpty();

    m_repository = false;
    m_rootPath.clear();
    m_branchName.clear();
    m_detached = false;
    m_loading = false;
    m_truncated = false;
    m_gitAvailable = false;
    m_error.clear();
    m_statuses.clear();

    if (hadStatuses) {
        ++m_revision;
        emit statusChanged();
    }
    if (hadState)
        emit stateChanged();

    const QStringList files = m_gitWatcher.files();
    if (!files.isEmpty())
        m_gitWatcher.removePaths(files);
    const QStringList directories = m_gitWatcher.directories();
    if (!directories.isEmpty())
        m_gitWatcher.removePaths(directories);
}

void GitStatusController::pruneFinishedFutures() {
    for (int i = m_futures.size() - 1; i >= 0; --i) {
        if (m_futures.at(i).isFinished())
            m_futures.removeAt(i);
    }
}

GitStatusController::RepositoryInfo GitStatusController::findRepository(const QString& path) {
    RepositoryInfo result;
    if (path.isEmpty())
        return result;

    QDir directory(path);
    if (!directory.exists())
        return result;

    while (true) {
        const QString markerPath = directory.filePath(QStringLiteral(".git"));
        const QFileInfo marker(markerPath);
        QString gitDir;

        if (marker.isDir()) {
            gitDir = marker.absoluteFilePath();
        } else if (marker.isFile()) {
            QFile markerFile(markerPath);
            if (markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QByteArray line = markerFile.readLine(8192).trimmed();
                if (line.startsWith("gitdir:")) {
                    const QString value = QString::fromUtf8(line.mid(7)).trimmed();
                    gitDir = QDir(directory.absolutePath()).absoluteFilePath(value);
                }
            }
        }

        if (!gitDir.isEmpty() && QDir(gitDir).exists()) {
            result.found = true;
            result.rootPath = directory.absolutePath();
            result.gitDir = QDir::cleanPath(gitDir);
            result.branchName = readBranch(result.gitDir, &result.detached);
            return result;
        }

        if (directory.isRoot() || !directory.cdUp())
            break;
    }

    return result;
}

QString GitStatusController::readBranch(const QString& gitDir, bool* detached) {
    if (detached)
        *detached = false;

    QFile head(QDir(gitDir).filePath(QStringLiteral("HEAD")));
    if (!head.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString value = QString::fromUtf8(head.readLine(8192)).trimmed();
    const QString prefix = QStringLiteral("ref: refs/heads/");
    if (value.startsWith(prefix))
        return value.mid(prefix.size());

    if (!value.isEmpty()) {
        if (detached)
            *detached = true;
        return value.left(8);
    }

    return {};
}

int GitStatusController::flagsForXY(char x, char y) {
    if ((x == '?' && y == '?'))
        return UntrackedFlag;
    if ((x == '!' && y == '!'))
        return IgnoredFlag;

    const bool conflict = x == 'U' || y == 'U'
        || (x == 'A' && y == 'A')
        || (x == 'D' && y == 'D')
        || (x == 'A' && y == 'U')
        || (x == 'U' && y == 'A')
        || (x == 'D' && y == 'U')
        || (x == 'U' && y == 'D');
    if (conflict)
        return ConflictFlag;

    int flags = 0;
    if (x != ' ' && x != '?' && x != '!')
        flags |= StagedFlag;
    if (y != ' ' && y != '?' && y != '!')
        flags |= ModifiedFlag;
    return flags;
}

int GitStatusController::mergeFlags(int current, int incoming) {
    return current | incoming;
}

QString GitStatusController::codeForFlags(int flags) {
    if (flags & ConflictFlag)
        return QStringLiteral("conflict");
    if ((flags & StagedFlag) && (flags & ModifiedFlag))
        return QStringLiteral("mixed");
    if (flags & StagedFlag)
        return QStringLiteral("staged");
    if (flags & ModifiedFlag)
        return QStringLiteral("modified");
    if (flags & UntrackedFlag)
        return QStringLiteral("untracked");
    if (flags & IgnoredFlag)
        return QStringLiteral("ignored");
    return {};
}

QString GitStatusController::labelForCode(const QString& code) {
    if (code == QStringLiteral("conflict")) return tr("CONFLICT");
    if (code == QStringLiteral("mixed")) return tr("STAGED + MODIFIED");
    if (code == QStringLiteral("staged")) return tr("STAGED");
    if (code == QStringLiteral("modified")) return tr("MODIFIED");
    if (code == QStringLiteral("untracked")) return tr("UNTRACKED");
    if (code == QStringLiteral("ignored")) return tr("IGNORED");
    return {};
}

void GitStatusController::parsePorcelain(
    const QByteArray& output,
    const QString& repositoryRoot,
    const QString& contextPath,
    QHash<QString, int>* statuses) {
    if (!statuses)
        return;

    const QList<QByteArray> records = output.split('\0');
    const QDir repository(repositoryRoot);
    const QDir context(contextPath);

    for (int i = 0; i < records.size(); ++i) {
        const QByteArray& record = records.at(i);
        if (record.size() < 4 || record.at(2) != ' ')
            continue;

        const char x = record.at(0);
        const char y = record.at(1);
        const int flags = flagsForXY(x, y);
        if (flags == 0)
            continue;

        const QString repositoryRelative = QString::fromUtf8(record.mid(3));
        if (repositoryRelative.isEmpty())
            continue;

        const QString absolute = QDir::cleanPath(repository.absoluteFilePath(repositoryRelative));
        const QString localRelative = context.relativeFilePath(absolute);
        if (localRelative == QStringLiteral("..")
            || localRelative.startsWith(QStringLiteral("../"))) {
            continue;
        }

        const QString directName = localRelative.section(QLatin1Char('/'), 0, 0);
        if (directName.isEmpty() || directName == QStringLiteral("."))
            continue;

        const QString directPath = QDir::cleanPath(context.absoluteFilePath(directName));
        statuses->insert(
            directPath,
            mergeFlags(statuses->value(directPath, 0), flags));

        if (x == 'R' || x == 'C' || y == 'R' || y == 'C')
            ++i;
    }
}

GitStatusController::RefreshResult GitStatusController::collectStatus(
    const QString& path,
    const std::atomic_bool& cancelled,
    const std::atomic_bool& stopping) {
    RefreshResult result;
    result.repository = findRepository(path);
    if (!result.repository.found)
        return result;

    const QString gitExecutable = QStandardPaths::findExecutable(QStringLiteral("git"));
    result.gitAvailable = !gitExecutable.isEmpty();
    if (gitExecutable.isEmpty()) {
        result.error = tr("Git executable not found");
        return result;
    }

    const QString pathspec = QDir(result.repository.rootPath).relativeFilePath(path);

    QProcess process;
    process.setWorkingDirectory(result.repository.rootPath);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(
        gitExecutable,
        {
            QStringLiteral("status"),
            QStringLiteral("--porcelain=v1"),
            QStringLiteral("-z"),
            QStringLiteral("--no-renames"),
            QStringLiteral("--untracked-files=normal"),
            QStringLiteral("--ignored=matching"),
            QStringLiteral("--ignore-submodules=none"),
            QStringLiteral("--"),
            pathspec,
        },
        QIODevice::ReadOnly);

    if (!process.waitForStarted(1000)) {
        result.error = tr("Could not start git status");
        return result;
    }

    QByteArray standardOutput;
    QByteArray standardError;
    QElapsedTimer elapsed;
    elapsed.start();
    bool forcedStop = false;

    while (process.state() != QProcess::NotRunning) {
        if (cancelled.load(std::memory_order_relaxed)
            || stopping.load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(200);
            return result;
        }

        process.waitForReadyRead(40);
        standardOutput += process.readAllStandardOutput();
        standardError += process.readAllStandardError();

        if (standardOutput.size() > MaxStatusOutputBytes) {
            result.truncated = true;
            result.error = tr("Git status output limit reached");
            forcedStop = true;
            process.kill();
            break;
        }
        if (standardError.size() > MaxErrorOutputBytes) {
            standardError.truncate(MaxErrorOutputBytes);
            result.truncated = true;
            result.error = tr("Git status error output limit reached");
            forcedStop = true;
            process.kill();
            break;
        }
        if (elapsed.elapsed() > ProcessTimeoutMs) {
            result.truncated = true;
            result.error = tr("Git status timed out");
            forcedStop = true;
            process.kill();
            break;
        }
    }

    process.waitForFinished(200);
    standardOutput += process.readAllStandardOutput();
    standardError += process.readAllStandardError();

    if (standardOutput.size() > MaxStatusOutputBytes) {
        standardOutput.truncate(MaxStatusOutputBytes);
        result.truncated = true;
    }
    if (result.truncated) {
        const qsizetype lastCompleteRecord = standardOutput.lastIndexOf('\0');
        if (lastCompleteRecord >= 0)
            standardOutput.truncate(lastCompleteRecord + 1);
        else
            standardOutput.clear();
    }

    if (!forcedStop && process.exitStatus() == QProcess::NormalExit && process.exitCode() != 0) {
        const QString message = QString::fromUtf8(standardError.left(MaxErrorOutputBytes)).trimmed();
        result.error = message.isEmpty() ? tr("git status failed") : message;
    }

    parsePorcelain(
        standardOutput,
        result.repository.rootPath,
        path,
        &result.statuses);
    return result;
}

void GitStatusController::startRefresh() {
    if (m_path.isEmpty())
        return;

    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();
    ++m_generation;

    const quint64 generation = m_generation;
    const QString refreshPath = m_path;
    auto token = std::make_shared<std::atomic_bool>(false);
    m_cancelToken = token;
    m_loading = true;
    m_error.clear();
    emit stateChanged();
    pruneFinishedFutures();

    QFuture<void> future = QtConcurrent::run([this, token, generation, refreshPath] {
        RefreshResult result = collectStatus(refreshPath, *token, m_stopping);
        if (token->load(std::memory_order_relaxed)
            || m_stopping.load(std::memory_order_relaxed)) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, generation, refreshPath, result = std::move(result)]() mutable {
                applyResult(generation, refreshPath, std::move(result));
            },
            Qt::QueuedConnection);
    });

    m_futures.push_back(std::move(future));
}

void GitStatusController::applyResult(
    quint64 generation,
    const QString& refreshPath,
    RefreshResult result) {
    if (generation != m_generation || refreshPath != m_path)
        return;

    m_cancelToken.reset();
    m_repository = result.repository.found;
    m_rootPath = result.repository.rootPath;
    m_branchName = result.repository.branchName;
    m_detached = result.repository.detached;
    m_loading = false;
    m_truncated = result.truncated;
    m_gitAvailable = result.gitAvailable;
    m_error = result.error;
    m_statuses = std::move(result.statuses);
    ++m_revision;

    updateWatchPaths(result.repository);
    emit statusChanged();
    emit stateChanged();
}

void GitStatusController::updateWatchPaths(const RepositoryInfo& repository) {
    const QStringList oldFiles = m_gitWatcher.files();
    if (!oldFiles.isEmpty())
        m_gitWatcher.removePaths(oldFiles);
    const QStringList oldDirectories = m_gitWatcher.directories();
    if (!oldDirectories.isEmpty())
        m_gitWatcher.removePaths(oldDirectories);

    if (!repository.found)
        return;

    QStringList directories;
    if (QDir(m_path).exists())
        directories.push_back(m_path);
    if (QDir(repository.gitDir).exists() && repository.gitDir != m_path)
        directories.push_back(repository.gitDir);
    directories.removeDuplicates();
    if (!directories.isEmpty())
        m_gitWatcher.addPaths(directories);

    QStringList files;
    const QString head = QDir(repository.gitDir).filePath(QStringLiteral("HEAD"));
    const QString index = QDir(repository.gitDir).filePath(QStringLiteral("index"));
    const QString marker = QDir(repository.rootPath).filePath(QStringLiteral(".git"));
    if (QFileInfo::exists(head))
        files.push_back(head);
    if (QFileInfo::exists(index))
        files.push_back(index);
    if (QFileInfo(marker).isFile())
        files.push_back(marker);
    files.removeDuplicates();
    if (!files.isEmpty())
        m_gitWatcher.addPaths(files);
}

QString GitStatusController::statusForPath(const QString& path) const {
    if (path.isEmpty())
        return {};
    return codeForFlags(m_statuses.value(QDir::cleanPath(path), 0));
}

QString GitStatusController::statusLabelForPath(const QString& path) const {
    return labelForCode(statusForPath(path));
}
