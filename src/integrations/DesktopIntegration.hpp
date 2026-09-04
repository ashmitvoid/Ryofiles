// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "locations/LocalPathGuard.hpp"

#include <QDir>
#include <QDirIterator>
#include <QEventLoopLocker>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>

#include <atomic>
#include <limits>
#include <memory>

class DesktopIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool applicationsReady READ applicationsReady NOTIFY applicationsReadyChanged)
    Q_PROPERTY(bool ryokuActionBusy READ ryokuActionBusy NOTIFY ryokuActionBusyChanged)
    Q_PROPERTY(QString ryokuActionError READ ryokuActionError NOTIFY ryokuActionErrorChanged)
    Q_PROPERTY(bool folderSizeBusy READ folderSizeBusy NOTIFY folderSizeBusyChanged)
    Q_PROPERTY(QVariantMap folderSizeResult READ folderSizeResult NOTIFY folderSizeResultChanged)

public:
    explicit DesktopIntegration(QObject* parent = nullptr);

    bool applicationsReady() const { return m_applicationsReady; }
    bool ryokuActionBusy() const { return m_ryokuActionBusy; }
    QString ryokuActionError() const { return m_ryokuActionError; }
    bool folderSizeBusy() const { return m_folderSizeBusy; }
    QVariantMap folderSizeResult() const { return m_folderSizeResult; }

    Q_INVOKABLE QString mimeTypeForPath(const QString& path) const;
    Q_INVOKABLE QVariantMap propertiesForPath(const QString& path) const;
    Q_INVOKABLE QVariantList applicationsForPath(const QString& path) const;
    Q_INVOKABLE bool openDefault(const QString& path) const;
    Q_INVOKABLE bool openWith(const QString& desktopFileId, const QString& path) const;

    Q_INVOKABLE bool canRyokuInstall(const QStringList& paths) const;
    Q_INVOKABLE bool canRyokuCompress(const QStringList& paths) const;
    Q_INVOKABLE bool installWithRyoku(const QStringList& paths);
    Q_INVOKABLE bool compressWithRyoku(const QStringList& paths);

    Q_INVOKABLE bool calculateFolderSize(const QString& path);
    Q_INVOKABLE void cancelFolderSize();

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
    void folderSizeBusyChanged();
    void folderSizeResultChanged();

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

    quint64 m_folderSizeGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_folderSizeCancel;
    bool m_folderSizeBusy = false;
    QVariantMap m_folderSizeResult;
};

inline bool DesktopIntegration::calculateFolderSize(const QString& requestedPath) {
    if (requestedPath.trimmed().isEmpty() || LocalPathGuard::isUriLike(requestedPath))
        return false;

    const QFileInfo rootInfo(requestedPath);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink())
        return false;

    const QString path = QDir::cleanPath(rootInfo.absoluteFilePath());
    if (path.isEmpty())
        return false;

    if (m_folderSizeCancel)
        m_folderSizeCancel->store(true, std::memory_order_relaxed);

    const quint64 generation = ++m_folderSizeGeneration;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_folderSizeCancel = cancelled;

    QVariantMap pending;
    pending.insert(QStringLiteral("path"), path);
    m_folderSizeResult = pending;
    emit folderSizeResultChanged();

    if (!m_folderSizeBusy) {
        m_folderSizeBusy = true;
        emit folderSizeBusyChanged();
    }

    auto* watcher = new QFutureWatcher<QVariantMap>(this);
    connect(
        watcher,
        &QFutureWatcher<QVariantMap>::finished,
        this,
        [this, watcher, generation] {
            QVariantMap result = watcher->result();
            watcher->deleteLater();

            if (generation != m_folderSizeGeneration)
                return;

            m_folderSizeCancel.reset();
            if (m_folderSizeBusy) {
                m_folderSizeBusy = false;
                emit folderSizeBusyChanged();
            }

            if (result.value(QStringLiteral("cancelled")).toBool()) {
                m_folderSizeResult.clear();
                emit folderSizeResultChanged();
                return;
            }

            const qint64 bytes = result.value(QStringLiteral("bytes")).toLongLong();
            result.insert(
                QStringLiteral("sizeText"),
                QLocale().formattedDataSize(bytes));
            m_folderSizeResult = result;
            emit folderSizeResultChanged();
        });

    watcher->setFuture(QtConcurrent::run([path, cancelled] {
        QVariantMap result;
        result.insert(QStringLiteral("path"), path);

        qint64 bytes = 0;
        qint64 files = 0;
        qint64 folders = 0;
        qint64 links = 0;

        QDirIterator iterator(
            path,
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDirIterator::Subdirectories);

        while (iterator.hasNext()) {
            if (cancelled->load(std::memory_order_relaxed)) {
                result.insert(QStringLiteral("cancelled"), true);
                return result;
            }

            iterator.next();
            const QFileInfo info = iterator.fileInfo();

            if (info.isSymLink()) {
                ++links;
                continue;
            }
            if (info.isDir()) {
                ++folders;
                continue;
            }
            if (!info.isFile())
                continue;

            ++files;
            const qint64 size = std::max<qint64>(0, info.size());
            const qint64 remaining = std::numeric_limits<qint64>::max() - bytes;
            bytes += std::min(size, remaining);
        }

        if (cancelled->load(std::memory_order_relaxed)) {
            result.insert(QStringLiteral("cancelled"), true);
            return result;
        }

        result.insert(QStringLiteral("bytes"), bytes);
        result.insert(QStringLiteral("files"), files);
        result.insert(QStringLiteral("folders"), folders);
        result.insert(QStringLiteral("links"), links);
        return result;
    }));

    return true;
}

inline void DesktopIntegration::cancelFolderSize() {
    ++m_folderSizeGeneration;
    if (m_folderSizeCancel)
        m_folderSizeCancel->store(true, std::memory_order_relaxed);
    m_folderSizeCancel.reset();

    if (m_folderSizeBusy) {
        m_folderSizeBusy = false;
        emit folderSizeBusyChanged();
    }

    if (!m_folderSizeResult.isEmpty()) {
        m_folderSizeResult.clear();
        emit folderSizeResultChanged();
    }
}
