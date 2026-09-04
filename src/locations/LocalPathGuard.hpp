// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace LocalPathGuard {

inline bool isUriLike(const QString& value) {
    const QString text = value.trimmed();
    const qsizetype separator = text.indexOf(QStringLiteral("://"));
    if (separator <= 0)
        return false;

    if (!text.at(0).isLetter())
        return false;

    for (qsizetype i = 1; i < separator; ++i) {
        const QChar ch = text.at(i);
        if (!ch.isLetterOrNumber()
            && ch != QLatin1Char('+')
            && ch != QLatin1Char('-')
            && ch != QLatin1Char('.')) {
            return false;
        }
    }

    return true;
}

inline bool allLocalPaths(const QStringList& values) {
    for (const QString& value : values) {
        if (isUriLike(value))
            return false;
    }
    return true;
}

} // namespace LocalPathGuard
