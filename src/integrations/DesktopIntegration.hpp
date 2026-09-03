// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QFutureWatcher>
#include <QVariantList>

class DesktopIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool applicationsReady READ applicationsReady NOTIFY applicationsReadyChanged)

public:
    explicit DesktopIntegration(QObject* parent = nullptr);

    bool applicationsReady() const { return m_applicationsReady; }

    Q_INVOKABLE QString mimeTypeForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap propertiesForPath(const QString& path) const;
    Q_INVOKABLE QVariantList applicationsForPath(const QString& path) const;
    Q_INVOKABLE bool openDefault(const QString& path) const;
    Q_INVOKABLE bool openWith(const QString& desktopFileId, const QString& path) const;

signals:
    void applicationsReadyChanged();

private:
    struct DesktopApp {
        QString id;
        QString name;
        QString exec;
        QString icon;
        QStringList mimeTypes;
        QString desktopFilePath;
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
    QFutureWatcher<QList<DesktopApp>> m_discoveryWatcher;
    bool m_applicationsReady = false;
};
