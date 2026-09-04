// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QVector>

#include <atomic>
#include <memory>

class DeepSearchModel : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY searchChanged)
    Q_PROPERTY(QString query READ query NOTIFY searchChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool cancelled READ cancelled NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(quint64 visitedCount READ visitedCount NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        ParentPathRole,
        RelativePathRole,
        DirectoryRole,
    };
    Q_ENUM(Role)

    explicit DeepSearchModel(QObject* parent = nullptr);
    ~DeepSearchModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const { return m_rootPath; }
    QString query() const { return m_query; }
    bool running() const { return m_running; }
    bool cancelled() const { return m_cancelled; }
    bool truncated() const { return m_truncated; }
    bool active() const { return !m_query.isEmpty(); }
    quint64 visitedCount() const { return m_visitedCount; }
    QString error() const { return m_error; }

    Q_INVOKABLE void start(
        const QString& rootPath,
        const QString& query,
        bool includeHidden = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE QString parentPathAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;

signals:
    void searchChanged();
    void stateChanged();
    void progressChanged();
    void countChanged();

private:
    struct Result {
        QString name;
        QString path;
        QString parentPath;
        QString relativePath;
        bool directory = false;
    };

    static constexpr quint64 MaxVisitedEntries = 200000;
    static constexpr int MaxResults = 2000;
    static constexpr int BatchSize = 64;
    static constexpr quint64 ProgressStride = 512;

    void cancelCurrent(bool markCancelled);
    void resetResults();
    void pruneFinishedFutures();
    void appendBatch(quint64 generation, QVector<Result> batch, quint64 visited);
    void publishProgress(quint64 generation, quint64 visited);
    void finishSearch(
        quint64 generation,
        quint64 visited,
        bool truncated,
        const QString& error = QString());

    QVector<Result> m_results;
    QString m_rootPath;
    QString m_query;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_truncated = false;
    quint64 m_visitedCount = 0;
    QString m_error;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    std::atomic_bool m_stopping = false;
    QVector<QFuture<void>> m_futures;
};
