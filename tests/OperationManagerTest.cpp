// SPDX-License-Identifier: GPL-3.0-only

#include "operations/OperationManager.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class OperationManagerTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

    static QByteArray readFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

private slots:
    void keepBothPreservesExistingDestination() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("source");
        const QString destinationDir = temp.filePath("destination");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        const QString source = QDir(sourceDir).filePath("report.txt");
        const QString existing = QDir(destinationDir).filePath("report.txt");
        writeFile(source, "new");
        writeFile(existing, "old");

        OperationManager manager;
        QSignalSpy conflictSpy(&manager, &OperationManager::conflictRaised);
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({source}, destinationDir);
        QVERIFY(!id.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(conflictSpy.count(), 1, 5000);
        manager.resolveConflict(id, OperationManager::KeepBoth);

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
        QVERIFY(finishedSpy.at(0).at(1).toBool());

        QCOMPARE(readFile(existing), QByteArray("old"));
        QCOMPARE(
            readFile(QDir(destinationDir).filePath("report (copy).txt")),
            QByteArray("new"));
    }

    void replaceRollsBackWhenCopyFails() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("source");
        const QString destinationDir = temp.filePath("destination");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        const QString source = QDir(sourceDir).filePath("report.txt");
        const QString existing = QDir(destinationDir).filePath("report.txt");
        writeFile(source, "new");
        writeFile(existing, "old");

        OperationManager manager;
        QSignalSpy conflictSpy(&manager, &OperationManager::conflictRaised);
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({source}, destinationDir);
        QVERIFY(!id.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(conflictSpy.count(), 1, 5000);
        QVERIFY(QFile::remove(source));

        manager.resolveConflict(id, OperationManager::Replace);

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
        QVERIFY(!finishedSpy.at(0).at(1).toBool());

        QCOMPARE(readFile(existing), QByteArray("old"));

        const QStringList leftovers = QDir(destinationDir).entryList(
            {QStringLiteral(".*.ryofiles-backup-*")},
            QDir::Files | QDir::Hidden);
        QVERIFY(leftovers.isEmpty());
    }

    void applyToAllKeepBothHandlesRemainingConflicts() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("source-all");
        const QString destinationDir = temp.filePath("destination-all");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        const QString a = QDir(sourceDir).filePath("a.txt");
        const QString b = QDir(sourceDir).filePath("b.txt");
        writeFile(a, "new-a");
        writeFile(b, "new-b");
        writeFile(QDir(destinationDir).filePath("a.txt"), "old-a");
        writeFile(QDir(destinationDir).filePath("b.txt"), "old-b");

        OperationManager manager;
        QSignalSpy conflictSpy(&manager, &OperationManager::conflictRaised);
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({a, b}, destinationDir);
        QVERIFY(!id.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(conflictSpy.count(), 1, 5000);
        manager.resolveConflict(id, OperationManager::KeepBoth, true);

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.at(0).at(1).toBool());
        QCOMPARE(conflictSpy.count(), 1);

        QCOMPARE(readFile(QDir(destinationDir).filePath("a.txt")), QByteArray("old-a"));
        QCOMPARE(readFile(QDir(destinationDir).filePath("b.txt")), QByteArray("old-b"));
        QCOMPARE(readFile(QDir(destinationDir).filePath("a (copy).txt")), QByteArray("new-a"));
        QCOMPARE(readFile(QDir(destinationDir).filePath("b (copy).txt")), QByteArray("new-b"));
    }

    void rejectsDirectoryCopyIntoOwnDescendant() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = temp.filePath("tree");
        const QString childDestination = QDir(source).filePath("inside");
        QVERIFY(QDir().mkpath(childDestination));
        writeFile(QDir(source).filePath("data.txt"), "payload");

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({source}, childDestination);
        QVERIFY(!id.isEmpty());

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(!finishedSpy.at(0).at(1).toBool());
        QVERIFY(!QFileInfo::exists(QDir(childDestination).filePath("tree")));
    }
};

QTEST_MAIN(OperationManagerTest)
#include "OperationManagerTest.moc"
