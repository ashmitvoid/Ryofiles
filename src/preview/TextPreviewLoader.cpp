// SPDX-License-Identifier: GPL-3.0-only

#include "TextPreviewLoader.hpp"

#include <QFutureWatcher>
#include <QtConcurrent>

#include <utility>

TextPreviewLoader::TextPreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(90);
    connect(&m_debounce, &QTimer::timeout, this, &TextPreviewLoader::startLoad);
}

TextPreviewLoader::~TextPreviewLoader() {
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
}

void TextPreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;

    m_path = path;
    emit pathChanged();
    scheduleLoad();
}

void TextPreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;

    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

bool TextPreviewLoader::isCandidate(const QString& path) const {
    return TextPreviewStore::isCandidatePath(path);
}

void TextPreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TextPreviewLoader::clearResult() {
    if (!m_result.supported && !m_result.truncated &&
        m_result.text.isEmpty() && m_result.error.isEmpty()) {
        return;
    }

    m_result = {};
    emit resultChanged();
}

void TextPreviewLoader::scheduleLoad() {
    ++m_generation;
    m_debounce.stop();

    if (m_cancelToken) {
        m_cancelToken->store(true, std::memory_order_relaxed);
        m_cancelToken.reset();
    }

    setLoading(false);
    clearResult();

    if (!m_active || m_path.isEmpty() || !TextPreviewStore::isCandidatePath(m_path))
        return;

    m_debounce.start();
}

void TextPreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !TextPreviewStore::isCandidatePath(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    auto token = std::make_shared<std::atomic_bool>(false);
    m_cancelToken = token;
    setLoading(true);

    auto* watcher = new QFutureWatcher<TextPreviewResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, generation, loadPath, token] {
            TextPreviewResult result = watcher->result();
            watcher->deleteLater();

            if (generation != m_generation || loadPath != m_path || !m_active ||
                token->load(std::memory_order_relaxed)) {
                return;
            }

            if (m_cancelToken == token)
                m_cancelToken.reset();

            setLoading(false);
            m_result = std::move(result);
            emit resultChanged();
        });

    watcher->setFuture(QtConcurrent::run([loadPath, token] {
        return TextPreviewStore::load(loadPath, *token);
    }));
}
