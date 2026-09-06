// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "PreviewFileAccess.hpp"

#include <QByteArray>
#include <QJsonObject>

#include <memory>

class QBuffer;
class QImageReader;

namespace ImageProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error);

class AnimationSession final {
public:
    static std::unique_ptr<AnimationSession> create(
        PreviewFileAccess::OpenedFile opened,
        const QJsonObject& request,
        QString* error);

    ~AnimationSession();

    QString token() const { return m_token; }
    QJsonObject readNext(QString* error);

private:
    AnimationSession(
        QByteArray bytes,
        QString token,
        int frameCount,
        int maxWidth,
        int maxHeight);

    QByteArray m_bytes;
    std::unique_ptr<QBuffer> m_buffer;
    std::unique_ptr<QImageReader> m_reader;
    QString m_token;
    int m_frameCount = 0;
    int m_nextFrame = 0;
    int m_maxWidth = 0;
    int m_maxHeight = 0;
};

} // namespace ImageProbe
