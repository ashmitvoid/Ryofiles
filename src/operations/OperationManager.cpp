// SPDX-License-Identifier: GPL-3.0-only

#include "OperationManager.hpp"
#include "locations/LocalPathGuard.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QUuid>
#include <QtConcurrent>

#include <unistd.h>

namespace {

QString stateText(OperationManager::OperationState state) {
    switch (state) {
    case OperationManager::Queued: return QStringLiteral("queued");
    case OperationManager::Running: return QStringLiteral("running");
    case OperationManager::WaitingForConflict: return QStringLiteral("conflict");
    case OperationManager::Completed: return QStringLiteral("completed");
    case OperationManager::Failed: return QStringLiteral("failed");
    case OperationManager::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

QString kindText(OperationManager::OperationKind kind) {
    switch (kind) {
    case OperationManager::CopyOperation: return QStringLiteral("copy");
    case OperationManager::MoveOperation: return QStringLiteral("move");
    case OperationManager::RenameOperation: return QStringLiteral("rename");
    case OperationManager::DuplicateOperation: return QStringLiteral("duplicate");
    case OperationManager::CreateFolderOperation: return QStringLiteral("new folder");
    }
    return QStringLiteral("unknown");
}

} // namespace

OperationManager::OperationManager(QObject* parent)
    : QAbstractListModel(parent) {
}

OperationManager::~OperationManager() {
    for (const auto& job : m_jobs) {
        job->cancelRequested.store(true, std::memory_order_relaxed);
        QMutexLocker locker(&job->conflictMutex);
        job->conflictDecision = CancelOperation;
        job->conflictResolved = true;
        job->conflictCondition.wakeAll();
    }

    for (const auto& job : m_jobs) {
        if (job->future.isRunning())
            job->future.waitForFinished();
    }
}

int OperationManager::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_jobs.size();
}

int OperationManager::activeCount() const {
    int countValue = 0;
    for (const auto& job : m_jobs) {
        if (!terminal(job->state))
            ++countValue;
    }
    return countValue;
}

bool OperationManager::terminal(OperationState state) {
    return state == Completed || state == Failed || state == Cancelled;
}

QVariant OperationManager::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};

    const auto& job = m_jobs.at(index.row());
    switch (role) {
    case IdRole:
        return job->id;
    case KindRole:
        return kindText(job->kind);
    case StateRole:
        return stateText(job->state);
    case CurrentSourceRole:
        return job->currentSource;
    case DestinationRole:
        return job->destinationDirectory;
    case ProgressRole:
        return job->totalItems <= 0
            ? 0.0
            : static_cast<double>(job->completedItems) / static_cast<double>(job->totalItems);
    case ErrorRole:
        return job->error;
    case ConflictSourceRole:
        return job->conflictSource;
    case ConflictDestinationRole:
        return job->conflictDestination;
    default:
        return {};
    }
}

QHash<int, QByteArray> OperationManager::roleNames() const {
    return {
        {IdRole, "jobId"},
        {KindRole, "kind"},
        {StateRole, "state"},
        {CurrentSourceRole, "currentSource"},
        {DestinationRole, "destination"},
        {ProgressRole, "progress"},
        {ErrorRole, "errorText"},
        {ConflictSourceRole, "conflictSource"},
        {ConflictDestinationRole, "conflictDestination"},
    };
}

QString OperationManager::copy(const QStringList& sources, const QString& destinationDirectory) {
    return startJob(CopyOperation, sources, destinationDirectory);
}

QString OperationManager::move(const QStringList& sources, const QString& destinationDirectory) {
    return startJob(MoveOperation, sources, destinationDirectory);
}

bool OperationManager::validLeafName(const QString& name) {
    const QString clean = name.trimmed();
    return !clean.isEmpty()
        && !clean.contains(QLatin1Char('/'))
        && clean != QStringLiteral(".")
        && clean != QStringLiteral("..");
}

QString OperationManager::rename(const QString& source, const QString& newName) {
    if (source.trimmed().isEmpty()
        || newName.trimmed().isEmpty()
        || LocalPathGuard::isUriLike(source)) {
        return {};
    }

    const QFileInfo info(source);
    if (!info.exists() && !info.isSymLink())
        return {};

    const QString cleanName = newName.trimmed();
    if (!validLeafName(cleanName))
        return {};

    return startJob(
        RenameOperation,
        {info.absoluteFilePath()},
        info.absolutePath(),
        cleanName);
}

QString OperationManager::duplicate(const QStringList& requestedSources) {
    if (!LocalPathGuard::allLocalPaths(requestedSources))
        return {};

    QStringList sources;
    QString parent;

    for (const QString& source : requestedSources) {
        const QFileInfo info(source);
        if (!info.exists() && !info.isSymLink())
            continue;

        if (parent.isEmpty())
            parent = info.absolutePath();
        else if (parent != info.absolutePath())
            return {};

        sources.push_back(info.absoluteFilePath());
    }

    if (sources.isEmpty() || parent.isEmpty())
        return {};

    return startJob(DuplicateOperation, sources, parent);
}

QString OperationManager::createFolder(
    const QString& parentDirectory,
    const QString& name) {
    if (LocalPathGuard::isUriLike(parentDirectory))
        return {};

    const QString cleanName = name.trimmed();
    if (!validLeafName(cleanName))
        return {};

    return startCreateFolderJob(parentDirectory, cleanName);
}

QString OperationManager::startCreateFolderJob(
    const QString& parentDirectory,
    const QString& name) {
    if (LocalPathGuard::isUriLike(parentDirectory))
        return {};

    const QDir parent(parentDirectory);
    if (!parent.exists())
        return {};

    pruneFinishedJobs();

    auto job = std::make_shared<Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job->kind = CreateFolderOperation;
    job->destinationDirectory = parent.absolutePath();
    job->renameTarget = name;
    job->totalItems = 1;

    const int row = m_jobs.size();
    beginInsertRows({}, row, row);
    m_jobs.push_back(job);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();

    job->future = QtConcurrent::run([this, job] {
        runJob(job);
    });

    return job->id;
}

void OperationManager::pruneFinishedJobs(int keep) {
    int finished = 0;
    for (const auto& job : m_jobs) {
        if (terminal(job->state))
            ++finished;
    }

    for (int row = 0; row < m_jobs.size() && finished > keep;) {
        if (!terminal(m_jobs.at(row)->state)) {
            ++row;
            continue;
        }

        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
        --finished;
        emit countChanged();
    }
}

QString OperationManager::startJob(
    OperationKind kind,
    const QStringList& requestedSources,
    const QString& requestedDestinationDirectory,
    const QString& renameTarget) {
    if (!LocalPathGuard::allLocalPaths(requestedSources)
        || LocalPathGuard::isUriLike(requestedDestinationDirectory)) {
        return {};
    }

    QStringList sources;
    sources.reserve(requestedSources.size());

    for (const QString& source : requestedSources) {
        const QFileInfo info(source);
        if (info.exists() || info.isSymLink())
            sources.push_back(info.absoluteFilePath());
    }

    if (sources.isEmpty())
        return {};

    const QDir destinationDir(requestedDestinationDirectory);
    if (!destinationDir.exists())
        return {};

    pruneFinishedJobs();

    auto job = std::make_shared<Job>();
    job->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    job->kind = kind;
    job->sources = sources;
    job->destinationDirectory = destinationDir.absolutePath();
    job->renameTarget = renameTarget;
    job->totalItems = sources.size();

    const int row = m_jobs.size();
    beginInsertRows({}, row, row);
    m_jobs.push_back(job);
    endInsertRows();
    emit countChanged();
    emit activeCountChanged();

    job->future = QtConcurrent::run([this, job] {
        runJob(job);
    });

    return job->id;
}

std::shared_ptr<OperationManager::Job> OperationManager::findJob(const QString& id) const {
    for (const auto& job : m_jobs) {
        if (job->id == id)
            return job;
    }
    return {};
}

int OperationManager::indexOfJob(const std::shared_ptr<Job>& job) const {
    return static_cast<int>(m_jobs.indexOf(job));
}

void OperationManager::updateJob(
    const std::shared_ptr<Job>& job,
    const std::function<void(Job&)>& update) {
    QMetaObject::invokeMethod(this, [this, job, update] {
        const int row = indexOfJob(job);
        if (row < 0)
            return;

        update(*job);
        emit dataChanged(index(row), index(row));
        emit activeCountChanged();
    }, Qt::QueuedConnection);
}

void OperationManager::finishJob(
    const std::shared_ptr<Job>& job,
    OperationState state,
    const QString& error) {
    QMetaObject::invokeMethod(this, [this, job, state, error] {
        const int row = indexOfJob(job);
        if (row < 0)
            return;

        job->state = state;
        job->error = error;
        job->conflictSource.clear();
        job->conflictDestination.clear();
        emit dataChanged(index(row), index(row));
        emit activeCountChanged();
        emit jobFinished(job->id, state == Completed);
    }, Qt::QueuedConnection);
}

void OperationManager::cancel(const QString& jobId) {
    const auto job = findJob(jobId);
    if (!job)
        return;

    job->cancelRequested.store(true, std::memory_order_relaxed);

    QMutexLocker locker(&job->conflictMutex);
    job->conflictDecision = CancelOperation;
    job->conflictResolved = true;
    job->conflictCondition.wakeAll();
}

QString OperationManager::errorFor(const QString& jobId) const {
    const auto job = findJob(jobId);
    return job ? job->error : QString();
}

void OperationManager::dismiss(const QString& jobId) {
    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row)->id != jobId)
            continue;
        if (!terminal(m_jobs.at(row)->state))
            return;

        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
        emit countChanged();
        return;
    }
}

void OperationManager::clearFinished() {
    for (int row = m_jobs.size() - 1; row >= 0; --row) {
        if (!terminal(m_jobs.at(row)->state))
            continue;

        beginRemoveRows({}, row, row);
        m_jobs.removeAt(row);
        endRemoveRows();
    }
    emit countChanged();
}

void OperationManager::resolveConflict(
    const QString& jobId,
    int decisionValue,
    bool applyToAll) {
    const auto job = findJob(jobId);
    if (!job)
        return;

    if (decisionValue < Skip || decisionValue > CancelOperation)
        return;

    QMutexLocker locker(&job->conflictMutex);
    if (job->state != WaitingForConflict)
        return;

    job->conflictDecision = static_cast<ConflictDecision>(decisionValue);
    if (applyToAll && job->conflictDecision != CancelOperation) {
        job->persistentConflictDecision = true;
        job->persistentDecision = job->conflictDecision;
    }
    job->conflictResolved = true;
    job->conflictCondition.wakeAll();
}

OperationManager::ConflictDecision OperationManager::waitForConflict(
    const std::shared_ptr<Job>& job,
    const QString& source,
    const QString& destination) {
    {
        QMutexLocker locker(&job->conflictMutex);
        if (job->persistentConflictDecision)
            return job->persistentDecision;
    }

    updateJob(job, [source, destination](Job& mutableJob) {
        mutableJob.state = WaitingForConflict;
        mutableJob.conflictSource = source;
        mutableJob.conflictDestination = destination;
    });

    QMetaObject::invokeMethod(this, [this, job, source, destination] {
        emit conflictRaised(job->id, source, destination);
    }, Qt::QueuedConnection);

    QMutexLocker locker(&job->conflictMutex);
    job->conflictResolved = false;

    while (!job->conflictResolved && !job->cancelRequested.load(std::memory_order_relaxed))
        job->conflictCondition.wait(&job->conflictMutex);

    if (job->cancelRequested.load(std::memory_order_relaxed))
        return CancelOperation;

    const ConflictDecision decision = job->conflictDecision;
    job->conflictResolved = false;

    updateJob(job, [](Job& mutableJob) {
        mutableJob.state = Running;
        mutableJob.conflictSource.clear();
        mutableJob.conflictDestination.clear();
    });

    return decision;
}

QString OperationManager::targetPathFor(
    const QString& source,
    const QString& destinationDirectory) {
    return QDir(destinationDirectory).filePath(QFileInfo(source).fileName());
}

bool OperationManager::destinationInsideSource(
    const QString& source,
    const QString& destination) {
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.isDir() || sourceInfo.isSymLink())
        return false;

    const QString sourcePath = QDir::cleanPath(sourceInfo.absoluteFilePath());
    const QString destinationPath = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());

    return destinationPath.startsWith(sourcePath + QDir::separator());
}

QString OperationManager::uniqueSiblingPath(const QString& desiredPath) {
    if (!QFileInfo::exists(desiredPath) && !QFileInfo(desiredPath).isSymLink())
        return desiredPath;

    const QFileInfo info(desiredPath);
    const QDir parent = info.dir();
    const QString name = info.fileName();

    QString stem = name;
    QString extension;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && !info.isDir()) {
        stem = name.left(dot);
        extension = name.mid(dot);
    }

    for (int n = 1; n < 100000; ++n) {
        const QString suffix = n == 1
            ? QStringLiteral(" (copy)")
            : QStringLiteral(" (copy %1)").arg(n);
        const QString candidate = parent.filePath(stem + suffix + extension);
        const QFileInfo candidateInfo(candidate);
        if (!candidateInfo.exists() && !candidateInfo.isSymLink())
            return candidate;
    }

    return {};
}

QString OperationManager::backupSiblingPath(const QString& desiredPath) {
    const QFileInfo info(desiredPath);
    return info.dir().filePath(
        QStringLiteral(".%1.ryofiles-backup-%2")
            .arg(info.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

bool OperationManager::renamePath(const QString& source, const QString& destination) {
    return QDir().rename(source, destination);
}

bool OperationManager::copySymbolicLink(
    const QString& source,
    const QString& destination,
    QString* error) {
    const QByteArray sourceBytes = QFile::encodeName(source);
    QByteArray target(256, '\0');

    while (true) {
        const ssize_t length =
            ::readlink(sourceBytes.constData(), target.data(), static_cast<size_t>(target.size()));

        if (length < 0) {
            if (error)
                *error = QObject::tr("Could not read symbolic link: %1").arg(source);
            return false;
        }

        if (length < target.size()) {
            target.resize(static_cast<qsizetype>(length));
            break;
        }

        if (target.size() >= 1024 * 1024) {
            if (error)
                *error = QObject::tr("Symbolic link target is unexpectedly large: %1").arg(source);
            return false;
        }

        target.resize(target.size() * 2);
    }

    const QByteArray destinationBytes = QFile::encodeName(destination);
    if (::symlink(target.constData(), destinationBytes.constData()) == 0)
        return true;

    if (error)
        *error = QObject::tr("Could not copy symbolic link: %1").arg(source);
    return false;
}

bool OperationManager::removePath(const QString& path, QString* error) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;

    if (info.isSymLink() || info.isFile()) {
        if (QFile::remove(path))
            return true;
        if (error)
            *error = QObject::tr("Could not remove %1").arg(path);
        return false;
    }

    QDir dir(path);
    if (dir.removeRecursively())
        return true;

    if (error)
        *error = QObject::tr("Could not remove directory %1").arg(path);
    return false;
}

bool OperationManager::copyPath(
    const QString& source,
    const QString& destination,
    const std::atomic_bool& cancelRequested,
    QString* error) {
    if (cancelRequested.load(std::memory_order_relaxed)) {
        if (error)
            *error = QObject::tr("Cancelled");
        return false;
    }

    const QFileInfo info(source);
    if (!info.exists() && !info.isSymLink()) {
        if (error)
            *error = QObject::tr("Source disappeared: %1").arg(source);
        return false;
    }

    if (info.isSymLink())
        return copySymbolicLink(source, destination, error);

    if (info.isFile()) {
        if (QFile::copy(source, destination))
            return true;
        if (error)
            *error = QObject::tr("Could not copy %1").arg(source);
        return false;
    }

    if (!info.isDir()) {
        if (error)
            *error = QObject::tr("Unsupported filesystem entry: %1").arg(source);
        return false;
    }

    if (!QDir().mkpath(destination)) {
        if (error)
            *error = QObject::tr("Could not create directory %1").arg(destination);
        return false;
    }

    const QDir sourceDir(source);
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::Name);

    for (const QFileInfo& child : entries) {
        if (cancelRequested.load(std::memory_order_relaxed)) {
            if (error)
                *error = QObject::tr("Cancelled");
            return false;
        }

        const QString childDestination =
            QDir(destination).filePath(child.fileName());
        if (!copyPath(
                child.absoluteFilePath(),
                childDestination,
                cancelRequested,
                error)) {
            return false;
        }
    }

    return true;
}

bool OperationManager::movePath(
    const QString& source,
    const QString& destination,
    const std::atomic_bool& cancelRequested,
    QString* error) {
    if (cancelRequested.load(std::memory_order_relaxed)) {
        if (error)
            *error = QObject::tr("Cancelled");
        return false;
    }

    if (renamePath(source, destination))
        return true;

    if (!copyPath(source, destination, cancelRequested, error)) {
        QString cleanupError;
        removePath(destination, &cleanupError);
        return false;
    }

    if (cancelRequested.load(std::memory_order_relaxed)) {
        QString cleanupError;
        removePath(destination, &cleanupError);
        if (error)
            *error = QObject::tr("Cancelled");
        return false;
    }

    if (!removePath(source, error)) {
        QString cleanupError;
        removePath(destination, &cleanupError);
        return false;
    }

    return true;
}

void OperationManager::runJob(const std::shared_ptr<Job>& job) {
    updateJob(job, [](Job& mutableJob) {
        mutableJob.state = Running;
    });

    if (job->kind == CreateFolderOperation) {
        if (job->cancelRequested.load(std::memory_order_relaxed)) {
            finishJob(job, Cancelled);
            return;
        }

        const QString target =
            QDir(job->destinationDirectory).filePath(job->renameTarget);

        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            finishJob(job, Failed, tr("A file or folder already exists: %1").arg(target));
            return;
        }

        if (!QDir(job->destinationDirectory).mkdir(job->renameTarget)) {
            finishJob(job, Failed, tr("Could not create folder: %1").arg(target));
            return;
        }

        updateJob(job, [](Job& mutableJob) {
            mutableJob.currentSource = mutableJob.renameTarget;
            mutableJob.completedItems = 1;
        });
        finishJob(job, Completed);
        return;
    }

    for (int i = 0; i < job->sources.size(); ++i) {
        if (job->cancelRequested.load(std::memory_order_relaxed)) {
            finishJob(job, Cancelled);
            return;
        }

        const QString source = job->sources.at(i);
        QString target;
        if (job->kind == RenameOperation) {
            target = QDir(job->destinationDirectory).filePath(job->renameTarget);
        } else if (job->kind == DuplicateOperation) {
            target = uniqueSiblingPath(source);
            if (target.isEmpty()) {
                finishJob(job, Failed, tr("Could not generate a unique duplicate name"));
                return;
            }
        } else {
            target = targetPathFor(source, job->destinationDirectory);
        }

        updateJob(job, [source](Job& mutableJob) {
            mutableJob.currentSource = source;
        });

        const bool samePath =
            QFileInfo(source).absoluteFilePath() == QFileInfo(target).absoluteFilePath();

        if (samePath && job->kind != CopyOperation) {
            updateJob(job, [](Job& mutableJob) {
                ++mutableJob.completedItems;
            });
            continue;
        }

        if (destinationInsideSource(source, target)) {
            finishJob(
                job,
                Failed,
                tr("Cannot copy or move a directory into itself: %1").arg(source));
            return;
        }

        bool replaceExisting = false;
        const QFileInfo targetInfo(target);
        if (targetInfo.exists() || targetInfo.isSymLink()) {
            const ConflictDecision decision = waitForConflict(job, source, target);

            if (decision == CancelOperation) {
                finishJob(job, Cancelled);
                return;
            }
            if (decision == Skip) {
                updateJob(job, [](Job& mutableJob) {
                    ++mutableJob.completedItems;
                });
                continue;
            }
            if (decision == KeepBoth) {
                target = uniqueSiblingPath(target);
                if (target.isEmpty()) {
                    finishJob(job, Failed, tr("Could not generate a unique destination name"));
                    return;
                }
            } else if (decision == Replace) {
                replaceExisting = true;
            }
        }

        QString backup;
        if (replaceExisting) {
            backup = backupSiblingPath(target);
            if (!renamePath(target, backup)) {
                finishJob(
                    job,
                    Failed,
                    tr("Could not prepare destination for replacement: %1").arg(target));
                return;
            }
        }

        QString error;
        bool success = false;
        if (job->kind == CopyOperation || job->kind == DuplicateOperation) {
            success = copyPath(source, target, job->cancelRequested, &error);
            if (!success) {
                QString cleanupError;
                removePath(target, &cleanupError);
            }
        } else {
            success = movePath(source, target, job->cancelRequested, &error);
        }

        if (!success) {
            if (!backup.isEmpty()) {
                QString cleanupError;
                removePath(target, &cleanupError);
                if (!renamePath(backup, target) && error.isEmpty())
                    error = tr("Operation failed and destination rollback also failed");
            }

            finishJob(
                job,
                job->cancelRequested.load(std::memory_order_relaxed) ? Cancelled : Failed,
                error);
            return;
        }

        if (!backup.isEmpty()) {
            QString cleanupError;
            if (!removePath(backup, &cleanupError)) {
                finishJob(
                    job,
                    Failed,
                    tr("Operation completed but old destination cleanup failed: %1").arg(backup));
                return;
            }
        }

        updateJob(job, [](Job& mutableJob) {
            ++mutableJob.completedItems;
        });
    }

    finishJob(job, Completed);
}
