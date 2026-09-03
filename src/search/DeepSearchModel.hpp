// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QThreadPool>
#include <QVector>

#include <atomic>
#include <memory>

class DeepSearchModel final : public QAbstractListModel {
    Q_OBJECT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY searchChanged)
    Q_PROPERTY(QString query READ query NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY truncatedChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
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
    bool searching() const { return m_searching; }
    bool truncated() const { return m_truncated; }

    Q_INVOKABLE void start(
        const QString& rootPath,
        const QString& query,
        bool showHidden = false);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    Q_INVOKABLE QString pathAt(int index) const;
    Q_INVOKABLE bool isDirectoryAt(int index) const;

signals:
    void searchChanged();
    void searchingChanged();
    void truncatedChanged();
    void countChanged();
    void errorOccurred(const QString& message);

private:
    struct Result {
        QString name;
        QString path;
        QString relativePath;
        bool directory = false;
    };

    struct Job {
        quint64 generation = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        QFuture<void> future;
    };

    static constexpr int kBatchSize = 32;
    static constexpr int kMaxResults = 2000;

    void setSearching(bool searching);
    void setTruncated(bool truncated);
    void pruneJobs();
    void appendBatch(quint64 generation, QVector<Result> batch);
    void finishGeneration(quint64 generation, bool truncated, const QString& error);

    QVector<Result> m_results;
    QVector<std::shared_ptr<Job>> m_jobs;
    std::shared_ptr<std::atomic_bool> m_currentCancellation;
    QThreadPool m_pool;

    QString m_rootPath;
    QString m_query;
    bool m_searching = false;
    bool m_truncated = false;
    std::atomic_bool m_stopping = false;
    quint64 m_generation = 0;
};
