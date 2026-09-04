// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFileSystemWatcher>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <atomic>
#include <memory>

class GitStatusController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool repository READ repository NOTIFY stateChanged)
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY stateChanged)
    Q_PROPERTY(QString branchName READ branchName NOTIFY stateChanged)
    Q_PROPERTY(bool detached READ detached NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool gitAvailable READ gitAvailable NOTIFY stateChanged)
    Q_PROPERTY(int changedCount READ changedCount NOTIFY statusChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    explicit GitStatusController(QObject* parent = nullptr);
    ~GitStatusController() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool repository() const { return m_repository; }
    QString rootPath() const { return m_rootPath; }
    QString branchName() const { return m_branchName; }
    bool detached() const { return m_detached; }
    bool loading() const { return m_loading; }
    bool truncated() const { return m_truncated; }
    bool gitAvailable() const { return m_gitAvailable; }
    int changedCount() const { return m_statuses.size(); }
    quint64 revision() const { return m_revision; }
    QString error() const { return m_error; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString statusForPath(const QString& path) const;
    Q_INVOKABLE QString statusLabelForPath(const QString& path) const;

signals:
    void pathChanged();
    void stateChanged();
    void statusChanged();

private:
    enum StatusFlag {
        StagedFlag = 1 << 0,
        ModifiedFlag = 1 << 1,
        UntrackedFlag = 1 << 2,
        IgnoredFlag = 1 << 3,
        ConflictFlag = 1 << 4,
    };

    struct RepositoryInfo {
        bool found = false;
        QString rootPath;
        QString gitDir;
        QString branchName;
        bool detached = false;
    };

    struct RefreshResult {
        RepositoryInfo repository;
        bool gitAvailable = false;
        bool truncated = false;
        QHash<QString, int> statuses;
        QString error;
    };

    static constexpr qsizetype MaxStatusOutputBytes = 2 * 1024 * 1024;
    static constexpr qsizetype MaxErrorOutputBytes = 64 * 1024;
    static constexpr int ProcessTimeoutMs = 4000;

    static RepositoryInfo findRepository(const QString& path);
    static QString readBranch(const QString& gitDir, bool* detached);
    static RefreshResult collectStatus(
        const QString& path,
        const std::atomic_bool& cancelled,
        const std::atomic_bool& stopping);
    static void parsePorcelain(
        const QByteArray& output,
        const QString& repositoryRoot,
        const QString& contextPath,
        QHash<QString, int>* statuses);
    static int flagsForXY(char x, char y);
    static int mergeFlags(int current, int incoming);
    static QString codeForFlags(int flags);
    static QString labelForCode(const QString& code);

    void scheduleRefresh(int delayMs = 140);
    void startRefresh();
    void applyResult(quint64 generation, const QString& path, RefreshResult result);
    void clearVisibleState();
    void updateWatchPaths(const RepositoryInfo& repository);
    void pruneFinishedFutures();

    QString m_path;
    bool m_repository = false;
    QString m_rootPath;
    QString m_branchName;
    bool m_detached = false;
    bool m_loading = false;
    bool m_truncated = false;
    bool m_gitAvailable = false;
    QString m_error;
    QHash<QString, int> m_statuses;
    quint64 m_revision = 0;
    quint64 m_generation = 0;

    QTimer m_refreshDebounce;
    QFileSystemWatcher m_gitWatcher;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    std::atomic_bool m_stopping = false;
    QVector<QFuture<void>> m_futures;
};
