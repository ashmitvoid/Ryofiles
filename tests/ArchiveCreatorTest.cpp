// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchiveCreator.hpp"
#include "archive/ArchiveExtractor.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray rawSymlinkTarget(const QString& path) {
    QByteArray buffer(256, '\0');
    const QByteArray encoded = QFile::encodeName(path);

    while (buffer.size() <= 1024 * 1024) {
        const ssize_t length = ::readlink(
            encoded.constData(),
            buffer.data(),
            static_cast<size_t>(buffer.size()));
        if (length < 0)
            return {};
        if (length < buffer.size()) {
            buffer.resize(static_cast<qsizetype>(length));
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

QString makeExtractionDirectory(QTemporaryDir& temporary) {
    const QString path = temporary.filePath(QStringLiteral("extracted"));
    QDir().mkpath(path);
    return path;
}

} // namespace

class ArchiveCreatorTest final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsSupportedFormats_data();
    void roundTripsSupportedFormats();
    void preservesSymlinkWithoutFollowingIt();
    void neverOverwritesExistingArchive();
    void cancellationRemovesPartialOutput();
    void rejectsUnsupportedRemoteAndMissingInputs();
    void rejectsOutputInsideSelectedDirectory();
    void rejectsDuplicateTopLevelNames();
    void rejectsSpecialFilesystemEntries();
};

void ArchiveCreatorTest::roundTripsSupportedFormats_data() {
    QTest::addColumn<QString>("suffix");

    QTest::newRow("tar") << QStringLiteral(".tar");
    QTest::newRow("tar-gzip") << QStringLiteral(".tar.gz");
    QTest::newRow("tgz") << QStringLiteral(".tgz");
    QTest::newRow("tar-xz") << QStringLiteral(".tar.xz");
    QTest::newRow("tar-zstd") << QStringLiteral(".tar.zst");
    QTest::newRow("zip") << QStringLiteral(".zip");
    QTest::newRow("7zip") << QStringLiteral(".7z");
}

void ArchiveCreatorTest::roundTripsSupportedFormats() {
    QFETCH(QString, suffix);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString collection = temporary.filePath(QStringLiteral("collection"));
    const QString nested = QDir(collection).filePath(QStringLiteral("folder"));
    QVERIFY(QDir().mkpath(nested));

    const QByteArray hello("hello archive\n");
    const QByteArray unicode("unicode payload\n");
    writeFile(QDir(nested).filePath(QStringLiteral("hello.txt")), hello);
    writeFile(
        QDir(collection).filePath(QStringLiteral("- spaced 雪 'quote'.txt")),
        unicode);

    const QString archivePath = temporary.filePath(QStringLiteral("sample") + suffix);
    std::atomic_bool cancel = false;
    quint64 largestEntries = 0;
    quint64 largestBytes = 0;

    const ArchiveCreationResult created = ArchiveCreator::create(
        {collection},
        archivePath,
        cancel,
        [&](const ArchiveCreationProgress& progress) {
            largestEntries = qMax(largestEntries, progress.entriesProcessed);
            largestBytes = qMax(largestBytes, progress.bytesRead);
        });

    QVERIFY2(created.succeeded(), qPrintable(created.error));
    QVERIFY(QFileInfo(archivePath).isFile());
    QCOMPARE(created.entriesWritten, quint64(4));
    QCOMPARE(created.bytesRead, quint64(hello.size() + unicode.size()));
    QCOMPARE(largestEntries, created.entriesWritten);
    QCOMPARE(largestBytes, created.bytesRead);

    const QString extraction = makeExtractionDirectory(temporary);
    const ArchiveExtractionResult extracted = ArchiveExtractor::extract(
        archivePath,
        extraction,
        cancel);
    QVERIFY2(extracted.succeeded(), qPrintable(extracted.error));

    QCOMPARE(
        readFile(QDir(extraction).filePath(QStringLiteral("collection/folder/hello.txt"))),
        hello);
    QCOMPARE(
        readFile(QDir(extraction).filePath(QStringLiteral("collection/- spaced 雪 'quote'.txt"))),
        unicode);
}

void ArchiveCreatorTest::preservesSymlinkWithoutFollowingIt() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString target = temporary.filePath(QStringLiteral("target.txt"));
    const QString link = temporary.filePath(QStringLiteral("link.txt"));
    writeFile(target, QByteArray("target payload"));

    const QByteArray encodedLink = QFile::encodeName(link);
    QCOMPARE(::symlink("target.txt", encodedLink.constData()), 0);
    QCOMPARE(rawSymlinkTarget(link), QByteArray("target.txt"));

    const QString archivePath = temporary.filePath(QStringLiteral("links.tar"));
    std::atomic_bool cancel = false;
    const ArchiveCreationResult created = ArchiveCreator::create(
        {target, link},
        archivePath,
        cancel);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    const QString extraction = makeExtractionDirectory(temporary);
    const ArchiveExtractionResult extracted = ArchiveExtractor::extract(
        archivePath,
        extraction,
        cancel);
    QVERIFY2(extracted.succeeded(), qPrintable(extracted.error));

    const QString extractedTarget = QDir(extraction).filePath(QStringLiteral("target.txt"));
    const QString extractedLink = QDir(extraction).filePath(QStringLiteral("link.txt"));
    QCOMPARE(readFile(extractedTarget), QByteArray("target payload"));
    QVERIFY(QFileInfo(extractedLink).isSymLink());
    QCOMPARE(rawSymlinkTarget(extractedLink), QByteArray("target.txt"));
}

void ArchiveCreatorTest::neverOverwritesExistingArchive() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("source.txt"));
    const QString archivePath = temporary.filePath(QStringLiteral("existing.tar"));
    writeFile(source, QByteArray("new payload"));
    writeFile(archivePath, QByteArray("original archive bytes"));

    std::atomic_bool cancel = false;
    const ArchiveCreationResult result = ArchiveCreator::create(
        {source},
        archivePath,
        cancel);

    QCOMPARE(result.status, ArchiveCreationStatus::Failed);
    QCOMPARE(readFile(archivePath), QByteArray("original archive bytes"));
}

void ArchiveCreatorTest::cancellationRemovesPartialOutput() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("large.bin"));
    QByteArray block(1024 * 1024, 'x');
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < 8; ++i)
        QCOMPARE(file.write(block), block.size());
    file.close();

    const QString archivePath = temporary.filePath(QStringLiteral("cancel.tar.zst"));
    std::atomic_bool cancel = false;
    const ArchiveCreationResult result = ArchiveCreator::create(
        {source},
        archivePath,
        cancel,
        [&](const ArchiveCreationProgress& progress) {
            if (progress.bytesRead >= 4ULL * 1024ULL * 1024ULL)
                cancel.store(true, std::memory_order_relaxed);
        });

    QCOMPARE(result.status, ArchiveCreationStatus::Cancelled);
    QVERIFY(!QFileInfo::exists(archivePath));

    const QStringList leftovers = QDir(temporary.path()).entryList(
        {QStringLiteral(".*.ryofiles-archive-*.tmp")},
        QDir::Files | QDir::Hidden);
    QVERIFY(leftovers.isEmpty());
}

void ArchiveCreatorTest::rejectsUnsupportedRemoteAndMissingInputs() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("source.txt"));
    const QString missing = temporary.filePath(QStringLiteral("missing.txt"));
    writeFile(source, QByteArray("payload"));

    std::atomic_bool cancel = false;

    QCOMPARE(
        ArchiveCreator::create(
            {source},
            temporary.filePath(QStringLiteral("unsupported.rar")),
            cancel).status,
        ArchiveCreationStatus::Failed);
    QCOMPARE(
        ArchiveCreator::create(
            {QStringLiteral("sftp://example.invalid/source.txt")},
            temporary.filePath(QStringLiteral("remote-source.tar")),
            cancel).status,
        ArchiveCreationStatus::Failed);
    QCOMPARE(
        ArchiveCreator::create(
            {source},
            QStringLiteral("sftp://example.invalid/output.tar"),
            cancel).status,
        ArchiveCreationStatus::Failed);
    QCOMPARE(
        ArchiveCreator::create(
            {missing},
            temporary.filePath(QStringLiteral("missing.tar")),
            cancel).status,
        ArchiveCreationStatus::Failed);
    QCOMPARE(
        ArchiveCreator::create(
            {},
            temporary.filePath(QStringLiteral("empty.tar")),
            cancel).status,
        ArchiveCreationStatus::Failed);
}

void ArchiveCreatorTest::rejectsOutputInsideSelectedDirectory() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString directory = temporary.filePath(QStringLiteral("tree"));
    QVERIFY(QDir().mkpath(directory));
    writeFile(QDir(directory).filePath(QStringLiteral("data.txt")), QByteArray("payload"));

    const QString archivePath = QDir(directory).filePath(QStringLiteral("inside.tar"));
    std::atomic_bool cancel = false;
    const ArchiveCreationResult result = ArchiveCreator::create(
        {directory},
        archivePath,
        cancel);

    QCOMPARE(result.status, ArchiveCreationStatus::Failed);
    QVERIFY(!QFileInfo::exists(archivePath));
}

void ArchiveCreatorTest::rejectsDuplicateTopLevelNames() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString firstDir = temporary.filePath(QStringLiteral("first"));
    const QString secondDir = temporary.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(firstDir));
    QVERIFY(QDir().mkpath(secondDir));

    const QString first = QDir(firstDir).filePath(QStringLiteral("same.txt"));
    const QString second = QDir(secondDir).filePath(QStringLiteral("same.txt"));
    writeFile(first, QByteArray("first"));
    writeFile(second, QByteArray("second"));

    const QString archivePath = temporary.filePath(QStringLiteral("duplicates.zip"));
    std::atomic_bool cancel = false;
    const ArchiveCreationResult result = ArchiveCreator::create(
        {first, second},
        archivePath,
        cancel);

    QCOMPARE(result.status, ArchiveCreationStatus::Failed);
    QVERIFY(!QFileInfo::exists(archivePath));
}

void ArchiveCreatorTest::rejectsSpecialFilesystemEntries() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString fifo = temporary.filePath(QStringLiteral("pipe"));
    const QByteArray encoded = QFile::encodeName(fifo);
    QCOMPARE(::mkfifo(encoded.constData(), 0600), 0);

    const QString archivePath = temporary.filePath(QStringLiteral("special.tar"));
    std::atomic_bool cancel = false;
    const ArchiveCreationResult result = ArchiveCreator::create(
        {fifo},
        archivePath,
        cancel);

    QCOMPARE(result.status, ArchiveCreationStatus::Failed);
    QVERIFY(!QFileInfo::exists(archivePath));
}

QTEST_MAIN(ArchiveCreatorTest)
#include "ArchiveCreatorTest.moc"
