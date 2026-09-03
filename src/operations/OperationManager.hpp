// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QMutex>
#include <QStringList>
#include <QVector>
#include <QWaitCondition>

#include <atomic>
#include <functional>
#include <memory>

class OperationManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)

public:
    enum OperationKind {
        CopyOperation = 0,
        MoveOperation,
        RenameOperation,
        DuplicateOperation,
        CreateFolderOperation,
    };
    Q_ENUM(OperationKind)

    enum OperationState {
        Queued = 0,
        Running,
        WaitingForConflict,
        Completed,
        Failed,
        Cancelled,
    };
    Q_ENUM(OperationState)

    enum ConflictDecision {
        Skip = 0,
        KeepBoth,
        Replace,
        CancelOperation,
    };
    Q_ENUM(ConflictDecision)

    enum Role {
        IdRole = Qt::UserRole + 1,
        KindRole,
        StateRole,
        CurrentSourceRole,
        DestinationRole,
        ProgressRole,
        ErrorRole,
        ConflictSourceRole,
        ConflictDestinationRole,
    };
    Q_ENUM(Role)

    explicit OperationManager(QObject* parent = nullptr);
    ~OperationManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    int activeCount() const;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString copy(const QStringList& sources, const QString& destinationDirectory);
    Q_INVOKABLE QString move(const QStringList& sources, const QString& destinationDirectory);
    Q_INVOKABLE QString rename(const QString& source, const QString& newName);
    Q_INVOKABLE QString duplicate(const QStringList& sources);
    Q_INVOKABLE QString createFolder(const QString& parentDirectory, const QString& name);

    Q_INVOKABLE void cancel(const QString& jobId);
    Q_INVOKABLE void resolveConflict(const QString& jobId, int decision, bool applyToAll = false);
    Q_INVOKABLE void dismiss(const QString& jobId);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QString errorFor(const QString& jobId) const;

signals:
    void countChanged();
    void activeCountChanged();
    void conflictRaised(const QString& jobId, const QString& source, const QString& destination);
    void jobFinished(const QString& jobId, bool success);

private:
    struct Job {
        QString id;
        OperationKind kind = CopyOperation;
        OperationState state = Queued;
        QStringList sources;
        QString destinationDirectory;
        QString renameTarget;

        QString currentSource;
        QString error;
        QString conflictSource;
        QString conflictDestination;
        int completedItems = 0;
        int totalItems = 0;

        std::atomic_bool cancelRequested = false;
        QFuture<void> future;

        QMutex conflictMutex;
        QWaitCondition conflictCondition;
        bool conflictResolved = false;
        ConflictDecision conflictDecision = Skip;
        bool persistentConflictDecision = false;
        ConflictDecision persistentDecision = Skip;
    };

    QString startJob(
        OperationKind kind,
        const QStringList& sources,
        const QString& destinationDirectory,
        const QString& renameTarget = QString());
    QString startCreateFolderJob(const QString& parentDirectory, const QString& name);

    std::shared_ptr<Job> findJob(const QString& id) const;
    static bool terminal(OperationState state);
    void pruneFinishedJobs(int keep = 32);
    int indexOfJob(const std::shared_ptr<Job>& job) const;

    void runJob(const std::shared_ptr<Job>& job);
    void updateJob(const std::shared_ptr<Job>& job, const std::function<void(Job&)>& update);
    void finishJob(const std::shared_ptr<Job>& job, OperationState state, const QString& error = QString());

    ConflictDecision waitForConflict(
        const std::shared_ptr<Job>& job,
        const QString& source,
        const QString& destination);

    static bool copyPath(
        const QString& source,
        const QString& destination,
        const std::atomic_bool& cancelRequested,
        QString* error);
    static bool movePath(
        const QString& source,
        const QString& destination,
        const std::atomic_bool& cancelRequested,
        QString* error);
    static bool removePath(const QString& path, QString* error);
    static bool renamePath(const QString& source, const QString& destination);
    static bool copySymbolicLink(const QString& source, const QString& destination, QString* error);
    static bool validLeafName(const QString& name);
    static QString uniqueSiblingPath(const QString& desiredPath);
    static QString backupSiblingPath(const QString& desiredPath);
    static QString targetPathFor(const QString& source, const QString& destinationDirectory);
    static bool destinationInsideSource(const QString& source, const QString& destination);

    QVector<std::shared_ptr<Job>> m_jobs;
};
