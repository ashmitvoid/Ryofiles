// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "locations/LocalPathGuard.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoopLocker>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>

class DesktopIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool applicationsReady READ applicationsReady NOTIFY applicationsReadyChanged)
    Q_PROPERTY(bool ryokuActionBusy READ ryokuActionBusy NOTIFY ryokuActionBusyChanged)
    Q_PROPERTY(QString ryokuActionError READ ryokuActionError NOTIFY ryokuActionErrorChanged)
    Q_PROPERTY(bool folderPickerBusy READ folderPickerBusy NOTIFY folderPickerBusyChanged)
    Q_PROPERTY(QString folderPickerError READ folderPickerError NOTIFY folderPickerErrorChanged)
    Q_PROPERTY(bool folderSizeBusy READ folderSizeBusy NOTIFY folderSizeBusyChanged)
    Q_PROPERTY(QVariantMap folderSizeResult READ folderSizeResult NOTIFY folderSizeResultChanged)

public:
    explicit DesktopIntegration(QObject* parent = nullptr);

    bool applicationsReady() const { return m_applicationsReady; }
    bool ryokuActionBusy() const { return m_ryokuActionBusy; }
    QString ryokuActionError() const { return m_ryokuActionError; }
    bool folderPickerBusy() const { return m_folderPickerBusy; }
    QString folderPickerError() const { return m_folderPickerError; }
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

    Q_INVOKABLE bool pickFolder(
        const QString& initialDirectory,
        const QString& title,
        const QString& acceptLabel);
    Q_INVOKABLE void cancelFolderPicker();

    Q_INVOKABLE bool calculateFolderSize(const QString& path);
    Q_INVOKABLE void cancelFolderSize();

    static bool isRyokuInstallablePath(const QString& path);
    static bool isRyokuCompressiblePath(const QString& path);
    static QStringList ryokuInstallablePaths(const QStringList& paths);
    static QStringList ryokuCompressiblePaths(const QStringList& paths);
    static QStringList folderPickerArguments(
        const QString& initialDirectory,
        const QString& title,
        const QString& acceptLabel);
    static QString folderFromPickerOutput(const QByteArray& output);

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
    void folderPickerBusyChanged();
    void folderPickerErrorChanged();
    void folderPicked(const QString& path);
    void folderPickerCancelled();
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
    void ensureFolderPickerConnections();
    void setFolderPickerBusy(bool busy);
    void setFolderPickerError(const QString& error);

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

    QProcess m_folderPickerProcess;
    bool m_folderPickerConnected = false;
    bool m_folderPickerBusy = false;
    QString m_folderPickerError;

    quint64 m_folderSizeGeneration = 0;
    std::shared_ptr<std::atomic_bool> m_folderSizeCancel;
    bool m_folderSizeBusy = false;
    QVariantMap m_folderSizeResult;
};

inline QStringList DesktopIntegration::folderPickerArguments(
    const QString& initialDirectory,
    const QString& title,
    const QString& acceptLabel) {
    return {
        QStringLiteral("--picker"),
        QStringLiteral("folder"),
        QStringLiteral("--initial-dir"),
        initialDirectory,
        QStringLiteral("--picker-title"),
        title,
        QStringLiteral("--accept-label"),
        acceptLabel,
    };
}

inline QString DesktopIntegration::folderFromPickerOutput(const QByteArray& output) {
    QByteArray selectedLine;
    const QList<QByteArray> lines = output.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (!selectedLine.isEmpty())
            return {};
        selectedLine = line;
    }

    if (selectedLine.isEmpty())
        return {};

    const QUrl url = QUrl::fromEncoded(selectedLine, QUrl::StrictMode);
    if (!url.isValid() || !url.isLocalFile())
        return {};

    const QString localPath = url.toLocalFile();
    if (localPath.isEmpty() || LocalPathGuard::isUriLike(localPath))
        return {};

    const QFileInfo info(localPath);
    if (!info.exists() || !info.isDir() || info.isSymLink())
        return {};

    return QDir::cleanPath(info.absoluteFilePath());
}

inline void DesktopIntegration::setFolderPickerBusy(bool busy) {
    if (m_folderPickerBusy == busy)
        return;
    m_folderPickerBusy = busy;
    emit folderPickerBusyChanged();
}

inline void DesktopIntegration::setFolderPickerError(const QString& error) {
    if (m_folderPickerError == error)
        return;
    m_folderPickerError = error;
    emit folderPickerErrorChanged();
}

inline void DesktopIntegration::ensureFolderPickerConnections() {
    if (m_folderPickerConnected)
        return;
    m_folderPickerConnected = true;

    connect(
        &m_folderPickerProcess,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!m_folderPickerBusy)
                return;

            const QByteArray output = m_folderPickerProcess.readAllStandardOutput();
            const QString standardError = QString::fromUtf8(
                m_folderPickerProcess.readAllStandardError()).trimmed();
            setFolderPickerBusy(false);

            if (exitStatus != QProcess::NormalExit) {
                setFolderPickerError(tr("Folder picker terminated unexpectedly"));
                return;
            }

            if (exitCode == 1) {
                setFolderPickerError(QString());
                emit folderPickerCancelled();
                return;
            }

            if (exitCode != 0) {
                setFolderPickerError(
                    standardError.isEmpty()
                        ? tr("Folder picker failed")
                        : standardError);
                return;
            }

            const QString selected = folderFromPickerOutput(output);
            if (selected.isEmpty()) {
                setFolderPickerError(tr("Folder picker returned an invalid local folder"));
                return;
            }

            setFolderPickerError(QString());
            emit folderPicked(selected);
        });

    connect(
        &m_folderPickerProcess,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) {
            if (!m_folderPickerBusy || error != QProcess::FailedToStart)
                return;
            setFolderPickerBusy(false);
            setFolderPickerError(tr("Could not start the Ryofiles folder picker"));
        });
}

inline bool DesktopIntegration::pickFolder(
    const QString& initialDirectory,
    const QString& title,
    const QString& acceptLabel) {
    if (m_folderPickerBusy) {
        setFolderPickerError(tr("Another folder picker is already open"));
        return false;
    }

    if (initialDirectory.trimmed().isEmpty()
        || LocalPathGuard::isUriLike(initialDirectory)) {
        setFolderPickerError(tr("Folder picker requires a local starting directory"));
        return false;
    }

    const QFileInfo initialInfo(initialDirectory);
    if (!initialInfo.exists() || !initialInfo.isDir() || initialInfo.isSymLink()) {
        setFolderPickerError(tr("Folder picker starting directory is unavailable"));
        return false;
    }

    QString program = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("RYOFILES_PICKER_EXECUTABLE"));
    if (program.isEmpty())
        program = QCoreApplication::applicationFilePath();
    if (program.isEmpty()) {
        setFolderPickerError(tr("Ryofiles picker executable is unavailable"));
        return false;
    }

    const QString cleanInitial = QDir::cleanPath(initialInfo.absoluteFilePath());
    const QString cleanTitle = title.trimmed().isEmpty()
        ? tr("Select Folder")
        : title;
    const QString cleanAcceptLabel = acceptLabel.trimmed().isEmpty()
        ? tr("SELECT")
        : acceptLabel;

    ensureFolderPickerConnections();
    setFolderPickerError(QString());
    m_folderPickerProcess.setWorkingDirectory(cleanInitial);
    m_folderPickerProcess.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    m_folderPickerProcess.setProcessChannelMode(QProcess::SeparateChannels);
    m_folderPickerProcess.setProgram(program);
    m_folderPickerProcess.setArguments(
        folderPickerArguments(cleanInitial, cleanTitle, cleanAcceptLabel));
    setFolderPickerBusy(true);
    m_folderPickerProcess.start();
    return true;
}

inline void DesktopIntegration::cancelFolderPicker() {
    if (!m_folderPickerBusy)
        return;

    setFolderPickerBusy(false);
    setFolderPickerError(QString());
    if (m_folderPickerProcess.state() != QProcess::NotRunning)
        m_folderPickerProcess.kill();
    emit folderPickerCancelled();
}

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
