// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchiveExtractor.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <sys/stat.h>
#include <unistd.h>

namespace {

enum class TestArchiveFormat {
    Tar,
    TarGzip,
    TarXz,
    TarZstd,
    Zip,
    SevenZip,
};

struct TestEntry {
    QString path;
    QByteArray data;
    mode_t type = AE_IFREG;
    QString symlinkTarget;
    QString hardlinkTarget;
    mode_t permissions = 0644;
};

bool archiveOk(int status) {
    return status >= ARCHIVE_OK;
}

bool writeArchive(
    const QString& archivePath,
    TestArchiveFormat format,
    const QVector<TestEntry>& entries,
    QString* error = nullptr) {
    struct archive* writer = archive_write_new();
    if (!writer) {
        if (error)
            *error = QStringLiteral("archive_write_new failed");
        return false;
    }

    int status = ARCHIVE_FATAL;
    switch (format) {
    case TestArchiveFormat::Tar:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    case TestArchiveFormat::TarGzip:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_gzip(writer);
        break;
    case TestArchiveFormat::TarXz:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_xz(writer);
        break;
    case TestArchiveFormat::TarZstd:
        status = archive_write_set_format_pax_restricted(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_zstd(writer);
        break;
    case TestArchiveFormat::Zip:
        status = archive_write_set_format_zip(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    case TestArchiveFormat::SevenZip:
        status = archive_write_set_format_7zip(writer);
        if (archiveOk(status))
            status = archive_write_add_filter_none(writer);
        break;
    }

    if (!archiveOk(status)) {
        if (error)
            *error = QString::fromLocal8Bit(archive_error_string(writer));
        archive_write_free(writer);
        return false;
    }

    const QByteArray encodedPath = QFile::encodeName(archivePath);
    status = archive_write_open_filename(writer, encodedPath.constData());
    if (!archiveOk(status)) {
        if (error)
            *error = QString::fromLocal8Bit(archive_error_string(writer));
        archive_write_free(writer);
        return false;
    }

    for (const TestEntry& specification : entries) {
        struct archive_entry* entry = archive_entry_new();
        const QByteArray encodedEntryPath = specification.path.toUtf8();
        archive_entry_set_pathname(entry, encodedEntryPath.constData());
        archive_entry_set_filetype(entry, specification.type);
        archive_entry_set_perm(entry, specification.permissions);

        if (!specification.symlinkTarget.isEmpty()) {
            const QByteArray target = specification.symlinkTarget.toUtf8();
            archive_entry_set_symlink(entry, target.constData());
            archive_entry_set_size(entry, 0);
        } else if (!specification.hardlinkTarget.isEmpty()) {
            const QByteArray target = specification.hardlinkTarget.toUtf8();
            archive_entry_set_hardlink(entry, target.constData());
            archive_entry_set_size(entry, 0);
        } else if (specification.type == AE_IFREG) {
            archive_entry_set_size(entry, specification.data.size());
        } else {
            archive_entry_set_size(entry, 0);
        }

        status = archive_write_header(writer, entry);
        if (!archiveOk(status)) {
            if (error)
                *error = QString::fromLocal8Bit(archive_error_string(writer));
            archive_entry_free(entry);
            archive_write_close(writer);
            archive_write_free(writer);
            return false;
        }

        if (specification.type == AE_IFREG
            && specification.hardlinkTarget.isEmpty()
            && !specification.data.isEmpty()) {
            const la_ssize_t written = archive_write_data(
                writer, specification.data.constData(), specification.data.size());
            if (written != specification.data.size()) {
                if (error)
                    *error = QStringLiteral("archive_write_data wrote an unexpected byte count");
                archive_entry_free(entry);
                archive_write_close(writer);
                archive_write_free(writer);
                return false;
            }
        }

        status = archive_write_finish_entry(writer);
        archive_entry_free(entry);
        if (!archiveOk(status)) {
            if (error)
                *error = QString::fromLocal8Bit(archive_error_string(writer));
            archive_write_close(writer);
            archive_write_free(writer);
            return false;
        }
    }

    status = archive_write_close(writer);
    const bool closeOk = archiveOk(status);
    if (!closeOk && error)
        *error = QString::fromLocal8Bit(archive_error_string(writer));
    archive_write_free(writer);
    return closeOk;
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString makeDestination(QTemporaryDir& temporary) {
    const QString destination = temporary.filePath(QStringLiteral("output"));
    QDir().mkpath(destination);
    return destination;
}

} // namespace

class ArchiveExtractorTest final : public QObject {
    Q_OBJECT

private slots:
    void extractsSupportedFormats_data();
    void extractsSupportedFormats();
    void refusesTraversalAndRollsBack();
    void refusesEscapingSymlink();
    void refusesSpecialFilesystemEntries();
    void refusesExistingDestinationFile();
    void refusesOnDiskSymlinkTraversal();
    void supportsForwardHardlinksInsideRoot();
    void cancellationRollsBackCompletedEntries();
    void reportsProgressAndHonorsLimits();
};

void ArchiveExtractorTest::extractsSupportedFormats_data() {
    QTest::addColumn<int>("format");
    QTest::addColumn<QString>("suffix");

    QTest::newRow("tar") << static_cast<int>(TestArchiveFormat::Tar) << QStringLiteral(".tar");
    QTest::newRow("tar-gzip") << static_cast<int>(TestArchiveFormat::TarGzip) << QStringLiteral(".tar.gz");
    QTest::newRow("tgz") << static_cast<int>(TestArchiveFormat::TarGzip) << QStringLiteral(".tgz");
    QTest::newRow("tar-xz") << static_cast<int>(TestArchiveFormat::TarXz) << QStringLiteral(".tar.xz");
    QTest::newRow("tar-zstd") << static_cast<int>(TestArchiveFormat::TarZstd) << QStringLiteral(".tar.zst");
    QTest::newRow("zip") << static_cast<int>(TestArchiveFormat::Zip) << QStringLiteral(".zip");
    QTest::newRow("7zip") << static_cast<int>(TestArchiveFormat::SevenZip) << QStringLiteral(".7z");
}

void ArchiveExtractorTest::extractsSupportedFormats() {
    QFETCH(int, format);
    QFETCH(QString, suffix);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("sample") + suffix);
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 static_cast<TestArchiveFormat>(format),
                 {
                     {QStringLiteral("folder"), {}, AE_IFDIR, {}, {}, 0755},
                     {QStringLiteral("folder/hello.txt"), QByteArray("hello archive\n")},
                     {QStringLiteral("- spaced 雪 'quote'.txt"), QByteArray("unicode\n")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(readFile(QDir(destination).filePath(QStringLiteral("folder/hello.txt"))), QByteArray("hello archive\n"));
    QCOMPARE(readFile(QDir(destination).filePath(QStringLiteral("- spaced 雪 'quote'.txt"))), QByteArray("unicode\n"));
}

void ArchiveExtractorTest::refusesTraversalAndRollsBack() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("traversal.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {
                     {QStringLiteral("safe.txt"), QByteArray("temporary")},
                     {QStringLiteral("../escape.txt"), QByteArray("escape")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QCOMPARE(result.status, ArchiveExtractionStatus::Failed);
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("safe.txt"))));
    QVERIFY(!QFileInfo::exists(temporary.filePath(QStringLiteral("escape.txt"))));
}

void ArchiveExtractorTest::refusesEscapingSymlink() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("symlink.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {
                     {QStringLiteral("dir"), {}, AE_IFDIR, {}, {}, 0755},
                     {QStringLiteral("dir/link"), {}, AE_IFLNK, QStringLiteral("../../outside")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QCOMPARE(result.status, ArchiveExtractionStatus::Failed);
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("dir"))));
}

void ArchiveExtractorTest::refusesSpecialFilesystemEntries() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("special.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {{QStringLiteral("pipe"), {}, AE_IFIFO}},
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QCOMPARE(result.status, ArchiveExtractionStatus::Failed);
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("pipe"))));
}

void ArchiveExtractorTest::refusesExistingDestinationFile() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("overwrite.zip"));
    const QString destination = makeDestination(temporary);
    const QString existingPath = QDir(destination).filePath(QStringLiteral("same.txt"));

    QFile existing(existingPath);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("original"), qint64(8));
    existing.close();

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Zip,
                 {{QStringLiteral("same.txt"), QByteArray("replacement")}},
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QCOMPARE(result.status, ArchiveExtractionStatus::Failed);
    QCOMPARE(readFile(existingPath), QByteArray("original"));
}

void ArchiveExtractorTest::refusesOnDiskSymlinkTraversal() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("ondisk.tar"));
    const QString destination = makeDestination(temporary);
    const QString outside = temporary.filePath(QStringLiteral("outside"));
    QVERIFY(QDir().mkpath(outside));

    const QByteArray linkPath = QFile::encodeName(QDir(destination).filePath(QStringLiteral("redirect")));
    const QByteArray targetPath = QFile::encodeName(outside);
    QCOMPARE(::symlink(targetPath.constData(), linkPath.constData()), 0);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {{QStringLiteral("redirect/pwn.txt"), QByteArray("nope")}},
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QCOMPARE(result.status, ArchiveExtractionStatus::Failed);
    QVERIFY(!QFileInfo::exists(QDir(outside).filePath(QStringLiteral("pwn.txt"))));
    QVERIFY(QFileInfo(QDir(destination).filePath(QStringLiteral("redirect"))).isSymLink());
}

void ArchiveExtractorTest::supportsForwardHardlinksInsideRoot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("hardlink.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {
                     {QStringLiteral("alias.txt"), {}, AE_IFREG, {}, QStringLiteral("base.txt")},
                     {QStringLiteral("base.txt"), QByteArray("linked data")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath, destination, cancel);
    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QCOMPARE(readFile(QDir(destination).filePath(QStringLiteral("alias.txt"))), QByteArray("linked data"));

    struct stat baseInfo {};
    struct stat aliasInfo {};
    const QByteArray basePath = QFile::encodeName(QDir(destination).filePath(QStringLiteral("base.txt")));
    const QByteArray aliasPath = QFile::encodeName(QDir(destination).filePath(QStringLiteral("alias.txt")));
    QCOMPARE(::stat(basePath.constData(), &baseInfo), 0);
    QCOMPARE(::stat(aliasPath.constData(), &aliasInfo), 0);
    QCOMPARE(baseInfo.st_ino, aliasInfo.st_ino);
}

void ArchiveExtractorTest::cancellationRollsBackCompletedEntries() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("cancel.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {
                     {QStringLiteral("first.txt"), QByteArray("first")},
                     {QStringLiteral("second.txt"), QByteArray("second")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    const ArchiveExtractionResult result = ArchiveExtractor::extract(
        archivePath,
        destination,
        cancel,
        [&](const ArchiveExtractionProgress& progress) {
            if (progress.entryPath == QStringLiteral("second.txt") && progress.bytesWritten > 0)
                cancel.store(true, std::memory_order_relaxed);
        });

    QCOMPARE(result.status, ArchiveExtractionStatus::Cancelled);
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("first.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(destination).filePath(QStringLiteral("second.txt"))));
}

void ArchiveExtractorTest::reportsProgressAndHonorsLimits() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString archivePath = temporary.filePath(QStringLiteral("limits.tar"));
    const QString destination = makeDestination(temporary);

    QString writeError;
    QVERIFY2(writeArchive(
                 archivePath,
                 TestArchiveFormat::Tar,
                 {
                     {QStringLiteral("one.txt"), QByteArray("12345")},
                     {QStringLiteral("two.txt"), QByteArray("67890")},
                 },
                 &writeError),
             qPrintable(writeError));

    std::atomic_bool cancel = false;
    quint64 largestProgress = 0;
    const ArchiveExtractionResult success = ArchiveExtractor::extract(
        archivePath,
        destination,
        cancel,
        [&](const ArchiveExtractionProgress& progress) {
            largestProgress = qMax(largestProgress, progress.bytesWritten);
        });
    QVERIFY2(success.succeeded(), qPrintable(success.error));
    QCOMPARE(success.bytesWritten, quint64(10));
    QCOMPARE(largestProgress, quint64(10));

    QTemporaryDir limitedTemporary;
    QVERIFY(limitedTemporary.isValid());
    const QString limitedDestination = makeDestination(limitedTemporary);
    ArchiveExtractionLimits limits;
    limits.maximumEntries = 1;
    const ArchiveExtractionResult limited = ArchiveExtractor::extract(
        archivePath, limitedDestination, cancel, {}, limits);
    QCOMPARE(limited.status, ArchiveExtractionStatus::Failed);
    QVERIFY(!QFileInfo::exists(QDir(limitedDestination).filePath(QStringLiteral("one.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(limitedDestination).filePath(QStringLiteral("two.txt"))));
}

QTEST_MAIN(ArchiveExtractorTest)
#include "ArchiveExtractorTest.moc"
