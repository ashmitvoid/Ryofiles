// SPDX-License-Identifier: GPL-3.0-only

#include "PortalPickerRequest.hpp"

#include "locations/LocalPathGuard.hpp"

#include <QDBusArgument>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace {

bool validLeafName(const QString& name) {
    return !name.isEmpty()
        && !name.trimmed().isEmpty()
        && !name.contains(QLatin1Char('/'))
        && !name.contains(QChar::Null)
        && name != QStringLiteral(".")
        && name != QStringLiteral("..");
}

QString existingDirectoryFromOption(
    const QVariantMap& options,
    const QString& key,
    QString* error) {
    if (!options.contains(key))
        return QDir::homePath();

    const QByteArray raw = options.value(key).toByteArray();
    const QString path = PortalPickerParsing::decodeNullTerminatedPath(raw, error);
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        if (error)
            *error = QStringLiteral("Portal current folder is not an existing local directory");
        return {};
    }
    return QDir::cleanPath(info.absoluteFilePath());
}

bool parseFilterOptions(
    const QVariantMap& options,
    QList<PortalFilter>* filters,
    QString* error) {
    if (!filters)
        return false;

    if (options.contains(QStringLiteral("current_filter"))) {
        PortalFilter current = PortalPickerParsing::decodeFilter(
            options.value(QStringLiteral("current_filter")), error);
        if (error && !error->isEmpty())
            return false;
        if (!current.conditions.isEmpty())
            filters->push_back(std::move(current));
        return true;
    }

    *filters = PortalPickerParsing::decodeFilters(
        options.value(QStringLiteral("filters")), error);
    return !error || error->isEmpty();
}

bool mimeMatches(const QString& path, const QString& filter) {
    QMimeDatabase database;
    const QMimeType mime = database.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    if (!mime.isValid())
        return false;

    const QString normalized = filter.trimmed().toLower();
    if (normalized.endsWith(QStringLiteral("/*"))) {
        const QString prefix = normalized.left(normalized.size() - 1);
        return mime.name().toLower().startsWith(prefix);
    }
    return mime.name().compare(normalized, Qt::CaseInsensitive) == 0
        || mime.inherits(normalized);
}

bool globMatches(const QString& fileName, const QString& pattern) {
    const QString expression = QRegularExpression::wildcardToRegularExpression(pattern);
    const QRegularExpression regex(expression);
    return regex.isValid() && regex.match(fileName).hasMatch();
}

PortalPickerRequest baseRequest(
    PortalPickerKind kind,
    const QString& title,
    const QVariantMap& options) {
    PortalPickerRequest request;
    request.kind = kind;
    request.title = title;
    request.modal = options.value(QStringLiteral("modal"), true).toBool();
    request.acceptLabel = options.value(QStringLiteral("accept_label")).toString();
    return request;
}

QString normalizeLocalUri(const QString& raw, QString* localPath, QString* error) {
    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || !url.isLocalFile() || url.scheme() != QStringLiteral("file")) {
        if (error)
            *error = QStringLiteral("Picker returned a non-local or invalid URI");
        return {};
    }

    const QString path = QDir::cleanPath(url.toLocalFile());
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || LocalPathGuard::isUriLike(path)) {
        if (error)
            *error = QStringLiteral("Picker returned an invalid local path");
        return {};
    }

    if (localPath)
        *localPath = path;
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QList<PortalFilterCondition> decodeConditionsFromArgument(
    const QDBusArgument& argument,
    QString* error) {
    QList<PortalFilterCondition> conditions;
    argument.beginArray();
    while (!argument.atEnd()) {
        quint32 type = 0;
        QString pattern;
        argument.beginStructure();
        argument >> type >> pattern;
        argument.endStructure();

        if (type > 1 || pattern.isEmpty()) {
            if (error)
                *error = QStringLiteral("Unsupported or empty portal file-filter condition");
            argument.endArray();
            return {};
        }
        conditions.push_back({type, pattern});
    }
    argument.endArray();
    return conditions;
}

PortalFilter decodeFilterFromArgument(const QDBusArgument& argument, QString* error) {
    PortalFilter filter;
    argument.beginStructure();
    argument >> filter.name;
    filter.conditions = decodeConditionsFromArgument(argument, error);
    argument.endStructure();
    return filter;
}

QString collisionStem(const QString& name, QString* suffix) {
    const qsizetype lastDot = name.lastIndexOf(QLatin1Char('.'));
    if (lastDot > 0 && lastDot < name.size() - 1) {
        if (suffix)
            *suffix = name.mid(lastDot);
        return name.left(lastDot);
    }
    if (suffix)
        suffix->clear();
    return name;
}

} // namespace

PortalPickerRequest PortalPickerRequest::openFile(
    const QString& title,
    const QVariantMap& options) {
    PortalPickerRequest request = baseRequest(PortalPickerKind::OpenFile, title, options);

    request.multiple = options.value(QStringLiteral("multiple"), false).toBool();
    const bool directory = options.value(QStringLiteral("directory"), false).toBool();
    request.mode = directory ? QStringLiteral("folder") : QStringLiteral("open");
    if (directory)
        request.multiple = false;

    QString parseError;
    request.initialDirectory = existingDirectoryFromOption(
        options, QStringLiteral("current_folder"), &parseError);
    if (!parseError.isEmpty()) {
        request.error = parseError;
        return request;
    }

    if (!directory
        && !parseFilterOptions(options, &request.filters, &parseError)) {
        request.error = parseError;
        return request;
    }

    request.valid = true;
    return request;
}

PortalPickerRequest PortalPickerRequest::saveFile(
    const QString& title,
    const QVariantMap& options) {
    PortalPickerRequest request = baseRequest(PortalPickerKind::SaveFile, title, options);
    request.mode = QStringLiteral("save");

    QString parseError;
    request.initialDirectory = existingDirectoryFromOption(
        options, QStringLiteral("current_folder"), &parseError);
    if (!parseError.isEmpty()) {
        request.error = parseError;
        return request;
    }

    if (options.contains(QStringLiteral("current_file"))) {
        const QString currentFile = PortalPickerParsing::decodeNullTerminatedPath(
            options.value(QStringLiteral("current_file")).toByteArray(), &parseError);
        if (currentFile.isEmpty()) {
            request.error = parseError;
            return request;
        }

        const QFileInfo info(currentFile);
        const QFileInfo parent(info.absolutePath());
        if (!parent.exists() || !parent.isDir()) {
            request.error = QStringLiteral("Portal current file has no existing parent directory");
            return request;
        }
        request.initialDirectory = QDir::cleanPath(parent.absoluteFilePath());
        request.suggestedName = info.fileName();
    }

    const QString currentName = options.value(QStringLiteral("current_name")).toString();
    if (!currentName.isEmpty())
        request.suggestedName = currentName;
    if (!request.suggestedName.isEmpty() && !validLeafName(request.suggestedName)) {
        request.error = QStringLiteral("Portal suggested file name is invalid");
        return request;
    }

    if (!parseFilterOptions(options, &request.filters, &parseError)) {
        request.error = parseError;
        return request;
    }

    request.valid = true;
    return request;
}

PortalPickerRequest PortalPickerRequest::saveFiles(
    const QString& title,
    const QVariantMap& options) {
    PortalPickerRequest request = baseRequest(PortalPickerKind::SaveFiles, title, options);
    request.mode = QStringLiteral("folder");

    QString parseError;
    request.initialDirectory = existingDirectoryFromOption(
        options, QStringLiteral("current_folder"), &parseError);
    if (!parseError.isEmpty()) {
        request.error = parseError;
        return request;
    }

    request.saveFiles = PortalPickerParsing::decodeFileNames(
        options.value(QStringLiteral("files")), &parseError);
    if (!parseError.isEmpty()) {
        request.error = parseError;
        return request;
    }
    if (request.saveFiles.isEmpty()) {
        request.error = QStringLiteral("SaveFiles request did not provide any file names");
        return request;
    }

    request.valid = true;
    return request;
}

QStringList PortalPickerRequest::pickerArguments() const {
    QStringList arguments;
    arguments << QStringLiteral("--picker") << mode;

    if (multiple)
        arguments << QStringLiteral("--multiple");
    if (!initialDirectory.isEmpty())
        arguments << QStringLiteral("--initial-dir") << initialDirectory;
    if (!suggestedName.isEmpty())
        arguments << QStringLiteral("--suggest-name") << suggestedName;

    QSet<QString> seenMimeTypes;
    for (const PortalFilter& filter : filters) {
        for (const PortalFilterCondition& condition : filter.conditions) {
            if (condition.type != 1)
                continue;
            const QString normalized = condition.pattern.trimmed().toLower();
            if (normalized.isEmpty() || seenMimeTypes.contains(normalized))
                continue;
            seenMimeTypes.insert(normalized);
            arguments << QStringLiteral("--mime") << normalized;
        }
    }
    return arguments;
}

bool PortalPickerRequest::pathMatchesFilters(const QString& path) const {
    if (filters.isEmpty())
        return true;

    const QString fileName = QFileInfo(path).fileName();
    for (const PortalFilter& filter : filters) {
        for (const PortalFilterCondition& condition : filter.conditions) {
            if (condition.type == 0 && globMatches(fileName, condition.pattern))
                return true;
            if (condition.type == 1 && mimeMatches(path, condition.pattern))
                return true;
        }
    }
    return false;
}

PortalPickerResult PortalPickerResult::fromPickerStdout(
    const PortalPickerRequest& request,
    const QByteArray& standardOutput) {
    PortalPickerResult result;
    if (!request.valid) {
        result.error = request.error;
        return result;
    }

    QStringList rawUris;
    const QList<QByteArray> lines = standardOutput.split('\n');
    for (QByteArray line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty())
            continue;
        rawUris.push_back(QString::fromUtf8(line));
    }

    if (request.kind == PortalPickerKind::SaveFiles) {
        if (rawUris.size() != 1) {
            result.error = QStringLiteral("Folder picker returned an unexpected number of results");
            return result;
        }

        QString folder;
        QString error;
        normalizeLocalUri(rawUris.constFirst(), &folder, &error);
        if (!error.isEmpty()) {
            result.error = error;
            return result;
        }
        const QFileInfo folderInfo(folder);
        if (!folderInfo.exists() || !folderInfo.isDir()) {
            result.error = QStringLiteral("SaveFiles picker result is not an existing directory");
            return result;
        }

        QStringList reserved;
        for (const QString& requestedName : request.saveFiles) {
            const QString destinationName = PortalPickerParsing::uniqueDestinationName(
                folder, requestedName, reserved);
            if (destinationName.isEmpty()) {
                result.error = QStringLiteral("Could not construct a safe SaveFiles destination");
                return result;
            }
            reserved.push_back(destinationName);
            result.uris.push_back(
                QUrl::fromLocalFile(QDir(folder).filePath(destinationName))
                    .toString(QUrl::FullyEncoded));
        }
        result.valid = true;
        return result;
    }

    const int expectedMinimum = request.multiple ? 1 : 1;
    if (rawUris.size() < expectedMinimum
        || (!request.multiple && rawUris.size() != 1)) {
        result.error = QStringLiteral("Picker returned an unexpected number of results");
        return result;
    }

    QSet<QString> seen;
    for (const QString& rawUri : rawUris) {
        QString localPath;
        QString error;
        const QString normalized = normalizeLocalUri(rawUri, &localPath, &error);
        if (!error.isEmpty()) {
            result.error = error;
            return result;
        }
        if (seen.contains(normalized))
            continue;

        const QFileInfo info(localPath);
        if (request.kind == PortalPickerKind::OpenFile) {
            if (request.mode == QStringLiteral("folder")) {
                if (!info.exists() || !info.isDir()) {
                    result.error = QStringLiteral("Folder picker returned a non-directory");
                    return result;
                }
            } else {
                if (!info.exists() || !info.isFile()) {
                    result.error = QStringLiteral("Open picker returned a non-file");
                    return result;
                }
                if (!request.pathMatchesFilters(localPath)) {
                    result.error = QStringLiteral("Picker result does not match the portal file filter");
                    return result;
                }
            }
        } else if (request.kind == PortalPickerKind::SaveFile) {
            if (info.isDir()) {
                result.error = QStringLiteral("Save picker returned an existing directory");
                return result;
            }
            const QFileInfo parent(info.absolutePath());
            if (!parent.exists() || !parent.isDir()) {
                result.error = QStringLiteral("Save picker returned a path without an existing parent");
                return result;
            }
            if (!request.pathMatchesFilters(localPath)) {
                result.error = QStringLiteral("Save picker result does not match the portal file filter");
                return result;
            }
        }

        seen.insert(normalized);
        result.uris.push_back(normalized);
    }

    if (result.uris.isEmpty()) {
        result.error = QStringLiteral("Picker returned no usable local file URI");
        return result;
    }
    if (!request.multiple && result.uris.size() != 1) {
        result.error = QStringLiteral("Single-selection picker returned multiple unique results");
        result.uris.clear();
        return result;
    }

    result.valid = true;
    return result;
}

namespace PortalPickerParsing {

QString decodeNullTerminatedPath(const QByteArray& bytes, QString* error) {
    if (error)
        error->clear();
    if (bytes.isEmpty() || bytes.constLast() != '\0') {
        if (error)
            *error = QStringLiteral("Portal filesystem path is not NUL-terminated");
        return {};
    }

    const QByteArray content = bytes.first(bytes.size() - 1);
    if (content.contains('\0')) {
        if (error)
            *error = QStringLiteral("Portal filesystem path contains an embedded NUL");
        return {};
    }

    const QString decoded = QFile::decodeName(content);
    if (decoded.isEmpty()
        || !QDir::isAbsolutePath(decoded)
        || LocalPathGuard::isUriLike(decoded)) {
        if (error)
            *error = QStringLiteral("Portal filesystem path is not an absolute local path");
        return {};
    }
    return QDir::cleanPath(decoded);
}

QString decodeNullTerminatedLeafName(const QByteArray& bytes, QString* error) {
    if (error)
        error->clear();
    if (bytes.isEmpty() || bytes.constLast() != '\0') {
        if (error)
            *error = QStringLiteral("Portal file name is not NUL-terminated");
        return {};
    }

    const QByteArray content = bytes.first(bytes.size() - 1);
    if (content.contains('\0')) {
        if (error)
            *error = QStringLiteral("Portal file name contains an embedded NUL");
        return {};
    }

    const QString decoded = QFile::decodeName(content);
    if (!validLeafName(decoded)) {
        if (error)
            *error = QStringLiteral("Portal file name is not a safe leaf name");
        return {};
    }
    return decoded;
}

QList<PortalFilter> decodeFilters(const QVariant& value, QString* error) {
    if (error)
        error->clear();
    if (!value.isValid())
        return {};

    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        QList<PortalFilter> filters;
        argument.beginArray();
        while (!argument.atEnd()) {
            PortalFilter filter = decodeFilterFromArgument(argument, error);
            if (error && !error->isEmpty()) {
                argument.endArray();
                return {};
            }
            filters.push_back(std::move(filter));
        }
        argument.endArray();
        return filters;
    }

    const QVariantList list = value.toList();
    QList<PortalFilter> filters;
    for (const QVariant& item : list) {
        PortalFilter filter = decodeFilter(item, error);
        if (error && !error->isEmpty())
            return {};
        filters.push_back(std::move(filter));
    }
    return filters;
}

PortalFilter decodeFilter(const QVariant& value, QString* error) {
    if (error)
        error->clear();

    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        return decodeFilterFromArgument(value.value<QDBusArgument>(), error);
    }

    PortalFilter filter;
    const QVariantMap map = value.toMap();
    filter.name = map.value(QStringLiteral("name")).toString();
    const QVariantList conditions = map.value(QStringLiteral("conditions")).toList();
    for (const QVariant& item : conditions) {
        const QVariantMap conditionMap = item.toMap();
        const quint32 type = conditionMap.value(QStringLiteral("type")).toUInt();
        const QString pattern = conditionMap.value(QStringLiteral("pattern")).toString();
        if (type > 1 || pattern.isEmpty()) {
            if (error)
                *error = QStringLiteral("Unsupported or empty portal file-filter condition");
            return {};
        }
        filter.conditions.push_back({type, pattern});
    }
    return filter;
}

QStringList decodeFileNames(const QVariant& value, QString* error) {
    if (error)
        error->clear();
    if (!value.isValid()) {
        if (error)
            *error = QStringLiteral("SaveFiles request is missing file names");
        return {};
    }

    QList<QByteArray> encoded;
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd()) {
            QByteArray item;
            argument >> item;
            encoded.push_back(item);
        }
        argument.endArray();
    } else {
        for (const QVariant& item : value.toList())
            encoded.push_back(item.toByteArray());
    }

    QStringList names;
    QSet<QString> seen;
    for (const QByteArray& item : encoded) {
        QString itemError;
        const QString name = decodeNullTerminatedLeafName(item, &itemError);
        if (name.isEmpty()) {
            if (error)
                *error = itemError;
            return {};
        }
        if (seen.contains(name)) {
            if (error)
                *error = QStringLiteral("SaveFiles request contains duplicate file names");
            return {};
        }
        seen.insert(name);
        names.push_back(name);
    }
    return names;
}

QString uniqueDestinationName(
    const QString& directory,
    const QString& requestedName,
    const QStringList& alreadyReserved) {
    if (!validLeafName(requestedName))
        return {};

    const QDir dir(directory);
    const QFileInfo directoryInfo(directory);
    if (!directoryInfo.exists() || !directoryInfo.isDir())
        return {};

    const QSet<QString> reserved(alreadyReserved.cbegin(), alreadyReserved.cend());
    const auto occupied = [&dir, &reserved](const QString& name) {
        if (reserved.contains(name))
            return true;
        const QFileInfo info(dir.filePath(name));
        return info.exists() || info.isSymLink();
    };

    if (!occupied(requestedName))
        return requestedName;

    QString suffix;
    const QString stem = collisionStem(requestedName, &suffix);
    for (int i = 1; i < 100000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
        if (!occupied(candidate))
            return candidate;
    }
    return {};
}

} // namespace PortalPickerParsing
