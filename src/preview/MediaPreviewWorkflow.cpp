// SPDX-License-Identifier: GPL-3.0-only

#include "MediaPreviewWorkflow.hpp"
#include "PreviewFileAccess.hpp"
#include "PreviewScheduler.hpp"

#include <QAudioOutput>
#include <QFileInfo>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QSet>
#include <QUrl>
#include <QVideoSink>
#include <QtQml>

#include <algorithm>
#include <utility>

namespace {

QPointer<MediaPlaybackController> g_activePlayback;

bool isMediaCandidatePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.isAbsolute())
        return false;

    static const QSet<QString> suffixes {
        QStringLiteral("aac"), QStringLiteral("ac3"), QStringLiteral("aiff"),
        QStringLiteral("alac"), QStringLiteral("ape"), QStringLiteral("flac"),
        QStringLiteral("m4a"), QStringLiteral("mp3"), QStringLiteral("oga"),
        QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("wav"),
        QStringLiteral("wma"), QStringLiteral("3gp"), QStringLiteral("avi"),
        QStringLiteral("m2ts"), QStringLiteral("m4v"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("mp4"), QStringLiteral("mpeg"),
        QStringLiteral("mpg"), QStringLiteral("mts"), QStringLiteral("ogv"),
        QStringLiteral("ts"), QStringLiteral("webm"), QStringLiteral("wmv")
    };
    return suffixes.contains(info.suffix().toLower());
}

void registerMediaPreviewQmlTypes() {
    qmlRegisterType<MediaPreviewLoader>(
        "Ryofiles.Core", 1, 0, "MediaPreviewLoader");
    qmlRegisterType<MediaPlaybackController>(
        "Ryofiles.Core", 1, 0, "MediaPlaybackController");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerMediaPreviewQmlTypes)

MediaPreviewLoader::MediaPreviewLoader(QObject* parent)
    : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(75);
    connect(&m_debounce, &QTimer::timeout, this, &MediaPreviewLoader::startLoad);
}

MediaPreviewLoader::~MediaPreviewLoader() {
    PreviewScheduler::instance().cancelOwner(this);
}

void MediaPreviewLoader::setPath(const QString& path) {
    if (m_path == path)
        return;
    m_path = path;
    emit pathChanged();
    scheduleLoad();
}

void MediaPreviewLoader::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    emit activeChanged();
    scheduleLoad();
}

void MediaPreviewLoader::setMetadataOnly(bool metadataOnly) {
    if (m_metadataOnly == metadataOnly)
        return;
    m_metadataOnly = metadataOnly;
    emit metadataOnlyChanged();
    scheduleLoad();
}

bool MediaPreviewLoader::isCandidate(const QString& path) const {
    return isMediaCandidatePath(path);
}

void MediaPreviewLoader::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MediaPreviewLoader::clearResult() {
    if (!m_supported && !m_hasAudio && !m_hasVideo && m_mediaKind.isEmpty()
        && m_posterSource.isEmpty() && m_formatName.isEmpty() && m_title.isEmpty()
        && m_artist.isEmpty() && m_album.isEmpty() && m_audioCodec.isEmpty()
        && m_videoCodec.isEmpty() && m_channelLayout.isEmpty() && m_durationMs == 0
        && m_bitRate == 0 && m_sampleRate == 0 && m_channels == 0
        && m_videoWidth == 0 && m_videoHeight == 0 && m_frameRate == 0.0
        && m_error.isEmpty()) {
        return;
    }

    m_supported = false;
    m_hasAudio = false;
    m_hasVideo = false;
    m_mediaKind.clear();
    m_posterSource.clear();
    m_formatName.clear();
    m_title.clear();
    m_artist.clear();
    m_album.clear();
    m_audioCodec.clear();
    m_videoCodec.clear();
    m_channelLayout.clear();
    m_durationMs = 0;
    m_bitRate = 0;
    m_sampleRate = 0;
    m_channels = 0;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_frameRate = 0.0;
    m_error.clear();
    emit resultChanged();
}

void MediaPreviewLoader::scheduleLoad() {
    ++m_generation;
    m_debounce.stop();
    PreviewScheduler::instance().cancelOwner(this);
    setLoading(false);
    clearResult();

    if (!m_active || m_path.isEmpty() || !isMediaCandidatePath(m_path))
        return;
    m_debounce.start();
}

void MediaPreviewLoader::startLoad() {
    if (!m_active || m_path.isEmpty() || !isMediaCandidatePath(m_path))
        return;

    const quint64 generation = m_generation;
    const QString loadPath = m_path;
    setLoading(true);

    QJsonObject request;
    request.insert(QStringLiteral("op"), QStringLiteral("media-probe"));
    request.insert(QStringLiteral("path"), loadPath);
    request.insert(QStringLiteral("poster"), !m_metadataOnly);
    request.insert(QStringLiteral("maxWidth"), 1000);
    request.insert(QStringLiteral("maxHeight"), 700);

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
                    ? QStringLiteral("Preview unavailable")
                    : result.error;
                emit resultChanged();
                return;
            }

            const QJsonObject payload = result.payload;
            m_hasAudio = payload.value(QStringLiteral("hasAudio")).toBool(false);
            m_hasVideo = payload.value(QStringLiteral("hasVideo")).toBool(false);
            m_supported = m_hasAudio || m_hasVideo;
            if (!m_supported) {
                m_error = QStringLiteral("No audio or video stream was found");
                emit resultChanged();
                return;
            }

            m_mediaKind = payload.value(QStringLiteral("mediaKind")).toString();
            m_formatName = payload.value(QStringLiteral("format")).toString();
            m_title = payload.value(QStringLiteral("title")).toString();
            m_artist = payload.value(QStringLiteral("artist")).toString();
            m_album = payload.value(QStringLiteral("album")).toString();
            m_audioCodec = payload.value(QStringLiteral("audioCodec")).toString();
            m_videoCodec = payload.value(QStringLiteral("videoCodec")).toString();
            m_channelLayout = payload.value(QStringLiteral("channelLayout")).toString();
            m_durationMs = static_cast<qint64>(payload.value(QStringLiteral("durationMs")).toDouble(0.0));
            m_bitRate = static_cast<qint64>(payload.value(QStringLiteral("bitRate")).toDouble(0.0));
            m_sampleRate = payload.value(QStringLiteral("sampleRate")).toInt(0);
            m_channels = payload.value(QStringLiteral("channels")).toInt(0);
            m_videoWidth = payload.value(QStringLiteral("videoWidth")).toInt(0);
            m_videoHeight = payload.value(QStringLiteral("videoHeight")).toInt(0);
            m_frameRate = payload.value(QStringLiteral("frameRate")).toDouble(0.0);

            const QString posterBase64 = payload.value(QStringLiteral("posterBase64")).toString();
            m_posterSource = posterBase64.isEmpty()
                ? QString{}
                : QStringLiteral("data:image/png;base64,") + posterBase64;
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

MediaPlaybackController::MediaPlaybackController(QObject* parent)
    : QObject(parent)
    , m_player(std::make_unique<QMediaPlayer>())
    , m_audioOutput(std::make_unique<QAudioOutput>()) {
    m_audioOutput->setVolume(0.8f);
    m_player->setAudioOutput(m_audioOutput.get());

    const auto notify = [this] { emit playbackChanged(); };
    connect(m_player.get(), &QMediaPlayer::playbackStateChanged, this, notify);
    connect(m_player.get(), &QMediaPlayer::durationChanged, this, [notify](qint64) { notify(); });
    connect(m_player.get(), &QMediaPlayer::positionChanged, this, [notify](qint64) { notify(); });
    connect(m_player.get(), &QMediaPlayer::seekableChanged, this, [notify](bool) { notify(); });
    connect(m_player.get(), &QMediaPlayer::hasAudioChanged, this, [notify](bool) { notify(); });
    connect(m_player.get(), &QMediaPlayer::hasVideoChanged, this, [notify](bool) { notify(); });
    connect(m_player.get(), &QMediaPlayer::mediaStatusChanged, this,
        [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia)
                emit playbackChanged();
        });
    connect(m_player.get(), &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error, const QString& errorString) {
            setError(errorString.isEmpty()
                ? QStringLiteral("Playback unavailable")
                : errorString);
        });
}

MediaPlaybackController::~MediaPlaybackController() {
    releaseSource(false);
}

void MediaPlaybackController::setPath(const QString& path) {
    if (m_path == path)
        return;
    releaseSource();
    m_path = path;
    emit pathChanged();
    emit playbackChanged();
}

void MediaPlaybackController::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    if (!m_active)
        releaseSource();
    emit activeChanged();
    emit playbackChanged();
}

bool MediaPlaybackController::prepared() const {
    return m_file && m_player && m_player->sourceDevice() == m_file.get();
}

bool MediaPlaybackController::playing() const {
    return m_player
        && m_player->playbackState() == QMediaPlayer::PlayingState;
}

bool MediaPlaybackController::seekable() const {
    return m_player && m_player->isSeekable();
}

bool MediaPlaybackController::hasAudio() const {
    return m_player && m_player->hasAudio();
}

bool MediaPlaybackController::hasVideo() const {
    return m_player && m_player->hasVideo();
}

qint64 MediaPlaybackController::duration() const {
    return m_player ? m_player->duration() : 0;
}

qint64 MediaPlaybackController::position() const {
    return m_player ? m_player->position() : 0;
}

bool MediaPlaybackController::prepareSource() {
    if (prepared())
        return true;
    if (!m_active || m_path.isEmpty() || !isMediaCandidatePath(m_path)) {
        setError(QStringLiteral("Media playback is not available for this selection"));
        return false;
    }

    if (g_activePlayback && g_activePlayback != this)
        g_activePlayback->releaseSource();

    QString openError;
    PreviewFileAccess::OpenedFile opened =
        PreviewFileAccess::openRegularNoFollow(m_path, &openError);
    if (!opened) {
        setError(openError.isEmpty()
            ? QStringLiteral("Could not open media for playback")
            : openError);
        return false;
    }

    m_file = std::move(opened.file);
    m_error.clear();
    if (m_videoSink)
        m_player->setVideoSink(m_videoSink);
    m_player->setSourceDevice(m_file.get());
    g_activePlayback = this;
    emit playbackChanged();
    return true;
}

void MediaPlaybackController::play() {
    if (!prepareSource())
        return;
    m_player->play();
    emit playbackChanged();
}

void MediaPlaybackController::pause() {
    if (!prepared())
        return;
    m_player->pause();
    emit playbackChanged();
}

void MediaPlaybackController::stop() {
    releaseSource();
}

void MediaPlaybackController::seek(qint64 positionMs) {
    if (!prepared() || !m_player->isSeekable())
        return;
    m_player->setPosition(std::clamp<qint64>(positionMs, 0, std::max<qint64>(0, m_player->duration())));
}

void MediaPlaybackController::attachVideoSink(QObject* sinkObject) {
    QVideoSink* sink = qobject_cast<QVideoSink*>(sinkObject);
    if (m_videoSink == sink)
        return;
    m_videoSink = sink;
    m_player->setVideoSink(sink);
    emit playbackChanged();
}

void MediaPlaybackController::releaseSource(bool clearError) {
    if (!m_player)
        return;

    const bool hadSource = prepared() || m_player->sourceDevice();
    m_player->stop();
    m_player->setSource(QUrl());
    m_file.reset();
    if (g_activePlayback == this)
        g_activePlayback.clear();
    if (clearError)
        m_error.clear();
    if (hadSource || clearError)
        emit playbackChanged();
}

void MediaPlaybackController::setError(const QString& error) {
    if (m_error == error)
        return;
    m_error = error;
    emit playbackChanged();
}
