// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <atomic>
#include <deque>

class ThumbnailResponse;

class ThumbnailScheduler final {
public:
    ThumbnailScheduler();
    ~ThumbnailScheduler();

    void enqueue(ThumbnailResponse* response, int priority);
    void markStarted(ThumbnailResponse* response);
    bool cancelQueued(ThumbnailResponse* response);

private:
    static constexpr qsizetype kMaxQueued = 64;

    QMutex m_mutex;
    QThreadPool m_pool;
    std::deque<ThumbnailResponse*> m_queued;
};

class ThumbnailResponse final : public QQuickImageResponse, public QRunnable {
public:
    ThumbnailResponse(
        ThumbnailScheduler* scheduler,
        QString path,
        QSize requestedSize,
        int priority);

    void run() override;
    QQuickTextureFactory* textureFactory() const override;
    void cancel() override;

    int priority() const { return m_priority; }
    bool cancelled() const { return m_cancelled.load(std::memory_order_relaxed); }
    void finishCancelled();

private:
    ThumbnailScheduler* m_scheduler = nullptr;
    QString m_path;
    QSize m_requestedSize;
    int m_priority = 0;
    std::atomic_bool m_cancelled = false;
    QImage m_image;
};

class ThumbnailImageProvider final : public QQuickAsyncImageProvider {
public:
    ThumbnailImageProvider();
    ~ThumbnailImageProvider() override;

    QQuickImageResponse* requestImageResponse(
        const QString& id,
        const QSize& requestedSize) override;

private:
    static QString decodePath(const QString& encoded);
    static int queryInteger(const QString& id, const QString& key, int fallback);
    static QString pathPart(const QString& id);

    ThumbnailScheduler m_scheduler;
};
