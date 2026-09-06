// SPDX-License-Identifier: GPL-3.0-only

#include "TextPreviewLoader.hpp"
#include "PreviewScheduler.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>
#include <QtConcurrent>
#include <QtQml>

#include <algorithm>
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

bool isFontCandidatePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.isAbsolute())
        return false;

    static const QSet<QString> suffixes {
        QStringLiteral("ttf"),
        QStringLiteral("otf"),
        QStringLiteral("ttc"),
        QStringLiteral("otc"),
    };
    return suffixes.contains(info.suffix().toLower());
}

void registerPreviewLoaderQmlTypes() {
    qmlRegisterType<ArchivePreviewLoader>(
        "Ryofiles.Core", 1, 0, "ArchivePreviewLoader");
    qmlRegisterType<PdfPreviewLoader>(
        "Ryofiles.Core", 1, 0, "PdfPreviewLoader");
    qmlRegisterType<FontPreviewLoader>(
        "Ryofiles.Core", 1, 0, "FontPreviewLoader");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerPreviewLoaderQmlTypes)

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

PdfPreviewLoader::PdfPreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(75);
    connect(&m_debounce, &QTimer::timeout, this, &PdfPreviewLoader::startLoad);
}

PdfPreviewLoader::~PdfPreviewLoader() {
    PreviewScheduler::instance().cancelOwner(this);
}

void PdfPreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;

    m_path = path;
    if (m_page != 0) {
        m_page = 0;
        emit pageChanged();
    }
    emit pathChanged();
    scheduleLoad();
}

void PdfPreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;

    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

void PdfPreviewLoader::setPage(int page) {
    int bounded = std::max(0, page);
    if (m_pageCount > 0)
        bounded = std::min(bounded, m_pageCount - 1);
    if (m_page == bounded)
        return;

    m_page = bounded;
    emit pageChanged();
    scheduleLoad(false);
}

bool PdfPreviewLoader::isCandidate(const QString& path) const {
    const QFileInfo info(path);
    return info.isAbsolute()
        && info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
}

void PdfPreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void PdfPreviewLoader::clearResult() {
    if (!m_supported && m_pageCount == 0 && m_imageSource.isEmpty()
        && m_title.isEmpty() && m_author.isEmpty() && m_subject.isEmpty()
        && m_keywords.isEmpty() && m_error.isEmpty()) {
        return;
    }

    m_supported = false;
    m_pageCount = 0;
    m_imageSource.clear();
    m_title.clear();
    m_author.clear();
    m_subject.clear();
    m_keywords.clear();
    m_error.clear();
    emit resultChanged();
}

void PdfPreviewLoader::scheduleLoad(bool clearExisting) {
    ++m_generation;
    m_debounce.stop();
    PreviewScheduler::instance().cancelOwner(this);
    setLoading(false);
    if (clearExisting)
        clearResult();

    if (!m_active || m_path.isEmpty() || !isCandidate(m_path))
        return;

    m_debounce.start();
}

void PdfPreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !isCandidate(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    const int requestedPage = m_page;
    setLoading(true);

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("pdf-page"));
    request.insert(QStringLiteral("path"), loadPath);
    request.insert(QStringLiteral("page"), requestedPage);
    request.insert(QStringLiteral("maxWidth"), 1200);
    request.insert(QStringLiteral("maxHeight"), 1600);

    const bool admitted = PreviewScheduler::instance().submit(
        PreviewScheduler::Lane::InteractivePreview,
        this,
        request,
        [this, generation, loadPath, requestedPage](PreviewResult result) {
            if (generation != m_generation || loadPath != m_path
                || requestedPage != m_page || !m_active) {
                return;
            }

            setLoading(false);
            if (!result.ok) {
                m_supported = false;
                m_imageSource.clear();
                m_error = result.error.isEmpty()
                    ? QStringLiteral("Preview unavailable")
                    : result.error;
                emit resultChanged();
                return;
            }

            const QJsonObject payload = result.payload;
            const QString base64 = payload.value(QStringLiteral("imageBase64")).toString();
            const int pageCount = payload.value(QStringLiteral("pageCount")).toInt(0);
            if (base64.isEmpty() || pageCount <= 0) {
                m_supported = false;
                m_imageSource.clear();
                m_error = QStringLiteral("PDF preview returned no image");
                emit resultChanged();
                return;
            }

            m_supported = true;
            m_pageCount = pageCount;
            m_imageSource = QStringLiteral("data:image/png;base64,") + base64;
            m_title = payload.value(QStringLiteral("title")).toString();
            m_author = payload.value(QStringLiteral("author")).toString();
            m_subject = payload.value(QStringLiteral("subject")).toString();
            m_keywords = payload.value(QStringLiteral("keywords")).toString();
            m_error.clear();
            emit resultChanged();
        });

    if (!admitted) {
        setLoading(false);
        m_supported = false;
        m_imageSource.clear();
        m_error = QStringLiteral("Preview queue is busy");
        emit resultChanged();
    }
}

FontPreviewLoader::FontPreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(75);
    connect(&m_debounce, &QTimer::timeout, this, &FontPreviewLoader::startLoad);
}

FontPreviewLoader::~FontPreviewLoader() {
    PreviewScheduler::instance().cancelOwner(this);
}

void FontPreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;
    m_path = path;
    emit pathChanged();
    scheduleLoad();
}

void FontPreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

bool FontPreviewLoader::isCandidate(const QString& path) const {
    return isFontCandidatePath(path);
}

void FontPreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void FontPreviewLoader::clearResult() {
    if (!m_supported && m_familyName.isEmpty() && m_styleName.isEmpty()
        && m_styleLabel.isEmpty() && m_weight == 0 && m_unitsPerEm == 0.0
        && m_writingSystems.isEmpty() && m_sampleText.isEmpty()
        && m_sampleSource.isEmpty() && m_error.isEmpty()) {
        return;
    }

    m_supported = false;
    m_familyName.clear();
    m_styleName.clear();
    m_styleLabel.clear();
    m_weight = 0;
    m_unitsPerEm = 0.0;
    m_writingSystems.clear();
    m_sampleText.clear();
    m_sampleSource.clear();
    m_error.clear();
    emit resultChanged();
}

void FontPreviewLoader::scheduleLoad() {
    ++m_generation;
    m_debounce.stop();
    PreviewScheduler::instance().cancelOwner(this);
    setLoading(false);
    clearResult();

    if (!m_active || m_path.isEmpty() || !isFontCandidatePath(m_path))
        return;
    m_debounce.start();
}

void FontPreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !isFontCandidatePath(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    setLoading(true);

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("font-preview"));
    request.insert(QStringLiteral("path"), loadPath);
    request.insert(QStringLiteral("maxWidth"), 1000);
    request.insert(QStringLiteral("maxHeight"), 520);
    request.insert(QStringLiteral("pixelSize"), 64);

    const bool admitted = PreviewScheduler::instance().submit(
        PreviewScheduler::Lane::InteractivePreview,
        this,
        request,
        [this, generation, loadPath](PreviewResult result) {
            if (generation != m_generation || loadPath != m_path || !m_active)
                return;

            setLoading(false);
            if (!result.ok) {
                m_supported = false;
                m_error = result.error.isEmpty()
                    ? QStringLiteral("Font preview unavailable")
                    : result.error;
                emit resultChanged();
                return;
            }

            const QJsonObject payload = result.payload;
            m_familyName = payload.value(QStringLiteral("family")).toString();
            m_styleName = payload.value(QStringLiteral("styleName")).toString();
            m_styleLabel = payload.value(QStringLiteral("style")).toString();
            m_weight = payload.value(QStringLiteral("weight")).toInt(0);
            m_unitsPerEm = payload.value(QStringLiteral("unitsPerEm")).toDouble(0.0);
            m_sampleText = payload.value(QStringLiteral("sampleText")).toString();

            QStringList writingSystems;
            const QJsonArray systems = payload.value(QStringLiteral("writingSystems")).toArray();
            writingSystems.reserve(systems.size());
            for (const QJsonValue value : systems) {
                const QString name = value.toString();
                if (!name.isEmpty())
                    writingSystems.append(name);
            }
            m_writingSystems = writingSystems.join(QStringLiteral(" · "));

            const QString sampleBase64 = payload.value(QStringLiteral("sampleBase64")).toString();
            m_sampleSource = sampleBase64.isEmpty()
                ? QString{}
                : QStringLiteral("data:image/png;base64,") + sampleBase64;
            m_supported = !m_sampleSource.isEmpty();
            m_error = m_supported
                ? QString{}
                : QStringLiteral("Font preview returned no sample image");
            emit resultChanged();
        });

    if (!admitted) {
        setLoading(false);
        m_supported = false;
        m_error = QStringLiteral("Preview queue is busy");
        emit resultChanged();
    }
}
