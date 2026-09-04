// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QUrl>

struct LocationSpec {
    enum class Kind {
        Invalid,
        Local,
        Network,
    };

    Kind kind = Kind::Invalid;
    QString canonical;
    QString localPath;
    QString uri;
    QString scheme;
    QString host;
    QString userName;
    QString displayName;
    QString error;

    [[nodiscard]] bool isValid() const { return kind != Kind::Invalid; }
    [[nodiscard]] bool isLocal() const { return kind == Kind::Local; }
    [[nodiscard]] bool isNetwork() const { return kind == Kind::Network; }

    static LocationSpec parse(const QString& input);
    static bool isSupportedNetworkScheme(const QString& scheme);
};
