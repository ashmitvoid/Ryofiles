// SPDX-License-Identifier: GPL-3.0-only

#include "DesktopIntegration.hpp"

#include "locations/LocalPathGuard.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcessEnvironment>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace {

QString permissionsText(QFileDevice::Permissions permissions) {
    QString text;
    text.reserve(9);

    const auto bit = [&](QFileDevice::Permission permission, QChar value) {
        text += permissions.testFlag(permission) ? value : QLatin1Char('-');
    };

    bit(QFileDevice::ReadOwner,  QLatin1Char('r'));
    bit(QFileDevice::WriteOwner, QLatin1Char('w'));
    bit(QFileDevice::ExeOwner,   QLatin1Char('x'));
    bit(QFileDevice::ReadGroup,  QLatin1Char('r'));
    bit(QFileDevice::WriteGroup, QLatin1Char('w'));
    bit(QFileDevice::ExeGroup,   QLatin1Char('x'));
    bit(QFileDevice::ReadOther,  QLatin1Char('r'));
    bit(QFileDevice::WriteOther, QLatin1Char('w'));
    bit(QFileDevice::ExeOther,   QLatin1Char('x'));

    return text;
}

QString typeText(const QFileInfo& info) {
    if (info.isSymLink())
        return QObject::tr("Symbolic link");
    if (info.isDir())
        return QObject::tr("Folder");
    if (info.isFile())
        return QObject::tr("File");
    return QObject::tr("Other");
}

bool hasSuffix(const QString& path, std::initializer_list<const char*> suffixes) {
    const QString lower = path.toLower();
    for (const char* suffix : suffixes) {
        if (lower.endsWith(QLatin1String(suffix)))
            return true;
    }
    return false;
}

template <typename Predicate>
QStringList eligibleLocalFiles(const QStringList& paths, Predicate predicate) {
    QStringList result;
    QSet<QString> seen;

    for (const QString& requested : paths) {
        if (requested.trimmed().isEmpty() || LocalPathGuard::isUriLike(requested))
            continue;

        const QFileInfo info(requested);
        if (!info.exists() || !info.isFile())
            continue;

        const QString absolute = info.absoluteFilePath();
        if (!predicate(absolute) || seen.contains(absolute))
            continue;

        seen.insert(absolute);
        result.push_back(absolute);
    }

    return result;
}

} // namespace

DesktopIntegration::DesktopIntegration(QObject* parent)
    : QObject(parent) {
    connect(
        &m_discoveryWatcher,
        &QFutureWatcher<QList<DesktopApp>>::finished,
        this,
        [this] {
            m_apps = m_discoveryWatcher.result();
            m_applicationsReady = true;
            emit applicationsReadyChanged();
        });

    connect(
        &m_ryokuProcess,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](int exitCode, QProcess::ExitStatus exitStatus) {
            if (!m_ryokuProcessPending)
                return;

            m_ryokuProcessPending = false;
            if (exitStatus == QProcess::NormalExit && exitCode == 0)
                ++m_ryokuSucceeded;
            else
                ++m_ryokuFailed;

            startNextRyokuFileAction();
        });

    connect(
        &m_ryokuProcess,
        &QProcess::errorOccurred,
        this,
        [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || !m_ryokuProcessPending)
                return;

            m_ryokuProcessPending = false;
            ++m_ryokuFailed;
            QTimer::singleShot(0, this, &DesktopIntegration::startNextRyokuFileAction);
        });

    m_discoveryWatcher.setFuture(QtConcurrent::run([] {
        return discoverApplications();
    }));
}

QStringList DesktopIntegration::desktopSearchPaths() {
    QStringList bases;

    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty())
        bases.push_back(QDir(dataHome).filePath(QStringLiteral("applications")));

    const QStringList generic =
        QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& data : generic)
        bases.push_back(QDir(data).filePath(QStringLiteral("applications")));

    bases.removeDuplicates();
    return bases;
}

QString DesktopIntegration::desktopIdForPath(
    const QString& base,
    const QString& filePath) {
    QString relative = QDir(base).relativeFilePath(filePath);
    relative.replace(QLatin1Char('/'), QLatin1Char('-'));
    return relative;
}

QString DesktopIntegration::unescapeDesktopValue(const QString& value) {
    QString out;
    out.reserve(value.size());

    bool escape = false;
    for (const QChar ch : value) {
        if (!escape) {
            if (ch == QLatin1Char('\\')) {
                escape = true;
                continue;
            }
            out += ch;
            continue;
        }

        escape = false;
        switch (ch.unicode()) {
        case 's': out += QLatin1Char(' '); break;
        case 'n': out += QLatin1Char('\n'); break;
        case 't': out += QLatin1Char('\t'); break;
        case 'r': out += QLatin1Char('\r'); break;
        case '\\': out += QLatin1Char('\\'); break;
        default:
            out += ch;
            break;
        }
    }

    if (escape)
        out += QLatin1Char('\\');

    return out;
}

bool DesktopIntegration::parseBool(const QString& value) {
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("1");
}

QStringList DesktopIntegration::parseList(const QString& value) {
    QStringList parts = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (QString& part : parts)
        part = unescapeDesktopValue(part.trimmed());
    return parts;
}

QList<DesktopIntegration::DesktopApp> DesktopIntegration::discoverApplications() {
    QHash<QString, DesktopApp> appsById;

    for (const QString& base : desktopSearchPaths()) {
        QDir root(base);
        if (!root.exists())
            continue;

        QDirIterator it(
            base,
            {QStringLiteral("*.desktop")},
            QDir::Files | QDir::Readable,
            QDirIterator::Subdirectories);

        while (it.hasNext()) {
            const QString path = it.next();
            const QString id = desktopIdForPath(base, path);
            if (appsById.contains(id))
                continue;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            DesktopApp app;
            app.id = id;
            app.desktopFilePath = path;

            bool inDesktopEntry = false;
            QTextStream stream(&file);
            while (!stream.atEnd()) {
                QString line = stream.readLine().trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;

                if (line.startsWith(QLatin1Char('['))) {
                    inDesktopEntry = line == QStringLiteral("[Desktop Entry]");
                    continue;
                }
                if (!inDesktopEntry)
                    continue;

                const int equals = line.indexOf(QLatin1Char('='));
                if (equals <= 0)
                    continue;

                const QString key = line.left(equals);
                const QString value = line.mid(equals + 1);

                if (key == QStringLiteral("Name") && app.name.isEmpty())
                    app.name = unescapeDesktopValue(value);
                else if (key == QStringLiteral("Exec"))
                    app.exec = value;
                else if (key == QStringLiteral("Icon"))
                    app.icon = unescapeDesktopValue(value);
                else if (key == QStringLiteral("MimeType"))
                    app.mimeTypes = parseList(value);
                else if (key == QStringLiteral("Terminal"))
                    app.terminal = parseBool(value);
                else if (key == QStringLiteral("NoDisplay"))
                    app.noDisplay = parseBool(value);
                else if (key == QStringLiteral("Hidden"))
                    app.hidden = parseBool(value);
            }

            if (app.name.isEmpty() || app.exec.isEmpty() || app.hidden)
                continue;

            appsById.insert(id, app);
        }
    }

    QList<DesktopApp> apps = appsById.values();
    std::sort(apps.begin(), apps.end(), [](const DesktopApp& a, const DesktopApp& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return apps;
}

QString DesktopIntegration::mimeTypeForPath(const QString& path) const {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return {};

    if (info.isDir() && !info.isSymLink())
        return QStringLiteral("inode/directory");

    QMimeDatabase database;
    return database.mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
}

QVariantMap DesktopIntegration::propertiesForPath(const QString& path) const {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return {};

    QVariantMap properties;
    properties.insert(QStringLiteral("name"), info.fileName());
    properties.insert(QStringLiteral("path"), info.absoluteFilePath());
    properties.insert(QStringLiteral("parent"), info.absolutePath());
    properties.insert(QStringLiteral("type"), typeText(info));
    properties.insert(QStringLiteral("mime"), mimeTypeForPath(path));
    properties.insert(QStringLiteral("isDirectory"), info.isDir() && !info.isSymLink());
    properties.insert(QStringLiteral("isSymlink"), info.isSymLink());
    properties.insert(
        QStringLiteral("sizeBytes"),
        (info.isDir() && !info.isSymLink()) ? QVariant() : QVariant::fromValue(info.size()));
    properties.insert(
        QStringLiteral("sizeText"),
        (info.isDir() && !info.isSymLink())
            ? QObject::tr("Not calculated")
            : QLocale().formattedDataSize(info.size()));
    properties.insert(
        QStringLiteral("modified"),
        info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    properties.insert(
        QStringLiteral("created"),
        info.birthTime().isValid()
            ? info.birthTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
            : QString());
    properties.insert(QStringLiteral("owner"), info.owner());
    properties.insert(QStringLiteral("group"), info.group());
    properties.insert(QStringLiteral("permissions"), permissionsText(info.permissions()));
    properties.insert(QStringLiteral("readable"), info.isReadable());
    properties.insert(QStringLiteral("writable"), info.isWritable());
    properties.insert(QStringLiteral("executable"), info.isExecutable());
    properties.insert(
        QStringLiteral("symlinkTarget"),
        info.isSymLink() ? info.symLinkTarget() : QString());

    return properties;
}

QVariantList DesktopIntegration::applicationsForPath(const QString& path) const {
    const QString mime = mimeTypeForPath(path);
    if (mime.isEmpty())
        return {};

    QVariantList result;
    for (const DesktopApp& app : m_apps) {
        if (app.noDisplay || app.terminal)
            continue;
        if (!app.mimeTypes.contains(mime))
            continue;

        QVariantMap item;
        item.insert(QStringLiteral("id"), app.id);
        item.insert(QStringLiteral("name"), app.name);
        item.insert(QStringLiteral("icon"), app.icon);
        result.push_back(item);
    }
    return result;
}

bool DesktopIntegration::openDefault(const QString& path) const {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

QStringList DesktopIntegration::tokenizeExec(const QString& exec) {
    QStringList tokens;
    QString current;
    bool quoted = false;
    QChar quote;
    bool escape = false;

    for (const QChar ch : exec) {
        if (escape) {
            current += ch;
            escape = false;
            continue;
        }

        if (ch == QLatin1Char('\\')) {
            escape = true;
            continue;
        }

        if (quoted) {
            if (ch == quote) {
                quoted = false;
                continue;
            }
            current += ch;
            continue;
        }

        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quoted = true;
            quote = ch;
            continue;
        }

        if (ch.isSpace()) {
            if (!current.isEmpty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }

        current += ch;
    }

    if (!current.isEmpty())
        tokens.push_back(current);

    return tokens;
}

QStringList DesktopIntegration::buildCommand(
    const QString& exec,
    const QString& appName,
    const QString& icon,
    const QString& desktopFilePath,
    const QString& targetPath) {
    const QString absolutePath = QFileInfo(targetPath).absoluteFilePath();
    const QString fileUrl = QUrl::fromLocalFile(absolutePath).toString();

    QStringList output;
    bool consumedTarget = false;

    const QStringList tokens = tokenizeExec(exec);
    for (QString token : tokens) {
        if (token == QStringLiteral("%f") || token == QStringLiteral("%F")) {
            output.push_back(absolutePath);
            consumedTarget = true;
            continue;
        }
        if (token == QStringLiteral("%u") || token == QStringLiteral("%U")) {
            output.push_back(fileUrl);
            consumedTarget = true;
            continue;
        }
        if (token == QStringLiteral("%i")) {
            if (!icon.isEmpty()) {
                output.push_back(QStringLiteral("--icon"));
                output.push_back(icon);
            }
            continue;
        }
        if (token == QStringLiteral("%c")) {
            output.push_back(appName);
            continue;
        }
        if (token == QStringLiteral("%k")) {
            output.push_back(desktopFilePath);
            continue;
        }
        if (token == QStringLiteral("%%")) {
            output.push_back(QStringLiteral("%"));
            continue;
        }

        token.replace(QStringLiteral("%%"), QStringLiteral("%"));
        token.replace(QStringLiteral("%c"), appName);
        token.replace(QStringLiteral("%k"), desktopFilePath);

        if (token.contains(QStringLiteral("%f")) || token.contains(QStringLiteral("%F"))) {
            token.replace(QStringLiteral("%f"), absolutePath);
            token.replace(QStringLiteral("%F"), absolutePath);
            consumedTarget = true;
        }
        if (token.contains(QStringLiteral("%u")) || token.contains(QStringLiteral("%U"))) {
            token.replace(QStringLiteral("%u"), fileUrl);
            token.replace(QStringLiteral("%U"), fileUrl);
            consumedTarget = true;
        }

        if (token.contains(QLatin1Char('%')))
            continue;

        if (!token.isEmpty())
            output.push_back(token);
    }

    if (!consumedTarget)
        output.push_back(absolutePath);

    return output;
}

const DesktopIntegration::DesktopApp* DesktopIntegration::appById(const QString& id) const {
    for (const DesktopApp& app : m_apps) {
        if (app.id == id)
            return &app;
    }
    return nullptr;
}

bool DesktopIntegration::openWith(
    const QString& desktopFileId,
    const QString& path) const {
    const DesktopApp* app = appById(desktopFileId);
    if (!app)
        return false;

    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return false;

    QStringList command =
        buildCommand(app->exec, app->name, app->icon, app->desktopFilePath, path);

    if (command.isEmpty())
        return false;

    const QString program = command.takeFirst();
    if (program.isEmpty())
        return false;

    return QProcess::startDetached(program, command);
}

bool DesktopIntegration::isRyokuInstallablePath(const QString& path) {
    return hasSuffix(path, {
        ".appimage",
        ".flatpak",
        ".deb",
        ".rpm",
        ".tar.gz",
        ".tgz",
        ".tar.xz",
        ".tar.bz2",
        ".tar.zst",
        ".tar",
    });
}

bool DesktopIntegration::isRyokuCompressiblePath(const QString& path) {
    return hasSuffix(path, {
        ".mp4",
        ".mkv",
        ".mov",
        ".webm",
        ".avi",
        ".m4v",
        ".jpg",
        ".jpeg",
        ".png",
        ".webp",
        ".bmp",
    });
}

QStringList DesktopIntegration::ryokuInstallablePaths(const QStringList& paths) {
    return eligibleLocalFiles(paths, [](const QString& path) {
        return isRyokuInstallablePath(path);
    });
}

QStringList DesktopIntegration::ryokuCompressiblePaths(const QStringList& paths) {
    return eligibleLocalFiles(paths, [](const QString& path) {
        return isRyokuCompressiblePath(path);
    });
}

QString DesktopIntegration::ryokuActionName(RyokuFileAction action) {
    switch (action) {
    case RyokuFileAction::Install:
        return QStringLiteral("install");
    case RyokuFileAction::Compress:
        return QStringLiteral("compress");
    case RyokuFileAction::None:
        break;
    }
    return {};
}

QString DesktopIntegration::ryokuHelperPath(RyokuFileAction action) {
    QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configRoot.isEmpty())
        configRoot = QDir::home().filePath(QStringLiteral(".config"));

    QString script;
    switch (action) {
    case RyokuFileAction::Install:
        script = QStringLiteral("stash-install.sh");
        break;
    case RyokuFileAction::Compress:
        script = QStringLiteral("stash-compress.sh");
        break;
    case RyokuFileAction::None:
        return {};
    }

    return QDir(configRoot).filePath(QStringLiteral("hypr/scripts/") + script);
}

bool DesktopIntegration::ryokuHelperAvailable(RyokuFileAction action) {
    const QFileInfo helper(ryokuHelperPath(action));
    return helper.exists() && helper.isFile() && helper.isReadable();
}

bool DesktopIntegration::canRyokuInstall(const QStringList& paths) const {
    return ryokuHelperAvailable(RyokuFileAction::Install)
        && !ryokuInstallablePaths(paths).isEmpty();
}

bool DesktopIntegration::canRyokuCompress(const QStringList& paths) const {
    return ryokuHelperAvailable(RyokuFileAction::Compress)
        && !ryokuCompressiblePaths(paths).isEmpty();
}

bool DesktopIntegration::installWithRyoku(const QStringList& paths) {
    return startRyokuFileAction(RyokuFileAction::Install, paths);
}

bool DesktopIntegration::compressWithRyoku(const QStringList& paths) {
    return startRyokuFileAction(RyokuFileAction::Compress, paths);
}

bool DesktopIntegration::startRyokuFileAction(
    RyokuFileAction action,
    const QStringList& paths) {
    if (m_ryokuActionBusy) {
        setRyokuActionError(tr("Another Ryoku file action is already running"));
        return false;
    }

    QStringList eligible;
    switch (action) {
    case RyokuFileAction::Install:
        eligible = ryokuInstallablePaths(paths);
        break;
    case RyokuFileAction::Compress:
        eligible = ryokuCompressiblePaths(paths);
        break;
    case RyokuFileAction::None:
        break;
    }

    if (eligible.isEmpty()) {
        setRyokuActionError(tr("No supported local files selected"));
        return false;
    }

    const QString helper = ryokuHelperPath(action);
    if (!ryokuHelperAvailable(action)) {
        setRyokuActionError(tr("Ryoku %1 helper is unavailable").arg(ryokuActionName(action)));
        return false;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("bash")).isEmpty()) {
        setRyokuActionError(tr("Bash is unavailable; Ryoku helper cannot be started"));
        return false;
    }

    m_ryokuAction = action;
    m_ryokuPaths = std::move(eligible);
    m_ryokuHelper = helper;
    m_ryokuNextIndex = 0;
    m_ryokuSucceeded = 0;
    m_ryokuFailed = 0;
    m_ryokuProcessPending = false;
    m_ryokuActionBusy = true;
    m_ryokuQuitLocker = std::make_unique<QEventLoopLocker>();
    setRyokuActionError(QString());
    emit ryokuActionBusyChanged();
    emit ryokuActionStarted(ryokuActionName(action), m_ryokuPaths.size());

    QTimer::singleShot(0, this, &DesktopIntegration::startNextRyokuFileAction);
    return true;
}

void DesktopIntegration::startNextRyokuFileAction() {
    if (!m_ryokuActionBusy || m_ryokuProcessPending)
        return;

    if (m_ryokuNextIndex >= m_ryokuPaths.size()) {
        finishRyokuFileAction();
        return;
    }

    const QString bash = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (bash.isEmpty()) {
        m_ryokuFailed += m_ryokuPaths.size() - m_ryokuNextIndex;
        m_ryokuNextIndex = m_ryokuPaths.size();
        finishRyokuFileAction();
        return;
    }

    const QString path = m_ryokuPaths.at(m_ryokuNextIndex++);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_ryokuAction == RyokuFileAction::Install)
        environment.insert(QStringLiteral("RYOKU_STASH_KEEP"), QStringLiteral("1"));

    m_ryokuProcess.setProcessEnvironment(environment);
    m_ryokuProcess.setProcessChannelMode(QProcess::ForwardedChannels);
    m_ryokuProcess.setProgram(bash);
    m_ryokuProcess.setArguments({m_ryokuHelper, path});
    m_ryokuProcessPending = true;
    m_ryokuProcess.start();
}

void DesktopIntegration::finishRyokuFileAction() {
    const QString action = ryokuActionName(m_ryokuAction);
    const int succeeded = m_ryokuSucceeded;
    const int failed = m_ryokuFailed;

    QString error;
    if (failed > 0) {
        error = tr("Ryoku %1 failed for %2 of %3 files")
            .arg(action)
            .arg(failed)
            .arg(succeeded + failed);
    }

    m_ryokuAction = RyokuFileAction::None;
    m_ryokuPaths.clear();
    m_ryokuHelper.clear();
    m_ryokuNextIndex = 0;
    m_ryokuSucceeded = 0;
    m_ryokuFailed = 0;
    m_ryokuProcessPending = false;
    m_ryokuActionBusy = false;
    setRyokuActionError(error);
    emit ryokuActionBusyChanged();
    emit ryokuActionFinished(action, succeeded, failed, error);
    m_ryokuQuitLocker.reset();
}

void DesktopIntegration::setRyokuActionError(const QString& error) {
    if (m_ryokuActionError == error)
        return;
    m_ryokuActionError = error;
    emit ryokuActionErrorChanged();
}
