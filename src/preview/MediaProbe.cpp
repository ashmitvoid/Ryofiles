// SPDX-License-Identifier: GPL-3.0-only

#include "MediaProbe.hpp"
#include "PreviewProtocol.hpp"

#include <QBuffer>
#include <QImage>
#include <QJsonObject>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

namespace {

struct AvioState {
    QFile* file = nullptr;
    qint64 bytesRead = 0;
    qint64 readBudget = PreviewProtocol::kMaxMediaReadBytes;
};

struct FormatSession {
    AVFormatContext* format = nullptr;
    AVIOContext* avio = nullptr;
    bool opened = false;

    ~FormatSession() {
        if (opened) {
            avformat_close_input(&format);
        } else if (format) {
            avformat_free_context(format);
            format = nullptr;
        }

        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
    }
};

QString avErrorString(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(code, buffer, sizeof(buffer)) < 0)
        return QStringLiteral("FFmpeg error %1").arg(code);
    return QString::fromLocal8Bit(buffer);
}

QString boundedString(const char* value) {
    if (!value)
        return {};
    QString text = QString::fromUtf8(value);
    if (text.size() > PreviewProtocol::kMaxMediaMetadataChars)
        text.truncate(PreviewProtocol::kMaxMediaMetadataChars);
    return text;
}

QString dictionaryValue(AVDictionary* dictionary, const char* key) {
    if (!dictionary || !key)
        return {};
    const AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0);
    return entry ? boundedString(entry->value) : QString{};
}

QString codecName(AVCodecID codecId) {
    const char* name = avcodec_get_name(codecId);
    return boundedString(name);
}

int readPacket(void* opaque, uint8_t* buffer, int bufferSize) {
    auto* state = static_cast<AvioState*>(opaque);
    if (!state || !state->file || bufferSize <= 0)
        return AVERROR(EINVAL);

    const qint64 remaining = state->readBudget - state->bytesRead;
    if (remaining <= 0)
        return AVERROR_EOF;

    const int requested = static_cast<int>(std::min<qint64>(bufferSize, remaining));
    const qint64 read = state->file->read(reinterpret_cast<char*>(buffer), requested);
    if (read > 0) {
        state->bytesRead += read;
        return static_cast<int>(read);
    }
    if (state->file->atEnd())
        return AVERROR_EOF;
    return AVERROR(EIO);
}

int64_t seekPacket(void* opaque, int64_t offset, int whence) {
    auto* state = static_cast<AvioState*>(opaque);
    if (!state || !state->file)
        return AVERROR(EINVAL);

    if (whence & AVSEEK_SIZE)
        return state->file->size();

    const int origin = whence & ~AVSEEK_FORCE;
    qint64 target = -1;
    if (origin == SEEK_SET) {
        target = offset;
    } else if (origin == SEEK_CUR) {
        if (offset > 0 && state->file->pos() > std::numeric_limits<qint64>::max() - offset)
            return AVERROR(EOVERFLOW);
        target = state->file->pos() + offset;
    } else if (origin == SEEK_END) {
        if (offset > 0 && state->file->size() > std::numeric_limits<qint64>::max() - offset)
            return AVERROR(EOVERFLOW);
        target = state->file->size() + offset;
    } else {
        return AVERROR(EINVAL);
    }

    if (target < 0 || !state->file->seek(target))
        return AVERROR(EINVAL);
    return target;
}

bool openFormatSession(AvioState& state, FormatSession* session, QString* error) {
    if (!session || !state.file)
        return false;
    if (!state.file->seek(0)) {
        if (error)
            *error = QStringLiteral("Could not seek media preview input");
        return false;
    }

    unsigned char* buffer = static_cast<unsigned char*>(
        av_malloc(PreviewProtocol::kMediaAvioBufferBytes));
    if (!buffer) {
        if (error)
            *error = QStringLiteral("Could not allocate media preview buffer");
        return false;
    }

    session->avio = avio_alloc_context(
        buffer,
        PreviewProtocol::kMediaAvioBufferBytes,
        0,
        &state,
        &readPacket,
        nullptr,
        &seekPacket);
    if (!session->avio) {
        av_free(buffer);
        if (error)
            *error = QStringLiteral("Could not allocate media preview I/O");
        return false;
    }
    session->avio->seekable = AVIO_SEEKABLE_NORMAL;

    session->format = avformat_alloc_context();
    if (!session->format) {
        if (error)
            *error = QStringLiteral("Could not allocate media preview context");
        return false;
    }

    session->format->pb = session->avio;
    session->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    session->format->probesize = PreviewProtocol::kMaxMediaProbeBytes;
    session->format->max_analyze_duration = PreviewProtocol::kMaxMediaAnalyzeUs;

    int result = avformat_open_input(&session->format, nullptr, nullptr, nullptr);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Could not read media container: %1").arg(avErrorString(result));
        return false;
    }
    session->opened = true;

    result = avformat_find_stream_info(session->format, nullptr);
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Could not inspect media streams: %1").arg(avErrorString(result));
        return false;
    }
    return true;
}

void appendCommonMetadata(AVFormatContext* format, QJsonObject* payload) {
    if (!format || !payload)
        return;

    const char* formatName = nullptr;
    if (format->iformat) {
        formatName = format->iformat->long_name
            ? format->iformat->long_name
            : format->iformat->name;
    }
    payload->insert(QStringLiteral("format"), boundedString(formatName));
    payload->insert(QStringLiteral("title"), dictionaryValue(format->metadata, "title"));
    payload->insert(QStringLiteral("artist"), dictionaryValue(format->metadata, "artist"));
    payload->insert(QStringLiteral("album"), dictionaryValue(format->metadata, "album"));
    payload->insert(QStringLiteral("albumArtist"), dictionaryValue(format->metadata, "album_artist"));
    payload->insert(QStringLiteral("genre"), dictionaryValue(format->metadata, "genre"));
    payload->insert(QStringLiteral("date"), dictionaryValue(format->metadata, "date"));

    if (format->duration != AV_NOPTS_VALUE && format->duration >= 0) {
        const qint64 durationMs = av_rescale_q(
            format->duration,
            AV_TIME_BASE_Q,
            AVRational{1, 1000});
        payload->insert(QStringLiteral("durationMs"), static_cast<double>(durationMs));
    } else {
        payload->insert(QStringLiteral("durationMs"), 0);
    }

    payload->insert(
        QStringLiteral("bitRate"),
        format->bit_rate > 0 ? static_cast<double>(format->bit_rate) : 0.0);
}

void appendAudioMetadata(AVFormatContext* format, int streamIndex, QJsonObject* payload) {
    if (!format || !payload || streamIndex < 0 || streamIndex >= static_cast<int>(format->nb_streams))
        return;

    const AVCodecParameters* parameters = format->streams[streamIndex]->codecpar;
    if (!parameters)
        return;

    payload->insert(QStringLiteral("audioCodec"), codecName(parameters->codec_id));
    payload->insert(QStringLiteral("sampleRate"), parameters->sample_rate);
    payload->insert(QStringLiteral("channels"), parameters->ch_layout.nb_channels);
    payload->insert(
        QStringLiteral("audioBitRate"),
        parameters->bit_rate > 0 ? static_cast<double>(parameters->bit_rate) : 0.0);

    char layout[128] = {};
    if (parameters->ch_layout.nb_channels > 0
        && av_channel_layout_describe(&parameters->ch_layout, layout, sizeof(layout)) >= 0) {
        payload->insert(QStringLiteral("channelLayout"), boundedString(layout));
    }

    if (parameters->format >= 0) {
        payload->insert(
            QStringLiteral("sampleFormat"),
            boundedString(av_get_sample_fmt_name(static_cast<AVSampleFormat>(parameters->format))));
    }
}

void appendVideoMetadata(AVFormatContext* format, int streamIndex, QJsonObject* payload) {
    if (!format || !payload || streamIndex < 0 || streamIndex >= static_cast<int>(format->nb_streams))
        return;

    const AVStream* stream = format->streams[streamIndex];
    const AVCodecParameters* parameters = stream->codecpar;
    if (!parameters)
        return;

    payload->insert(QStringLiteral("videoCodec"), codecName(parameters->codec_id));
    payload->insert(QStringLiteral("videoWidth"), parameters->width);
    payload->insert(QStringLiteral("videoHeight"), parameters->height);
    payload->insert(
        QStringLiteral("videoBitRate"),
        parameters->bit_rate > 0 ? static_cast<double>(parameters->bit_rate) : 0.0);

    if (parameters->format >= 0) {
        payload->insert(
            QStringLiteral("pixelFormat"),
            boundedString(av_get_pix_fmt_name(static_cast<AVPixelFormat>(parameters->format))));
    }

    const AVRational rate = av_guess_frame_rate(format, const_cast<AVStream*>(stream), nullptr);
    if (rate.num > 0 && rate.den > 0) {
        payload->insert(QStringLiteral("frameRate"), av_q2d(rate));
        payload->insert(QStringLiteral("frameRateNumerator"), rate.num);
        payload->insert(QStringLiteral("frameRateDenominator"), rate.den);
    } else {
        payload->insert(QStringLiteral("frameRate"), 0.0);
    }
}

QByteArray encodePoster(QImage* image, QString* error) {
    if (!image || image->isNull()) {
        if (error)
            *error = QStringLiteral("Media poster did not produce an image");
        return {};
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image->save(&buffer, "PNG")) {
            if (error)
                *error = QStringLiteral("Could not encode media poster");
            return {};
        }
        if (encoded.size() <= PreviewProtocol::kMaxEncodedImageBytes)
            return encoded;

        if (image->width() <= 320 || image->height() <= 320)
            break;
        *image = image->scaled(
            std::max(320, qRound(image->width() * 0.80)),
            std::max(320, qRound(image->height() * 0.80)),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    if (error)
        *error = QStringLiteral("Media poster exceeded the output limit");
    return {};
}

bool frameDimensionsAllowed(int width, int height) {
    if (width <= 0 || height <= 0)
        return false;
    if (width > PreviewProtocol::kMaxMediaSourceDimension
        || height > PreviewProtocol::kMaxMediaSourceDimension) {
        return false;
    }
    return static_cast<qint64>(width) * static_cast<qint64>(height)
        <= PreviewProtocol::kMaxMediaSourcePixels;
}

QImage convertFrameToImage(const AVFrame* frame, int maxWidth, int maxHeight, QString* error) {
    if (!frame || !frameDimensionsAllowed(frame->width, frame->height)) {
        if (error)
            *error = QStringLiteral("Media frame dimensions exceed the preview limit");
        return {};
    }

    const double scale = std::min({
        1.0,
        static_cast<double>(maxWidth) / frame->width,
        static_cast<double>(maxHeight) / frame->height,
    });
    const int outputWidth = std::max(1, qRound(frame->width * scale));
    const int outputHeight = std::max(1, qRound(frame->height * scale));

    QImage image(outputWidth, outputHeight, QImage::Format_RGBA8888);
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("Could not allocate media poster image");
        return {};
    }

    SwsContext* scaleContext = sws_getContext(
        frame->width,
        frame->height,
        static_cast<AVPixelFormat>(frame->format),
        outputWidth,
        outputHeight,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (!scaleContext) {
        if (error)
            *error = QStringLiteral("Could not create media poster converter");
        return {};
    }

    const uint8_t* sourceData[4] = {
        frame->data[0], frame->data[1], frame->data[2], frame->data[3]
    };
    const int sourceLines[4] = {
        frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3]
    };
    uint8_t* destinationData[4] = {image.bits(), nullptr, nullptr, nullptr};
    const int destinationLines[4] = {image.bytesPerLine(), 0, 0, 0};

    const int scaledRows = sws_scale(
        scaleContext,
        sourceData,
        sourceLines,
        0,
        frame->height,
        destinationData,
        destinationLines);
    sws_freeContext(scaleContext);

    if (scaledRows != outputHeight) {
        if (error)
            *error = QStringLiteral("Could not convert media poster frame");
        return {};
    }
    if (image.sizeInBytes() > PreviewProtocol::kMaxPublishedImageBytes) {
        if (error)
            *error = QStringLiteral("Media poster exceeded the decoded-image limit");
        return {};
    }
    return image;
}

QJsonObject renderPoster(
    AVFormatContext* format,
    int videoIndex,
    int maxWidth,
    int maxHeight,
    QString* error) {
    if (!format || videoIndex < 0 || videoIndex >= static_cast<int>(format->nb_streams))
        return {};

    AVStream* stream = format->streams[videoIndex];
    AVCodecParameters* parameters = stream->codecpar;
    if (!parameters || !frameDimensionsAllowed(parameters->width, parameters->height)) {
        if (error)
            *error = QStringLiteral("Video dimensions exceed the preview limit");
        return {};
    }

    const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
    if (!decoder) {
        if (error)
            *error = QStringLiteral("No decoder is available for this video");
        return {};
    }

    AVCodecContext* decoderContext = avcodec_alloc_context3(decoder);
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!decoderContext || !packet || !frame) {
        avcodec_free_context(&decoderContext);
        av_packet_free(&packet);
        av_frame_free(&frame);
        if (error)
            *error = QStringLiteral("Could not allocate media decoder state");
        return {};
    }

    int result = avcodec_parameters_to_context(decoderContext, parameters);
    if (result >= 0) {
        decoderContext->thread_count = 1;
        result = avcodec_open2(decoderContext, decoder, nullptr);
    }
    if (result < 0) {
        if (error)
            *error = QStringLiteral("Could not open media decoder: %1").arg(avErrorString(result));
        avcodec_free_context(&decoderContext);
        av_packet_free(&packet);
        av_frame_free(&frame);
        return {};
    }

    bool gotFrame = false;
    int packetsRead = 0;
    while (packetsRead < PreviewProtocol::kMaxMediaPackets) {
        result = av_read_frame(format, packet);
        if (result < 0)
            break;
        ++packetsRead;

        if (packet->stream_index != videoIndex) {
            av_packet_unref(packet);
            continue;
        }

        result = avcodec_send_packet(decoderContext, packet);
        av_packet_unref(packet);
        if (result < 0 && result != AVERROR(EAGAIN))
            break;

        while (true) {
            result = avcodec_receive_frame(decoderContext, frame);
            if (result == 0) {
                gotFrame = true;
                break;
            }
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                break;
            break;
        }
        if (gotFrame)
            break;
    }

    if (!gotFrame) {
        avcodec_send_packet(decoderContext, nullptr);
        if (avcodec_receive_frame(decoderContext, frame) == 0)
            gotFrame = true;
    }

    QJsonObject poster;
    if (gotFrame) {
        QImage image = convertFrameToImage(frame, maxWidth, maxHeight, error);
        if (!image.isNull()) {
            QByteArray png = encodePoster(&image, error);
            if (!png.isEmpty()) {
                poster.insert(QStringLiteral("posterFormat"), QStringLiteral("png"));
                poster.insert(QStringLiteral("posterBase64"), QString::fromLatin1(png.toBase64()));
                poster.insert(QStringLiteral("posterWidth"), image.width());
                poster.insert(QStringLiteral("posterHeight"), image.height());
            }
        }
    } else if (error) {
        *error = QStringLiteral("Could not decode a bounded video poster frame");
    }

    avcodec_free_context(&decoderContext);
    av_packet_free(&packet);
    av_frame_free(&frame);
    return poster;
}

} // namespace

namespace MediaProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error) {
    if (fileSize <= 0 || fileSize > PreviewProtocol::kMaxMediaInputBytes) {
        if (error)
            *error = QStringLiteral("Media file exceeds the preview input limit");
        return {};
    }

    av_log_set_level(AV_LOG_ERROR);

    AvioState ioState;
    ioState.file = &file;

    FormatSession session;
    if (!openFormatSession(ioState, &session, error))
        return {};

    AVFormatContext* format = session.format;
    const int audioIndex = av_find_best_stream(
        format,
        AVMEDIA_TYPE_AUDIO,
        -1,
        -1,
        nullptr,
        0);
    const int videoIndex = av_find_best_stream(
        format,
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        nullptr,
        0);

    if (audioIndex < 0 && videoIndex < 0) {
        if (error)
            *error = QStringLiteral("No audio or video stream was found");
        return {};
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("hasAudio"), audioIndex >= 0);
    payload.insert(QStringLiteral("hasVideo"), videoIndex >= 0);
    payload.insert(
        QStringLiteral("mediaKind"),
        videoIndex >= 0 ? QStringLiteral("video") : QStringLiteral("audio"));
    payload.insert(QStringLiteral("fileSize"), QString::number(fileSize));
    appendCommonMetadata(format, &payload);
    if (audioIndex >= 0)
        appendAudioMetadata(format, audioIndex, &payload);
    if (videoIndex >= 0)
        appendVideoMetadata(format, videoIndex, &payload);

    const bool wantsPoster = request.value(QStringLiteral("poster")).toBool(true);
    if (wantsPoster && videoIndex >= 0) {
        const int maxWidth = std::clamp(
            request.value(QStringLiteral("maxWidth")).toInt(PreviewProtocol::kDefaultMediaPosterWidth),
            64,
            PreviewProtocol::kMaxMediaPosterDimension);
        const int maxHeight = std::clamp(
            request.value(QStringLiteral("maxHeight")).toInt(PreviewProtocol::kDefaultMediaPosterHeight),
            64,
            PreviewProtocol::kMaxMediaPosterDimension);
        QJsonObject poster = renderPoster(format, videoIndex, maxWidth, maxHeight, error);
        if (poster.isEmpty())
            return {};
        for (auto iterator = poster.begin(); iterator != poster.end(); ++iterator)
            payload.insert(iterator.key(), iterator.value());
    }

    payload.insert(QStringLiteral("bytesRead"), QString::number(ioState.bytesRead));
    return payload;
}

} // namespace MediaProbe
