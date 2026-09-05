// SPDX-License-Identifier: GPL-3.0-only

#include "PortalPickerRequest.hpp"

#include "locations/LocalPathGuard.hpp"

#include <QDBusArgument>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QMimeType>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {

constexpr qsizetype kMaximumStructuredResultBytes = 1024 * 1024;
constexpr qsizetype kMaximumFilters = 64;
constexpr qsizetype kMaximumFilterConditions = 128;
constexpr qsizetype kMaximumChoices = 32;
constexpr qsizetype kMaximumChoiceOptions = 64;
constexpr qsizetype kMaximumExpandedPatterns = 256;
constexpr qsizetype kMaximumMetadataString = 512;

bool validLeafName(const QString& name) {
    return !name.isEmpty()
        && !name.trimmed().isEmpty()
        && !name.contains(QLatin1Char('/'))
        && !name.contains(QChar::Null)
        && name != QStringLiteral(".")
        && name != QStringLiteral("..");
}

bool boundedString(const QString& value, qsizetype maximum = kMaximumMetadataString) {
    return value.size() <= maximum && !value.contains(QChar::Null);
}

bool validChoiceId(const QString& value) {
    return !value.isEmpty() && boundedString(value, 128);
}

bool validateChoice(PortalChoice* choice, QString* error) {
    if (!choice
        || !validChoiceId(choice->id)
        || choice->label.isEmpty()
        || !boundedString(choice->label)) {
        if (error)
            *error = QStringLiteral("Portal choice metadata is invalid");
        return false;
    }

    choice->boolean = choice->options.isEmpty();
    if (choice->boolean) {
        if (choice->initialSelection.isEmpty())
            choice->initialSelection = QStringLiteral("false");
        if (choice->initialSelection != QStringLiteral("true")
            && choice->initialSelection != QStringLiteral("false")) {
            if (error)
                *error = QStringLiteral("Portal boolean choice initial value is invalid");
            return false;
        }
        return true;
    }

    if (choice->options.size() > kMaximumChoiceOptions) {
        if (error)
            *error = QStringLiteral("Portal choice has too many options");
        return false;
    }

    QSet<QString> optionIds;
    for (const PortalChoiceOption& option : choice->options) {
        if (!validChoiceId(option.id)
            || option.label.isEmpty()
            || !boundedString(option.label)
            || optionIds.contains(option.id)) {
            if (error)
                *error = QStringLiteral("Portal choice option is invalid");
            return false;
        }
        optionIds.insert(option.id);
    }

    if (choice->initialSelection.isEmpty())
        choice->initialSelection = choice->options.constFirst().id;
    if (!optionIds.contains(choice->initialSelection)) {
        if (error)
            *error = QStringLiteral("Portal choice initial value is invalid");
        return false;
    }
    return true;
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

bool filtersEqual(const PortalFilter& left, const PortalFilter& right) {
    if (left.name != right.name || left.conditions.size() != right.conditions.size())
        return false;
    for (qsizetype i = 0; i < left.conditions.size(); ++i) {
        const PortalFilterCondition& a = left.conditions.at(i);
        const PortalFilterCondition& b = right.conditions.at(i);
        if (a.type != b.type || a.pattern != b.pattern)
            return false;
    }
    return true;
}

bool parseFilterOptions(
    const QVariantMap& options,
    QList<PortalFilter>* filters,
    int* initialFilterIndex,
    bool* filterLocked,
    QString* error) {
    if (!filters || !initialFilterIndex || !filterLocked)
        return false;

    *filters = PortalPickerParsing::decodeFilters(
        options.value(QStringLiteral("filters")), error);
    if (error && !error->isEmpty())
        return false;

    *filterLocked = false;
    *initialFilterIndex = filters->isEmpty() ? -1 : 0;
    if (!options.contains(QStringLiteral("current_filter")))
        return true;

    PortalFilter current = PortalPickerParsing::decodeFilter(
        options.value(QStringLiteral("current_filter")), error);
    if (error && !error->isEmpty())
        return false;

    if (filters->isEmpty()) {
        filters->push_back(std::move(current));
        *initialFilterIndex = 0;
        *filterLocked = true;
        return true;
    }

    for (qsizetype i = 0; i < filters->size(); ++i) {
        if (filtersEqual(filters->at(i), current)) {
            *initialFilterIndex = static_cast<int>(i);
            break;
        }
    }
    return true;
}

bool parseChoicesOption(
    const QVariantMap& options,
    QList<PortalChoice>* choices,
    QString* error) {
    if (!choices)
        return false;
    *choices = PortalPickerParsing::decodeChoices(
        options.value(QStringLiteral("choices")), error);
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

QStringList patternsForFilter(const PortalFilter& filter) {
    QSet<QString> seen;
    QStringList patterns;
    const auto appendPattern = [&seen, &patterns](const QString& pattern) {
        if (pattern.isEmpty()
            || seen.contains(pattern)
            || patterns.size() >= kMaximumExpandedPatterns) {
            return;
        }
        seen.insert(pattern);
        patterns.push_back(pattern);
    };

    QMimeDatabase database;
    for (const PortalFilterCondition& condition : filter.conditions) {
        if (condition.type == 0) {
            appendPattern(condition.pattern);
            continue;
        }

        const QString mimeName = condition.pattern.trimmed().toLower();
        if (mimeName.endsWith(QStringLiteral("/*"))) {
            const QString prefix = mimeName.left(mimeName.size() - 1);
            const QList<QMimeType> allTypes = database.allMimeTypes();
            for (const QMimeType& mime : allTypes) {
                if (!mime.name().toLower().startsWith(prefix))
                    continue;
                for (const QString& pattern : mime.globPatterns())
                    appendPattern(pattern);
                if (patterns.size() >= kMaximumExpandedPatterns)
                    break;
            }
            continue;
        }

        const QMimeType mime = database.mimeTypeForName(mimeName);
        if (!mime.isValid())
            continue;
        for (const QString& pattern : mime.globPatterns())
            appendPattern(pattern);
    }
    return patterns;
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
        if (conditions.size() >= kMaximumFilterConditions) {
            if (error)
                *error = QStringLiteral("Portal file filter has too many conditions");
            argument.endArray();
            return {};
        }

        quint32 type = 0;
        QString pattern;
        argument.beginStructure();
        argument >> type >> pattern;
        argument.endStructure();

        if (type > 1 || pattern.isEmpty() || !boundedString(pattern, 1024)) {
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
    if (!boundedString(filter.name)) {
        if (error)
            *error = QStringLiteral("Portal file-filter name is invalid");
        argument.endStructure();
        return {};
    }
    filter.conditions = decodeConditionsFromArgument(argument, error);
    argument.endStructure();
    return filter;
}

QList<PortalChoice> decodeChoicesFromArgument(
    const QDBusArgument& argument,
    QString* error) {
    QList<PortalChoice> choices;
    QSet<QString> choiceIds;

    argument.beginArray();
    while (!argument.atEnd()) {
        if (choices.size() >= kMaximumChoices) {
            if (error)
                *error = QStringLiteral("Portal request has too many choices");
            argument.endArray();
            return {};
        }

        PortalChoice choice;
        argument.beginStructure();
        argument >> choice.id >> choice.label;

        argument.beginArray();
        while (!argument.atEnd()) {
            if (choice.options.size() >= kMaximumChoiceOptions) {
                if (error)
                    *error = QStringLiteral("Portal choice has too many options");
                argument.endArray();
                argument.endStructure();
                argument.endArray();
                return {};
            }
            PortalChoiceOption option;
            argument.beginStructure();
            argument >> option.id >> option.label;
            argument.endStructure();
            choice.options.push_back(std::move(option));
        }
        argument.endArray();
        argument >> choice.initialSelection;
        argument.endStructure();

        if (choiceIds.contains(choice.id) || !validateChoice(&choice, error)) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Portal request contains duplicate choice IDs");
            argument.endArray();
            return {};
        }
        choiceIds.insert(choice.id);
        choices.push_back(std::move(choice));
    }
    argument.endArray();
    return choices;
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

bool validateStructuredSelections(
    const PortalPickerRequest& request,
    PortalPickerResult* result,
    bool structured,
    QString* error) {
    if (!result)
        return false;

    if (request.filters.isEmpty()) {
        result->selectedFilterIndex = -1;
    } else if (!structured) {
        result->selectedFilterIndex = request.initialFilterIndex;
    } else if (result->selectedFilterIndex < 0
        || result->selectedFilterIndex >= request.filters.size()) {
        if (error)
            *error = QStringLiteral("Picker returned an invalid selected filter");
        return false;
    }

    if (request.filterLocked
        && result->selectedFilterIndex != request.initialFilterIndex) {
        if (error)
            *error = QStringLiteral("Picker changed a locked portal file filter");
        return false;
    }

    QSet<QString> expectedIds;
    for (const PortalChoice& choice : request.choices) {
        expectedIds.insert(choice.id);
        QString selected;
        if (structured) {
            if (!result->choiceSelections.contains(choice.id)) {
                if (error)
                    *error = QStringLiteral("Picker omitted a portal choice result");
                return false;
            }
            selected = result->choiceSelections.value(choice.id);
        } else {
            selected = choice.initialSelection;
            result->choiceSelections.insert(choice.id, selected);
        }

        bool valid = false;
        if (choice.boolean) {
            valid = selected == QStringLiteral("true")
                || selected == QStringLiteral("false");
        } else {
            for (const PortalChoiceOption& option : choice.options) {
                if (option.id == selected) {
                    valid = true;
                    break;
                }
            }
        }
        if (!valid) {
            if (error)
                *error = QStringLiteral("Picker returned an invalid portal choice result");
            return false;
        }
    }

    if (structured) {
        for (auto it = result->choiceSelections.cbegin();
             it != result->choiceSelections.cend(); ++it) {
            if (!expectedIds.contains(it.key())) {
                if (error)
                    *error = QStringLiteral("Picker returned an unknown portal choice result");
                return false;
            }
        }
    }
    return true;
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
        && !parseFilterOptions(
            options,
            &request.filters,
            &request.initialFilterIndex,
            &request.filterLocked,
            &parseError)) {
        request.error = parseError;
        return request;
    }

    if (!parseChoicesOption(options, &request.choices, &parseError)) {
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

    if (!parseFilterOptions(
            options,
            &request.filters,
            &request.initialFilterIndex,
            &request.filterLocked,
            &parseError)) {
        request.error = parseError;
        return request;
    }

    if (!parseChoicesOption(options, &request.choices, &parseError)) {
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

    request.saveFileNames = PortalPickerParsing::decodeFileNames(
        options.value(QStringLiteral("files")), &parseError);
    if (!parseError.isEmpty()) {
        request.error = parseError;
        return request;
    }
    if (request.saveFileNames.isEmpty()) {
        request.error = QStringLiteral("SaveFiles request did not provide any file names");
        return request;
    }

    if (!parseChoicesOption(options, &request.choices, &parseError)) {
        request.error = parseError;
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

    return arguments;
}

QJsonObject PortalPickerRequest::pickerContextJson() const {
    QJsonArray filtersArray;
    for (const PortalFilter& filter : filters) {
        QJsonArray patternsArray;
        for (const QString& pattern : patternsForFilter(filter))
            patternsArray.push_back(pattern);
        filtersArray.push_back(QJsonObject{
            {QStringLiteral("name"), filter.name},
            {QStringLiteral("patterns"), patternsArray},
        });
    }

    QJsonArray choicesArray;
    for (const PortalChoice& choice : choices) {
        QJsonArray optionsArray;
        for (const PortalChoiceOption& option : choice.options) {
            optionsArray.push_back(QJsonObject{
                {QStringLiteral("id"), option.id},
                {QStringLiteral("label"), option.label},
            });
        }
        choicesArray.push_back(QJsonObject{
            {QStringLiteral("id"), choice.id},
            {QStringLiteral("label"), choice.label},
            {QStringLiteral("boolean"), choice.boolean},
            {QStringLiteral("initial"), choice.initialSelection},
            {QStringLiteral("options"), optionsArray},
        });
    }

    return QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("filters"), filtersArray},
        {QStringLiteral("initial_filter"), initialFilterIndex},
        {QStringLiteral("filter_locked"), filterLocked},
        {QStringLiteral("choices"), choicesArray},
    };
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
    if (standardOutput.size() > kMaximumStructuredResultBytes) {
        result.error = QStringLiteral("Picker result exceeded the portal result size limit");
        return result;
    }

    QStringList rawUris;
    const QByteArray trimmed = standardOutput.trimmed();
    const bool structured = trimmed.startsWith('{');
    if (structured) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            result.error = QStringLiteral("Picker returned malformed structured portal output");
            return result;
        }
        const QJsonObject object = document.object();
        if (object.value(QStringLiteral("version")).toInt() != 1) {
            result.error = QStringLiteral("Picker returned an unsupported portal result version");
            return result;
        }

        const QJsonArray uris = object.value(QStringLiteral("uris")).toArray();
        for (const QJsonValue& value : uris) {
            if (!value.isString()) {
                result.error = QStringLiteral("Picker returned a malformed URI result");
                return result;
            }
            rawUris.push_back(value.toString());
        }
        result.selectedFilterIndex = object.value(QStringLiteral("filter")).toInt(-1);

        const QJsonObject choicesObject = object.value(QStringLiteral("choices")).toObject();
        for (auto it = choicesObject.begin(); it != choicesObject.end(); ++it) {
            if (!it.value().isString()) {
                result.error = QStringLiteral("Picker returned a malformed choice result");
                return result;
            }
            result.choiceSelections.insert(it.key(), it.value().toString());
        }
    } else {
        const QList<QByteArray> lines = standardOutput.split('\n');
        for (QByteArray line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
            if (line.isEmpty())
                continue;
            rawUris.push_back(QString::fromUtf8(line));
        }
    }

    QString selectionError;
    if (!validateStructuredSelections(
            request,
            &result,
            structured,
            &selectionError)) {
        result.error = selectionError;
        return result;
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
        for (const QString& requestedName : request.saveFileNames) {
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

    if (rawUris.isEmpty() || (!request.multiple && rawUris.size() != 1)) {
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
            } else if (!info.exists() || !info.isFile()) {
                result.error = QStringLiteral("Open picker returned a non-file");
                return result;
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
    if (bytes.isEmpty() || bytes.at(bytes.size() - 1) != '\0') {
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
    if (bytes.isEmpty() || bytes.at(bytes.size() - 1) != '\0') {
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
            if (filters.size() >= kMaximumFilters) {
                if (error)
                    *error = QStringLiteral("Portal request has too many file filters");
                argument.endArray();
                return {};
            }
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
    if (list.size() > kMaximumFilters) {
        if (error)
            *error = QStringLiteral("Portal request has too many file filters");
        return {};
    }

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

    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return decodeFilterFromArgument(value.value<QDBusArgument>(), error);

    PortalFilter filter;
    const QVariantMap map = value.toMap();
    filter.name = map.value(QStringLiteral("name")).toString();
    if (!boundedString(filter.name)) {
        if (error)
            *error = QStringLiteral("Portal file-filter name is invalid");
        return {};
    }

    const QVariantList conditions = map.value(QStringLiteral("conditions")).toList();
    if (conditions.size() > kMaximumFilterConditions) {
        if (error)
            *error = QStringLiteral("Portal file filter has too many conditions");
        return {};
    }
    for (const QVariant& item : conditions) {
        const QVariantMap conditionMap = item.toMap();
        const quint32 type = conditionMap.value(QStringLiteral("type")).toUInt();
        const QString pattern = conditionMap.value(QStringLiteral("pattern")).toString();
        if (type > 1 || pattern.isEmpty() || !boundedString(pattern, 1024)) {
            if (error)
                *error = QStringLiteral("Unsupported or empty portal file-filter condition");
            return {};
        }
        filter.conditions.push_back({type, pattern});
    }
    return filter;
}

QList<PortalChoice> decodeChoices(const QVariant& value, QString* error) {
    if (error)
        error->clear();
    if (!value.isValid())
        return {};

    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return decodeChoicesFromArgument(value.value<QDBusArgument>(), error);

    const QVariantList list = value.toList();
    if (list.size() > kMaximumChoices) {
        if (error)
            *error = QStringLiteral("Portal request has too many choices");
        return {};
    }

    QList<PortalChoice> choices;
    QSet<QString> choiceIds;
    for (const QVariant& item : list) {
        const QVariantMap map = item.toMap();
        PortalChoice choice;
        choice.id = map.value(QStringLiteral("id")).toString();
        choice.label = map.value(QStringLiteral("label")).toString();
        choice.initialSelection = map.value(QStringLiteral("initial")).toString();

        const QVariantList options = map.value(QStringLiteral("options")).toList();
        if (options.size() > kMaximumChoiceOptions) {
            if (error)
                *error = QStringLiteral("Portal choice has too many options");
            return {};
        }
        for (const QVariant& optionItem : options) {
            const QVariantMap optionMap = optionItem.toMap();
            choice.options.push_back({
                optionMap.value(QStringLiteral("id")).toString(),
                optionMap.value(QStringLiteral("label")).toString(),
            });
        }

        if (choiceIds.contains(choice.id) || !validateChoice(&choice, error)) {
            if (error && error->isEmpty())
                *error = QStringLiteral("Portal request contains duplicate choice IDs");
            return {};
        }
        choiceIds.insert(choice.id);
        choices.push_back(std::move(choice));
    }
    return choices;
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
