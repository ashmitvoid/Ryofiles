// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QFile>
#include <QJsonObject>
#include <QString>

extern "C" {
#include <libavutil/mathematics.h>
#include <libavutil/rational.h>
}

namespace MediaProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error);

} // namespace MediaProbe
