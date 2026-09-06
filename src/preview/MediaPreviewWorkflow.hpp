// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

class QAudioOutput;
class QFile;
class QMediaPlayer;
class QVideoSink;

class MediaPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY resultChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY resultChanged)
    Q_PROPERTY(QString mediaKind READ mediaKind NOTIFY resultChanged)
    Q_PROPERTY(QString posterSource READ posterSource NOTIFY resultChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY resultChanged)
    Q_PROPERTY(QString title READ title NOTIFY resultChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY resultChanged)
    Q_PROPERTY(QString album READ album NOTIFY resultChanged)
    Q_PROPERTY(QString audioCodec READ audioCodec NOTIFY resultChanged)
    Q_PROPERTY(QString videoCodec READ videoCodec NOTIFY resultChanged)
    Q_PROPERTY(QString channelLayout READ channelLayout NOTIFY resultChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY resultChanged)
    Q_PROPERTY(qint64 bitRate READ bitRate NOTIFY resultChanged)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY resultChanged)
    Q_PROPERTY(int channels READ channels NOTIFY resultChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY resultChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY resultChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit MediaPreviewLoader(QObject* parent = nullptr);
    ~MediaPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_supported; }
    bool hasAudio() const { return m_hasAudio; }
    bool hasVideo() const { return m_hasVideo; }
    QString mediaKind() const { return m_mediaKind; }
    QString posterSource() const { return m_posterSource; }
    QString formatName() const { return m_formatName; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString audioCodec() const { return m_audioCodec; }
    QString videoCodec() const { return m_videoCodec; }
    QString channelLayout() const { return m_channelLayout; }
    qint64 durationMs() const { return m_durationMs; }
    qint64 bitRate() const { return m_bitRate; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    double frameRate() const { return m_frameRate; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool isCandidate(const QString& path) const;

signals:
    void pathChanged();
    void activeChanged();
    void loadingChanged();
    void resultChanged();

private:
    void scheduleLoad();
    void startLoad();
    void clearResult();
    void setLoading(bool loading);

    QString m_path;
    bool m_active = false;
    bool m_loading = false;
    bool m_supported = false;
    bool m_hasAudio = false;
    bool m_hasVideo = false;
    quint64 m_generation = 0;
    QString m_mediaKind;
    QString m_posterSource;
    QString m_formatName;
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_audioCodec;
    QString m_videoCodec;
    QString m_channelLayout;
    qint64 m_durationMs = 0;
    qint64 m_bitRate = 0;
    int m_sampleRate = 0;
    int m_channels = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    double m_frameRate = 0.0;
    QString m_error;
    QTimer m_debounce;
};

class MediaPlaybackController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool prepared READ prepared NOTIFY playbackChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY playbackChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY playbackChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY playbackChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY playbackChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY playbackChanged)
    Q_PROPERTY(QString error READ error NOTIFY playbackChanged)

public:
    explicit MediaPlaybackController(QObject* parent = nullptr);
    ~MediaPlaybackController() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool active() const { return m_active; }
    void setActive(bool active);

    bool prepared() const;
    bool playing() const;
    bool seekable() const;
    bool hasAudio() const;
    bool hasVideo() const;
    qint64 duration() const;
    qint64 position() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void attachVideoSink(QObject* sinkObject);

signals:
    void pathChanged();
    void activeChanged();
    void playbackChanged();

private:
    bool prepareSource();
    void releaseSource(bool clearError = true);
    void setError(const QString& error);

    QString m_path;
    bool m_active = false;
    std::unique_ptr<QMediaPlayer> m_player;
    std::unique_ptr<QAudioOutput> m_audioOutput;
    std::unique_ptr<QFile> m_file;
    QPointer<QVideoSink> m_videoSink;
    QString m_error;
};
