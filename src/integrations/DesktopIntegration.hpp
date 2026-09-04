// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QEventLoopLocker>
#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>

#include <memory>

class DesktopIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool applicationsReady READ applicationsReady NOTIFY applicationsReadyChanged)
    Q_PROPERTY(bool ryokuActionBusy READ ryokuActionBusy NOTIFY ryokuActionBusyChanged)
    Q_PROPERTY(QString ryokuActionError READ ryokuActionError NOTIFY ryokuActionErrorChanged)

public:
    explicit DesktopIntegration(QObject* parent = nullptr);

    bool applicationsReady() const { return m_applicationsReady; }
    bool ryokuActionBusy() const { return m_ryokuActionBusy; }
    QString ryokuActionError() const { return m_ryokuActionError; }

    Q_INVOKABLE QString mimeTypeForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap propertiesForPath(const QString& path) const;
    Q_INVOKABLE QVariantList applicationsForPath(const QString& path) const;
    Q_INVOKABLE bool openDefault(const QString& path) const;
    Q_INVOKABLE bool openWith(const QString& desktopFileId, const QString& path) const;

    Q_INVOKABLE bool canRyokuInstall(const QStringList& paths) const;
    Q_INVOKABLE bool canRyokuCompress(const QStringList& paths) const;
    Q_INVOKABLE bool installWithRyoku(const QStringList& paths);
    Q_INVOKABLE bool compressWithRyoku(const QStringList& paths);

    static bool isRyokuInstallablePath(const QString& path);
    static bool isRyokuCompressiblePath(const QString& path);
    static QStringList ryokuInstallablePaths(const QStringList& paths);
    static QStringList ryokuCompressiblePaths(const QStringList& paths);

signals:
    void applicationsReadyChanged();
    void ryokuActionBusyChanged();
    void ryokuActionErrorChanged();
    void ryokuActionStarted(const QString& action, int count);
    void ryokuActionFinished(
        const QString& action,
        int succeeded,
        int failed,
        const QString& error);

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

    enum class RyokuFileAction {
        None,
        Install,
        Compress,
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
    static QString ryokuActionName(RyokuFileAction action);
    static QString ryokuHelperPath(RyokuFileAction action);
    static bool ryokuHelperAvailable(RyokuFileAction action);

    const DesktopApp* appById(const QString& id) const;
    bool startRyokuFileAction(RyokuFileAction action, const QStringList& paths);
    void startNextRyokuFileAction();
    void finishRyokuFileAction();
    void setRyokuActionError(const QString& error);

    QList<DesktopApp> m_apps;
    QFutureWatcher<QList<DesktopApp>> m_discoveryWatcher;
    bool m_applicationsReady = false;

    QProcess m_ryokuProcess;
    std::unique_ptr<QEventLoopLocker> m_ryokuQuitLocker;
    RyokuFileAction m_ryokuAction = RyokuFileAction::None;
    QStringList m_ryokuPaths;
    QString m_ryokuHelper;
    int m_ryokuNextIndex = 0;
    int m_ryokuSucceeded = 0;
    int m_ryokuFailed = 0;
    bool m_ryokuProcessPending = false;
    bool m_ryokuActionBusy = false;
    QString m_ryokuActionError;
};
