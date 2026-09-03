// SPDX-License-Identifier: GPL-3.0-only

#include "ThumbnailController.hpp"

#include "ThumbnailStore.hpp"

#include <QFileInfo>

ThumbnailController::ThumbnailController(QObject* parent)
    : QObject(parent) {
}

bool ThumbnailController::isCandidate(const QString& path) const {
    return ThumbnailStore::isCandidatePath(path);
}

QString ThumbnailController::urlForPath(
    const QString& path,
    int targetPixels,
    int priority) const {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink())
        return {};
    if (!ThumbnailStore::isCandidatePath(path))
        return {};

    const QByteArray encoded = info.absoluteFilePath().toUtf8().toBase64(
        QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    return QStringLiteral("image://ryofiles-thumb/%1?s=%2&p=%3&v=%4-%5")
        .arg(
            QString::fromLatin1(encoded),
            QString::number(targetPixels),
            QString::number(priority),
            QString::number(info.lastModified().toMSecsSinceEpoch()),
            QString::number(info.size()));
}
