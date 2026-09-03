// SPDX-License-Identifier: GPL-3.0-only

#include "ThumbnailProvider.hpp"

#include "ThumbnailStore.hpp"

#include <QMetaObject>
#include <QThread>
#include <QUrlQuery>

#include <algorithm>

ThumbnailScheduler::ThumbnailScheduler() {
    const int ideal = QThread::idealThreadCount();
    const int workers = std::clamp(ideal > 0 ? ideal / 2 : 2, 2, 4);
    m_pool.setMaxThreadCount(workers);
    m_pool.setExpiryTimeout(15000);
}

ThumbnailScheduler::~ThumbnailScheduler() {
    m_pool.waitForDone();
}

void ThumbnailScheduler::enqueue(ThumbnailResponse* response, int priority) {
    ThumbnailResponse* evicted = nullptr;

    {
        QMutexLocker locker(&m_mutex);

        while (static_cast<qsizetype>(m_queued.size()) >= kMaxQueued) {
            ThumbnailResponse* candidate = m_queued.front();
            m_queued.pop_front();

            if (m_pool.tryTake(candidate)) {
                evicted = candidate;
                break;
            }
        }

        m_queued.push_back(response);
        m_pool.start(response, priority);
    }

    if (evicted)
        evicted->finishCancelled();
}

void ThumbnailScheduler::markStarted(ThumbnailResponse* response) {
    QMutexLocker locker(&m_mutex);
    const auto it = std::find(m_queued.begin(), m_queued.end(), response);
    if (it != m_queued.end())
        m_queued.erase(it);
}

bool ThumbnailScheduler::cancelQueued(ThumbnailResponse* response) {
    QMutexLocker locker(&m_mutex);

    const auto it = std::find(m_queued.begin(), m_queued.end(), response);
    if (it == m_queued.end())
        return false;

    if (!m_pool.tryTake(response))
        return false;

    m_queued.erase(it);
    return true;
}

ThumbnailResponse::ThumbnailResponse(
    ThumbnailScheduler* scheduler,
    QString path,
    QSize requestedSize,
    int priority)
    : m_scheduler(scheduler)
    , m_path(std::move(path))
    , m_requestedSize(requestedSize)
    , m_priority(priority) {
    setAutoDelete(false);
}

void ThumbnailResponse::run() {
    if (m_scheduler)
        m_scheduler->markStarted(this);

    if (!m_cancelled.load(std::memory_order_relaxed)) {
        int target = std::max(m_requestedSize.width(), m_requestedSize.height());
        if (target <= 0)
            target = 256;

        QString error;
        m_image = ThumbnailStore::load(m_path, target, m_cancelled, &error);
    }

    emit finished();
}

QQuickTextureFactory* ThumbnailResponse::textureFactory() const {
    if (m_cancelled.load(std::memory_order_relaxed) || m_image.isNull())
        return nullptr;
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

void ThumbnailResponse::cancel() {
    if (m_cancelled.exchange(true, std::memory_order_relaxed))
        return;

    if (m_scheduler && m_scheduler->cancelQueued(this))
        finishCancelled();
}

void ThumbnailResponse::finishCancelled() {
    m_cancelled.store(true, std::memory_order_relaxed);
    emit finished();
}

ThumbnailImageProvider::ThumbnailImageProvider() = default;
ThumbnailImageProvider::~ThumbnailImageProvider() = default;

QString ThumbnailImageProvider::pathPart(const QString& id) {
    const qsizetype query = id.indexOf(QLatin1Char('?'));
    return query < 0 ? id : id.left(query);
}

QString ThumbnailImageProvider::decodePath(const QString& encoded) {
    const QByteArray bytes = QByteArray::fromBase64(
        encoded.toLatin1(),
        QByteArray::Base64UrlEncoding);
    return QString::fromUtf8(bytes);
}

int ThumbnailImageProvider::queryInteger(
    const QString& id,
    const QString& key,
    int fallback) {
    const qsizetype queryIndex = id.indexOf(QLatin1Char('?'));
    if (queryIndex < 0)
        return fallback;

    const QUrlQuery query(id.mid(queryIndex + 1));
    bool ok = false;
    const int value = query.queryItemValue(key).toInt(&ok);
    return ok ? value : fallback;
}

QQuickImageResponse* ThumbnailImageProvider::requestImageResponse(
    const QString& id,
    const QSize& requestedSize) {
    const QString path = decodePath(pathPart(id));
    const int requestedPixels = queryInteger(id, QStringLiteral("s"), 256);
    const int priority = queryInteger(id, QStringLiteral("p"), 0);

    QSize effective = requestedSize;
    if (!effective.isValid() || effective.isEmpty())
        effective = QSize(requestedPixels, requestedPixels);
    else if (requestedPixels > std::max(effective.width(), effective.height()))
        effective = QSize(requestedPixels, requestedPixels);

    auto* response = new ThumbnailResponse(&m_scheduler, path, effective, priority);
    m_scheduler.enqueue(response, priority);
    return response;
}
