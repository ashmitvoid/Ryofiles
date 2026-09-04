// SPDX-License-Identifier: GPL-3.0-only

#include "ClipboardController.hpp"

#include "locations/LocationSpec.hpp"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace {
constexpr auto kGnomeCopiedFiles = "x-special/gnome-copied-files";
constexpr auto kKdeCutSelection = "application/x-kde-cutselection";
constexpr auto kRyofilesCut = "application/x-ryofiles-cut";
constexpr auto kRyofilesLocations = "application/x-ryofiles-locations";
}

ClipboardController::ClipboardController(QObject* parent)
    : QObject(parent) {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::changed, this, [this] {
            emit changed();
        });
    }
}

QStringList ClipboardController::normalizedLocations(const QStringList& requestedLocations) {
    QStringList result;
    result.reserve(requestedLocations.size());

    for (const QString& requested : requestedLocations) {
        const LocationSpec spec = LocationSpec::parse(requested);
        if (!spec.isValid())
            continue;

        if (spec.isLocal()) {
            const QFileInfo info(spec.localPath);
            if (!info.exists() && !info.isSymLink())
                continue;
            result.push_back(info.absoluteFilePath());
        } else if (spec.isNetwork()) {
            result.push_back(spec.canonical);
        }
    }

    result.removeDuplicates();
    return result;
}

QStringList ClipboardController::normalizedPaths(const QStringList& paths) {
    QStringList result;
    const QStringList locations = normalizedLocations(paths);
    result.reserve(locations.size());

    for (const QString& location : locations) {
        const LocationSpec spec = LocationSpec::parse(location);
        if (spec.isLocal())
            result.push_back(spec.localPath);
    }

    result.removeDuplicates();
    return result;
}

QStringList ClipboardController::locationsFromGnomePayload(const QByteArray& payload) {
    const QList<QByteArray> lines = payload.split('\n');
    QStringList locations;

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;

        const QUrl url = QUrl::fromEncoded(line, QUrl::StrictMode);
        if (!url.isValid())
            continue;

        const LocationSpec spec = LocationSpec::parse(url.toString(QUrl::FullyEncoded));
        if (!spec.isValid())
            continue;
        locations.push_back(spec.isNetwork() ? spec.canonical : spec.localPath);
    }

    return normalizedLocations(locations);
}

bool ClipboardController::gnomePayloadIsCut(const QByteArray& payload) {
    return payload.split('\n').value(0).trimmed() == QByteArrayLiteral("cut");
}

QStringList ClipboardController::locations() const {
    const auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData* mime = clipboard->mimeData();
    if (!mime)
        return {};

    if (mime->hasFormat(QString::fromLatin1(kRyofilesLocations))) {
        QStringList stored;
        const QList<QByteArray> lines =
            mime->data(QString::fromLatin1(kRyofilesLocations)).split('\n');
        for (const QByteArray& line : lines) {
            const QByteArray trimmed = line.trimmed();
            if (!trimmed.isEmpty())
                stored.push_back(QString::fromUtf8(trimmed));
        }
        const QStringList normalized = normalizedLocations(stored);
        if (!normalized.isEmpty())
            return normalized;
    }

    if (mime->hasFormat(QString::fromLatin1(kGnomeCopiedFiles))) {
        const QStringList parsed =
            locationsFromGnomePayload(mime->data(QString::fromLatin1(kGnomeCopiedFiles)));
        if (!parsed.isEmpty())
            return parsed;
    }

    QStringList result;
    for (const QUrl& url : mime->urls()) {
        if (!url.isValid())
            continue;
        const QString encoded = url.isLocalFile()
            ? QUrl::fromLocalFile(url.toLocalFile()).toString(QUrl::FullyEncoded)
            : url.toString(QUrl::FullyEncoded);
        const LocationSpec spec = LocationSpec::parse(encoded);
        if (spec.isValid())
            result.push_back(spec.isNetwork() ? spec.canonical : spec.localPath);
    }
    return normalizedLocations(result);
}

QStringList ClipboardController::filePaths() const {
    QStringList paths;
    const QStringList stored = locations();
    paths.reserve(stored.size());
    for (const QString& location : stored) {
        const LocationSpec spec = LocationSpec::parse(location);
        if (spec.isLocal())
            paths.push_back(spec.localPath);
    }
    return normalizedPaths(paths);
}

bool ClipboardController::hasFiles() const {
    return !filePaths().isEmpty();
}

bool ClipboardController::hasLocations() const {
    return !locations().isEmpty();
}

bool ClipboardController::isCut() const {
    const auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;

    const QMimeData* mime = clipboard->mimeData();
    if (!mime)
        return false;

    if (mime->hasFormat(QString::fromLatin1(kGnomeCopiedFiles))) {
        const QByteArray payload =
            mime->data(QString::fromLatin1(kGnomeCopiedFiles));
        if (!locationsFromGnomePayload(payload).isEmpty())
            return gnomePayloadIsCut(payload);
    }

    if (mime->hasFormat(QString::fromLatin1(kKdeCutSelection))) {
        return mime->data(QString::fromLatin1(kKdeCutSelection)).trimmed()
            == QByteArrayLiteral("1");
    }

    if (mime->hasFormat(QString::fromLatin1(kRyofilesCut))) {
        return mime->data(QString::fromLatin1(kRyofilesCut)).trimmed()
            == QByteArrayLiteral("1");
    }

    return false;
}

void ClipboardController::setLocations(
    const QStringList& requestedLocations,
    bool cutValue) {
    const QStringList stored = normalizedLocations(requestedLocations);
    if (stored.isEmpty())
        return;

    QList<QUrl> urls;
    urls.reserve(stored.size());

    QByteArray gnomePayload = cutValue
        ? QByteArrayLiteral("cut\n")
        : QByteArrayLiteral("copy\n");
    QByteArray ryofilesPayload;

    for (const QString& location : stored) {
        const LocationSpec spec = LocationSpec::parse(location);
        if (!spec.isValid())
            continue;

        const QUrl url = spec.isNetwork()
            ? QUrl(spec.canonical, QUrl::StrictMode)
            : QUrl::fromLocalFile(spec.localPath);
        if (!url.isValid())
            continue;

        urls.push_back(url);
        gnomePayload += url.toEncoded(QUrl::FullyEncoded);
        gnomePayload += '\n';
        ryofilesPayload += location.toUtf8();
        ryofilesPayload += '\n';
    }

    if (urls.isEmpty())
        return;

    auto* mime = new QMimeData;
    mime->setUrls(urls);
    mime->setData(QString::fromLatin1(kGnomeCopiedFiles), gnomePayload);
    mime->setData(QString::fromLatin1(kRyofilesLocations), ryofilesPayload);
    mime->setData(
        QString::fromLatin1(kKdeCutSelection),
        cutValue ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
    mime->setData(
        QString::fromLatin1(kRyofilesCut),
        cutValue ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));

    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->setMimeData(mime);
    else
        delete mime;
}

void ClipboardController::copyFiles(const QStringList& paths) {
    setLocations(paths, false);
}

void ClipboardController::cutFiles(const QStringList& paths) {
    setLocations(paths, true);
}

void ClipboardController::copyLocations(const QStringList& storedLocations) {
    setLocations(storedLocations, false);
}

void ClipboardController::cutLocations(const QStringList& storedLocations) {
    setLocations(storedLocations, true);
}

void ClipboardController::copyText(const QString& text) {
    if (text.isEmpty())
        return;
    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text, QClipboard::Clipboard);
}

bool ClipboardController::matchesFiles(
    const QStringList& requestedPaths,
    bool cutExpected) const {
    QStringList expected = normalizedPaths(requestedPaths);
    QStringList actual = normalizedPaths(filePaths());

    expected.sort();
    actual.sort();

    return !expected.isEmpty()
        && expected == actual
        && isCut() == cutExpected;
}

bool ClipboardController::matchesLocations(
    const QStringList& requestedLocations,
    bool cutExpected) const {
    QStringList expected = normalizedLocations(requestedLocations);
    QStringList actual = normalizedLocations(locations());

    expected.sort();
    actual.sort();

    return !expected.isEmpty()
        && expected == actual
        && isCut() == cutExpected;
}

void ClipboardController::clearIfMatches(
    const QStringList& paths,
    bool cutExpected) {
    if (matchesFiles(paths, cutExpected))
        clear();
}

void ClipboardController::clearIfMatchesLocations(
    const QStringList& storedLocations,
    bool cutExpected) {
    if (matchesLocations(storedLocations, cutExpected))
        clear();
}

void ClipboardController::clear() {
    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->clear();
}
