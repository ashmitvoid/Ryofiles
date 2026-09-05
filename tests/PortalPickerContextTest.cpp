// SPDX-License-Identifier: GPL-3.0-only

#include "picker/PortalPickerContext.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

class PortalPickerContextTest final : public QObject {
    Q_OBJECT

private:
    static QJsonObject filter(
        const QString& name,
        const QStringList& patterns) {
        QJsonArray serialized;
        for (const QString& pattern : patterns)
            serialized.push_back(pattern);
        return QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("patterns"), serialized},
        };
    }

    static QJsonObject booleanChoice(
        const QString& id,
        const QString& label,
        const QString& initial) {
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("boolean"), true},
            {QStringLiteral("initial"), initial},
            {QStringLiteral("options"), QJsonArray{}},
        };
    }

    static QJsonObject comboChoice() {
        return QJsonObject{
            {QStringLiteral("id"), QStringLiteral("encoding")},
            {QStringLiteral("label"), QStringLiteral("Encoding")},
            {QStringLiteral("boolean"), false},
            {QStringLiteral("initial"), QStringLiteral("latin1")},
            {QStringLiteral("options"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("utf8")},
                    {QStringLiteral("label"), QStringLiteral("UTF-8")},
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("latin1")},
                    {QStringLiteral("label"), QStringLiteral("Latin-1")},
                },
            }},
        };
    }

    static QJsonObject validContext(bool locked = false) {
        return QJsonObject{
            {QStringLiteral("version"), 1},
            {QStringLiteral("filters"), QJsonArray{
                filter(QStringLiteral("Images"), {QStringLiteral("*.png"), QStringLiteral("*.jpg")}),
                filter(QStringLiteral("Text"), {QStringLiteral("*.txt")}),
            }},
            {QStringLiteral("initial_filter"), 0},
            {QStringLiteral("filter_locked"), locked},
            {QStringLiteral("choices"), QJsonArray{
                booleanChoice(
                    QStringLiteral("readonly"),
                    QStringLiteral("Open read-only"),
                    QStringLiteral("false")),
                comboChoice(),
            }},
        };
    }

private slots:
    void configuresFilterAndChoiceState() {
        PortalPickerContext context;
        QString error;
        QVERIFY2(context.configure(validContext(), &error), qPrintable(error));
        QVERIFY(context.active());
        QCOMPARE(context.filters().size(), 2);
        QCOMPARE(context.selectedFilterIndex(), 0);
        QCOMPARE(
            context.selectedFilterPatterns(),
            QStringList({QStringLiteral("*.png"), QStringLiteral("*.jpg")}));
        QCOMPARE(context.choices().size(), 2);
        QCOMPARE(context.choiceSelection(QStringLiteral("readonly")), QStringLiteral("false"));
        QCOMPARE(context.choiceSelection(QStringLiteral("encoding")), QStringLiteral("latin1"));
    }

    void switchingFilterIsEventDrivenAndBounded() {
        PortalPickerContext context;
        QString error;
        QVERIFY2(context.configure(validContext(), &error), qPrintable(error));

        QSignalSpy filterSpy(&context, &PortalPickerContext::selectedFilterChanged);
        context.setSelectedFilterIndex(1);
        QCOMPARE(context.selectedFilterIndex(), 1);
        QCOMPARE(context.selectedFilterPatterns(), QStringList({QStringLiteral("*.txt")}));
        QCOMPARE(filterSpy.count(), 1);

        context.setSelectedFilterIndex(99);
        context.setSelectedFilterIndex(-1);
        QCOMPARE(context.selectedFilterIndex(), 1);
        QCOMPARE(filterSpy.count(), 1);
    }

    void lockedFilterCannotBeChanged() {
        PortalPickerContext context;
        QString error;
        QVERIFY2(context.configure(validContext(true), &error), qPrintable(error));
        QVERIFY(context.filterLocked());

        QSignalSpy filterSpy(&context, &PortalPickerContext::selectedFilterChanged);
        context.setSelectedFilterIndex(1);
        QCOMPARE(context.selectedFilterIndex(), 0);
        QCOMPARE(filterSpy.count(), 0);
    }

    void choicesCycleAndRejectUnknownSelections() {
        PortalPickerContext context;
        QString error;
        QVERIFY2(context.configure(validContext(), &error), qPrintable(error));

        QSignalSpy choiceSpy(&context, &PortalPickerContext::choicesChanged);
        QCOMPARE(
            context.nextChoiceSelection(QStringLiteral("readonly")),
            QStringLiteral("true"));
        QVERIFY(context.setChoiceSelection(QStringLiteral("readonly"), QStringLiteral("true")));
        QCOMPARE(context.choiceSelection(QStringLiteral("readonly")), QStringLiteral("true"));

        QCOMPARE(
            context.nextChoiceSelection(QStringLiteral("encoding")),
            QStringLiteral("utf8"));
        QVERIFY(context.setChoiceSelection(QStringLiteral("encoding"), QStringLiteral("utf8")));
        QCOMPARE(context.choiceSelection(QStringLiteral("encoding")), QStringLiteral("utf8"));
        QCOMPARE(choiceSpy.count(), 2);

        QVERIFY(!context.setChoiceSelection(QStringLiteral("encoding"), QStringLiteral("missing")));
        QVERIFY(!context.setChoiceSelection(QStringLiteral("missing"), QStringLiteral("true")));
        QCOMPARE(choiceSpy.count(), 2);
    }

    void resultObjectContainsCurrentPortalMetadata() {
        PortalPickerContext context;
        QString error;
        QVERIFY2(context.configure(validContext(), &error), qPrintable(error));
        context.setSelectedFilterIndex(1);
        QVERIFY(context.setChoiceSelection(QStringLiteral("readonly"), QStringLiteral("true")));
        QVERIFY(context.setChoiceSelection(QStringLiteral("encoding"), QStringLiteral("utf8")));

        const QJsonObject result = context.resultObject({
            QStringLiteral("file:///tmp/example.txt"),
        });
        QCOMPARE(result.value(QStringLiteral("version")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("filter")).toInt(), 1);
        QCOMPARE(
            result.value(QStringLiteral("uris")).toArray().at(0).toString(),
            QStringLiteral("file:///tmp/example.txt"));
        const QJsonObject choices = result.value(QStringLiteral("choices")).toObject();
        QCOMPARE(choices.value(QStringLiteral("readonly")).toString(), QStringLiteral("true"));
        QCOMPARE(choices.value(QStringLiteral("encoding")).toString(), QStringLiteral("utf8"));
    }

    void rejectsMalformedOrOversizedContext() {
        PortalPickerContext context;
        QString error;

        QJsonObject malformed = validContext();
        malformed.insert(QStringLiteral("version"), 2);
        QVERIFY(!context.configure(malformed, &error));
        QVERIFY(!context.active());
        QVERIFY(error.contains(QStringLiteral("version"), Qt::CaseInsensitive));

        malformed = validContext();
        QJsonArray tooManyFilters;
        for (int i = 0; i < 65; ++i)
            tooManyFilters.push_back(filter(QString::number(i), {QStringLiteral("*")}));
        malformed.insert(QStringLiteral("filters"), tooManyFilters);
        QVERIFY(!context.configure(malformed, &error));
        QVERIFY(error.contains(QStringLiteral("too many"), Qt::CaseInsensitive));

        malformed = validContext();
        QJsonArray choices = malformed.value(QStringLiteral("choices")).toArray();
        QJsonObject duplicate = choices.at(0).toObject();
        choices.push_back(duplicate);
        malformed.insert(QStringLiteral("choices"), choices);
        QVERIFY(!context.configure(malformed, &error));
        QVERIFY(error.contains(QStringLiteral("choice"), Qt::CaseInsensitive));
    }
};

QTEST_GUILESS_MAIN(PortalPickerContextTest)
#include "PortalPickerContextTest.moc"
