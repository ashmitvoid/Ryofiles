// SPDX-License-Identifier: GPL-3.0-only

#include "PortalPickerContext.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QVariantMap>

PortalPickerContext::PortalPickerContext(QObject* parent)
    : QObject(parent) {
}

bool PortalPickerContext::validId(const QString& value) {
    return !value.isEmpty() && boundedString(value, 128);
}

bool PortalPickerContext::boundedString(const QString& value, int maximum) {
    return value.size() <= maximum && !value.contains(QChar::Null);
}

bool PortalPickerContext::configure(const QJsonObject& object, QString* error) {
    if (error)
        error->clear();

    m_active = false;
    m_filters.clear();
    m_choices.clear();
    m_selectedFilterIndex = -1;
    m_filterLocked = false;

    if (object.value(QStringLiteral("version")).toInt() != 1) {
        if (error)
            *error = QStringLiteral("Unsupported portal picker context version");
        return false;
    }

    const QJsonArray filtersArray = object.value(QStringLiteral("filters")).toArray();
    if (filtersArray.size() > 64) {
        if (error)
            *error = QStringLiteral("Portal picker context has too many filters");
        return false;
    }

    for (const QJsonValue& value : filtersArray) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("Portal picker filter is malformed");
            return false;
        }
        const QJsonObject item = value.toObject();
        Filter filter;
        filter.name = item.value(QStringLiteral("name")).toString();
        if (!boundedString(filter.name)) {
            if (error)
                *error = QStringLiteral("Portal picker filter name is invalid");
            return false;
        }

        const QJsonArray patterns = item.value(QStringLiteral("patterns")).toArray();
        if (patterns.size() > 256) {
            if (error)
                *error = QStringLiteral("Portal picker filter has too many patterns");
            return false;
        }
        QSet<QString> seenPatterns;
        for (const QJsonValue& patternValue : patterns) {
            const QString pattern = patternValue.toString();
            if (pattern.isEmpty() || !boundedString(pattern, 1024)) {
                if (error)
                    *error = QStringLiteral("Portal picker filter pattern is invalid");
                return false;
            }
            if (!seenPatterns.contains(pattern)) {
                seenPatterns.insert(pattern);
                filter.patterns.push_back(pattern);
            }
        }
        m_filters.push_back(std::move(filter));
    }

    m_filterLocked = object.value(QStringLiteral("filter_locked")).toBool(false);
    m_selectedFilterIndex = object.value(QStringLiteral("initial_filter")).toInt(-1);
    if (m_filters.isEmpty()) {
        m_selectedFilterIndex = -1;
        m_filterLocked = false;
    } else if (m_selectedFilterIndex < 0 || m_selectedFilterIndex >= m_filters.size()) {
        m_selectedFilterIndex = 0;
    }

    const QJsonArray choicesArray = object.value(QStringLiteral("choices")).toArray();
    if (choicesArray.size() > 32) {
        if (error)
            *error = QStringLiteral("Portal picker context has too many choices");
        return false;
    }

    QSet<QString> seenChoiceIds;
    for (const QJsonValue& value : choicesArray) {
        if (!value.isObject()) {
            if (error)
                *error = QStringLiteral("Portal picker choice is malformed");
            return false;
        }
        const QJsonObject item = value.toObject();
        Choice choice;
        choice.id = item.value(QStringLiteral("id")).toString();
        choice.label = item.value(QStringLiteral("label")).toString();
        choice.boolean = item.value(QStringLiteral("boolean")).toBool(false);
        choice.selected = item.value(QStringLiteral("initial")).toString();

        if (!validId(choice.id)
            || choice.label.isEmpty()
            || !boundedString(choice.label)
            || seenChoiceIds.contains(choice.id)) {
            if (error)
                *error = QStringLiteral("Portal picker choice metadata is invalid");
            return false;
        }
        seenChoiceIds.insert(choice.id);

        if (choice.boolean) {
            if (choice.selected.isEmpty())
                choice.selected = QStringLiteral("false");
            if (choice.selected != QStringLiteral("true")
                && choice.selected != QStringLiteral("false")) {
                if (error)
                    *error = QStringLiteral("Portal boolean choice has invalid initial value");
                return false;
            }
        } else {
            const QJsonArray optionsArray = item.value(QStringLiteral("options")).toArray();
            if (optionsArray.isEmpty() || optionsArray.size() > 64) {
                if (error)
                    *error = QStringLiteral("Portal picker choice has invalid option count");
                return false;
            }

            QSet<QString> seenOptionIds;
            for (const QJsonValue& optionValue : optionsArray) {
                if (!optionValue.isObject()) {
                    if (error)
                        *error = QStringLiteral("Portal picker choice option is malformed");
                    return false;
                }
                const QJsonObject optionObject = optionValue.toObject();
                ChoiceOption option;
                option.id = optionObject.value(QStringLiteral("id")).toString();
                option.label = optionObject.value(QStringLiteral("label")).toString();
                if (!validId(option.id)
                    || option.label.isEmpty()
                    || !boundedString(option.label)
                    || seenOptionIds.contains(option.id)) {
                    if (error)
                        *error = QStringLiteral("Portal picker choice option is invalid");
                    return false;
                }
                seenOptionIds.insert(option.id);
                choice.options.push_back(std::move(option));
            }

            if (choice.selected.isEmpty())
                choice.selected = choice.options.constFirst().id;
            if (!seenOptionIds.contains(choice.selected)) {
                if (error)
                    *error = QStringLiteral("Portal picker choice initial value is invalid");
                return false;
            }
        }
        m_choices.push_back(std::move(choice));
    }

    m_active = true;
    return true;
}

QVariantList PortalPickerContext::filters() const {
    QVariantList result;
    result.reserve(m_filters.size());
    for (const Filter& filter : m_filters) {
        result.push_back(QVariantMap{
            {QStringLiteral("name"), filter.name},
            {QStringLiteral("patterns"), filter.patterns},
        });
    }
    return result;
}

void PortalPickerContext::setSelectedFilterIndex(int index) {
    if (m_filterLocked || m_filters.isEmpty())
        return;
    if (index < 0 || index >= m_filters.size() || index == m_selectedFilterIndex)
        return;
    m_selectedFilterIndex = index;
    emit selectedFilterChanged();
}

QStringList PortalPickerContext::selectedFilterPatterns() const {
    if (m_selectedFilterIndex < 0 || m_selectedFilterIndex >= m_filters.size())
        return {};
    return m_filters.at(m_selectedFilterIndex).patterns;
}

QVariantList PortalPickerContext::choices() const {
    QVariantList rows;
    rows.reserve(m_choices.size());
    for (const Choice& choice : m_choices) {
        QVariantList options;
        if (choice.boolean) {
            options.push_back(QVariantMap{
                {QStringLiteral("id"), QStringLiteral("false")},
                {QStringLiteral("label"), QStringLiteral("Off")},
            });
            options.push_back(QVariantMap{
                {QStringLiteral("id"), QStringLiteral("true")},
                {QStringLiteral("label"), QStringLiteral("On")},
            });
        } else {
            for (const ChoiceOption& option : choice.options) {
                options.push_back(QVariantMap{
                    {QStringLiteral("id"), option.id},
                    {QStringLiteral("label"), option.label},
                });
            }
        }
        rows.push_back(QVariantMap{
            {QStringLiteral("id"), choice.id},
            {QStringLiteral("label"), choice.label},
            {QStringLiteral("boolean"), choice.boolean},
            {QStringLiteral("selected"), choice.selected},
            {QStringLiteral("options"), options},
        });
    }
    return rows;
}

bool PortalPickerContext::setChoiceSelection(
    const QString& id,
    const QString& selection) {
    for (Choice& choice : m_choices) {
        if (choice.id != id)
            continue;

        bool valid = false;
        if (choice.boolean) {
            valid = selection == QStringLiteral("true")
                || selection == QStringLiteral("false");
        } else {
            for (const ChoiceOption& option : choice.options) {
                if (option.id == selection) {
                    valid = true;
                    break;
                }
            }
        }
        if (!valid)
            return false;
        if (choice.selected == selection)
            return true;
        choice.selected = selection;
        emit choicesChanged();
        return true;
    }
    return false;
}

QString PortalPickerContext::choiceSelection(const QString& id) const {
    for (const Choice& choice : m_choices) {
        if (choice.id == id)
            return choice.selected;
    }
    return {};
}

QString PortalPickerContext::nextChoiceSelection(const QString& id) const {
    for (const Choice& choice : m_choices) {
        if (choice.id != id)
            continue;
        if (choice.boolean)
            return choice.selected == QStringLiteral("true")
                ? QStringLiteral("false")
                : QStringLiteral("true");
        for (qsizetype i = 0; i < choice.options.size(); ++i) {
            if (choice.options.at(i).id == choice.selected)
                return choice.options.at((i + 1) % choice.options.size()).id;
        }
        return choice.options.isEmpty() ? QString() : choice.options.constFirst().id;
    }
    return {};
}

QJsonObject PortalPickerContext::resultObject(const QStringList& uris) const {
    QJsonArray uriArray;
    for (const QString& uri : uris)
        uriArray.push_back(uri);

    QJsonObject choiceSelections;
    for (const Choice& choice : m_choices)
        choiceSelections.insert(choice.id, choice.selected);

    return QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("uris"), uriArray},
        {QStringLiteral("filter"), m_selectedFilterIndex},
        {QStringLiteral("choices"), choiceSelections},
    };
}
