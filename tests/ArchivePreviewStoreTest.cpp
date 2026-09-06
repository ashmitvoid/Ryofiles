// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchiveCreator.hpp"
#include "preview/ArchivePreviewStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <unistd.h>

namespace {

void writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(contents), contents.size());
}

ArchiveCreationResult createArchive(
    const QStringList& sources,
    const QString& archivePath) {
    std::atomic_bool cancel = false;
    return ArchiveCreator::create(sources, archivePath, cancel);
}

const ArchivePreviewEntry* findEntry(
    const ArchivePreviewResult& result,
    const QString& path) {
    const auto it = std::find_if(
        result.entries.cbegin(),
        result.entries.cend(),
        [&](const ArchivePreviewEntry& entry) { return entry.path == path; });
    return it == result.entries.cend() ? nullptr : &(*it);
}

} // namespace

class ArchivePreviewStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void classifiesOnlyTestedLocalArchiveCandidates();
    void inspectsSupportedFormats_data();
    void inspectsSupportedFormats();
    void entryLimitStopsPreviewEarly();
    void logicalBudgetStopsBeforeLargePayload();
    void rejectsArchiveBeyondRawInputLimit();
    void cancellationStopsBeforePayloadTraversal();
    void rejectsRemoteMissingSymlinkAndMalformedInputs();
    void rejectsUnboundedCallerLimits();
};

void ArchivePreviewStoreTest::classifiesOnlyTestedLocalArchiveCandidates() {
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.tar")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.TAR.GZ")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.tgz")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.tar.xz")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.tar.zst")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.zip")));
    QVERIFY(ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.7z")));

    QVERIFY(!ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.rar")));
    QVERIFY(!ArchivePreviewStore::isCandidatePath(QStringLiteral("sample.gz")));
    QVERIFY(!ArchivePreviewStore::isCandidatePath(QStringLiteral("sftp://example.invalid/sample.zip")));
    QVERIFY(!ArchivePreviewStore::isCandidatePath(QString()));
}

void ArchivePreviewStoreTest::inspectsSupportedFormats_data() {
    QTest::addColumn<QString>("suffix");

    QTest::newRow("tar") << QStringLiteral(".tar");
    QTest::newRow("tar-gzip") << QStringLiteral(".tar.gz");
    QTest::newRow("tgz") << QStringLiteral(".tgz");
    QTest::newRow("tar-xz") << QStringLiteral(".tar.xz");
    QTest::newRow("tar-zstd") << QStringLiteral(".tar.zst");
    QTest::newRow("zip") << QStringLiteral(".zip");
    QTest::newRow("7zip") << QStringLiteral(".7z");
}

void ArchivePreviewStoreTest::inspectsSupportedFormats() {
    QFETCH(QString, suffix);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString collection = temporary.filePath(QStringLiteral("collection"));
    const QString nested = QDir(collection).filePath(QStringLiteral("nested"));
    QVERIFY(QDir().mkpath(nested));

    const QByteArray firstPayload("hello archive preview\n");
    const QByteArray secondPayload("unicode payload\n");
    writeFile(QDir(nested).filePath(QStringLiteral("hello.txt")), firstPayload);
    writeFile(
        QDir(collection).filePath(QStringLiteral("- spaced 雪 'quote'.txt")),
        secondPayload);

    const QString archivePath = temporary.filePath(QStringLiteral("sample") + suffix);
    const ArchiveCreationResult created = createArchive({collection}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    std::atomic_bool cancel = false;
    quint64 progressEntries = 0;
    const ArchivePreviewResult result = ArchivePreviewStore::inspect(
        archivePath,
        cancel,
        [&](const ArchivePreviewProgress& progress) {
            progressEntries = qMax(progressEntries, progress.entriesSeen);
        });

    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(!result.truncated);
    QVERIFY(!result.formatName.isEmpty());
    QCOMPARE(result.entries.size(), qsizetype(4));
    QCOMPARE(result.entriesSeen, quint64(4));
    QCOMPARE(progressEntries, result.entriesSeen);
    QVERIFY(result.archiveBytesConsumed > 0);
    QVERIFY(result.archiveBytesConsumed <= static_cast<quint64>(QFileInfo(archivePath).size()));

    const ArchivePreviewEntry* root = findEntry(result, QStringLiteral("collection/"));
    if (!root)
        root = findEntry(result, QStringLiteral("collection"));
    QVERIFY(root);
    QCOMPARE(root->kind, ArchivePreviewEntryKind::Directory);

    const ArchivePreviewEntry* hello =
        findEntry(result, QStringLiteral("collection/nested/hello.txt"));
    QVERIFY(hello);
    QCOMPARE(hello->kind, ArchivePreviewEntryKind::RegularFile);
    QVERIFY(hello->sizeKnown);
    QCOMPARE(hello->size, static_cast<quint64>(firstPayload.size()));

    const ArchivePreviewEntry* unicode =
        findEntry(result, QStringLiteral("collection/- spaced 雪 'quote'.txt"));
    QVERIFY(unicode);
    QCOMPARE(unicode->kind, ArchivePreviewEntryKind::RegularFile);
    QVERIFY(unicode->sizeKnown);
    QCOMPARE(unicode->size, static_cast<quint64>(secondPayload.size()));
}

void ArchivePreviewStoreTest::entryLimitStopsPreviewEarly() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    QStringList sources;
    for (int i = 0; i < 5; ++i) {
        const QString path = temporary.filePath(QStringLiteral("file-%1.txt").arg(i));
        writeFile(path, QByteArray(1024, static_cast<char>('a' + i)));
        sources.push_back(path);
    }

    const QString archivePath = temporary.filePath(QStringLiteral("many.tar"));
    const ArchiveCreationResult created = createArchive(sources, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    ArchivePreviewLimits limits;
    limits.maximumEntries = 2;

    std::atomic_bool cancel = false;
    const ArchivePreviewResult result = ArchivePreviewStore::inspect(
        archivePath, cancel, {}, limits);

    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(result.truncated);
    QCOMPARE(result.entries.size(), qsizetype(2));
    QCOMPARE(result.entriesSeen, quint64(2));
    QVERIFY(result.archiveBytesConsumed > 0);
    QVERIFY(result.archiveBytesConsumed <= static_cast<quint64>(QFileInfo(archivePath).size()));
}

void ArchivePreviewStoreTest::logicalBudgetStopsBeforeLargePayload() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("large.bin"));
    writeFile(source, QByteArray(2 * 1024 * 1024, 'x'));

    const QString archivePath = temporary.filePath(QStringLiteral("large.tar"));
    const ArchiveCreationResult created = createArchive({source}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    ArchivePreviewLimits limits;
    limits.maximumLogicalBytes = 1024;

    std::atomic_bool cancel = false;
    const ArchivePreviewResult result = ArchivePreviewStore::inspect(
        archivePath, cancel, {}, limits);

    QVERIFY2(result.succeeded(), qPrintable(result.error));
    QVERIFY(result.truncated);
    QCOMPARE(result.entries.size(), qsizetype(1));
    QCOMPARE(result.entries.first().kind, ArchivePreviewEntryKind::RegularFile);
    QVERIFY(result.entries.first().sizeKnown);
    QCOMPARE(result.entries.first().size, quint64(2 * 1024 * 1024));
    QVERIFY(result.archiveBytesConsumed < static_cast<quint64>(QFileInfo(archivePath).size()));
}

void ArchivePreviewStoreTest::rejectsArchiveBeyondRawInputLimit() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("payload.bin"));
    writeFile(source, QByteArray(512 * 1024, 'p'));

    const QString archivePath = temporary.filePath(QStringLiteral("payload.tar"));
    const ArchiveCreationResult created = createArchive({source}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    const qint64 archiveSize = QFileInfo(archivePath).size();
    QVERIFY(archiveSize > 1);

    ArchivePreviewLimits limits;
    limits.maximumArchiveBytes = static_cast<quint64>(archiveSize - 1);

    std::atomic_bool cancel = false;
    const ArchivePreviewResult result = ArchivePreviewStore::inspect(
        archivePath, cancel, {}, limits);

    QCOMPARE(result.status, ArchivePreviewStatus::Failed);
    QVERIFY(result.error.contains(QStringLiteral("input-byte"), Qt::CaseInsensitive));
    QVERIFY(result.entries.isEmpty());
}

void ArchivePreviewStoreTest::cancellationStopsBeforePayloadTraversal() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString first = temporary.filePath(QStringLiteral("first.bin"));
    const QString second = temporary.filePath(QStringLiteral("second.bin"));
    writeFile(first, QByteArray(1024 * 1024, 'a'));
    writeFile(second, QByteArray(1024 * 1024, 'b'));

    const QString archivePath = temporary.filePath(QStringLiteral("cancel.tar.gz"));
    const ArchiveCreationResult created = createArchive({first, second}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    std::atomic_bool cancel = false;
    const ArchivePreviewResult result = ArchivePreviewStore::inspect(
        archivePath,
        cancel,
        [&](const ArchivePreviewProgress& progress) {
            if (progress.entriesSeen == 1)
                cancel.store(true, std::memory_order_relaxed);
        });

    QCOMPARE(result.status, ArchivePreviewStatus::Cancelled);
    QCOMPARE(result.entries.size(), qsizetype(1));
    QCOMPARE(result.entriesSeen, quint64(1));
    QVERIFY(result.archiveBytesConsumed > 0);
}

void ArchivePreviewStoreTest::rejectsRemoteMissingSymlinkAndMalformedInputs() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    std::atomic_bool cancel = false;

    QCOMPARE(
        ArchivePreviewStore::inspect(
            QStringLiteral("sftp://example.invalid/archive.zip"), cancel).status,
        ArchivePreviewStatus::Failed);
    QCOMPARE(
        ArchivePreviewStore::inspect(
            temporary.filePath(QStringLiteral("missing.zip")), cancel).status,
        ArchivePreviewStatus::Failed);

    const QString source = temporary.filePath(QStringLiteral("source.txt"));
    writeFile(source, QByteArrayLiteral("payload"));
    const QString archivePath = temporary.filePath(QStringLiteral("real.tar"));
    const ArchiveCreationResult created = createArchive({source}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    const QString symlinkPath = temporary.filePath(QStringLiteral("link.tar"));
    const QByteArray encodedLink = QFile::encodeName(symlinkPath);
    QCOMPARE(::symlink("real.tar", encodedLink.constData()), 0);
    const ArchivePreviewResult symlink = ArchivePreviewStore::inspect(symlinkPath, cancel);
    QCOMPARE(symlink.status, ArchivePreviewStatus::Failed);

    const QString malformedPath = temporary.filePath(QStringLiteral("broken.zip"));
    writeFile(malformedPath, QByteArrayLiteral("this is not an archive"));
    const ArchivePreviewResult malformed = ArchivePreviewStore::inspect(malformedPath, cancel);
    QCOMPARE(malformed.status, ArchivePreviewStatus::Failed);
    QVERIFY(!malformed.error.isEmpty());

    const QString unsupportedPath = temporary.filePath(QStringLiteral("archive.rar"));
    writeFile(unsupportedPath, QByteArrayLiteral("not inspected"));
    QCOMPARE(
        ArchivePreviewStore::inspect(unsupportedPath, cancel).status,
        ArchivePreviewStatus::Failed);
}

void ArchivePreviewStoreTest::rejectsUnboundedCallerLimits() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString source = temporary.filePath(QStringLiteral("source.txt"));
    writeFile(source, QByteArrayLiteral("payload"));
    const QString archivePath = temporary.filePath(QStringLiteral("sample.tar"));
    const ArchiveCreationResult created = createArchive({source}, archivePath);
    QVERIFY2(created.succeeded(), qPrintable(created.error));

    std::atomic_bool cancel = false;

    ArchivePreviewLimits zeroEntries;
    zeroEntries.maximumEntries = 0;
    QCOMPARE(
        ArchivePreviewStore::inspect(archivePath, cancel, {}, zeroEntries).status,
        ArchivePreviewStatus::Failed);

    ArchivePreviewLimits excessiveEntries;
    excessiveEntries.maximumEntries = 1025;
    QCOMPARE(
        ArchivePreviewStore::inspect(archivePath, cancel, {}, excessiveEntries).status,
        ArchivePreviewStatus::Failed);

    ArchivePreviewLimits excessiveInput;
    excessiveInput.maximumArchiveBytes = 257ULL * 1024ULL * 1024ULL;
    QCOMPARE(
        ArchivePreviewStore::inspect(archivePath, cancel, {}, excessiveInput).status,
        ArchivePreviewStatus::Failed);
}

QTEST_GUILESS_MAIN(ArchivePreviewStoreTest)
#include "ArchivePreviewStoreTest.moc"
