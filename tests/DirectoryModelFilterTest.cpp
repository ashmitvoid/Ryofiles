// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class DirectoryModelFilterTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& data = "x") {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(data), data.size());
    }

    static void waitUntilReady(DirectoryModel& model) {
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 5000);
    }

private slots:
    void filterIsCaseInsensitiveAndDoesNotRescan() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        writeFile(QDir(temp.path()).filePath("Alpha.txt"));
        writeFile(QDir(temp.path()).filePath("beta.log"));
        QVERIFY(QDir().mkdir(QDir(temp.path()).filePath("alpha-folder")));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);
        QCOMPARE(model.rowCount(), 3);

        QSignalSpy loadingSpy(&model, &DirectoryModel::loadingChanged);
        QSignalSpy countSpy(&model, &DirectoryModel::countChanged);

        model.setFilterQuery(QStringLiteral("ALPHA"));

        QCOMPARE(model.filterQuery(), QStringLiteral("ALPHA"));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(loadingSpy.count(), 0);
        QCOMPARE(countSpy.count(), 1);

        const QString first = model.pathAt(0);
        const QString second = model.pathAt(1);
        QVERIFY(QFileInfo(first).fileName().contains("alpha", Qt::CaseInsensitive));
        QVERIFY(QFileInfo(second).fileName().contains("alpha", Qt::CaseInsensitive));
    }

    void filterTrimsQueryAndCanBeClearedWithoutIo() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        writeFile(QDir(temp.path()).filePath("notes.txt"));
        writeFile(QDir(temp.path()).filePath("photo.txt"));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);

        model.setFilterQuery(QStringLiteral("  note  "));
        QCOMPARE(model.filterQuery(), QStringLiteral("note"));
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy loadingSpy(&model, &DirectoryModel::loadingChanged);
        model.setFilterQuery(QString());
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(loadingSpy.count(), 0);
    }

    void refreshPreservesFilterAndAppliesItToNewEntries() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        writeFile(QDir(temp.path()).filePath("match-one.txt"));
        writeFile(QDir(temp.path()).filePath("other.txt"));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);
        model.setFilterQuery(QStringLiteral("match"));
        QCOMPARE(model.rowCount(), 1);

        writeFile(QDir(temp.path()).filePath("match-two.txt"));
        model.refresh();
        waitUntilReady(model);

        QCOMPARE(model.filterQuery(), QStringLiteral("match"));
        QCOMPARE(model.rowCount(), 2);
    }

    void navigationClearsLocationScopedFilter() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString child = QDir(temp.path()).filePath("child");
        QVERIFY(QDir().mkdir(child));
        writeFile(QDir(temp.path()).filePath("needle.txt"));
        writeFile(QDir(child).filePath("inside.txt"));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);
        model.setFilterQuery(QStringLiteral("needle"));
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy filterSpy(&model, &DirectoryModel::filterQueryChanged);
        model.setPath(child);
        waitUntilReady(model);

        QCOMPARE(model.filterQuery(), QString());
        QCOMPARE(filterSpy.count(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(QFileInfo(model.pathAt(0)).fileName(), QStringLiteral("inside.txt"));
    }
};

QTEST_GUILESS_MAIN(DirectoryModelFilterTest)
#include "DirectoryModelFilterTest.moc"
