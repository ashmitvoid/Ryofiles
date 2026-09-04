// SPDX-License-Identifier: GPL-3.0-only

#include "DeepSearchModel.hpp"
#include "locations/LocalPathGuard.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QtConcurrent>

DeepSearchModel::DeepSearchModel(QObject* parent)
    : QAbstractListModel(parent) {
}

DeepSearchModel::~DeepSearchModel() {
    m_stopping.store(true, std::memory_order_relaxed);
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);

    for (QFuture<void>& future : m_futures)
        future.waitForFinished();
}

int DeepSearchModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_results.size();
}

QVariant DeepSearchModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
        return {};

    const Result& result = m_results.at(index.row());
    switch (role) {
    case NameRole: return result.name;
    case PathRole: return result.path;
    case ParentPathRole: return result.parentPath;
    case RelativePathRole: return result.relativePath;
    case DirectoryRole: return result.directory;
    default: return {};
    }
}

QHash<int, QByteArray> DeepSearchModel::roleNames() const {
    return {
        {NameRole, "name"},
        {PathRole, "filePath"},
        {ParentPathRole, "parentPath"},
        {RelativePathRole, "relativePath"},
        {DirectoryRole, "isDir"},
    };
}

QString DeepSearchModel::pathAt(int index) const {
    if (index < 0 || index >= m_results.size())
        return {};
    return m_results.at(index).path;
}

QString DeepSearchModel::parentPathAt(int index) const {
    if (index < 0 || index >= m_results.size())
        return {};
    return m_results.at(index).parentPath;
}

bool DeepSearchModel::isDirectoryAt(int index) const {
    if (index < 0 || index >= m_results.size())
        return false;
    return m_results.at(index).directory;
}

void DeepSearchModel::pruneFinishedFutures() {
    for (int i = m_futures.size() - 1; i >= 0; --i) {
        if (m_futures.at(i).isFinished())
            m_futures.removeAt(i);
    }
}

void DeepSearchModel::resetResults() {
    if (m_results.isEmpty())
        return;

    beginResetModel();
    m_results.clear();
    endResetModel();
    emit countChanged();
}

void DeepSearchModel::cancelCurrent(bool markCancelled) {
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();

    ++m_generation;
    const bool stateChangedNeeded = m_running || (markCancelled && !m_cancelled);
    m_running = false;
    if (markCancelled)
        m_cancelled = true;

    if (stateChangedNeeded)
        emit stateChanged();
}

void DeepSearchModel::cancel() {
    if (!m_running)
        return;
    cancelCurrent(true);
}

void DeepSearchModel::clear() {
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();
    ++m_generation;

    const bool hadSearch = !m_rootPath.isEmpty() || !m_query.isEmpty();
    const bool hadState = m_running || m_cancelled || m_truncated || !m_error.isEmpty();
    const bool hadProgress = m_visitedCount != 0;

    m_running = false;
    m_cancelled = false;
    m_truncated = false;
    m_visitedCount = 0;
    m_error.clear();
    m_rootPath.clear();
    m_query.clear();
    resetResults();

    if (hadSearch)
        emit searchChanged();
    if (hadProgress)
        emit progressChanged();
    if (hadState)
        emit stateChanged();
}

void DeepSearchModel::appendBatch(
    quint64 generation,
    QVector<Result> batch,
    quint64 visited) {
    if (generation != m_generation || batch.isEmpty())
        return;

    const int first = m_results.size();
    const int last = first + batch.size() - 1;
    beginInsertRows({}, first, last);
    m_results.reserve(m_results.size() + batch.size());
    for (Result& result : batch)
        m_results.push_back(std::move(result));
    endInsertRows();
    emit countChanged();

    if (visited > m_visitedCount) {
        m_visitedCount = visited;
        emit progressChanged();
    }
}

void DeepSearchModel::publishProgress(quint64 generation, quint64 visited) {
    if (generation != m_generation || visited <= m_visitedCount)
        return;
    m_visitedCount = visited;
    emit progressChanged();
}

void DeepSearchModel::finishSearch(
    quint64 generation,
    quint64 visited,
    bool truncated,
    const QString& error) {
    if (generation != m_generation)
        return;

    m_cancelToken.reset();
    m_running = false;
    m_cancelled = false;
    m_truncated = truncated;
    m_error = error;

    if (visited != m_visitedCount) {
        m_visitedCount = visited;
        emit progressChanged();
    }
    emit stateChanged();
}

void DeepSearchModel::start(
    const QString& requestedRoot,
    const QString& requestedQuery,
    bool includeHidden) {
    const QString cleanQuery = requestedQuery.trimmed();

    if (cleanQuery.isEmpty()) {
        clear();
        return;
    }

    if (LocalPathGuard::isUriLike(requestedRoot)) {
        clear();
        m_rootPath = requestedRoot.trimmed();
        m_query = cleanQuery;
        m_error = tr("Remote URI is not supported by local deep search: %1").arg(m_rootPath);
        emit searchChanged();
        emit stateChanged();
        return;
    }

    const QDir rootDirectory(QDir::cleanPath(requestedRoot));
    const QString cleanRoot = rootDirectory.absolutePath();

    if (!rootDirectory.exists()) {
        clear();
        m_rootPath = cleanRoot;
        m_query = cleanQuery;
        m_error = tr("Search root does not exist: %1").arg(cleanRoot);
        emit searchChanged();
        emit stateChanged();
        return;
    }

    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
    m_cancelToken.reset();
    ++m_generation;
    const quint64 generation = m_generation;

    m_rootPath = cleanRoot;
    m_query = cleanQuery;
    m_running = true;
    m_cancelled = false;
    m_truncated = false;
    m_visitedCount = 0;
    m_error.clear();
    resetResults();
    emit searchChanged();
    emit progressChanged();
    emit stateChanged();

    auto token = std::make_shared<std::atomic_bool>(false);
    m_cancelToken = token;
    pruneFinishedFutures();

    QFuture<void> future = QtConcurrent::run([
        this,
        token,
        generation,
        cleanRoot,
        cleanQuery,
        includeHidden] {
        QStringList pendingDirectories;
        pendingDirectories.push_back(cleanRoot);

        quint64 visited = 0;
        quint64 lastProgress = 0;
        int found = 0;
        bool truncated = false;
        QVector<Result> batch;
        batch.reserve(BatchSize);

        auto cancelled = [&] {
            return m_stopping.load(std::memory_order_relaxed)
                || token->load(std::memory_order_relaxed);
        };

        auto postProgress = [&](quint64 value) {
            QMetaObject::invokeMethod(
                this,
                [this, generation, value] {
                    publishProgress(generation, value);
                },
                Qt::QueuedConnection);
        };

        auto flushBatch = [&] {
            if (batch.isEmpty())
                return;

            QVector<Result> outgoing;
            outgoing.swap(batch);
            batch.reserve(BatchSize);
            const quint64 batchVisited = visited;
            QMetaObject::invokeMethod(
                this,
                [this, generation, outgoing = std::move(outgoing), batchVisited]() mutable {
                    appendBatch(generation, std::move(outgoing), batchVisited);
                },
                Qt::QueuedConnection);
        };

        QDir::Filters filters =
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (includeHidden)
            filters |= QDir::Hidden;

        while (!pendingDirectories.isEmpty() && !cancelled() && !truncated) {
            const QString directoryPath = pendingDirectories.takeLast();
            QDirIterator iterator(directoryPath, filters, QDirIterator::NoIteratorFlags);

            while (iterator.hasNext() && !cancelled()) {
                iterator.next();
                const QFileInfo info = iterator.fileInfo();
                ++visited;

                if (info.fileName().contains(cleanQuery, Qt::CaseInsensitive)) {
                    Result result;
                    result.name = info.fileName();
                    result.path = info.absoluteFilePath();
                    result.parentPath = info.absolutePath();
                    result.relativePath = QDir(cleanRoot).relativeFilePath(result.path);
                    result.directory = info.isDir();
                    batch.push_back(std::move(result));
                    ++found;

                    if (batch.size() >= BatchSize)
                        flushBatch();

                    if (found >= MaxResults) {
                        truncated = true;
                        break;
                    }
                }

                if (info.isDir() && !info.isSymLink())
                    pendingDirectories.push_back(info.absoluteFilePath());

                if (visited >= MaxVisitedEntries) {
                    truncated = true;
                    break;
                }

                if (visited - lastProgress >= ProgressStride) {
                    lastProgress = visited;
                    postProgress(visited);
                }
            }
        }

        if (cancelled())
            return;

        flushBatch();
        QMetaObject::invokeMethod(
            this,
            [this, generation, visited, truncated] {
                finishSearch(generation, visited, truncated);
            },
            Qt::QueuedConnection);
    });

    m_futures.push_back(std::move(future));
}
