// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "ArchivePreviewStore.hpp"
#include "TextPreviewStore.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <memory>

class TextPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultChanged)
    Q_PROPERTY(QString text READ text NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit TextPreviewLoader(QObject* parent = nullptr);
    ~TextPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_result.supported; }
    bool truncated() const { return m_result.truncated; }
    QString text() const { return m_result.text; }
    QString error() const { return m_result.error; }

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
    quint64 m_generation = 0;
    TextPreviewResult m_result;
    QTimer m_debounce;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
};

class ArchivePreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY resultChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY resultChanged)
    Q_PROPERTY(quint64 entriesSeen READ entriesSeen NOTIFY resultChanged)
    Q_PROPERTY(quint64 archiveBytesConsumed READ archiveBytesConsumed NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit ArchivePreviewLoader(QObject* parent = nullptr);
    ~ArchivePreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);

    bool loading() const { return m_loading; }
    bool supported() const { return m_result.succeeded(); }
    bool truncated() const { return m_result.truncated; }
    QString formatName() const { return m_result.formatName; }
    QVariantList entries() const { return m_entries; }
    quint64 entriesSeen() const { return m_result.entriesSeen; }
    quint64 archiveBytesConsumed() const { return m_result.archiveBytesConsumed; }
    QString error() const { return m_result.error; }

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
    quint64 m_generation = 0;
    ArchivePreviewResult m_result;
    QVariantList m_entries;
    QTimer m_debounce;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
};

class PdfPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool metadataOnly READ metadataOnly WRITE setMetadataOnly NOTIFY metadataOnlyChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY resultChanged)
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY resultChanged)
    Q_PROPERTY(QString title READ title NOTIFY resultChanged)
    Q_PROPERTY(QString author READ author NOTIFY resultChanged)
    Q_PROPERTY(QString subject READ subject NOTIFY resultChanged)
    Q_PROPERTY(QString keywords READ keywords NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit PdfPreviewLoader(QObject* parent = nullptr);
    ~PdfPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);

    bool active() const { return m_active; }
    void setActive(bool active);
    bool metadataOnly() const { return m_metadataOnly; }
    void setMetadataOnly(bool metadataOnly);

    bool loading() const { return m_loading; }
    bool supported() const { return m_supported; }
    int page() const { return m_page; }
    void setPage(int page);
    int pageCount() const { return m_pageCount; }
    QString imageSource() const { return m_imageSource; }
    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString subject() const { return m_subject; }
    QString keywords() const { return m_keywords; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool isCandidate(const QString& path) const;

signals:
    void pathChanged();
    void activeChanged();
    void metadataOnlyChanged();
    void loadingChanged();
    void pageChanged();
    void resultChanged();

private:
    void scheduleLoad(bool clearExisting = true);
    void startLoad();
    void clearResult();
    void setLoading(bool loading);

    QString m_path;
    bool m_active = false;
    bool m_metadataOnly = false;
    bool m_loading = false;
    bool m_supported = false;
    int m_page = 0;
    int m_pageCount = 0;
    quint64 m_generation = 0;
    QString m_imageSource;
    QString m_title;
    QString m_author;
    QString m_subject;
    QString m_keywords;
    QString m_error;
    QTimer m_debounce;
};

class FontPreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool metadataOnly READ metadataOnly WRITE setMetadataOnly NOTIFY metadataOnlyChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(QString familyName READ familyName NOTIFY resultChanged)
    Q_PROPERTY(QString styleName READ styleName NOTIFY resultChanged)
    Q_PROPERTY(QString styleLabel READ styleLabel NOTIFY resultChanged)
    Q_PROPERTY(int weight READ weight NOTIFY resultChanged)
    Q_PROPERTY(double unitsPerEm READ unitsPerEm NOTIFY resultChanged)
    Q_PROPERTY(QString writingSystems READ writingSystems NOTIFY resultChanged)
    Q_PROPERTY(QString sampleText READ sampleText NOTIFY resultChanged)
    Q_PROPERTY(QString sampleSource READ sampleSource NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit FontPreviewLoader(QObject* parent = nullptr);
    ~FontPreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool metadataOnly() const { return m_metadataOnly; }
    void setMetadataOnly(bool metadataOnly);
    bool loading() const { return m_loading; }
    bool supported() const { return m_supported; }
    QString familyName() const { return m_familyName; }
    QString styleName() const { return m_styleName; }
    QString styleLabel() const { return m_styleLabel; }
    int weight() const { return m_weight; }
    double unitsPerEm() const { return m_unitsPerEm; }
    QString writingSystems() const { return m_writingSystems; }
    QString sampleText() const { return m_sampleText; }
    QString sampleSource() const { return m_sampleSource; }
    QString error() const { return m_error; }

    Q_INVOKABLE bool isCandidate(const QString& path) const;

signals:
    void pathChanged();
    void activeChanged();
    void metadataOnlyChanged();
    void loadingChanged();
    void resultChanged();

private:
    void scheduleLoad();
    void startLoad();
    void clearResult();
    void setLoading(bool loading);

    QString m_path;
    bool m_active = false;
    bool m_metadataOnly = false;
    bool m_loading = false;
    bool m_supported = false;
    quint64 m_generation = 0;
    QString m_familyName;
    QString m_styleName;
    QString m_styleLabel;
    int m_weight = 0;
    double m_unitsPerEm = 0.0;
    QString m_writingSystems;
    QString m_sampleText;
    QString m_sampleSource;
    QString m_error;
    QTimer m_debounce;
};

class ImagePreviewLoader : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool supported READ supported NOTIFY resultChanged)
    Q_PROPERTY(int pixelWidth READ pixelWidth NOTIFY resultChanged)
    Q_PROPERTY(int pixelHeight READ pixelHeight NOTIFY resultChanged)
    Q_PROPERTY(QString formatName READ formatName NOTIFY resultChanged)
    Q_PROPERTY(bool metadataAvailable READ metadataAvailable NOTIFY resultChanged)
    Q_PROPERTY(bool metadataLimited READ metadataLimited NOTIFY resultChanged)
    Q_PROPERTY(bool animated READ animated NOTIFY resultChanged)
    Q_PROPERTY(bool animationSupported READ animationSupported NOTIFY resultChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY resultChanged)
    Q_PROPERTY(QString cameraMake READ cameraMake NOTIFY resultChanged)
    Q_PROPERTY(QString cameraModel READ cameraModel NOTIFY resultChanged)
    Q_PROPERTY(QString lensModel READ lensModel NOTIFY resultChanged)
    Q_PROPERTY(QString dateTaken READ dateTaken NOTIFY resultChanged)
    Q_PROPERTY(QString exposureTime READ exposureTime NOTIFY resultChanged)
    Q_PROPERTY(QString aperture READ aperture NOTIFY resultChanged)
    Q_PROPERTY(QString iso READ iso NOTIFY resultChanged)
    Q_PROPERTY(QString focalLength READ focalLength NOTIFY resultChanged)
    Q_PROPERTY(QString orientation READ orientation NOTIFY resultChanged)
    Q_PROPERTY(QString exposureBias READ exposureBias NOTIFY resultChanged)
    Q_PROPERTY(QString whiteBalance READ whiteBalance NOTIFY resultChanged)
    Q_PROPERTY(QString colorSpace READ colorSpace NOTIFY resultChanged)
    Q_PROPERTY(QString software READ software NOTIFY resultChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY resultChanged)
    Q_PROPERTY(QString copyright READ copyright NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)

public:
    explicit ImagePreviewLoader(QObject* parent = nullptr);
    ~ImagePreviewLoader() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool loading() const { return m_loading; }
    bool supported() const { return m_supported; }
    int pixelWidth() const { return m_pixelWidth; }
    int pixelHeight() const { return m_pixelHeight; }
    QString formatName() const { return m_formatName; }
    bool metadataAvailable() const { return m_metadataAvailable; }
    bool metadataLimited() const { return m_metadataLimited; }
    bool animated() const { return m_animated; }
    bool animationSupported() const { return m_animationSupported; }
    int frameCount() const { return m_frameCount; }
    QString cameraMake() const { return m_cameraMake; }
    QString cameraModel() const { return m_cameraModel; }
    QString lensModel() const { return m_lensModel; }
    QString dateTaken() const { return m_dateTaken; }
    QString exposureTime() const { return m_exposureTime; }
    QString aperture() const { return m_aperture; }
    QString iso() const { return m_iso; }
    QString focalLength() const { return m_focalLength; }
    QString orientation() const { return m_orientation; }
    QString exposureBias() const { return m_exposureBias; }
    QString whiteBalance() const { return m_whiteBalance; }
    QString colorSpace() const { return m_colorSpace; }
    QString software() const { return m_software; }
    QString artist() const { return m_artist; }
    QString copyright() const { return m_copyright; }
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
    int m_pixelWidth = 0;
    int m_pixelHeight = 0;
    bool m_metadataAvailable = false;
    bool m_metadataLimited = false;
    bool m_animated = false;
    bool m_animationSupported = false;
    int m_frameCount = 0;
    quint64 m_generation = 0;
    QString m_formatName;
    QString m_cameraMake;
    QString m_cameraModel;
    QString m_lensModel;
    QString m_dateTaken;
    QString m_exposureTime;
    QString m_aperture;
    QString m_iso;
    QString m_focalLength;
    QString m_orientation;
    QString m_exposureBias;
    QString m_whiteBalance;
    QString m_colorSpace;
    QString m_software;
    QString m_artist;
    QString m_copyright;
    QString m_error;
    QTimer m_debounce;
};

class ImageAnimationController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(QString frameSource READ frameSource NOTIFY playbackChanged)
    Q_PROPERTY(int frame READ frame NOTIFY playbackChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY playbackChanged)
    Q_PROPERTY(QString error READ error NOTIFY playbackChanged)

public:
    explicit ImageAnimationController(QObject* parent = nullptr);
    ~ImageAnimationController() override;

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool playing() const { return m_playing; }
    QString frameSource() const { return m_frameSource; }
    int frame() const { return m_frame; }
    int frameCount() const { return m_frameCount; }
    QString error() const { return m_error; }

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();

signals:
    void pathChanged();
    void activeChanged();
    void playbackChanged();

private:
    void requestStart();
    void requestNext();
    void applyFrame(const QJsonObject& payload);
    void fail(const QString& error);
    void stopInternal();

    QString m_path;
    bool m_active = false;
    bool m_playing = false;
    quint64 m_generation = 0;
    QString m_session;
    QString m_frameSource;
    int m_frame = -1;
    int m_frameCount = 0;
    QString m_error;
    QTimer m_frameTimer;
    QElapsedTimer m_elapsed;
};
