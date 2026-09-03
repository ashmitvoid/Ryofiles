// SPDX-License-Identifier: GPL-3.0-only

#include "trash/TrashManager.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class TrashManagerTest final : public QObject {
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
    void initTestCase() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());

        m_dataHome = m_root->filePath("xdg-data");
        QVERIFY(QDir().mkpath(m_dataHome));
        qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
    }

    void cleanup() {
        QDir trash(QDir(m_dataHome).filePath("Trash"));
        if (trash.exists())
            trash.removeRecursively();
    }

    void trashCreatesMetadataAndRestoreUsesOriginalPath() {
        const QString work = m_root->filePath("work-a");
        QVERIFY(QDir().mkpath(work));

        const QString source = QDir(work).filePath("notes.txt");
        writeFile(source, "hello");

        TrashManager manager;
        QSignalSpy finished(&manager, &TrashManager::operationFinished);

        const QString operationId = manager.trash({source});
        QVERIFY(!operationId.isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
        const auto first = finished.takeFirst();
        QCOMPARE(first.at(0).toString(), operationId);
        QVERIFY(first.at(1).toBool());

        QVERIFY(!QFileInfo::exists(source));

        QTRY_COMPARE_WITH_TIMEOUT(manager.count(), 1, 5000);

        const QModelIndex row = manager.index(0, 0);
        const QString itemId = manager.data(row, TrashManager::IdRole).toString();
        const QString original = manager.data(row, TrashManager::OriginalPathRole).toString();
        const QString trashed = manager.data(row, TrashManager::TrashedPathRole).toString();

        QCOMPARE(original, source);
        QVERIFY(QFileInfo::exists(trashed));

        const QString infoPath =
            QDir(m_dataHome).filePath("Trash/info/notes.txt.trashinfo");
        QVERIFY(QFileInfo::exists(infoPath));

        QSignalSpy restoreFinished(&manager, &TrashManager::operationFinished);
        const QString restoreId = manager.restore(itemId, TrashManager::RestoreSkip);
        QVERIFY(!restoreId.isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(restoreFinished.count() >= 1, 5000);
        const auto restored = restoreFinished.takeFirst();
        QCOMPARE(restored.at(0).toString(), restoreId);
        QVERIFY(restored.at(1).toBool());

        QCOMPARE(readFile(source), QByteArray("hello"));
        QVERIFY(!QFileInfo::exists(infoPath));
    }

    void restoreKeepBothPreservesExistingFile() {
        const QString work = m_root->filePath("work-b");
        QVERIFY(QDir().mkpath(work));

        const QString source = QDir(work).filePath("report.txt");
        writeFile(source, "trashed");

        TrashManager manager;
        QSignalSpy finished(&manager, &TrashManager::operationFinished);

        const QString trashId = manager.trash({source});
        QVERIFY(!trashId.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
        QVERIFY(finished.takeFirst().at(1).toBool());

        QTRY_COMPARE_WITH_TIMEOUT(manager.count(), 1, 5000);
        const QString itemId =
            manager.data(manager.index(0, 0), TrashManager::IdRole).toString();

        writeFile(source, "existing");

        QSignalSpy restoreFinished(&manager, &TrashManager::operationFinished);
        const QString restoreId =
            manager.restore(itemId, TrashManager::RestoreKeepBoth);
        QVERIFY(!restoreId.isEmpty());

        QTRY_VERIFY_WITH_TIMEOUT(restoreFinished.count() >= 1, 5000);
        QVERIFY(restoreFinished.takeFirst().at(1).toBool());

        QCOMPARE(readFile(source), QByteArray("existing"));
        QCOMPARE(
            readFile(QDir(work).filePath("report (restored).txt")),
            QByteArray("trashed"));
    }

    void defaultRestoreRaisesConflictWithoutChangingEitherCopy() {
        const QString work = m_root->filePath("work-c");
        QVERIFY(QDir().mkpath(work));

        const QString source = QDir(work).filePath("draft.txt");
        writeFile(source, "trashed");

        TrashManager manager;
        QSignalSpy finished(&manager, &TrashManager::operationFinished);

        const QString trashId = manager.trash({source});
        QVERIFY(!trashId.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
        QVERIFY(finished.takeFirst().at(1).toBool());

        QTRY_COMPARE_WITH_TIMEOUT(manager.count(), 1, 5000);
        const QModelIndex row = manager.index(0, 0);
        const QString itemId = manager.data(row, TrashManager::IdRole).toString();
        const QString trashedPath =
            manager.data(row, TrashManager::TrashedPathRole).toString();

        writeFile(source, "existing");

        QSignalSpy conflict(&manager, &TrashManager::restoreConflict);
        const QString restoreId =
            manager.restore(itemId, TrashManager::RestoreSkip);

        QVERIFY(restoreId.isEmpty());
        QCOMPARE(conflict.count(), 1);
        QCOMPARE(readFile(source), QByteArray("existing"));
        QCOMPARE(readFile(trashedPath), QByteArray("trashed"));
    }

private:
    std::unique_ptr<QTemporaryDir> m_root;
    QString m_dataHome;
};

QTEST_MAIN(TrashManagerTest)
#include "TrashManagerTest.moc"
