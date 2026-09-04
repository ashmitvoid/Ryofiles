// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <glib.h>

#include <QAbstractListModel>
#include <QFuture>
#include <QMutex>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QWaitCondition>

#include <atomic>
#include <memory>

typedef struct _GAsyncResult GAsyncResult;
typedef struct _GCancellable GCancellable;
typedef struct _GFile GFile;
typedef struct _GObject GObject;

struct RemoteOperationPlan {
    bool valid = false;
    bool involvesNetwork = false;
    QString source;
    QString destinationDirectory;
    QString name;
    QString error;
};

class RemoteOperationManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)

public:
    enum OperationKind {
        CopyOperation = 0,
        MoveOperation,
        RenameOperation,
        CreateFolderOperation,
        TrashOperation,
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

    explicit RemoteOperationManager(QObject* parent = nullptr);
    ~RemoteOperationManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    int activeCount() const;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString copyFile(
        const QString& source,
        const QString& destinationDirectory);
    Q_INVOKABLE QString moveFile(
        const QString& source,
        const QString& destinationDirectory);
    Q_INVOKABLE QString rename(const QString& source, const QString& newName);
    Q_INVOKABLE QString createFolder(const QString& parentDirectory, const QString& name);
    Q_INVOKABLE QString trash(const QString& source);

    Q_INVOKABLE void cancel(const QString& jobId);
    Q_INVOKABLE void resolveConflict(const QString& jobId, int decision, bool applyToAll = false);
    Q_INVOKABLE void dismiss(const QString& jobId);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QString errorFor(const QString& jobId) const;

    static RemoteOperationPlan planTransfer(
        const QString& source,
        const QString& destinationDirectory);
    static RemoteOperationPlan planRemoteItem(const QString& source);
    static RemoteOperationPlan planCreateFolder(
        const QString& parentDirectory,
        const QString& name);
    static bool validLeafName(const QString& name);
    static QString keepBothName(const QString& originalName, int attempt);
    static bool nonDestructiveConflictDecision(int decision);

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
        QString source;
        QString destinationDirectory;
        QString name;
        QString currentSource;
        QString error;
        QString conflictSource;
        QString conflictDestination;
        double progress = 0.0;
        bool persistentConflictDecision = false;
        ConflictDecision persistentDecision = Skip;

        QMutex conflictMutex;
        QWaitCondition conflictCondition;
        bool conflictResolved = false;
        ConflictDecision conflictDecision = Skip;
    };

    struct ActiveContext {
        QPointer<RemoteOperationManager> owner;
        std::shared_ptr<Job> job;
        GFile* sourceFile = nullptr;
        GFile* destinationFile = nullptr;
        GCancellable* cancellable = nullptr;
        QString sourceDisplayName;
        int keepBothAttempt = 0;
        bool keepBothMode = false;
        bool overwrite = false;
        bool treeMode = false;

        ~ActiveContext();
    };

    static constexpr int MaxTreeDepth = 256;
    static constexpr int MaxKeepBothAttempts = 1000;

    static bool terminal(OperationState state);
    static bool validConflictDecision(int decision);
    static QString normalizedLocation(const QString& input, bool* network, QString* error);
    static GFile* fileForLocation(const QString& location);
    static QString locationForFile(GFile* file);
    static QString gioErrorText(const GError* error, const QString& fallback);

    QString enqueue(
        OperationKind kind,
        const RemoteOperationPlan& plan,
        const QString& name = QString());
    std::shared_ptr<Job> findJob(const QString& id) const;
    int indexOfJob(const std::shared_ptr<Job>& job) const;
    void pruneFinished(int keep = 32);
    void startNext();
    void startActive(ActiveContext* context);
    void startTransferTypeQuery(ActiveContext* context);
    bool prepareTransferDestination(ActiveContext* context, const QString& displayName);
    void startTransfer(ActiveContext* context);
    void startDirectoryTransfer(ActiveContext* context);
    void handleTransferExists(ActiveContext* context);
    void raiseConflict(ActiveContext* context);
    void clearConflict(const std::shared_ptr<Job>& job);
    void finishActive(
        ActiveContext* context,
        OperationState state,
        const QString& error = QString());
    void setProgress(ActiveContext* context, double progress);

    ConflictDecision waitForTreeConflict(
        ActiveContext* context,
        GFile* source,
        GFile* destination);
    void resumeTreeJobAfterConflict(const std::shared_ptr<Job>& job);
    void updateTreeCurrentSource(const std::shared_ptr<Job>& job, GFile* source);
    bool copyDirectoryTree(
        ActiveContext* context,
        GFile* source,
        GFile* desiredDestination,
        const QString& displayName,
        int depth,
        bool* skipped,
        QString* error);
    bool copyTreeEntry(
        ActiveContext* context,
        GFile* source,
        GFile* desiredDestination,
        const QString& displayName,
        int depth,
        bool* skipped,
        QString* error);
    bool copyLeafWithConflicts(
        ActiveContext* context,
        GFile* source,
        GFile* desiredDestination,
        const QString& displayName,
        bool* skipped,
        QString* error);
    GFile* createDirectoryWithConflicts(
        ActiveContext* context,
        GFile* source,
        GFile* desiredDestination,
        const QString& displayName,
        bool* skipped,
        QString* error);
    bool deleteTree(
        ActiveContext* context,
        GFile* root,
        int depth,
        QString* error);
    GFile* keepBothSibling(
        GFile* desiredDestination,
        const QString& displayName,
        int attempt,
        QString* error) const;

    static void transferTypeReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void transferProgress(goffset currentBytes, goffset totalBytes, gpointer userData);
    static void copyReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void moveReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void renameReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void createFolderReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void trashReady(GObject* source, GAsyncResult* result, gpointer userData);

    QVector<std::shared_ptr<Job>> m_jobs;
    ActiveContext* m_active = nullptr;
    QFuture<void> m_treeFuture;
    std::atomic_bool m_stopping = false;
};
