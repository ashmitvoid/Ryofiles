// SPDX-License-Identifier: GPL-3.0-only

#include "ClipboardController.hpp"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>

namespace {
constexpr auto kGnomeCopiedFiles = "x-special/gnome-copied-files";
constexpr auto kKdeCutSelection = "application/x-kde-cutselection";
constexpr auto kRyofilesCut = "application/x-ryofiles-cut";
}

ClipboardController::ClipboardController(QObject* parent)
    : QObject(parent) {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::changed, this, [this] {
            emit changed();
        });
    }
}

QStringList ClipboardController::normalizedPaths(const QStringList& paths) {
    QStringList result;
    result.reserve(paths.size());

    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!info.exists() && !info.isSymLink())
            continue;
        result.push_back(info.absoluteFilePath());
    }

    result.removeDuplicates();
    return result;
}

QStringList ClipboardController::pathsFromGnomePayload(const QByteArray& payload) {
    const QList<QByteArray> lines = payload.split('\n');
    QStringList paths;

    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;

        const QUrl url = QUrl::fromEncoded(line);
        if (url.isLocalFile())
            paths.push_back(url.toLocalFile());
    }

    paths.removeDuplicates();
    return paths;
}

bool ClipboardController::gnomePayloadIsCut(const QByteArray& payload) {
    return payload.split('\n').value(0).trimmed() == QByteArrayLiteral("cut");
}

QStringList ClipboardController::filePaths() const {
    const auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return {};

    const QMimeData* mime = clipboard->mimeData();
    if (!mime)
        return {};

    if (mime->hasFormat(QString::fromLatin1(kGnomeCopiedFiles))) {
        const QStringList paths =
            pathsFromGnomePayload(mime->data(QString::fromLatin1(kGnomeCopiedFiles)));
        if (!paths.isEmpty())
            return paths;
    }

    QStringList paths;
    for (const QUrl& url : mime->urls()) {
        if (url.isLocalFile())
            paths.push_back(url.toLocalFile());
    }
    paths.removeDuplicates();
    return paths;
}

bool ClipboardController::hasFiles() const {
    return !filePaths().isEmpty();
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
        if (!pathsFromGnomePayload(payload).isEmpty())
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

void ClipboardController::setFiles(
    const QStringList& requestedPaths,
    bool cutValue) {
    const QStringList paths = normalizedPaths(requestedPaths);
    if (paths.isEmpty())
        return;

    QList<QUrl> urls;
    urls.reserve(paths.size());

    QByteArray gnomePayload = cutValue
        ? QByteArrayLiteral("cut\n")
        : QByteArrayLiteral("copy\n");

    for (const QString& path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.push_back(url);
        gnomePayload += url.toEncoded();
        gnomePayload += '\n';
    }

    auto* mime = new QMimeData;
    mime->setUrls(urls);
    mime->setData(QString::fromLatin1(kGnomeCopiedFiles), gnomePayload);
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
    setFiles(paths, false);
}

void ClipboardController::cutFiles(const QStringList& paths) {
    setFiles(paths, true);
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

void ClipboardController::clearIfMatches(
    const QStringList& paths,
    bool cutExpected) {
    if (matchesFiles(paths, cutExpected))
        clear();
}

void ClipboardController::clear() {
    if (auto* clipboard = QGuiApplication::clipboard())
        clipboard->clear();
}
