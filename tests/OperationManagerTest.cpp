// SPDX-License-Identifier: GPL-3.0-only

#include "operations/OperationManager.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

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

    static QByteArray rawSymlinkTarget(const QString& path) {
        QByteArray buffer(256, '\0');
        const QByteArray encoded = QFile::encodeName(path);

        while (true) {
            const ssize_t length =
                ::readlink(encoded.constData(), buffer.data(), static_cast<size_t>(buffer.size()));
            if (length < 0)
                return {};
            if (length < buffer.size()) {
                buffer.resize(static_cast<qsizetype>(length));
                return buffer;
            }
            buffer.resize(buffer.size() * 2);
        }
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

    void createFolderIsAsyncAndNeverOverwrites() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString firstId = manager.createFolder(temp.path(), "Project");
        QVERIFY(!firstId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.takeFirst().at(1).toBool());
        QVERIFY(QFileInfo::exists(QDir(temp.path()).filePath("Project")));

        const QString secondId = manager.createFolder(temp.path(), "Project");
        QVERIFY(!secondId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(!finishedSpy.takeFirst().at(1).toBool());

        QVERIFY(QFileInfo(QDir(temp.path()).filePath("Project")).isDir());
    }

    void duplicateUsesUniqueSiblingNames() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = QDir(temp.path()).filePath("note.txt");
        writeFile(source, "payload");

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.duplicate({source});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.takeFirst().at(1).toBool());

        QCOMPARE(
            readFile(QDir(temp.path()).filePath("note (copy).txt")),
            QByteArray("payload"));
        QCOMPARE(readFile(source), QByteArray("payload"));
    }

    void copyPreservesRelativeSymlinkTarget() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("source-link");
        const QString destinationDir = temp.filePath("destination-link");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        writeFile(QDir(sourceDir).filePath("target.txt"), "target");

        const QString sourceLink = QDir(sourceDir).filePath("link.txt");
        const QByteArray encodedLink = QFile::encodeName(sourceLink);
        QVERIFY(::symlink("target.txt", encodedLink.constData()) == 0);
        QCOMPARE(rawSymlinkTarget(sourceLink), QByteArray("target.txt"));

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({sourceLink}, destinationDir);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.takeFirst().at(1).toBool());

        const QString copiedLink = QDir(destinationDir).filePath("link.txt");
        QVERIFY(QFileInfo(copiedLink).isSymLink());
        QCOMPARE(rawSymlinkTarget(copiedLink), QByteArray("target.txt"));
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

    void rejectsUriLikeInputsWithoutCreatingJobs() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("uri-source");
        const QString destinationDir = temp.filePath("uri-destination");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        const QString localSource = QDir(sourceDir).filePath("local.txt");
        writeFile(localSource, "payload");

        const QString remoteSource = QStringLiteral("sftp://example.invalid/file.txt");
        const QString remoteDestination = QStringLiteral("sftp://example.invalid/folder");

        OperationManager manager;

        QVERIFY(manager.copy({remoteSource}, destinationDir).isEmpty());
        QVERIFY(manager.move({remoteSource}, destinationDir).isEmpty());
        QVERIFY(manager.copy({localSource, remoteSource}, destinationDir).isEmpty());
        QVERIFY(manager.move({localSource, remoteSource}, destinationDir).isEmpty());
        QVERIFY(manager.copy({localSource}, remoteDestination).isEmpty());
        QVERIFY(manager.move({localSource}, remoteDestination).isEmpty());
        QVERIFY(manager.rename(remoteSource, "renamed.txt").isEmpty());
        QVERIFY(manager.duplicate({remoteSource}).isEmpty());
        QVERIFY(manager.duplicate({localSource, remoteSource}).isEmpty());
        QVERIFY(manager.createFolder(remoteDestination, "Folder").isEmpty());

        QCOMPARE(manager.rowCount(), 0);
        QVERIFY(QFileInfo::exists(localSource));
        QVERIFY(!QFileInfo::exists(QDir(destinationDir).filePath("local.txt")));
    }

    void localColonPathRemainsValid() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString sourceDir = temp.filePath("source:colon");
        const QString destinationDir = temp.filePath("destination");
        QVERIFY(QDir().mkpath(sourceDir));
        QVERIFY(QDir().mkpath(destinationDir));

        const QString source = QDir(sourceDir).filePath("report:2026.txt");
        writeFile(source, "payload");

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.copy({source}, destinationDir);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.takeFirst().at(1).toBool());

        QCOMPARE(
            readFile(QDir(destinationDir).filePath("report:2026.txt")),
            QByteArray("payload"));
        QCOMPARE(readFile(source), QByteArray("payload"));
    }
};

QTEST_MAIN(OperationManagerTest)
#include "OperationManagerTest.moc"
