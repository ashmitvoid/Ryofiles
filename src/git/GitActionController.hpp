// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFuture>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>

class GitActionController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString diffText READ diffText NOTIFY diffChanged)
    Q_PROPERTY(QString diffPath READ diffPath NOTIFY diffChanged)
    Q_PROPERTY(bool diffStaged READ diffStaged NOTIFY diffChanged)
    Q_PROPERTY(bool diffTruncated READ diffTruncated NOTIFY diffChanged)

public:
    explicit GitActionController(QObject* parent = nullptr);
    ~GitActionController() override;

    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString diffText() const { return m_diffText; }
    QString diffPath() const { return m_diffPath; }
    bool diffStaged() const { return m_diffStaged; }
    bool diffTruncated() const { return m_diffTruncated; }

    Q_INVOKABLE QString stage(const QString& repositoryRoot, const QStringList& paths);
    Q_INVOKABLE QString unstage(const QString& repositoryRoot, const QStringList& paths);
    Q_INVOKABLE QString requestDiff(const QString& repositoryRoot, const QString& path, bool staged);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool openTerminal(const QString& path) const;

signals:
    void stateChanged();
    void diffChanged();
    void operationFinished(const QString& operationId, bool success, const QString& error);

private:
    enum class Kind {
        Stage,
        Unstage,
        Diff,
    };

    struct ProcessResult {
        bool success = false;
        bool cancelled = false;
        bool truncated = false;
        QByteArray standardOutput;
        QByteArray standardError;
        QString error;
    };

    struct Request {
        QString id;
        Kind kind = Kind::Stage;
        QString repositoryRoot;
        QStringList relativePaths;
        bool stagedDiff = false;
    };

    static constexpr qsizetype MaxDiffBytes = 512 * 1024;
    static constexpr qsizetype MaxCommandOutputBytes = 128 * 1024;
    static constexpr qsizetype MaxErrorBytes = 64 * 1024;
    static constexpr int ProcessTimeoutMs = 5000;

    static bool validatedRelativePaths(
        const QString& repositoryRoot,
        const QStringList& requestedPaths,
        QStringList* relativePaths,
        QString* error);
    static ProcessResult runProcess(
        const QString& repositoryRoot,
        const QStringList& arguments,
        qsizetype outputLimit,
        const std::atomic_bool& cancelled,
        const std::atomic_bool& stopping,
        bool truncateIsSuccess = false);
    static bool hasHead(
        const QString& repositoryRoot,
        const std::atomic_bool& cancelled,
        const std::atomic_bool& stopping);

    QString startMutation(Kind kind, const QString& repositoryRoot, const QStringList& paths);
    QString startRequest(Request request);
    void runRequest(Request request, const std::shared_ptr<std::atomic_bool>& token, quint64 generation);
    void applyResult(
        const Request& request,
        quint64 generation,
        ProcessResult result);
    void setImmediateError(const QString& message);

    bool m_busy = false;
    QString m_error;
    QString m_diffText;
    QString m_diffPath;
    bool m_diffStaged = false;
    bool m_diffTruncated = false;

    quint64 m_generation = 0;
    std::atomic_bool m_stopping = false;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    QFuture<void> m_future;
};
