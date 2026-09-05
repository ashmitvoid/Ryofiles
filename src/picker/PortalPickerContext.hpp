// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class PortalPickerContext final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active CONSTANT)
    Q_PROPERTY(QVariantList filters READ filters CONSTANT)
    Q_PROPERTY(int selectedFilterIndex READ selectedFilterIndex WRITE setSelectedFilterIndex NOTIFY selectedFilterChanged)
    Q_PROPERTY(bool filterLocked READ filterLocked CONSTANT)
    Q_PROPERTY(QStringList selectedFilterPatterns READ selectedFilterPatterns NOTIFY selectedFilterChanged)
    Q_PROPERTY(QVariantList choices READ choices NOTIFY choicesChanged)

public:
    explicit PortalPickerContext(QObject* parent = nullptr);

    bool configure(const QJsonObject& object, QString* error = nullptr);

    bool active() const { return m_active; }
    QVariantList filters() const;
    int selectedFilterIndex() const { return m_selectedFilterIndex; }
    void setSelectedFilterIndex(int index);
    bool filterLocked() const { return m_filterLocked; }
    QStringList selectedFilterPatterns() const;
    QVariantList choices() const;

    Q_INVOKABLE bool setChoiceSelection(const QString& id, const QString& selection);
    Q_INVOKABLE QString choiceSelection(const QString& id) const;
    Q_INVOKABLE QString nextChoiceSelection(const QString& id) const;

    QJsonObject resultObject(const QStringList& uris) const;

signals:
    void selectedFilterChanged();
    void choicesChanged();

private:
    struct Filter {
        QString name;
        QStringList patterns;
    };

    struct ChoiceOption {
        QString id;
        QString label;
    };

    struct Choice {
        QString id;
        QString label;
        QList<ChoiceOption> options;
        QString selected;
        bool boolean = false;
    };

    static bool validId(const QString& value);
    static bool boundedString(const QString& value, int maximum = 512);

    bool m_active = false;
    QList<Filter> m_filters;
    int m_selectedFilterIndex = -1;
    bool m_filterLocked = false;
    QList<Choice> m_choices;
};
