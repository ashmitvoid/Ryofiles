// SPDX-License-Identifier: GPL-3.0-only

#include "operations/OperationManager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

class ArchiveOperationManagerTest final : public QObject {
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
    void creationRunsAsIndeterminateOperation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = temp.filePath("hello.txt");
        const QString archivePath = temp.filePath("bundle.tar");
        writeFile(source, "hello archive");

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        QVERIFY(manager.canCreateArchive({source}, archivePath));
        const QString id = manager.createArchive({source}, archivePath);
        QVERIFY(!id.isEmpty());
        QCOMPARE(manager.rowCount(), 1);

        const QModelIndex jobIndex = manager.index(0, 0);
        QCOMPARE(
            manager.data(jobIndex, OperationManager::KindRole).toString(),
            QStringLiteral("create archive"));
        QCOMPARE(
            manager.data(jobIndex, OperationManager::SourceRole).toString(),
            QFileInfo(source).absoluteFilePath());
        QCOMPARE(
            manager.data(jobIndex, OperationManager::DestinationRole).toString(),
            QFileInfo(archivePath).absoluteFilePath());
        QVERIFY(manager.data(jobIndex, OperationManager::ProgressIndeterminateRole).toBool());

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
        QVERIFY(finishedSpy.at(0).at(1).toBool());
        QVERIFY(QFileInfo(archivePath).isFile());
        QCOMPARE(
            manager.data(jobIndex, OperationManager::EntriesProcessedRole).toULongLong(),
            quint64(1));
        QCOMPARE(
            manager.data(jobIndex, OperationManager::BytesProcessedRole).toULongLong(),
            quint64(13));
    }

    void creationNeverOverwritesExistingArchive() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = temp.filePath("source.txt");
        const QString archivePath = temp.filePath("existing.zip");
        writeFile(source, "new payload");
        writeFile(archivePath, "original archive bytes");

        OperationManager manager;
        QVERIFY(!manager.canCreateArchive({source}, archivePath));
        QVERIFY(manager.createArchive({source}, archivePath).isEmpty());
        QCOMPARE(manager.rowCount(), 0);
        QCOMPARE(readFile(archivePath), QByteArray("original archive bytes"));
    }

    void creationRejectsUnsafeRequestsBeforeQueueing() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString source = temp.filePath("source.txt");
        const QString missing = temp.filePath("missing.txt");
        const QString unsupported = temp.filePath("bundle.rar");
        const QString remoteSource = QStringLiteral("sftp://example.invalid/source.txt");
        const QString remoteOutput = QStringLiteral("sftp://example.invalid/bundle.tar");
        writeFile(source, "payload");

        const QString directory = temp.filePath("tree");
        QVERIFY(QDir().mkpath(directory));
        writeFile(QDir(directory).filePath("nested.txt"), "nested");
        const QString insideOutput = QDir(directory).filePath("inside.tar");

        OperationManager manager;
        QVERIFY(!manager.canCreateArchive({}, temp.filePath("empty.tar")));
        QVERIFY(!manager.canCreateArchive({remoteSource}, temp.filePath("remote-source.tar")));
        QVERIFY(!manager.canCreateArchive({source}, remoteOutput));
        QVERIFY(!manager.canCreateArchive({source}, unsupported));
        QVERIFY(!manager.canCreateArchive({missing}, temp.filePath("missing.tar")));
        QVERIFY(!manager.canCreateArchive({directory}, insideOutput));

        QVERIFY(manager.createArchive({}, temp.filePath("empty.tar")).isEmpty());
        QVERIFY(manager.createArchive({remoteSource}, temp.filePath("remote-source.tar")).isEmpty());
        QVERIFY(manager.createArchive({source}, remoteOutput).isEmpty());
        QVERIFY(manager.createArchive({source}, unsupported).isEmpty());
        QVERIFY(manager.createArchive({missing}, temp.filePath("missing.tar")).isEmpty());
        QVERIFY(manager.createArchive({directory}, insideOutput).isEmpty());
        QCOMPARE(manager.rowCount(), 0);
    }

    void creationPreservesSymlinkSourcesAsEligibleEntries() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString target = temp.filePath("target.txt");
        const QString link = temp.filePath("link.txt");
        const QString archivePath = temp.filePath("link.tar");
        writeFile(target, "target payload");

        const QByteArray targetName("target.txt");
        const QByteArray linkBytes = QFile::encodeName(link);
        QVERIFY(::symlink(targetName.constData(), linkBytes.constData()) == 0);
        QVERIFY(QFileInfo(link).isSymLink());

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);
        QVERIFY(manager.canCreateArchive({link}, archivePath));

        const QString id = manager.createArchive({link}, archivePath);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.at(0).at(1).toBool());
        QVERIFY(QFileInfo(archivePath).isFile());
        QCOMPARE(
            manager.data(manager.index(0, 0), OperationManager::EntriesProcessedRole).toULongLong(),
            quint64(1));
        QCOMPARE(
            manager.data(manager.index(0, 0), OperationManager::BytesProcessedRole).toULongLong(),
            quint64(0));
    }
};

QTEST_MAIN(ArchiveOperationManagerTest)
#include "ArchiveOperationManagerTest.moc"
