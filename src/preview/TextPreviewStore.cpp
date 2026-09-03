// SPDX-License-Identifier: GPL-3.0-only

#include "TextPreviewStore.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>

namespace {

constexpr qint64 kMaxPreviewBytes = 192 * 1024;
constexpr int kMaxPreviewLines = 2500;

QString lowerName(const QString& path) {
    return QFileInfo(path).fileName().toLower();
}

QString lowerSuffix(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

bool hasBinaryNull(const QByteArray& data) {
    return data.contains('\0');
}

bool truncateLines(QString* text) {
    if (!text)
        return false;

    int lines = 1;
    for (qsizetype i = 0; i < text->size(); ++i) {
        if (text->at(i) != QLatin1Char('\n'))
            continue;

        ++lines;
        if (lines <= kMaxPreviewLines)
            continue;

        text->truncate(i);
        return true;
    }

    return false;
}

} // namespace

bool TextPreviewStore::isCandidatePath(const QString& path) {
    const QString suffix = lowerSuffix(path);
    const QString name = lowerName(path);

    static const QSet<QString> suffixes = {
        QStringLiteral("txt"),
        QStringLiteral("md"),
        QStringLiteral("markdown"),
        QStringLiteral("log"),
        QStringLiteral("json"),
        QStringLiteral("jsonc"),
        QStringLiteral("yaml"),
        QStringLiteral("yml"),
        QStringLiteral("toml"),
        QStringLiteral("ini"),
        QStringLiteral("conf"),
        QStringLiteral("cfg"),
        QStringLiteral("xml"),
        QStringLiteral("html"),
        QStringLiteral("htm"),
        QStringLiteral("css"),
        QStringLiteral("scss"),
        QStringLiteral("sass"),
        QStringLiteral("less"),
        QStringLiteral("js"),
        QStringLiteral("mjs"),
        QStringLiteral("cjs"),
        QStringLiteral("ts"),
        QStringLiteral("tsx"),
        QStringLiteral("jsx"),
        QStringLiteral("vue"),
        QStringLiteral("svelte"),
        QStringLiteral("py"),
        QStringLiteral("pyi"),
        QStringLiteral("c"),
        QStringLiteral("cc"),
        QStringLiteral("cpp"),
        QStringLiteral("cxx"),
        QStringLiteral("h"),
        QStringLiteral("hh"),
        QStringLiteral("hpp"),
        QStringLiteral("hxx"),
        QStringLiteral("rs"),
        QStringLiteral("go"),
        QStringLiteral("java"),
        QStringLiteral("kt"),
        QStringLiteral("kts"),
        QStringLiteral("swift"),
        QStringLiteral("sh"),
        QStringLiteral("bash"),
        QStringLiteral("zsh"),
        QStringLiteral("fish"),
        QStringLiteral("sql"),
        QStringLiteral("csv"),
        QStringLiteral("tsv"),
        QStringLiteral("properties"),
        QStringLiteral("desktop"),
        QStringLiteral("service"),
        QStringLiteral("socket"),
        QStringLiteral("timer"),
        QStringLiteral("target"),
        QStringLiteral("mount"),
        QStringLiteral("path"),
        QStringLiteral("cmake"),
        QStringLiteral("gradle"),
        QStringLiteral("nix"),
        QStringLiteral("lua"),
        QStringLiteral("qml"),
    };

    static const QSet<QString> names = {
        QStringLiteral("dockerfile"),
        QStringLiteral("makefile"),
        QStringLiteral("justfile"),
        QStringLiteral("meson.build"),
        QStringLiteral("cmakelists.txt"),
        QStringLiteral("pkgbuild"),
        QStringLiteral(".gitignore"),
        QStringLiteral(".gitattributes"),
        QStringLiteral(".gitmodules"),
        QStringLiteral(".editorconfig"),
        QStringLiteral(".env"),
        QStringLiteral(".npmrc"),
        QStringLiteral(".prettierrc"),
    };

    return suffixes.contains(suffix) || names.contains(name);
}

TextPreviewResult TextPreviewStore::load(
    const QString& path,
    const std::atomic_bool& cancelled) {
    TextPreviewResult result;

    if (cancelled.load(std::memory_order_relaxed))
        return result;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.isSymLink())
        return result;
    if (!isCandidatePath(path))
        return result;

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }

    if (cancelled.load(std::memory_order_relaxed))
        return result;

    QByteArray data = file.read(kMaxPreviewBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        result.error = file.errorString();
        return result;
    }

    if (cancelled.load(std::memory_order_relaxed))
        return result;

    if (hasBinaryNull(data))
        return result;

    if (data.size() > kMaxPreviewBytes) {
        data.truncate(kMaxPreviewBytes);
        result.truncated = true;
    }
    if (info.size() > kMaxPreviewBytes)
        result.truncated = true;

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(data);
    if (decoder.hasError())
        return TextPreviewResult{};

    if (!text.isEmpty() && text.front().unicode() == 0xFEFF)
        text.remove(0, 1);

    if (truncateLines(&text))
        result.truncated = true;

    if (cancelled.load(std::memory_order_relaxed))
        return TextPreviewResult{};

    result.supported = true;
    result.text = std::move(text);
    return result;
}
