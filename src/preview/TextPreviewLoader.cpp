// SPDX-License-Identifier: GPL-3.0-only

#include "TextPreviewLoader.hpp"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QtConcurrent>
#include <QtQml>

#include <utility>

namespace {

QString archiveEntryKindName(ArchivePreviewEntryKind kind) {
    switch (kind) {
    case ArchivePreviewEntryKind::RegularFile:
        return QStringLiteral("file");
    case ArchivePreviewEntryKind::Directory:
        return QStringLiteral("directory");
    case ArchivePreviewEntryKind::Symlink:
        return QStringLiteral("symlink");
    case ArchivePreviewEntryKind::Hardlink:
        return QStringLiteral("hardlink");
    case ArchivePreviewEntryKind::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

QVariantList archiveEntriesForQml(const QVector<ArchivePreviewEntry>& entries) {
    QVariantList converted;
    converted.reserve(entries.size());

    for (const ArchivePreviewEntry& entry : entries) {
        QVariantMap item;
        item.insert(QStringLiteral("path"), entry.path);
        item.insert(QStringLiteral("kind"), archiveEntryKindName(entry.kind));
        item.insert(QStringLiteral("sizeKnown"), entry.sizeKnown);
        item.insert(
            QStringLiteral("size"),
            QVariant::fromValue<qulonglong>(entry.size));
        converted.push_back(item);
    }

    return converted;
}

void registerArchivePreviewLoaderQmlType() {
    qmlRegisterType<ArchivePreviewLoader>(
        "Ryofiles.Core", 1, 0, "ArchivePreviewLoader");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerArchivePreviewLoaderQmlType)

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

ArchivePreviewLoader::ArchivePreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(90);
    connect(&m_debounce, &QTimer::timeout, this, &ArchivePreviewLoader::startLoad);
}

ArchivePreviewLoader::~ArchivePreviewLoader() {
    if (m_cancelToken)
        m_cancelToken->store(true, std::memory_order_relaxed);
}

void ArchivePreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;

    m_path = path;
    emit pathChanged();
    scheduleLoad();
}

void ArchivePreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;

    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

bool ArchivePreviewLoader::isCandidate(const QString& path) const {
    return ArchivePreviewStore::isCandidatePath(path);
}

void ArchivePreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void ArchivePreviewLoader::clearResult() {
    if (!m_result.succeeded() && !m_result.truncated &&
        m_result.error.isEmpty() && m_result.formatName.isEmpty() &&
        m_result.entries.isEmpty() && m_entries.isEmpty() &&
        m_result.entriesSeen == 0 && m_result.archiveBytesConsumed == 0) {
        return;
    }

    m_result = {};
    m_entries.clear();
    emit resultChanged();
}

void ArchivePreviewLoader::scheduleLoad() {
    ++m_generation;
    m_debounce.stop();

    if (m_cancelToken) {
        m_cancelToken->store(true, std::memory_order_relaxed);
        m_cancelToken.reset();
    }

    setLoading(false);
    clearResult();

    if (!m_active || m_path.isEmpty() || !ArchivePreviewStore::isCandidatePath(m_path))
        return;

    m_debounce.start();
}

void ArchivePreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !ArchivePreviewStore::isCandidatePath(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    auto token = std::make_shared<std::atomic_bool>(false);
    m_cancelToken = token;
    setLoading(true);

    auto* watcher = new QFutureWatcher<ArchivePreviewResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
        [this, watcher, generation, loadPath, token] {
            ArchivePreviewResult result = watcher->result();
            watcher->deleteLater();

            if (generation != m_generation || loadPath != m_path || !m_active ||
                token->load(std::memory_order_relaxed)) {
                return;
            }

            if (m_cancelToken == token)
                m_cancelToken.reset();

            setLoading(false);
            m_result = std::move(result);
            m_entries = archiveEntriesForQml(m_result.entries);
            emit resultChanged();
        });

    watcher->setFuture(QtConcurrent::run([loadPath, token] {
        return ArchivePreviewStore::inspect(loadPath, *token);
    }));
}
