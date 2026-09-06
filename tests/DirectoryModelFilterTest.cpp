// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
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

    void portalNameFiltersAreRebuildOnlyAndKeepDirectoriesVisible() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        writeFile(QDir(temp.path()).filePath("photo.png"));
        writeFile(QDir(temp.path()).filePath("notes.txt"));
        QVERIFY(QDir().mkdir(QDir(temp.path()).filePath("Pictures")));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);
        QCOMPARE(model.rowCount(), 3);

        QSignalSpy loadingSpy(&model, &DirectoryModel::loadingChanged);
        QSignalSpy portalFilterSpy(&model, &DirectoryModel::portalNameFiltersChanged);
        model.setPortalNameFilters({QStringLiteral("*.png")});

        QCOMPARE(model.portalNameFilters(), QStringList({QStringLiteral("*.png")}));
        QCOMPARE(portalFilterSpy.count(), 1);
        QCOMPARE(loadingSpy.count(), 0);
        QCOMPARE(model.rowCount(), 2);
        QVERIFY(model.indexOfPath(QDir(temp.path()).filePath("photo.png")) >= 0);
        QVERIFY(model.indexOfPath(QDir(temp.path()).filePath("Pictures")) >= 0);
        QCOMPARE(model.indexOfPath(QDir(temp.path()).filePath("notes.txt")), -1);

        model.setFilterQuery(QStringLiteral("photo"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(QFileInfo(model.pathAt(0)).fileName(), QStringLiteral("photo.png"));
        QCOMPARE(loadingSpy.count(), 0);

        model.setFilterQuery(QString());
        model.setPortalNameFilters({});
        QCOMPARE(model.rowCount(), 3);
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

    void inactiveModelDefersIoUntilActivated() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        writeFile(QDir(temp.path()).filePath("one.txt"));

        DirectoryModel model(false, nullptr);
        QVERIFY(!model.active());
        QVERIFY(!model.loading());

        QSignalSpy loadingSpy(&model, &DirectoryModel::loadingChanged);
        model.setPath(temp.path());
        QCOMPARE(model.path(), temp.path());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(loadingSpy.count(), 0);

        model.setActive(true);
        QVERIFY(model.active());
        waitUntilReady(model);
        QCOMPARE(model.rowCount(), 1);
    }

    void deactivationStopsWatcherDrivenRefreshes() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        writeFile(QDir(temp.path()).filePath("one.txt"));

        DirectoryModel model;
        model.setPath(temp.path());
        waitUntilReady(model);
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy activeSpy(&model, &DirectoryModel::activeChanged);
        model.setActive(false);
        QVERIFY(!model.active());
        QCOMPARE(activeSpy.count(), 1);

        QSignalSpy loadingSpy(&model, &DirectoryModel::loadingChanged);
        writeFile(QDir(temp.path()).filePath("two.txt"));
        QTest::qWait(250);

        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(loadingSpy.count(), 0);

        model.setActive(true);
        waitUntilReady(model);
        QCOMPARE(model.rowCount(), 2);
    }

    void uriLikePathIsRejectedButLocalColonDirectoryRemainsValid() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString colonDirectory = QDir(temp.path()).filePath("folder:local");
        QVERIFY(QDir().mkpath(colonDirectory));

        DirectoryModel model(false, nullptr);
        model.setPath(colonDirectory);
        QCOMPARE(model.path(), colonDirectory);

        QSignalSpy errorSpy(&model, &DirectoryModel::errorOccurred);
        QSignalSpy pathSpy(&model, &DirectoryModel::pathChanged);
        model.setPath(QStringLiteral("sftp://example.invalid/share"));

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(pathSpy.count(), 0);
        QCOMPARE(model.path(), colonDirectory);
        QVERIFY(!model.loading());
    }

    void largeDirectoryPublishesCompleteSortedSnapshot() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString root = QDir(temp.path()).filePath(QStringLiteral("large"));
        QVERIFY(QDir().mkpath(root));

        constexpr int kEntryCount = 4096;
        for (int i = 0; i < kEntryCount; ++i) {
            writeFile(QDir(root).filePath(
                QStringLiteral("item-%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
        }

        DirectoryModel model(false, nullptr);
        model.setPath(root);
        model.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 15000);

        QCOMPARE(model.rowCount(), kEntryCount);
        QCOMPARE(
            QFileInfo(model.pathAt(0)).fileName(),
            QStringLiteral("item-0000.txt"));
        QCOMPARE(
            QFileInfo(model.pathAt(kEntryCount - 1)).fileName(),
            QStringLiteral("item-4095.txt"));
    }

    void rapidNavigationNeverPublishesStaleLargeScan() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString slowRoot = QDir(temp.path()).filePath(QStringLiteral("slow"));
        const QString fastRoot = QDir(temp.path()).filePath(QStringLiteral("fast"));
        QVERIFY(QDir().mkpath(slowRoot));
        QVERIFY(QDir().mkpath(fastRoot));

        constexpr int kSlowEntryCount = 2048;
        for (int i = 0; i < kSlowEntryCount; ++i) {
            writeFile(QDir(slowRoot).filePath(
                QStringLiteral("stale-%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
        }
        const QString sentinel = QDir(fastRoot).filePath(QStringLiteral("current.txt"));
        writeFile(sentinel);

        DirectoryModel model(false, nullptr);
        model.setPath(slowRoot);
        model.setActive(true);
        QVERIFY(model.loading());

        model.setPath(fastRoot);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 15000);

        QVERIFY(QThreadPool::globalInstance()->waitForDone(15000));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1000);

        QCOMPARE(model.path(), QDir(fastRoot).absolutePath());
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.pathAt(0), QFileInfo(sentinel).absoluteFilePath());
        QCOMPARE(QFileInfo(model.pathAt(0)).fileName(), QStringLiteral("current.txt"));
    }
};

QTEST_GUILESS_MAIN(DirectoryModelFilterTest)
#include "DirectoryModelFilterTest.moc"
