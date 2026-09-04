// SPDX-License-Identifier: GPL-3.0-only
#include "portal/PortalRoutingManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

constexpr auto kPreferredSection = "preferred";
constexpr auto kFileChooserKey = "org.freedesktop.impl.portal.FileChooser";
constexpr auto kRyofilesBackend = "ryofiles";
constexpr int kStateVersion = 1;

struct ParsedConfig {
    bool ok = false;
    QString error;
    QStringList lines;
    bool endedWithNewline = false;
    int preferredHeader = -1;
    int preferredEnd = -1;
    int keyIndex = -1;
    QString keyLine;
    QString value;
};

struct StoredState {
    bool exists = false;
    bool ok = false;
    QString error;
    QString configPath;
    bool originalPresent = false;
    QString originalLine;
    QString managedValue;
};

QString cleanAbsolutePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString configHome() {
    const QString fromEnvironment = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!fromEnvironment.isEmpty())
        return QDir::cleanPath(fromEnvironment);
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.config"));
}

QString stateHome() {
    const QString fromEnvironment = qEnvironmentVariable("XDG_STATE_HOME");
    if (!fromEnvironment.isEmpty())
        return QDir::cleanPath(fromEnvironment);
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.local/state"));
}

bool readRegularFile(const QString& path, QByteArray* bytes, QString* error) {
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("Portal routing config does not exist: %1").arg(path);
        return false;
    }
    if (info.isSymLink()) {
        if (error)
            *error = QStringLiteral("Refusing to modify symlinked portal routing config: %1").arg(path);
        return false;
    }
    if (!info.isFile()) {
        if (error)
            *error = QStringLiteral("Portal routing config is not a regular file: %1").arg(path);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not read portal routing config: %1").arg(file.errorString());
        return false;
    }

    *bytes = file.readAll();
    return true;
}

ParsedConfig parseConfig(const QByteArray& bytes) {
    ParsedConfig parsed;
    const QString text = QString::fromUtf8(bytes);
    parsed.endedWithNewline = text.endsWith(QLatin1Char('\n'));
    parsed.lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    if (parsed.endedWithNewline && !parsed.lines.isEmpty() && parsed.lines.constLast().isEmpty())
        parsed.lines.removeLast();

    const QRegularExpression sectionPattern(
        QStringLiteral(R"(^\s*\[([^\]]+)\]\s*$)"));
    const QRegularExpression keyPattern(
        QStringLiteral(R"(^\s*org\.freedesktop\.impl\.portal\.FileChooser\s*=\s*(.*?)\s*$)"));

    int preferredCount = 0;
    int keyCount = 0;
    bool inPreferred = false;

    for (int index = 0; index < parsed.lines.size(); ++index) {
        const QString& line = parsed.lines.at(index);
        const QRegularExpressionMatch sectionMatch = sectionPattern.match(line);
        if (sectionMatch.hasMatch()) {
            if (inPreferred && parsed.preferredEnd < 0)
                parsed.preferredEnd = index;

            const QString section = sectionMatch.captured(1).trimmed();
            inPreferred = section.compare(
                              QString::fromLatin1(kPreferredSection),
                              Qt::CaseInsensitive)
                == 0;
            if (inPreferred) {
                ++preferredCount;
                parsed.preferredHeader = index;
            }
            continue;
        }

        if (!inPreferred)
            continue;

        const QRegularExpressionMatch keyMatch = keyPattern.match(line);
        if (!keyMatch.hasMatch())
            continue;

        ++keyCount;
        parsed.keyIndex = index;
        parsed.keyLine = line;
        parsed.value = keyMatch.captured(1).trimmed();
    }

    if (inPreferred && parsed.preferredEnd < 0)
        parsed.preferredEnd = parsed.lines.size();

    if (preferredCount != 1) {
        parsed.error = preferredCount == 0
            ? QStringLiteral("Portal routing config has no [preferred] section")
            : QStringLiteral("Portal routing config has multiple [preferred] sections");
        return parsed;
    }
    if (keyCount > 1) {
        parsed.error = QStringLiteral("Portal routing config has multiple FileChooser entries");
        return parsed;
    }

    parsed.ok = true;
    return parsed;
}

QStringList backendTokens(const QString& value) {
    QStringList result;
    const QStringList pieces = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& piece : pieces) {
        const QString token = piece.trimmed();
        if (!token.isEmpty())
            result.append(token);
    }
    return result;
}

bool containsRyofiles(const QString& value) {
    return backendTokens(value).contains(QString::fromLatin1(kRyofilesBackend));
}

QString managedBackendValue(const QString& originalValue) {
    QStringList backends = backendTokens(originalValue);
    backends.removeAll(QString::fromLatin1(kRyofilesBackend));
    backends.prepend(QString::fromLatin1(kRyofilesBackend));
    return backends.join(QLatin1Char(';'));
}

QByteArray serializeConfig(const ParsedConfig& parsed) {
    QString text = parsed.lines.join(QLatin1Char('\n'));
    if (parsed.endedWithNewline)
        text.append(QLatin1Char('\n'));
    return text.toUtf8();
}

bool writeConfigAtomically(
    const QString& path,
    const QByteArray& bytes,
    QFileDevice::Permissions permissions,
    QString* error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open portal routing config for update: %1")
                         .arg(file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = QStringLiteral("Could not write portal routing config: %1")
                         .arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    file.setPermissions(permissions);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not atomically replace portal routing config: %1")
                         .arg(file.errorString());
        return false;
    }
    return true;
}

StoredState readState(const QString& path) {
    StoredState state;
    const QFileInfo info(path);
    if (!info.exists()) {
        state.ok = true;
        return state;
    }
    state.exists = true;
    if (info.isSymLink() || !info.isFile()) {
        state.error = QStringLiteral("Portal routing state is not a regular file: %1").arg(path);
        return state;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        state.error = QStringLiteral("Could not read portal routing state: %1").arg(file.errorString());
        return state;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        state.error = QStringLiteral("Portal routing state is invalid JSON");
        return state;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("version")).toInt(-1) != kStateVersion
        || !object.value(QStringLiteral("configPath")).isString()
        || !object.value(QStringLiteral("originalPresent")).isBool()
        || !object.value(QStringLiteral("originalLine")).isString()
        || !object.value(QStringLiteral("managedValue")).isString()) {
        state.error = QStringLiteral("Portal routing state has an unsupported schema");
        return state;
    }

    state.configPath = object.value(QStringLiteral("configPath")).toString();
    state.originalPresent = object.value(QStringLiteral("originalPresent")).toBool();
    state.originalLine = object.value(QStringLiteral("originalLine")).toString();
    state.managedValue = object.value(QStringLiteral("managedValue")).toString();
    state.ok = true;
    return state;
}

bool writeState(
    const QString& path,
    const QString& configPath,
    bool originalPresent,
    const QString& originalLine,
    const QString& managedValue,
    QString* error) {
    const QFileInfo stateInfo(path);
    if (stateInfo.exists() && (stateInfo.isSymLink() || !stateInfo.isFile())) {
        if (error)
            *error = QStringLiteral("Portal routing state is not a regular file: %1").arg(path);
        return false;
    }

    const QString directoryPath = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directoryPath)) {
        if (error)
            *error = QStringLiteral("Could not create portal routing state directory: %1")
                         .arg(directoryPath);
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("version"), kStateVersion);
    object.insert(QStringLiteral("configPath"), cleanAbsolutePath(configPath));
    object.insert(QStringLiteral("originalPresent"), originalPresent);
    object.insert(QStringLiteral("originalLine"), originalLine);
    object.insert(QStringLiteral("managedValue"), managedValue);

    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not write portal routing state: %1").arg(file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = QStringLiteral("Could not write portal routing state: %1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Could not commit portal routing state: %1").arg(file.errorString());
        return false;
    }
    return true;
}

bool removeState(const QString& path, QString* error = nullptr) {
    if (!QFileInfo::exists(path))
        return true;
    if (!QFile::remove(path)) {
        if (error)
            *error = QStringLiteral("Could not remove portal routing state: %1").arg(path);
        return false;
    }
    return true;
}

} // namespace

PortalRoutingManager::PortalRoutingManager(Paths paths)
    : m_paths{cleanAbsolutePath(paths.configPath), cleanAbsolutePath(paths.statePath)} {
}

PortalRoutingManager::Paths PortalRoutingManager::defaultPaths() {
    return {
        QDir::cleanPath(
            configHome()
            + QStringLiteral("/xdg-desktop-portal/hyprland-portals.conf")),
        QDir::cleanPath(
            stateHome()
            + QStringLiteral("/ryofiles/portal-routing.json")),
    };
}

PortalRoutingManager::Status PortalRoutingManager::status() const {
    Status result;
    QByteArray bytes;
    QString error;
    if (!readRegularFile(m_paths.configPath, &bytes, &error)) {
        result.message = error;
        return result;
    }

    const ParsedConfig parsed = parseConfig(bytes);
    if (!parsed.ok) {
        result.message = parsed.error;
        return result;
    }

    result.backendList = parsed.keyIndex >= 0 ? parsed.value : QString();
    result.enabled = parsed.keyIndex >= 0 && containsRyofiles(parsed.value);

    const StoredState state = readState(m_paths.statePath);
    if (!state.ok) {
        result.message = state.error;
        return result;
    }
    if (state.exists && cleanAbsolutePath(state.configPath) != m_paths.configPath) {
        result.message = QStringLiteral("Portal routing state belongs to a different config file");
        return result;
    }

    result.managed = state.exists
        && result.enabled
        && parsed.value == state.managedValue;
    result.ok = true;
    if (result.managed) {
        result.message = QStringLiteral("Ryofiles is the managed FileChooser preference");
    } else if (result.enabled) {
        result.message = QStringLiteral("Ryofiles is present in externally managed FileChooser routing");
    } else {
        result.message = QStringLiteral("Ryofiles is not the FileChooser preference");
    }
    return result;
}

PortalRoutingManager::Result PortalRoutingManager::enable() {
    Result result;
    QByteArray bytes;
    QString error;
    if (!readRegularFile(m_paths.configPath, &bytes, &error)) {
        result.message = error;
        return result;
    }

    ParsedConfig parsed = parseConfig(bytes);
    if (!parsed.ok) {
        result.message = parsed.error;
        return result;
    }

    StoredState state = readState(m_paths.statePath);
    if (!state.ok) {
        result.message = state.error;
        return result;
    }
    if (state.exists && cleanAbsolutePath(state.configPath) != m_paths.configPath) {
        result.message = QStringLiteral("Portal routing state belongs to a different config file");
        return result;
    }

    if (state.exists) {
        if (parsed.keyIndex >= 0 && parsed.value == state.managedValue && containsRyofiles(parsed.value)) {
            result.ok = true;
            result.message = QStringLiteral("Ryofiles FileChooser routing is already enabled");
            return result;
        }
        if (parsed.keyIndex < 0 || !containsRyofiles(parsed.value)) {
            if (!removeState(m_paths.statePath, &error)) {
                result.message = error;
                return result;
            }
            state = StoredState{};
            state.ok = true;
        } else {
            result.message = QStringLiteral(
                "FileChooser routing changed after Ryofiles enabled it; refusing to overwrite external changes");
            return result;
        }
    }

    if (parsed.keyIndex >= 0 && containsRyofiles(parsed.value)) {
        result.ok = true;
        result.message = QStringLiteral(
            "Ryofiles is already present in externally managed FileChooser routing; no changes made");
        return result;
    }

    const bool originalPresent = parsed.keyIndex >= 0;
    const QString originalLine = originalPresent ? parsed.keyLine : QString();
    const QString originalValue = originalPresent ? parsed.value : QString();
    const QString managedValue = managedBackendValue(originalValue);

    if (!writeState(
            m_paths.statePath,
            m_paths.configPath,
            originalPresent,
            originalLine,
            managedValue,
            &error)) {
        result.message = error;
        return result;
    }

    if (originalPresent) {
        parsed.lines[parsed.keyIndex] = QString::fromLatin1(kFileChooserKey)
            + QLatin1Char('=') + managedValue;
    } else {
        parsed.lines.insert(
            parsed.preferredEnd,
            QString::fromLatin1(kFileChooserKey) + QLatin1Char('=') + managedValue);
    }

    const QFileDevice::Permissions permissions = QFileInfo(m_paths.configPath).permissions();
    if (!writeConfigAtomically(
            m_paths.configPath,
            serializeConfig(parsed),
            permissions,
            &error)) {
        removeState(m_paths.statePath);
        result.message = error;
        return result;
    }

    result.ok = true;
    result.changed = true;
    result.message = QStringLiteral(
        "Enabled Ryofiles FileChooser routing with the previous backend list preserved as fallback");
    return result;
}

PortalRoutingManager::Result PortalRoutingManager::disable() {
    Result result;
    QByteArray bytes;
    QString error;
    if (!readRegularFile(m_paths.configPath, &bytes, &error)) {
        result.message = error;
        return result;
    }

    ParsedConfig parsed = parseConfig(bytes);
    if (!parsed.ok) {
        result.message = parsed.error;
        return result;
    }

    const StoredState state = readState(m_paths.statePath);
    if (!state.ok) {
        result.message = state.error;
        return result;
    }

    const bool currentlyContainsRyofiles =
        parsed.keyIndex >= 0 && containsRyofiles(parsed.value);

    if (!state.exists) {
        if (currentlyContainsRyofiles) {
            result.message = QStringLiteral(
                "Ryofiles is configured externally; refusing to remove a FileChooser preference this tool did not create");
            return result;
        }
        result.ok = true;
        result.message = QStringLiteral("Ryofiles FileChooser routing is already disabled");
        return result;
    }

    if (cleanAbsolutePath(state.configPath) != m_paths.configPath) {
        result.message = QStringLiteral("Portal routing state belongs to a different config file");
        return result;
    }

    if (!currentlyContainsRyofiles) {
        if (!removeState(m_paths.statePath, &error)) {
            result.message = error;
            return result;
        }
        result.ok = true;
        result.message = QStringLiteral(
            "FileChooser routing was already restored externally; cleared stale Ryofiles state");
        return result;
    }

    if (parsed.value != state.managedValue) {
        result.message = QStringLiteral(
            "FileChooser routing changed after Ryofiles enabled it; refusing to clobber external changes");
        return result;
    }

    if (state.originalPresent) {
        parsed.lines[parsed.keyIndex] = state.originalLine;
    } else {
        parsed.lines.removeAt(parsed.keyIndex);
    }

    const QFileDevice::Permissions permissions = QFileInfo(m_paths.configPath).permissions();
    if (!writeConfigAtomically(
            m_paths.configPath,
            serializeConfig(parsed),
            permissions,
            &error)) {
        result.message = error;
        return result;
    }

    if (!removeState(m_paths.statePath, &error)) {
        result.ok = true;
        result.changed = true;
        result.message = QStringLiteral(
            "Restored the previous FileChooser routing, but stale Ryofiles state could not be removed: %1")
                             .arg(error);
        return result;
    }

    result.ok = true;
    result.changed = true;
    result.message = QStringLiteral("Restored the exact previous FileChooser routing");
    return result;
}
