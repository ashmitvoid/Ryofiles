// SPDX-License-Identifier: GPL-3.0-only

#include "RyokuIntegration.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <algorithm>

namespace {
constexpr auto kDefaultPaper = "#000000";
constexpr auto kDefaultPaperLift = "#0a0a0a";
constexpr auto kDefaultInk = "#cdc4ba";
constexpr auto kDefaultInkDim = "#b0a9a0";
constexpr auto kDefaultSun = "#e2342a";

bool usable(const QJsonValue& value) {
    return value.isString() && !value.toString().trimmed().isEmpty();
}
} // namespace

RyokuIntegration::RyokuIntegration(QObject* parent)
    : QObject(parent) {
    const QString configRoot = xdgPath("XDG_CONFIG_HOME", QStringLiteral(".config"));
    const QString cacheRoot = xdgPath("XDG_CACHE_HOME", QStringLiteral(".cache"));

    m_configDir = configRoot + QStringLiteral("/ryoku");
    m_cacheDir = cacheRoot + QStringLiteral("/ryoku");
    m_themePath = m_configDir + QStringLiteral("/theme.json");
    m_shellPath = m_configDir + QStringLiteral("/shell.json");
    m_palettePath = m_cacheDir + QStringLiteral("/colors.json");

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(35);
    connect(&m_reloadTimer, &QTimer::timeout, this, &RyokuIntegration::reloadAll);

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleReload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleReload(); });

    ensureWatchPaths();
    reloadAll();
}

QString RyokuIntegration::xdgPath(const char* variable, const QString& fallbackSuffix) {
    const QByteArray value = qgetenv(variable);
    if (!value.isEmpty())
        return QString::fromLocal8Bit(value);
    return QDir(QDir::homePath()).filePath(fallbackSuffix);
}

QJsonObject RyokuIntegration::readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

void RyokuIntegration::scheduleReload() {
    m_reloadTimer.start();
}

void RyokuIntegration::ensureWatchPaths() {
    const QStringList old = m_watcher.files() + m_watcher.directories();
    if (!old.isEmpty())
        m_watcher.removePaths(old);

    QStringList paths;
    for (const QString& dir : {m_configDir, m_cacheDir}) {
        if (QDir(dir).exists())
            paths << dir;
    }
    for (const QString& file : {m_themePath, m_shellPath, m_palettePath}) {
        if (QFile::exists(file))
            paths << file;
    }
    if (!paths.isEmpty())
        m_watcher.addPaths(paths);
}

void RyokuIntegration::reloadAll() {
    reloadTheme();
    reloadShell();
    reloadPalette();
    ensureWatchPaths();
    emit configChanged();
    emit paletteChanged();
}

void RyokuIntegration::reloadTheme() {
    const QJsonObject root = readObject(m_themePath);
    const QJsonValue follow = root.value(QStringLiteral("followWallpaper"));
    m_matchWallpaper = follow.isBool() ? follow.toBool() : true;
}

void RyokuIntegration::reloadShell() {
    const QJsonObject root = readObject(m_shellPath);

    const QJsonValue palette = root.value(QStringLiteral("themePalette"));
    m_hasNamedScheme = palette.isObject();
    m_namedScheme = m_hasNamedScheme ? palette.toObject() : QJsonObject{};

    m_motionScale = 1.0;
    m_reduceMotion = false;
    const QJsonObject theme = root.value(QStringLiteral("theme")).toObject();
    const QJsonObject motion = theme.value(QStringLiteral("motion")).toObject();
    const QJsonValue scale = motion.value(QStringLiteral("scale"));
    if (scale.isDouble() && scale.toDouble() > 0.0)
        m_motionScale = scale.toDouble();
    m_reduceMotion = motion.value(QStringLiteral("reduce")).toBool(false);

    m_uiScales = root.value(QStringLiteral("displays")).toObject()
                     .value(QStringLiteral("ui_scale")).toObject();

    m_uiFont = QStringLiteral("Space Grotesk");
    m_monoFont = QStringLiteral("SpaceMono Nerd Font");
    const QString configuredFont = root.value(QStringLiteral("fontFamily")).toString().trimmed();
    if (!configuredFont.isEmpty()) {
        m_uiFont = configuredFont;
        m_monoFont = configuredFont;
    }

    const QString configuredDecor = root.value(QStringLiteral("hubDecor")).toString().trimmed();
    m_decor = configuredDecor.isEmpty() ? QStringLiteral("calm") : configuredDecor;
}

void RyokuIntegration::reloadPalette() {
    m_wall = readObject(m_palettePath);
}

QColor RyokuIntegration::role(const QString& key, const QColor& fallback) const {
    if (m_hasNamedScheme && usable(m_namedScheme.value(key))) {
        const QColor color(m_namedScheme.value(key).toString());
        if (color.isValid())
            return color;
    }

    if (m_matchWallpaper && usable(m_wall.value(key))) {
        const QColor color(m_wall.value(key).toString());
        if (color.isValid())
            return color;
    }

    return fallback;
}

QColor RyokuIntegration::alpha(const QColor& color, qreal value) {
    QColor out = color;
    out.setAlphaF(std::clamp(value, 0.0, 1.0));
    return out;
}

QColor RyokuIntegration::paper() const {
    return role(QStringLiteral("surface"), QColor(kDefaultPaper));
}

QColor RyokuIntegration::paperLift() const {
    return role(QStringLiteral("surfaceContainerLow"), QColor(kDefaultPaperLift));
}

QColor RyokuIntegration::ink() const {
    return role(QStringLiteral("onSurface"), QColor(kDefaultInk));
}

QColor RyokuIntegration::inkDim() const {
    return role(QStringLiteral("onSurfaceVariant"), QColor(kDefaultInkDim));
}

QColor RyokuIntegration::inkMuted() const {
    return alpha(inkDim(), 0.78);
}

QColor RyokuIntegration::inkFaint() const {
    return alpha(inkDim(), 0.55);
}

QColor RyokuIntegration::bone() const {
    return role(QStringLiteral("inverseSurface"), QColor(kDefaultInk));
}

QColor RyokuIntegration::inkOnBone() const {
    return role(QStringLiteral("inverseOnSurface"), QColor(Qt::black));
}

QColor RyokuIntegration::inkOnBoneDim() const {
    return alpha(inkOnBone(), 0.62);
}

QColor RyokuIntegration::line() const {
    return alpha(ink(), 0.26);
}

QColor RyokuIntegration::lineSoft() const {
    return alpha(ink(), 0.13);
}

QColor RyokuIntegration::lineStrong() const {
    return alpha(ink(), 0.42);
}

QColor RyokuIntegration::tint5() const {
    return alpha(ink(), 0.05);
}

QColor RyokuIntegration::tint10() const {
    return alpha(ink(), 0.10);
}

QColor RyokuIntegration::tint16() const {
    return alpha(ink(), 0.16);
}

QColor RyokuIntegration::sun() const {
    return role(QStringLiteral("primary"), QColor(kDefaultSun));
}

bool RyokuIntegration::light() const {
    const QColor value = paper();
    const qreal luminance =
        (0.299 * value.redF()) + (0.587 * value.greenF()) + (0.114 * value.blueF());
    return luminance > 0.5;
}

qreal RyokuIntegration::uiScaleFor(const QString& outputName) const {
    if (outputName.isEmpty())
        return 1.0;
    const QJsonValue value = m_uiScales.value(outputName);
    if (!value.isDouble() || value.toDouble() <= 0.0)
        return 1.0;
    return std::clamp(value.toDouble(), 0.5, 2.0);
}

int RyokuIntegration::duration(int milliseconds) const {
    if (m_reduceMotion)
        return 0;
    return qRound(milliseconds * m_motionScale);
}
