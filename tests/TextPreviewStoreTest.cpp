// SPDX-License-Identifier: GPL-3.0-only

#include "preview/TextPreviewStore.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>

class TextPreviewStoreTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& data) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(data), data.size());
    }

private slots:
    void candidateClassificationIsConservative() {
        QVERIFY(TextPreviewStore::isCandidatePath("notes.txt"));
        QVERIFY(TextPreviewStore::isCandidatePath("main.cpp"));
        QVERIFY(TextPreviewStore::isCandidatePath("config.yaml"));
        QVERIFY(TextPreviewStore::isCandidatePath("Dockerfile"));
        QVERIFY(TextPreviewStore::isCandidatePath("PKGBUILD"));
        QVERIFY(!TextPreviewStore::isCandidatePath("movie.mkv"));
        QVERIFY(!TextPreviewStore::isCandidatePath("archive.zip"));
        QVERIFY(!TextPreviewStore::isCandidatePath("photo.png"));
    }

    void ordinaryUtf8TextLoads() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath("sample.md");
        writeFile(path, "# Ryofiles\nFast preview.\n");

        std::atomic_bool cancelled = false;
        const TextPreviewResult result = TextPreviewStore::load(path, cancelled);

        QVERIFY(result.supported);
        QVERIFY(!result.truncated);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.text, QStringLiteral("# Ryofiles\nFast preview.\n"));
    }

    void byteLimitTruncatesWithoutWholeFileRead() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath("huge.log");
        writeFile(path, QByteArray(320 * 1024, 'a'));

        std::atomic_bool cancelled = false;
        const TextPreviewResult result = TextPreviewStore::load(path, cancelled);

        QVERIFY(result.supported);
        QVERIFY(result.truncated);
        QVERIFY(result.text.size() <= 192 * 1024);
    }

    void lineLimitBoundsQmlLayoutWork() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        QByteArray lines;
        for (int i = 0; i < 3200; ++i)
            lines += "line\n";

        const QString path = QDir(temp.path()).filePath("many.txt");
        writeFile(path, lines);

        std::atomic_bool cancelled = false;
        const TextPreviewResult result = TextPreviewStore::load(path, cancelled);

        QVERIFY(result.supported);
        QVERIFY(result.truncated);
        QVERIFY(result.text.count(QLatin1Char('\n')) < 2500);
    }

    void binaryAndInvalidUtf8AreRejected() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString binaryPath = QDir(temp.path()).filePath("binary.txt");
        QByteArray binary("abc", 3);
        binary.append('\0');
        binary.append("def", 3);
        writeFile(binaryPath, binary);

        const QString invalidPath = QDir(temp.path()).filePath("invalid.log");
        QByteArray invalid;
        invalid.append(char(0xFF));
        invalid.append(char(0xFE));
        invalid.append(char(0xFA));
        writeFile(invalidPath, invalid);

        std::atomic_bool cancelled = false;
        QVERIFY(!TextPreviewStore::load(binaryPath, cancelled).supported);
        QVERIFY(!TextPreviewStore::load(invalidPath, cancelled).supported);
    }

    void cancelledAndNonCandidatesDoNoPreviewWork() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString textPath = QDir(temp.path()).filePath("cancel.txt");
        writeFile(textPath, "hello");

        const QString imagePath = QDir(temp.path()).filePath("fake.png");
        writeFile(imagePath, "not actually an image");

        std::atomic_bool cancelled = true;
        QVERIFY(!TextPreviewStore::load(textPath, cancelled).supported);

        cancelled.store(false);
        QVERIFY(!TextPreviewStore::load(imagePath, cancelled).supported);
    }
};

QTEST_APPLESS_MAIN(TextPreviewStoreTest)
#include "TextPreviewStoreTest.moc"
