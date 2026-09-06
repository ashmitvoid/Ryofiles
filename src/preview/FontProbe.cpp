// SPDX-License-Identifier: GPL-3.0-only

#include "FontProbe.hpp"
#include "PreviewProtocol.hpp"

#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QGlyphRun>
#include <QImage>
#include <QJsonArray>
#include <QPainter>
#include <QPointF>
#include <QRawFont>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {

struct WritingSystemProbe {
    const char* name;
    ushort representative;
};

QJsonArray detectedWritingSystems(const QRawFont& font) {
    static constexpr std::array<WritingSystemProbe, 31> probes {{
        {"Latin", 0x0041},
        {"Vietnamese", 0x0102},
        {"Greek", 0x0391},
        {"Cyrillic", 0x0410},
        {"Armenian", 0x0531},
        {"Hebrew", 0x05D0},
        {"Arabic", 0x0627},
        {"Syriac", 0x0710},
        {"Thaana", 0x0780},
        {"Nko", 0x07CA},
        {"Devanagari", 0x0915},
        {"Bengali", 0x0995},
        {"Gurmukhi", 0x0A15},
        {"Gujarati", 0x0A95},
        {"Odia", 0x0B15},
        {"Tamil", 0x0B95},
        {"Telugu", 0x0C15},
        {"Kannada", 0x0C95},
        {"Malayalam", 0x0D15},
        {"Sinhala", 0x0D9A},
        {"Thai", 0x0E01},
        {"Lao", 0x0E81},
        {"Tibetan", 0x0F40},
        {"Myanmar", 0x1000},
        {"Georgian", 0x10D0},
        {"Ogham", 0x1681},
        {"Runic", 0x16A0},
        {"Khmer", 0x1780},
        {"Japanese", 0x3042},
        {"CJK", 0x4E2D},
        {"Korean", 0xAC00},
    }};

    QJsonArray systems;
    for (const WritingSystemProbe& probe : probes) {
        if (!font.supportsCharacter(QChar(probe.representative)))
            continue;
        systems.append(QString::fromLatin1(probe.name));
        if (systems.size() >= PreviewProtocol::kMaxFontWritingSystems)
            break;
    }
    return systems;
}

QString styleLabel(QFont::Style style) {
    switch (style) {
    case QFont::StyleItalic: return QStringLiteral("Italic");
    case QFont::StyleOblique: return QStringLiteral("Oblique");
    case QFont::StyleNormal:
    default:
        return QStringLiteral("Normal");
    }
}

int visibleCharacterCount(const QString& text) {
    int count = 0;
    for (const QChar character : text) {
        if (!character.isSpace())
            ++count;
    }
    return count;
}

QString filterSupported(const QRawFont& font, const QString& source) {
    QString result;
    result.reserve(std::min<qsizetype>(
        source.size(),
        static_cast<qsizetype>(PreviewProtocol::kMaxFontSampleChars)));
    bool pendingSpace = false;
    for (const QChar character : source) {
        if (result.size() >= PreviewProtocol::kMaxFontSampleChars)
            break;
        if (character.isSpace()) {
            pendingSpace = !result.isEmpty();
            continue;
        }
        if (!font.supportsCharacter(character))
            continue;
        if (pendingSpace && !result.isEmpty())
            result.append(QChar::Space);
        pendingSpace = false;
        result.append(character);
    }
    return result.trimmed();
}

QString fallbackSample(const QRawFont& font) {
    static constexpr std::array<ushort, 40> candidates {
        0x0041, 0x0061, 0x0030, 0x0031,
        0x0391, 0x03B1, 0x03A9, 0x03C9,
        0x0410, 0x0430, 0x0416, 0x0436,
        0x05D0, 0x05D1, 0x05E9, 0x05EA,
        0x0627, 0x0628, 0x0645, 0x0646,
        0x0915, 0x0928, 0x092E, 0x0938,
        0x0E01, 0x0E17, 0x0E22, 0x0E44,
        0x3042, 0x304B, 0x3055, 0x306A,
        0x4E2D, 0x6587, 0x65E5, 0x672C,
        0xAC00, 0xB098, 0x2605, 0x2665,
    };

    QString result;
    int glyphs = 0;
    for (const ushort value : candidates) {
        const QChar character(value);
        if (!font.supportsCharacter(character))
            continue;
        if (!result.isEmpty())
            result.append(QChar::Space);
        result.append(character);
        if (++glyphs >= 12)
            break;
    }
    return result;
}

struct LineLayout {
    QString text;
    QList<quint32> glyphs;
    QList<QPointF> positions;
    qreal width = 0.0;
};

LineLayout layoutLine(const QRawFont& font, const QString& text) {
    LineLayout line;
    line.text = text;
    line.glyphs = font.glyphIndexesForString(text);
    const QList<QPointF> advances = font.advancesForGlyphIndexes(line.glyphs);
    if (line.glyphs.isEmpty() || advances.size() != line.glyphs.size()) {
        line.glyphs.clear();
        return line;
    }

    qreal x = 0.0;
    line.positions.reserve(line.glyphs.size());
    for (const QPointF& advance : advances) {
        line.positions.append(QPointF(x, 0.0));
        x += std::max<qreal>(0.0, advance.x());
    }
    line.width = x;
    return line;
}

QByteArray encodeFontPng(QImage* image, QString* error) {
    if (!image || image->isNull()) {
        if (error)
            *error = QStringLiteral("Font sample did not render an image");
        return {};
    }

    for (int attempt = 0; attempt < 8; ++attempt) {
        QByteArray encoded;
        QBuffer buffer(&encoded);
        if (!buffer.open(QIODevice::WriteOnly) || !image->save(&buffer, "PNG")) {
            if (error)
                *error = QStringLiteral("Could not encode font preview image");
            return {};
        }
        if (encoded.size() <= PreviewProtocol::kMaxEncodedImageBytes)
            return encoded;

        if (image->width() <= 320 || image->height() <= 180)
            break;
        *image = image->scaled(
            std::max(320, qRound(image->width() * 0.80)),
            std::max(180, qRound(image->height() * 0.80)),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
    }

    if (error)
        *error = QStringLiteral("Font preview image exceeded the output limit");
    return {};
}

} // namespace

namespace FontProbe {

QJsonObject probe(
    QFile& file,
    qint64 fileSize,
    const QJsonObject& request,
    QString* error) {
    if (fileSize <= 0 || fileSize > PreviewProtocol::kMaxFontInputBytes) {
        if (error)
            *error = QStringLiteral("Font exceeds the preview input limit");
        return {};
    }
    if (!file.seek(0)) {
        if (error)
            *error = QStringLiteral("Could not seek font preview input");
        return {};
    }

    QByteArray fontData = file.read(PreviewProtocol::kMaxFontInputBytes + 1);
    if (fontData.size() > PreviewProtocol::kMaxFontInputBytes) {
        if (error)
            *error = QStringLiteral("Font exceeds the preview input limit");
        return {};
    }
    if (static_cast<qint64>(fontData.size()) != fileSize) {
        if (error)
            *error = QStringLiteral("Could not read complete font preview input");
        return {};
    }

    const int requestedPixelSize = std::clamp(
        request.value(QStringLiteral("pixelSize")).toInt(PreviewProtocol::kDefaultFontPixelSize),
        24,
        PreviewProtocol::kMaxFontPixelSize);
    QRawFont rawFont(fontData, requestedPixelSize, QFont::PreferDefaultHinting);
    if (!rawFont.isValid()) {
        if (error)
            *error = QStringLiteral("Could not load TrueType/OpenType font");
        return {};
    }

    QString firstLine = filterSupported(rawFont, QStringLiteral("Aa Bb Cc 0123456789"));
    QString secondLine = filterSupported(rawFont, QStringLiteral("The quick brown fox"));
    if (visibleCharacterCount(firstLine) < 4)
        firstLine = fallbackSample(rawFont);
    if (visibleCharacterCount(firstLine) < 2) {
        if (error)
            *error = QStringLiteral("Font has no bounded preview sample characters");
        return {};
    }
    if (visibleCharacterCount(secondLine) < 6 || secondLine == firstLine)
        secondLine.clear();

    QStringList sampleLines {firstLine};
    if (!secondLine.isEmpty())
        sampleLines.append(secondLine);

    const int maxWidth = std::clamp(
        request.value(QStringLiteral("maxWidth")).toInt(PreviewProtocol::kDefaultFontRenderWidth),
        320,
        PreviewProtocol::kMaxFontRenderDimension);
    const int maxHeight = std::clamp(
        request.value(QStringLiteral("maxHeight")).toInt(PreviewProtocol::kDefaultFontRenderHeight),
        180,
        PreviewProtocol::kMaxFontRenderDimension);
    constexpr qreal padding = 28.0;

    auto buildLayouts = [&rawFont, &sampleLines]() {
        QList<LineLayout> layouts;
        for (const QString& text : sampleLines) {
            LineLayout line = layoutLine(rawFont, text);
            if (!line.glyphs.isEmpty())
                layouts.append(std::move(line));
        }
        return layouts;
    };

    QList<LineLayout> layouts = buildLayouts();
    if (layouts.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not map font sample glyphs");
        return {};
    }

    auto maxLineWidth = [&layouts]() {
        qreal width = 0.0;
        for (const LineLayout& line : layouts)
            width = std::max(width, line.width);
        return width;
    };
    auto totalLineHeight = [&rawFont, &layouts]() {
        const qreal lineHeight = std::max<qreal>(
            1.0,
            rawFont.ascent() + rawFont.descent() + std::max<qreal>(0.0, rawFont.leading()));
        const qreal gap = layouts.size() > 1 ? rawFont.pixelSize() * 0.28 : 0.0;
        const qreal gapCount = layouts.size() > 1
            ? static_cast<qreal>(layouts.size() - 1)
            : 0.0;
        return lineHeight * static_cast<qreal>(layouts.size()) + gap * gapCount;
    };

    const qreal availableWidth = std::max<qreal>(1.0, maxWidth - 2.0 * padding);
    const qreal availableHeight = std::max<qreal>(1.0, maxHeight - 2.0 * padding);
    const qreal widthScale = maxLineWidth() > 0.0 ? availableWidth / maxLineWidth() : 1.0;
    const qreal heightScale = totalLineHeight() > 0.0 ? availableHeight / totalLineHeight() : 1.0;
    const qreal fitScale = std::min({1.0, widthScale, heightScale});
    if (fitScale < 0.999) {
        rawFont.setPixelSize(std::max<qreal>(16.0, rawFont.pixelSize() * fitScale));
        layouts = buildLayouts();
    }

    QImage image(maxWidth, maxHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()
        || image.width() > PreviewProtocol::kMaxPublishedImageDimension
        || image.height() > PreviewProtocol::kMaxPublishedImageDimension
        || image.sizeInBytes() > PreviewProtocol::kMaxPublishedImageBytes) {
        if (error)
            *error = QStringLiteral("Font preview exceeded the decoded-image limit");
        return {};
    }
    image.fill(QColor(248, 248, 246));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(28, 30, 33));

    const qreal lineHeight = std::max<qreal>(
        1.0,
        rawFont.ascent() + rawFont.descent() + std::max<qreal>(0.0, rawFont.leading()));
    const qreal gap = layouts.size() > 1 ? rawFont.pixelSize() * 0.28 : 0.0;
    const qreal gapCount = layouts.size() > 1
        ? static_cast<qreal>(layouts.size() - 1)
        : 0.0;
    const qreal totalHeight = lineHeight * static_cast<qreal>(layouts.size()) + gap * gapCount;
    qreal baseline = (image.height() - totalHeight) / 2.0 + rawFont.ascent();

    for (const LineLayout& line : layouts) {
        QGlyphRun run;
        run.setRawFont(rawFont);
        run.setGlyphIndexes(line.glyphs);
        run.setPositions(line.positions);
        const qreal x = std::max<qreal>(padding, (image.width() - line.width) / 2.0);
        painter.drawGlyphRun(QPointF(x, baseline), run);
        baseline += lineHeight + gap;
    }
    painter.end();

    QByteArray png = encodeFontPng(&image, error);
    if (png.isEmpty())
        return {};

    const QJsonArray writingSystems = detectedWritingSystems(rawFont);

    QJsonObject payload;
    payload.insert(QStringLiteral("family"), rawFont.familyName());
    payload.insert(QStringLiteral("styleName"), rawFont.styleName());
    payload.insert(QStringLiteral("style"), styleLabel(rawFont.style()));
    payload.insert(QStringLiteral("weight"), rawFont.weight());
    payload.insert(QStringLiteral("unitsPerEm"), rawFont.unitsPerEm());
    payload.insert(QStringLiteral("writingSystems"), writingSystems);
    payload.insert(QStringLiteral("sampleText"), sampleLines.join(QChar::LineFeed));
    payload.insert(QStringLiteral("sampleFormat"), QStringLiteral("png"));
    payload.insert(QStringLiteral("sampleBase64"), QString::fromLatin1(png.toBase64()));
    payload.insert(QStringLiteral("sampleWidth"), image.width());
    payload.insert(QStringLiteral("sampleHeight"), image.height());
    payload.insert(QStringLiteral("fileSize"), QString::number(fileSize));
    return payload;
}

} // namespace FontProbe
