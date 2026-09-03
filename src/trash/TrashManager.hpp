// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFileDevice>
#include <QFileSystemWatcher>
#include <QFuture>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <functional>

class TrashManager : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        OriginalPathRole,
        TrashedPathRole,
        DeletionDateRole,
        TrashRootRole,
        OrphanedRole,
    };
    Q_ENUM(Role)

    enum RestoreDecision {
        RestoreSkip = 0,
        RestoreKeepBoth,
        RestoreReplace,
    };
    Q_ENUM(RestoreDecision)

    explicit TrashManager(QObject* parent = nullptr);
    ~TrashManager() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const { return m_busy; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString trash(const QStringList& paths);
    Q_INVOKABLE QString restore(const QString& itemId, int decision = RestoreSkip);

signals:
    void countChanged();
    void busyChanged();
    void operationFinished(const QString& operationId, bool success, const QString& error);
    void restoreConflict(const QString& itemId, const QString& originalPath);

private:
    struct Entry {
        QString id;
        QString name;
        QString originalPath;
        QString trashedPath;
        QString infoPath;
        QString deletionDate;
        QString trashRoot;
        QString relativeBase;
        bool orphaned = false;
    };

    struct TrashLocation {
        QString root;
        QString relativeBase;
        bool home = false;
    };

    struct TrashResult {
        bool success = false;
        QString error;
    };

    static QString dataHome();
    static QString homeTrashRoot();
    static QString percentEncodePath(const QString& path);
    static QString percentDecodePath(const QString& value);
    static QString uniqueName(const QString& filesDir, const QString& preferred);
    static QString uniqueSiblingPath(const QString& desiredPath);
    static QString backupSiblingPath(const QString& desiredPath);

    static bool ensureDirectory(const QString& path, QFileDevice::Permissions permissions);
    static bool ensureTrashLayout(const QString& root, QString* error);
    static bool isSecureSharedTrash(const QString& path);
    static TrashLocation locationForSource(const QString& source, QString* error);
    static QList<TrashLocation> discoverTrashLocations();

    static bool createTrashInfo(
        const TrashLocation& location,
        const QString& source,
        QString* itemName,
        QString* infoPath,
        QString* error);
    static bool moveToTrash(
        const TrashLocation& location,
        const QString& source,
        QString* error);
    static bool parseTrashInfo(
        const QString& infoPath,
        const TrashLocation& location,
        QString* originalPath,
        QString* deletionDate);
    static QVector<Entry> scanTrash(QString* error);

    static bool pathExists(const QString& path);
    static bool movePath(const QString& source, const QString& destination);
    static bool removePath(const QString& path);
    static TrashResult restoreEntry(const Entry& entry, RestoreDecision decision);

    void setBusy(bool busy);
    void pruneFutures();
    void rebuildWatches();
    void startOperation(const QString& operationId, const std::function<TrashResult()>& work);

    QVector<Entry> m_entries;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
    bool m_busy = false;
    std::atomic_bool m_stopping = false;
    std::atomic<quint64> m_refreshGeneration = 0;
    QVector<QFuture<void>> m_futures;
};
