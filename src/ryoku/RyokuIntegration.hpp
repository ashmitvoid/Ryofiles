// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

class RyokuIntegration final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QColor paper READ paper NOTIFY paletteChanged)
    Q_PROPERTY(QColor paperLift READ paperLift NOTIFY paletteChanged)
    Q_PROPERTY(QColor ink READ ink NOTIFY paletteChanged)
    Q_PROPERTY(QColor inkDim READ inkDim NOTIFY paletteChanged)
    Q_PROPERTY(QColor inkMuted READ inkMuted NOTIFY paletteChanged)
    Q_PROPERTY(QColor inkFaint READ inkFaint NOTIFY paletteChanged)
    Q_PROPERTY(QColor bone READ bone NOTIFY paletteChanged)
    Q_PROPERTY(QColor inkOnBone READ inkOnBone NOTIFY paletteChanged)
    Q_PROPERTY(QColor inkOnBoneDim READ inkOnBoneDim NOTIFY paletteChanged)
    Q_PROPERTY(QColor line READ line NOTIFY paletteChanged)
    Q_PROPERTY(QColor lineSoft READ lineSoft NOTIFY paletteChanged)
    Q_PROPERTY(QColor lineStrong READ lineStrong NOTIFY paletteChanged)
    Q_PROPERTY(QColor tint5 READ tint5 NOTIFY paletteChanged)
    Q_PROPERTY(QColor tint10 READ tint10 NOTIFY paletteChanged)
    Q_PROPERTY(QColor tint16 READ tint16 NOTIFY paletteChanged)
    Q_PROPERTY(QColor sun READ sun NOTIFY paletteChanged)
    Q_PROPERTY(bool light READ light NOTIFY paletteChanged)

    Q_PROPERTY(QString uiFont READ uiFont NOTIFY configChanged)
    Q_PROPERTY(QString monoFont READ monoFont NOTIFY configChanged)
    Q_PROPERTY(QString displayFont READ displayFont CONSTANT)
    Q_PROPERTY(QString decor READ decor NOTIFY configChanged)
    Q_PROPERTY(qreal motionScale READ motionScale NOTIFY configChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY configChanged)
    Q_PROPERTY(bool matchWallpaper READ matchWallpaper NOTIFY configChanged)

public:
    explicit RyokuIntegration(QObject* parent = nullptr);

    QColor paper() const;
    QColor paperLift() const;
    QColor ink() const;
    QColor inkDim() const;
    QColor inkMuted() const;
    QColor inkFaint() const;
    QColor bone() const;
    QColor inkOnBone() const;
    QColor inkOnBoneDim() const;
    QColor line() const;
    QColor lineSoft() const;
    QColor lineStrong() const;
    QColor tint5() const;
    QColor tint10() const;
    QColor tint16() const;
    QColor sun() const;
    bool light() const;

    QString uiFont() const { return m_uiFont; }
    QString monoFont() const { return m_monoFont; }
    QString displayFont() const { return QStringLiteral("Fraunces"); }
    QString decor() const { return m_decor; }
    qreal motionScale() const { return m_motionScale; }
    bool reduceMotion() const { return m_reduceMotion; }
    bool matchWallpaper() const { return m_matchWallpaper; }

    Q_INVOKABLE qreal uiScaleFor(const QString& outputName) const;
    Q_INVOKABLE int duration(int milliseconds) const;

signals:
    void paletteChanged();
    void configChanged();

private:
    void scheduleReload();
    void reloadAll();
    void reloadTheme();
    void reloadShell();
    void reloadPalette();
    void ensureWatchPaths();

    QColor role(const QString& key, const QColor& fallback) const;
    static QColor alpha(const QColor& color, qreal value);
    static QJsonObject readObject(const QString& path);
    static QString xdgPath(const char* variable, const QString& fallbackSuffix);

    QString m_themePath;
    QString m_shellPath;
    QString m_palettePath;
    QString m_configDir;
    QString m_cacheDir;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;

    QJsonObject m_namedScheme;
    QJsonObject m_wall;
    QJsonObject m_uiScales;

    bool m_hasNamedScheme = false;
    bool m_matchWallpaper = true;
    qreal m_motionScale = 1.0;
    bool m_reduceMotion = false;
    QString m_uiFont = QStringLiteral("Space Grotesk");
    QString m_monoFont = QStringLiteral("SpaceMono Nerd Font");
    QString m_decor = QStringLiteral("calm");
};
