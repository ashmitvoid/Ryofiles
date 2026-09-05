// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

struct PortalFilterCondition {
    quint32 type = 0; // 0 = glob, 1 = MIME type
    QString pattern;
};

struct PortalFilter {
    QString name;
    QList<PortalFilterCondition> conditions;
};

struct PortalChoiceOption {
    QString id;
    QString label;
};

struct PortalChoice {
    QString id;
    QString label;
    QList<PortalChoiceOption> options;
    QString initialSelection;
    bool boolean = false;
};

struct PortalChoiceSelection {
    QString id;
    QString selection;
};

struct PortalChoiceSelectionList {
    QList<PortalChoiceSelection> values;
};

Q_DECLARE_METATYPE(PortalFilterCondition)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(PortalChoiceSelection)
Q_DECLARE_METATYPE(PortalChoiceSelectionList)

inline QDBusArgument& operator<<(
    QDBusArgument& argument,
    const PortalFilterCondition& condition) {
    argument.beginStructure();
    argument << condition.type << condition.pattern;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    PortalFilterCondition& condition) {
    argument.beginStructure();
    argument >> condition.type >> condition.pattern;
    argument.endStructure();
    return argument;
}

inline QDBusArgument& operator<<(
    QDBusArgument& argument,
    const PortalFilter& filter) {
    argument.beginStructure();
    argument << filter.name;
    argument.beginArray(QMetaType::fromType<PortalFilterCondition>());
    for (const PortalFilterCondition& condition : filter.conditions)
        argument << condition;
    argument.endArray();
    argument.endStructure();
    return argument;
}

inline const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    PortalFilter& filter) {
    argument.beginStructure();
    argument >> filter.name;
    argument.beginArray();
    filter.conditions.clear();
    while (!argument.atEnd()) {
        PortalFilterCondition condition;
        argument >> condition;
        filter.conditions.push_back(std::move(condition));
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

inline QDBusArgument& operator<<(
    QDBusArgument& argument,
    const PortalChoiceSelection& choice) {
    argument.beginStructure();
    argument << choice.id << choice.selection;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    PortalChoiceSelection& choice) {
    argument.beginStructure();
    argument >> choice.id >> choice.selection;
    argument.endStructure();
    return argument;
}

inline QDBusArgument& operator<<(
    QDBusArgument& argument,
    const PortalChoiceSelectionList& choices) {
    argument.beginArray(QMetaType::fromType<PortalChoiceSelection>());
    for (const PortalChoiceSelection& choice : choices.values)
        argument << choice;
    argument.endArray();
    return argument;
}

inline const QDBusArgument& operator>>(
    const QDBusArgument& argument,
    PortalChoiceSelectionList& choices) {
    argument.beginArray();
    choices.values.clear();
    while (!argument.atEnd()) {
        PortalChoiceSelection choice;
        argument >> choice;
        choices.values.push_back(std::move(choice));
    }
    argument.endArray();
    return argument;
}

inline void registerPortalDbusTypes() {
    qDBusRegisterMetaType<PortalFilterCondition>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<PortalChoiceSelection>();
    qDBusRegisterMetaType<PortalChoiceSelectionList>();
}

enum class PortalPickerKind {
    OpenFile,
    SaveFile,
    SaveFiles,
};

struct PortalPickerRequest {
    bool valid = false;
    QString error;
    PortalPickerKind kind = PortalPickerKind::OpenFile;
    QString mode;
    bool multiple = false;
    QString initialDirectory;
    QString suggestedName;
    QString acceptLabel;
    QString title;
    bool modal = true;
    QList<PortalFilter> filters;
    int initialFilterIndex = -1;
    bool filterLocked = false;
    QList<PortalChoice> choices;
    QStringList saveFileNames;

    static PortalPickerRequest openFile(
        const QString& title,
        const QVariantMap& options);
    static PortalPickerRequest saveFile(
        const QString& title,
        const QVariantMap& options);
    static PortalPickerRequest saveFiles(
        const QString& title,
        const QVariantMap& options);

    QStringList pickerArguments() const;
    QStringList pickerProcessArguments() const {
        QStringList arguments = pickerArguments();
        if (!title.isEmpty())
            arguments << QStringLiteral("--picker-title=%1").arg(title);
        if (!acceptLabel.isEmpty())
            arguments << QStringLiteral("--accept-label=%1").arg(acceptLabel);
        arguments << QStringLiteral("--portal-context-stdin");
        return arguments;
    }
    QJsonObject pickerContextJson() const;
    bool pathMatchesFilters(const QString& path) const;
};

struct PortalPickerResult {
    bool valid = false;
    QString error;
    QStringList uris;
    int selectedFilterIndex = -1;
    QMap<QString, QString> choiceSelections;

    static PortalPickerResult fromPickerStdout(
        const PortalPickerRequest& request,
        const QByteArray& standardOutput);
};

namespace PortalPickerParsing {

QString decodeNullTerminatedPath(const QByteArray& bytes, QString* error = nullptr);
QString decodeNullTerminatedLeafName(const QByteArray& bytes, QString* error = nullptr);
QList<PortalFilter> decodeFilters(const QVariant& value, QString* error = nullptr);
PortalFilter decodeFilter(const QVariant& value, QString* error = nullptr);
QList<PortalChoice> decodeChoices(const QVariant& value, QString* error = nullptr);
QStringList decodeFileNames(const QVariant& value, QString* error = nullptr);
QString uniqueDestinationName(
    const QString& directory,
    const QString& requestedName,
    const QStringList& alreadyReserved = {});

} // namespace PortalPickerParsing
