// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <glib.h>

#include <QAbstractListModel>
#include <QPointer>
#include <QString>
#include <QVector>

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
        Completed,
        Failed,
        Cancelled,
    };
    Q_ENUM(OperationState)

    enum Role {
        IdRole = Qt::UserRole + 1,
        KindRole,
        StateRole,
        CurrentSourceRole,
        DestinationRole,
        ProgressRole,
        ErrorRole,
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

signals:
    void countChanged();
    void activeCountChanged();
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
        double progress = 0.0;
    };

    struct ActiveContext {
        QPointer<RemoteOperationManager> owner;
        std::shared_ptr<Job> job;
        GFile* sourceFile = nullptr;
        GFile* destinationFile = nullptr;
        GCancellable* cancellable = nullptr;

        ~ActiveContext();
    };

    static bool terminal(OperationState state);
    static QString normalizedLocation(const QString& input, bool* network, QString* error);
    static GFile* fileForLocation(const QString& location);
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
    void startTransfer(ActiveContext* context);
    void finishActive(
        ActiveContext* context,
        OperationState state,
        const QString& error = QString());
    void setProgress(ActiveContext* context, double progress);

    static void transferTypeReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void transferProgress(goffset currentBytes, goffset totalBytes, gpointer userData);
    static void copyReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void moveReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void renameReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void createFolderReady(GObject* source, GAsyncResult* result, gpointer userData);
    static void trashReady(GObject* source, GAsyncResult* result, gpointer userData);

    QVector<std::shared_ptr<Job>> m_jobs;
    ActiveContext* m_active = nullptr;
};
