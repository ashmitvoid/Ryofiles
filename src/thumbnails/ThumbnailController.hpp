// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>

class ThumbnailController final : public QObject {
    Q_OBJECT

public:
    explicit ThumbnailController(QObject* parent = nullptr);

    Q_INVOKABLE bool isCandidate(const QString& path) const;
    Q_INVOKABLE QString urlForPath(
        const QString& path,
        int targetPixels,
        int priority = 0) const;
};
