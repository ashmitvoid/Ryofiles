// SPDX-License-Identifier: GPL-3.0-only

#include "operations/OperationManager.hpp"

#include <archive.h>
#include <archive_entry.h>

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

    static void writeTarArchive(
        const QString& path,
        const QVector<QPair<QString, QByteArray>>& entries) {
        struct archive* writer = archive_write_new();
        QVERIFY(writer != nullptr);
        QVERIFY(archive_write_set_format_pax_restricted(writer) >= ARCHIVE_OK);
        QVERIFY(archive_write_add_filter_none(writer) >= ARCHIVE_OK);

        const QByteArray encodedPath = QFile::encodeName(path);
        QVERIFY2(
            archive_write_open_filename(writer, encodedPath.constData()) >= ARCHIVE_OK,
            archive_error_string(writer));

        for (const auto& [name, data] : entries) {
            struct archive_entry* entry = archive_entry_new();
            QVERIFY(entry != nullptr);
            const QByteArray encodedName = name.toUtf8();
            archive_entry_set_pathname(entry, encodedName.constData());
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, data.size());
            QVERIFY2(
                archive_write_header(writer, entry) >= ARCHIVE_OK,
                archive_error_string(writer));
            if (!data.isEmpty())
                QCOMPARE(archive_write_data(writer, data.constData(), data.size()), la_ssize_t(data.size()));
            QVERIFY(archive_write_finish_entry(writer) >= ARCHIVE_OK);
            archive_entry_free(entry);
        }

        QVERIFY(archive_write_close(writer) >= ARCHIVE_OK);
        QVERIFY(archive_write_free(writer) >= ARCHIVE_OK);
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

    void permanentDeleteRemovesFilesDirectoriesAndOnlyTheSymlink() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString file = temp.filePath("delete-me.txt");
        const QString directory = temp.filePath("delete-tree");
        const QString nested = QDir(directory).filePath("nested.txt");
        const QString targetDirectory = temp.filePath("keep-target");
        const QString targetFile = QDir(targetDirectory).filePath("keep.txt");
        const QString link = temp.filePath("delete-link");

        writeFile(file, "file");
        QVERIFY(QDir().mkpath(directory));
        writeFile(nested, "nested");
        QVERIFY(QDir().mkpath(targetDirectory));
        writeFile(targetFile, "keep");

        const QByteArray targetBytes = QFile::encodeName(targetDirectory);
        const QByteArray linkBytes = QFile::encodeName(link);
        QVERIFY(::symlink(targetBytes.constData(), linkBytes.constData()) == 0);
        QVERIFY(QFileInfo(link).isSymLink());

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.removePermanently({file, directory, link});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
        QVERIFY(finishedSpy.at(0).at(1).toBool());

        QVERIFY(!QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(directory));
        QVERIFY(!QFileInfo(link).isSymLink());
        QVERIFY(QFileInfo(targetDirectory).isDir());
        QCOMPARE(readFile(targetFile), QByteArray("keep"));
    }

    void permanentDeleteRejectsUnsafeOrPartialRequests() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString local = temp.filePath("keep-local.txt");
        const QString missing = temp.filePath("already-gone.txt");
        const QString remote = QStringLiteral("sftp://example.invalid/delete-me.txt");
        writeFile(local, "keep");

        OperationManager manager;

        QVERIFY(manager.removePermanently({}).isEmpty());
        QVERIFY(manager.removePermanently({remote}).isEmpty());
        QVERIFY(manager.removePermanently({local, remote}).isEmpty());
        QVERIFY(manager.removePermanently({local, missing}).isEmpty());
        QVERIFY(manager.removePermanently({QStringLiteral("/")}).isEmpty());

        QCOMPARE(manager.rowCount(), 0);
        QCOMPARE(readFile(local), QByteArray("keep"));
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

        const QString deleteId = manager.removePermanently({source});
        QVERIFY(!deleteId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(finishedSpy.takeFirst().at(1).toBool());
        QVERIFY(!QFileInfo::exists(source));
    }

    void archiveFormatEligibilityMatchesDecoderCoverage() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QStringList supported = {
            QStringLiteral("sample.tar"),
            QStringLiteral("sample.tar.gz"),
            QStringLiteral("sample.tgz"),
            QStringLiteral("sample.tar.xz"),
            QStringLiteral("sample.tar.zst"),
            QStringLiteral("sample.zip"),
            QStringLiteral("sample.7z"),
        };

        OperationManager manager;
        for (const QString& name : supported) {
            const QString path = temp.filePath(name);
            writeFile(path, "archive placeholder");
            QVERIFY2(manager.canExtractArchive(path), qPrintable(name));
        }

        const QStringList unsupported = {
            QStringLiteral("sample.rar"),
            QStringLiteral("sample.tar.bz2"),
            QStringLiteral("sample.gz"),
            QStringLiteral("sample.xz"),
            QStringLiteral("sample.zst"),
        };
        for (const QString& name : unsupported) {
            const QString path = temp.filePath(name);
            writeFile(path, "not exposed");
            QVERIFY2(!manager.canExtractArchive(path), qPrintable(name));
        }
    }

    void archiveExtractionRunsAsIndeterminateOperation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString archivePath = temp.filePath("sample.tar");
        const QString destination = temp.filePath("output");
        QVERIFY(QDir().mkpath(destination));
        writeTarArchive(
            archivePath,
            {{QStringLiteral("folder/hello.txt"), QByteArray("hello archive")}});

        OperationManager manager;
        QVERIFY(manager.canExtractArchive(archivePath));
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.extractArchive(archivePath, destination);
        QVERIFY(!id.isEmpty());
        QCOMPARE(manager.rowCount(), 1);
        const QModelIndex jobIndex = manager.index(0, 0);
        QCOMPARE(manager.data(jobIndex, OperationManager::KindRole).toString(), QStringLiteral("extract"));
        QCOMPARE(manager.data(jobIndex, OperationManager::SourceRole).toString(), QFileInfo(archivePath).absoluteFilePath());
        QCOMPARE(manager.data(jobIndex, OperationManager::DestinationRole).toString(), QFileInfo(destination).absoluteFilePath());
        QVERIFY(manager.data(jobIndex, OperationManager::ProgressIndeterminateRole).toBool());

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QCOMPARE(finishedSpy.at(0).at(0).toString(), id);
        QVERIFY(finishedSpy.at(0).at(1).toBool());
        QCOMPARE(readFile(QDir(destination).filePath("folder/hello.txt")), QByteArray("hello archive"));
        QCOMPARE(manager.data(jobIndex, OperationManager::EntriesProcessedRole).toULongLong(), quint64(1));
        QCOMPARE(manager.data(jobIndex, OperationManager::BytesProcessedRole).toULongLong(), quint64(13));
    }

    void archiveExtractionNeverOverwritesAndRollsBack() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString archivePath = temp.filePath("conflict.tar");
        const QString destination = temp.filePath("output");
        QVERIFY(QDir().mkpath(destination));
        writeTarArchive(
            archivePath,
            {
                {QStringLiteral("new.txt"), QByteArray("new")},
                {QStringLiteral("same.txt"), QByteArray("replacement")},
            });
        writeFile(QDir(destination).filePath("same.txt"), "original");

        OperationManager manager;
        QSignalSpy finishedSpy(&manager, &OperationManager::jobFinished);

        const QString id = manager.extractArchive(archivePath, destination);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
        QVERIFY(!finishedSpy.at(0).at(1).toBool());
        QVERIFY(!manager.errorFor(id).isEmpty());
        QCOMPARE(readFile(QDir(destination).filePath("same.txt")), QByteArray("original"));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath("new.txt")));
    }

    void archiveExtractionRejectsRemoteUnsupportedAndSymlinkInputs() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString archivePath = temp.filePath("sample.tar");
        const QString unsupported = temp.filePath("sample.rar");
        const QString destination = temp.filePath("output");
        QVERIFY(QDir().mkpath(destination));
        writeTarArchive(archivePath, {{QStringLiteral("file.txt"), QByteArray("x")}});
        writeFile(unsupported, "not an archive");

        const QString archiveLink = temp.filePath("linked.tar");
        const QByteArray archiveBytes = QFile::encodeName(archivePath);
        const QByteArray linkBytes = QFile::encodeName(archiveLink);
        QVERIFY(::symlink(archiveBytes.constData(), linkBytes.constData()) == 0);

        OperationManager manager;
        QVERIFY(!manager.canExtractArchive(QStringLiteral("sftp://example.invalid/sample.tar")));
        QVERIFY(!manager.canExtractArchive(unsupported));
        QVERIFY(!manager.canExtractArchive(archiveLink));
        QVERIFY(manager.extractArchive(archivePath, QStringLiteral("sftp://example.invalid/output")).isEmpty());
        QVERIFY(manager.extractArchive(unsupported, destination).isEmpty());
        QVERIFY(manager.extractArchive(archiveLink, destination).isEmpty());
        QCOMPARE(manager.rowCount(), 0);
    }
};

QTEST_MAIN(OperationManagerTest)
#include "OperationManagerTest.moc"
