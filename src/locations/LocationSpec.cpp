// SPDX-License-Identifier: GPL-3.0-only

#include "locations/LocationSpec.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace {
LocationSpec invalidLocation(const QString& error) {
    LocationSpec spec;
    spec.error = error;
    return spec;
}

QString expandHomePath(const QString& input) {
    if (input == QStringLiteral("~"))
        return QDir::homePath();
    if (input.startsWith(QStringLiteral("~/")))
        return QDir::homePath() + input.mid(1);
    return input;
}

LocationSpec localLocation(const QString& rawPath) {
    QString path = expandHomePath(rawPath);
    if (path.startsWith(QLatin1Char('~')))
        return invalidLocation(QStringLiteral("Only the current user's home shortcut (~) is supported"));

    path = QDir::fromNativeSeparators(path);
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (absolute.isEmpty())
        return invalidLocation(QStringLiteral("Local path is empty"));

    LocationSpec spec;
    spec.kind = LocationSpec::Kind::Local;
    spec.localPath = absolute;
    spec.uri = QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded);
    spec.canonical = spec.uri;
    spec.scheme = QStringLiteral("file");

    const QFileInfo info(absolute);
    spec.displayName = info.fileName();
    if (spec.displayName.isEmpty())
        spec.displayName = absolute;
    return spec;
}

QString networkDisplayName(const QUrl& url) {
    QString authority;
    const QString user = url.userName(QUrl::FullyDecoded);
    if (!user.isEmpty())
        authority = user + QLatin1Char('@');
    authority += url.host();
    if (url.port() >= 0)
        authority += QLatin1Char(':') + QString::number(url.port());
    return authority;
}
} // namespace

bool LocationSpec::isSupportedNetworkScheme(const QString& inputScheme) {
    const QString scheme = inputScheme.trimmed().toLower();
    return scheme == QStringLiteral("sftp") ||
        scheme == QStringLiteral("smb") ||
        scheme == QStringLiteral("dav") ||
        scheme == QStringLiteral("davs") ||
        scheme == QStringLiteral("ftp");
}

LocationSpec LocationSpec::parse(const QString& input) {
    const QString text = input.trimmed();
    if (text.isEmpty())
        return invalidLocation(QStringLiteral("Location is empty"));

    // An explicit filesystem path is always local, even if a colon appears later in it.
    if (text.startsWith(QLatin1Char('/')) ||
        text.startsWith(QStringLiteral("./")) ||
        text.startsWith(QStringLiteral("../")) ||
        text == QStringLiteral(".") ||
        text == QStringLiteral("..") ||
        text.startsWith(QLatin1Char('~'))) {
        return localLocation(text);
    }

    QUrl url(text, QUrl::StrictMode);
    if (!url.isValid())
        return invalidLocation(QStringLiteral("Location URI is invalid"));

    if (url.scheme().isEmpty())
        return localLocation(text);

    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("file")) {
        if (!url.userName().isEmpty() || !url.password().isEmpty())
            return invalidLocation(QStringLiteral("Credentials are not valid in a local file URI"));
        if (!url.query().isEmpty() || !url.fragment().isEmpty())
            return invalidLocation(QStringLiteral("Local file URIs cannot contain a query or fragment"));

        const QString host = url.host().toLower();
        if (!host.isEmpty() && host != QStringLiteral("localhost"))
            return invalidLocation(QStringLiteral("Remote file:// authorities are not supported"));

        const QString localPath = url.toLocalFile();
        if (localPath.isEmpty())
            return invalidLocation(QStringLiteral("File URI does not contain a local path"));
        return localLocation(localPath);
    }

    if (!isSupportedNetworkScheme(scheme))
        return invalidLocation(QStringLiteral("Unsupported network protocol: %1").arg(scheme));

    if (url.host().isEmpty())
        return invalidLocation(QStringLiteral("Network location requires a host"));
    if (!url.password().isEmpty())
        return invalidLocation(QStringLiteral("Do not embed passwords in network locations"));
    if (!url.query().isEmpty() || !url.fragment().isEmpty())
        return invalidLocation(QStringLiteral("Network locations cannot contain a query or fragment"));

    url.setScheme(scheme);
    url.setPassword(QString());
    if (url.path().isEmpty())
        url.setPath(QStringLiteral("/"));
    url = url.adjusted(QUrl::NormalizePathSegments);

    LocationSpec spec;
    spec.kind = Kind::Network;
    spec.scheme = scheme;
    spec.host = url.host();
    spec.userName = url.userName(QUrl::FullyDecoded);
    spec.uri = url.toString(QUrl::FullyEncoded);
    spec.canonical = spec.uri;
    spec.displayName = networkDisplayName(url);
    return spec;
}
