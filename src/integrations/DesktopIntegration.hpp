// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QVariantList>

class DesktopIntegration final : public QObject {
    Q_OBJECT

public:
    explicit DesktopIntegration(QObject* parent = nullptr);

    Q_INVOKABLE QString mimeTypeForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap propertiesForPath(const QString& path) const;
    Q_INVOKABLE QVariantList applicationsForPath(const QString& path) const;
    Q_INVOKABLE bool openDefault(const QString& path) const;
    Q_INVOKABLE bool openWith(const QString& desktopFileId, const QString& path) const;

private:
    struct DesktopApp {
        QString id;
        QString name;
        QString exec;
        QString icon;
        QStringList mimeTypes;
        bool terminal = false;
        bool noDisplay = false;
        bool hidden = false;
    };

    static QStringList desktopSearchPaths();
    static QList<DesktopApp> discoverApplications();
    static QString desktopIdForPath(const QString& base, const QString& filePath);
    static QString unescapeDesktopValue(const QString& value);
    static bool parseBool(const QString& value);
    static QStringList parseList(const QString& value);
    static QStringList buildCommand(
        const QString& exec,
        const QString& appName,
        const QString& icon,
        const QString& desktopFilePath,
        const QString& targetPath);
    static QStringList tokenizeExec(const QString& exec);

    const DesktopApp* appById(const QString& id) const;

    QList<DesktopApp> m_apps;
    QHash<QString, QString> m_desktopPaths;
};
