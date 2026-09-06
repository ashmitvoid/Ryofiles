// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>

class QFile;
class QString;

namespace FontProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error);

} // namespace FontProbe
