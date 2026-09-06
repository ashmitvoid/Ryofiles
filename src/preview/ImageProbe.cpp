// SPDX-License-Identifier: GPL-3.0-only

#include "ImageProbe.hpp"
#include "PreviewProtocol.hpp"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSize>

#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <exception>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {

QString boundedMetadata(QString value) {
    value = value.simplified();
    if (value.size() > PreviewProtocol::kMaxImageMetadataChars)
        value.truncate(PreviewProtocol::kMaxImageMetadataChars);
    return value;
}

QString boundedMetadata(const std::string& value) {
    return boundedMetadata(QString::fromStdString(value));
}

QString exifValue(const Exiv2::ExifData& data, const char* key) {
    try {
        const auto it = data.findKey(Exiv2::ExifKey(key));
        if (it == data.end())
            return {};
        return boundedMetadata(it->toString());
    } catch (const Exiv2::Error&) {
        return {};
    } catch (const std::exception&) {
        return {};
    }
}

QString firstExifValue(
    const Exiv2::ExifData& data,
    std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const QString value = exifValue(data, key);
        if (!value.isEmpty())
            return value;
    }
    return {};
}

void insertIfNotEmpty(QJsonObject& payload, const QString& key, const QString& value) {
    if (!value.isEmpty())
        payload.insert(key, value);
}

int safeDimension(uint32_t value) {
    if (value == 0 || value > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        return 0;
    return static_cast<int>(value);
}

bool dimensionsWithinAnimationBounds(const QSize& size) {
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;
    if (size.width() > PreviewProtocol::kMaxAnimationSourceDimension
        || size.height() > PreviewProtocol::kMaxAnimationSourceDimension) {
        return false;
    }
    const qint64 pixels =
        static_cast<qint64>(size.width()) * static_cast<qint64>(size.height());
    return pixels > 0 && pixels <= PreviewProtocol::kMaxAnimationSourcePixels;
}

QByteArray encodePngBounded(QImage* image, QString* error) {
    if (!image || image->isNull()) {
        if (error)
            *error = QStringLiteral("Animation frame did not decode");
        return {};
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image->save(&buffer, "PNG")) {
            if (error)
                *error = QStringLiteral("Could not encode animation frame");
            return {};
        }
        if (encoded.size() <= PreviewProtocol::kMaxEncodedImageBytes)
            return encoded;

        if (image->width() <= 320 || image->height() <= 320)
            break;

        const int nextWidth = std::max(320, qRound(image->width() * 0.80));
        const int nextHeight = std::max(320, qRound(image->height() * 0.80));
        *image = image->scaled(
            nextWidth,
            nextHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    if (error)
        *error = QStringLiteral("Animation frame exceeded the output limit");
    return {};
}

} // namespace

namespace ImageProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error) {
    if (fileSize < 0) {
        if (error)
            *error = QStringLiteral("Image size is invalid");
        return {};
    }

    QJsonObject payload;
    bool identified = false;
    int pixelWidth = 0;
    int pixelHeight = 0;
    QString formatName;
    QString mimeType;

    if (file.seek(0)) {
        QImageReader reader(&file);
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
        if (reader.canRead()) {
            identified = true;
            formatName = QString::fromLatin1(reader.format()).toLower();
            const QSize size = reader.size();
            if (size.isValid()) {
                pixelWidth = size.width();
                pixelHeight = size.height();
            }
        }
    }

    bool metadataAvailable = false;
    const bool metadataLimited =
        fileSize > PreviewProtocol::kMaxImageMetadataInputBytes;

    if (!metadataLimited && file.seek(0)) {
        const QByteArray bytes = file.readAll();
        if (static_cast<qint64>(bytes.size()) == fileSize && !bytes.isEmpty()) {
            try {
                auto image = Exiv2::ImageFactory::open(
                    reinterpret_cast<const Exiv2::byte*>(bytes.constData()),
                    static_cast<size_t>(bytes.size()));
                if (image) {
                    image->readMetadata();
                    identified = true;

                    const int exivWidth = safeDimension(image->pixelWidth());
                    const int exivHeight = safeDimension(image->pixelHeight());
                    if (pixelWidth <= 0 && exivWidth > 0)
                        pixelWidth = exivWidth;
                    if (pixelHeight <= 0 && exivHeight > 0)
                        pixelHeight = exivHeight;

                    mimeType = boundedMetadata(image->mimeType());
                    if (formatName.isEmpty() && mimeType.startsWith(QStringLiteral("image/")))
                        formatName = mimeType.mid(6).toLower();

                    const Exiv2::ExifData& exif = image->exifData();
                    insertIfNotEmpty(
                        payload, QStringLiteral("cameraMake"),
                        exifValue(exif, "Exif.Image.Make"));
                    insertIfNotEmpty(
                        payload, QStringLiteral("cameraModel"),
                        exifValue(exif, "Exif.Image.Model"));
                    insertIfNotEmpty(
                        payload, QStringLiteral("lensModel"),
                        firstExifValue(exif, {
                            "Exif.Photo.LensModel",
                            "Exif.Image.LensModel",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("dateTaken"),
                        firstExifValue(exif, {
                            "Exif.Photo.DateTimeOriginal",
                            "Exif.Image.DateTimeOriginal",
                            "Exif.Photo.DateTimeDigitized",
                            "Exif.Image.DateTimeDigitized",
                            "Exif.Image.DateTime",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("exposureTime"),
                        firstExifValue(exif, {
                            "Exif.Photo.ExposureTime",
                            "Exif.Image.ExposureTime",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("aperture"),
                        firstExifValue(exif, {
                            "Exif.Photo.FNumber",
                            "Exif.Image.FNumber",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("iso"),
                        firstExifValue(exif, {
                            "Exif.Photo.PhotographicSensitivity",
                            "Exif.Photo.ISOSpeedRatings",
                            "Exif.Image.PhotographicSensitivity",
                            "Exif.Image.ISOSpeedRatings",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("focalLength"),
                        firstExifValue(exif, {
                            "Exif.Photo.FocalLength",
                            "Exif.Image.FocalLength",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("orientation"),
                        exifValue(exif, "Exif.Image.Orientation"));
                    insertIfNotEmpty(
                        payload, QStringLiteral("exposureBias"),
                        firstExifValue(exif, {
                            "Exif.Photo.ExposureBiasValue",
                            "Exif.Image.ExposureBiasValue",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("whiteBalance"),
                        firstExifValue(exif, {
                            "Exif.Photo.WhiteBalance",
                            "Exif.Image.WhiteBalance",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("colorSpace"),
                        firstExifValue(exif, {
                            "Exif.Photo.ColorSpace",
                            "Exif.Image.ColorSpace",
                        }));
                    insertIfNotEmpty(
                        payload, QStringLiteral("software"),
                        exifValue(exif, "Exif.Image.Software"));
                    insertIfNotEmpty(
                        payload, QStringLiteral("artist"),
                        exifValue(exif, "Exif.Image.Artist"));
                    insertIfNotEmpty(
                        payload, QStringLiteral("copyright"),
                        exifValue(exif, "Exif.Image.Copyright"));
                    metadataAvailable = !payload.isEmpty();
                }
            } catch (const Exiv2::Error&) {
                // A valid image can contain malformed or unsupported metadata.
                // Keep the bounded Qt header result rather than failing the preview.
            } catch (const std::exception&) {
                // Same rule: metadata failure must not take down an otherwise valid image.
            }
        }
    }

    if (!identified) {
        if (error)
            *error = QStringLiteral("Could not identify image");
        return {};
    }

    const QString suffix = QFileInfo(
        request.value(QStringLiteral("path")).toString()).suffix().toLower();

    bool animated = false;
    bool animationSupported = false;
    int frameCount = 0;
    int loopCount = 0;

    if (fileSize <= PreviewProtocol::kMaxAnimationInputBytes && file.seek(0)) {
        QImageReader animationReader(&file);
        animationReader.setDecideFormatFromContent(true);
        const QString animationFormat =
            QString::fromLatin1(animationReader.format()).toLower();
        const bool animationFormatCandidate =
            animationFormat == QStringLiteral("gif")
            || animationFormat == QStringLiteral("webp")
            || suffix == QStringLiteral("gif")
            || suffix == QStringLiteral("webp");

        if (animationFormatCandidate && animationReader.supportsAnimation()) {
            frameCount = animationReader.imageCount();
            animated = frameCount > 1;
            loopCount = animationReader.loopCount();

            QSize sourceSize = animationReader.size();
            if (!sourceSize.isValid() && pixelWidth > 0 && pixelHeight > 0)
                sourceSize = QSize(pixelWidth, pixelHeight);

            animationSupported =
                animated
                && frameCount <= PreviewProtocol::kMaxAnimationFrames
                && dimensionsWithinAnimationBounds(sourceSize);
        }
    }

    payload.insert(QStringLiteral("pixelWidth"), pixelWidth);
    payload.insert(QStringLiteral("pixelHeight"), pixelHeight);
    payload.insert(QStringLiteral("format"), formatName);
    payload.insert(QStringLiteral("mime"), mimeType);
    payload.insert(QStringLiteral("metadataAvailable"), metadataAvailable);
    payload.insert(QStringLiteral("metadataLimited"), metadataLimited);
    payload.insert(QStringLiteral("animated"), animated);
    payload.insert(QStringLiteral("animationSupported"), animationSupported);
    payload.insert(QStringLiteral("frameCount"), frameCount);
    payload.insert(QStringLiteral("loopCount"), loopCount);
    payload.insert(QStringLiteral("fileSize"), QString::number(fileSize));
    return payload;
}

AnimationSession::AnimationSession(
    QByteArray bytes,
    QString token,
    int frameCount,
    int maxWidth,
    int maxHeight)
    : m_bytes(std::move(bytes))
    , m_buffer(std::make_unique<QBuffer>(&m_bytes))
    , m_token(std::move(token))
    , m_frameCount(frameCount)
    , m_maxWidth(maxWidth)
    , m_maxHeight(maxHeight) {
    m_buffer->open(QIODevice::ReadOnly);
    m_reader = std::make_unique<QImageReader>(m_buffer.get());
    m_reader->setDecideFormatFromContent(true);
    m_reader->setAutoTransform(true);
}

AnimationSession::~AnimationSession() = default;

std::unique_ptr<AnimationSession> AnimationSession::create(
    PreviewFileAccess::OpenedFile opened,
    const QJsonObject& request,
    QString* error) {
    if (!opened || opened.size < 0
        || opened.size > PreviewProtocol::kMaxAnimationInputBytes) {
        if (error)
            *error = QStringLiteral("Animation exceeds the preview input limit");
        return {};
    }

    const QString token = request.value(QStringLiteral("session")).toString();
    if (token.isEmpty() || token.size() > 128 || token.contains(QChar::Null)) {
        if (error)
            *error = QStringLiteral("Animation session token is invalid");
        return {};
    }

    if (!opened.file->seek(0)) {
        if (error)
            *error = QStringLiteral("Could not rewind animation");
        return {};
    }
    QByteArray bytes = opened.file->readAll();
    if (static_cast<qint64>(bytes.size()) != opened.size || bytes.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not read bounded animation input");
        return {};
    }

    int frameCount = 0;
    QSize sourceSize;
    {
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Could not open bounded animation buffer");
            return {};
        }
        QImageReader reader(&buffer);
        reader.setDecideFormatFromContent(true);
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            if (error)
                *error = QStringLiteral("Could not read animation");
            return {};
        }

        const QString format = QString::fromLatin1(reader.format()).toLower();
        if (format != QStringLiteral("gif") && format != QStringLiteral("webp")) {
            if (error)
                *error = QStringLiteral("Only GIF and WebP Quick Look are supported");
            return {};
        }
        if (!reader.supportsAnimation()) {
            if (error)
                *error = QStringLiteral("Image is not animated");
            return {};
        }

        frameCount = reader.imageCount();
        if (frameCount <= 1 || frameCount > PreviewProtocol::kMaxAnimationFrames) {
            if (error)
                *error = QStringLiteral("Animation frame count exceeds the preview limit");
            return {};
        }

        sourceSize = reader.size();
        if (!dimensionsWithinAnimationBounds(sourceSize)) {
            if (error)
                *error = QStringLiteral("Animation dimensions exceed the preview limit");
            return {};
        }
    }

    const int maxWidth = std::clamp(
        request.value(QStringLiteral("maxWidth"))
            .toInt(PreviewProtocol::kDefaultAnimationFrameWidth),
        128,
        PreviewProtocol::kMaxAnimationFrameDimension);
    const int maxHeight = std::clamp(
        request.value(QStringLiteral("maxHeight"))
            .toInt(PreviewProtocol::kDefaultAnimationFrameHeight),
        128,
        PreviewProtocol::kMaxAnimationFrameDimension);

    return std::unique_ptr<AnimationSession>(new AnimationSession(
        std::move(bytes),
        token,
        frameCount,
        maxWidth,
        maxHeight));
}

QJsonObject AnimationSession::readNext(QString* error) {
    if (!m_reader || m_nextFrame < 0 || m_nextFrame >= m_frameCount) {
        if (error)
            *error = QStringLiteral("Animation has no more frames");
        return {};
    }

    QImage frame = m_reader->read();
    if (frame.isNull()) {
        if (error)
            *error = m_reader->errorString().isEmpty()
                ? QStringLiteral("Could not decode animation frame")
                : m_reader->errorString();
        return {};
    }

    if (frame.width() > PreviewProtocol::kMaxAnimationSourceDimension
        || frame.height() > PreviewProtocol::kMaxAnimationSourceDimension
        || frame.sizeInBytes() > PreviewProtocol::kMaxPublishedImageBytes) {
        if (error)
            *error = QStringLiteral("Animation frame exceeded the decoded-image limit");
        return {};
    }

    if (frame.width() > m_maxWidth || frame.height() > m_maxHeight) {
        frame = frame.scaled(
            m_maxWidth,
            m_maxHeight,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    QByteArray png = encodePngBounded(&frame, error);
    if (png.isEmpty())
        return {};

    int delayMs = m_reader->nextImageDelay();
    if (delayMs <= 0)
        delayMs = 100;
    delayMs = std::clamp(
        delayMs,
        PreviewProtocol::kMinAnimationDelayMs,
        PreviewProtocol::kMaxAnimationDelayMs);

    QJsonObject payload;
    payload.insert(QStringLiteral("session"), m_token);
    payload.insert(QStringLiteral("frame"), m_nextFrame);
    payload.insert(QStringLiteral("frameCount"), m_frameCount);
    payload.insert(QStringLiteral("delayMs"), delayMs);
    payload.insert(QStringLiteral("pixelWidth"), frame.width());
    payload.insert(QStringLiteral("pixelHeight"), frame.height());
    payload.insert(QStringLiteral("imageFormat"), QStringLiteral("png"));
    payload.insert(
        QStringLiteral("imageBase64"),
        QString::fromLatin1(png.toBase64()));

    ++m_nextFrame;
    return payload;
}

} // namespace ImageProbe
