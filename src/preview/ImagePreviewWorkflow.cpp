// SPDX-License-Identifier: GPL-3.0-only

#include "TextPreviewLoader.hpp"
#include "PreviewProtocol.hpp"
#include "PreviewScheduler.hpp"

#include <QFileInfo>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <QtQml>

#include <algorithm>

namespace {

bool isImageCandidatePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.isAbsolute())
        return false;

    static const QSet<QString> suffixes {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
        QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("avif"),
        QStringLiteral("heic"), QStringLiteral("heif"),
    };
    return suffixes.contains(info.suffix().toLower());
}

void registerImagePreviewQmlTypes() {
    qmlRegisterType<ImagePreviewLoader>(
        "Ryofiles.Core", 1, 0, "ImagePreviewLoader");
    qmlRegisterType<ImageAnimationController>(
        "Ryofiles.Core", 1, 0, "ImageAnimationController");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerImagePreviewQmlTypes)

ImagePreviewLoader::ImagePreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(75);
    connect(&m_debounce, &QTimer::timeout, this, &ImagePreviewLoader::startLoad);
}

ImagePreviewLoader::~ImagePreviewLoader() {
    PreviewScheduler::instance().cancelOwner(this);
}

void ImagePreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;
    m_path = path;
    emit pathChanged();
    scheduleLoad();
}

void ImagePreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

bool ImagePreviewLoader::isCandidate(const QString& path) const {
    return isImageCandidatePath(path);
}

void ImagePreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void ImagePreviewLoader::clearResult() {
    const bool alreadyClear = !m_supported && m_pixelWidth == 0 && m_pixelHeight == 0
        && m_formatName.isEmpty() && !m_metadataAvailable && !m_metadataLimited
        && !m_animated && !m_animationSupported && m_frameCount == 0
        && m_cameraMake.isEmpty() && m_cameraModel.isEmpty() && m_lensModel.isEmpty()
        && m_dateTaken.isEmpty() && m_exposureTime.isEmpty() && m_aperture.isEmpty()
        && m_iso.isEmpty() && m_focalLength.isEmpty() && m_orientation.isEmpty()
        && m_exposureBias.isEmpty() && m_whiteBalance.isEmpty()
        && m_colorSpace.isEmpty() && m_software.isEmpty() && m_artist.isEmpty()
        && m_copyright.isEmpty() && m_error.isEmpty();
    if (alreadyClear)
        return;

    m_supported = false;
    m_pixelWidth = 0;
    m_pixelHeight = 0;
    m_formatName.clear();
    m_metadataAvailable = false;
    m_metadataLimited = false;
    m_animated = false;
    m_animationSupported = false;
    m_frameCount = 0;
    m_cameraMake.clear();
    m_cameraModel.clear();
    m_lensModel.clear();
    m_dateTaken.clear();
    m_exposureTime.clear();
    m_aperture.clear();
    m_iso.clear();
    m_focalLength.clear();
    m_orientation.clear();
    m_exposureBias.clear();
    m_whiteBalance.clear();
    m_colorSpace.clear();
    m_software.clear();
    m_artist.clear();
    m_copyright.clear();
    m_error.clear();
    emit resultChanged();
}

void ImagePreviewLoader::scheduleLoad() {
    ++m_generation;
    m_debounce.stop();
    PreviewScheduler::instance().cancelOwner(this);
    setLoading(false);
    clearResult();

    if (!m_active || m_path.isEmpty() || !isImageCandidatePath(m_path))
        return;
    m_debounce.start();
}

void ImagePreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !isImageCandidatePath(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    setLoading(true);

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("image-probe"));
    request.insert(QStringLiteral("path"), loadPath);

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
                    ? QStringLiteral("Image metadata unavailable")
                    : result.error;
                emit resultChanged();
                return;
            }

            const QJsonObject payload = result.payload;
            m_pixelWidth = payload.value(QStringLiteral("pixelWidth")).toInt(0);
            m_pixelHeight = payload.value(QStringLiteral("pixelHeight")).toInt(0);
            m_formatName = payload.value(QStringLiteral("format")).toString();
            m_metadataAvailable = payload.value(QStringLiteral("metadataAvailable")).toBool(false);
            m_metadataLimited = payload.value(QStringLiteral("metadataLimited")).toBool(false);
            m_animated = payload.value(QStringLiteral("animated")).toBool(false);
            m_animationSupported = payload.value(QStringLiteral("animationSupported")).toBool(false);
            m_frameCount = payload.value(QStringLiteral("frameCount")).toInt(0);
            m_cameraMake = payload.value(QStringLiteral("cameraMake")).toString();
            m_cameraModel = payload.value(QStringLiteral("cameraModel")).toString();
            m_lensModel = payload.value(QStringLiteral("lensModel")).toString();
            m_dateTaken = payload.value(QStringLiteral("dateTaken")).toString();
            m_exposureTime = payload.value(QStringLiteral("exposureTime")).toString();
            m_aperture = payload.value(QStringLiteral("aperture")).toString();
            m_iso = payload.value(QStringLiteral("iso")).toString();
            m_focalLength = payload.value(QStringLiteral("focalLength")).toString();
            m_orientation = payload.value(QStringLiteral("orientation")).toString();
            m_exposureBias = payload.value(QStringLiteral("exposureBias")).toString();
            m_whiteBalance = payload.value(QStringLiteral("whiteBalance")).toString();
            m_colorSpace = payload.value(QStringLiteral("colorSpace")).toString();
            m_software = payload.value(QStringLiteral("software")).toString();
            m_artist = payload.value(QStringLiteral("artist")).toString();
            m_copyright = payload.value(QStringLiteral("copyright")).toString();
            m_supported = m_pixelWidth > 0 && m_pixelHeight > 0;
            m_error.clear();
            emit resultChanged();
        });

    if (!admitted) {
        setLoading(false);
        m_supported = false;
        m_error = QStringLiteral("Preview queue is busy");
        emit resultChanged();
    }
}

ImageAnimationController::ImageAnimationController(QObject* parent)
    : QObject(parent) {
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, [this] {
        if (!m_playing)
            return;
        if (m_frameCount > 0 && m_frame + 1 >= m_frameCount) {
            stopInternal();
            return;
        }
        requestNext();
    });
}

ImageAnimationController::~ImageAnimationController() {
    stopInternal();
}

void ImageAnimationController::setPath(const QString& path) {
    if (m_path == path)
        return;
    stopInternal();
    m_path = path;
    emit pathChanged();
}

void ImageAnimationController::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    if (!m_active)
        stopInternal();
    emit activeChanged();
}

void ImageAnimationController::play() {
    if (m_playing)
        return;
    if (!m_active || m_path.isEmpty() || !isImageCandidatePath(m_path)) {
        m_error = QStringLiteral("Animation Quick Look is unavailable for this selection");
        emit playbackChanged();
        return;
    }

    ++m_generation;
    PreviewScheduler::instance().cancelOwner(this);
    m_frameTimer.stop();
    m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_frameSource.clear();
    m_frame = -1;
    m_frameCount = 0;
    m_error.clear();
    m_playing = true;
    m_elapsed.start();
    emit playbackChanged();
    requestStart();
}

void ImageAnimationController::stop() {
    stopInternal();
}

void ImageAnimationController::requestStart() {
    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    const QString session = m_session;

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("image-animation-start"));
    request.insert(QStringLiteral("path"), loadPath);
    request.insert(QStringLiteral("session"), session);
    request.insert(QStringLiteral("maxWidth"), 1000);
    request.insert(QStringLiteral("maxHeight"), 700);

    const bool admitted = PreviewScheduler::instance().submit(
        PreviewScheduler::Lane::ExplicitLongWork,
        this,
        request,
        [this, generation, loadPath, session](PreviewResult result) {
            if (!m_playing || generation != m_generation || loadPath != m_path
                || session != m_session || !m_active) {
                return;
            }
            if (!result.ok) {
                fail(result.error.isEmpty()
                    ? QStringLiteral("Animation Quick Look unavailable")
                    : result.error);
                return;
            }
            applyFrame(result.payload);
        });

    if (!admitted)
        fail(QStringLiteral("Preview queue is busy"));
}

void ImageAnimationController::requestNext() {
    if (!m_playing || !m_active)
        return;
    if (!m_elapsed.isValid() || m_elapsed.elapsed() >= PreviewProtocol::kMaxAnimationPlayMs) {
        stopInternal();
        return;
    }

    const quint64 generation = m_generation;
    const QString session = m_session;

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("image-animation-next"));
    request.insert(QStringLiteral("session"), session);

    const bool admitted = PreviewScheduler::instance().submit(
        PreviewScheduler::Lane::ExplicitLongWork,
        this,
        request,
        [this, generation, session](PreviewResult result) {
            if (!m_playing || generation != m_generation || session != m_session || !m_active)
                return;
            if (!result.ok) {
                fail(result.error.isEmpty()
                    ? QStringLiteral("Animation Quick Look unavailable")
                    : result.error);
                return;
            }
            applyFrame(result.payload);
        });

    if (!admitted)
        fail(QStringLiteral("Preview queue is busy"));
}

void ImageAnimationController::applyFrame(const QJsonObject& payload) {
    const QString session = payload.value(QStringLiteral("session")).toString();
    const QString base64 = payload.value(QStringLiteral("imageBase64")).toString();
    const int frame = payload.value(QStringLiteral("frame")).toInt(-1);
    const int frameCount = payload.value(QStringLiteral("frameCount")).toInt(0);
    int delayMs = payload.value(QStringLiteral("delayMs")).toInt(100);

    if (session != m_session || base64.isEmpty() || frame < 0 || frameCount <= 1
        || frame >= frameCount || frameCount > PreviewProtocol::kMaxAnimationFrames) {
        fail(QStringLiteral("Animation helper returned an invalid frame"));
        return;
    }

    delayMs = std::clamp(
        delayMs,
        PreviewProtocol::kMinAnimationDelayMs,
        PreviewProtocol::kMaxAnimationDelayMs);
    m_frameSource = QStringLiteral("data:image/png;base64,") + base64;
    m_frame = frame;
    m_frameCount = frameCount;
    m_error.clear();
    emit playbackChanged();

    if (m_elapsed.elapsed() >= PreviewProtocol::kMaxAnimationPlayMs) {
        stopInternal();
        return;
    }
    m_frameTimer.start(delayMs);
}

void ImageAnimationController::fail(const QString& error) {
    ++m_generation;
    m_frameTimer.stop();
    PreviewScheduler::instance().cancelOwner(this);
    m_session.clear();
    m_frameSource.clear();
    m_frame = -1;
    m_frameCount = 0;
    m_playing = false;
    m_error = error;
    emit playbackChanged();
}

void ImageAnimationController::stopInternal() {
    const bool hadState = m_playing || !m_session.isEmpty() || !m_frameSource.isEmpty()
        || m_frame >= 0 || m_frameCount != 0 || !m_error.isEmpty();
    ++m_generation;
    m_frameTimer.stop();
    PreviewScheduler::instance().cancelOwner(this);
    m_session.clear();
    m_frameSource.clear();
    m_frame = -1;
    m_frameCount = 0;
    m_playing = false;
    m_error.clear();
    if (hadState)
        emit playbackChanged();
}
