// SPDX-License-Identifier: GPL-3.0-only

#include "thumbnails/ThumbnailStore.hpp"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <memory>

class ThumbnailStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());

        const QString cache = m_root->filePath("cache");
        QVERIFY(QDir().mkpath(cache));
        qputenv("XDG_CACHE_HOME", cache.toUtf8());
    }

    void candidateClassificationIsConservative() {
        QVERIFY(ThumbnailStore::isCandidatePath("photo.JPG"));
        QVERIFY(ThumbnailStore::isCandidatePath("picture.webp"));
        QVERIFY(ThumbnailStore::isCandidatePath("photo:local.png"));
        QVERIFY(!ThumbnailStore::isCandidatePath("sftp://example.invalid/photo.png"));
        QVERIFY(!ThumbnailStore::isCandidatePath("movie.mkv"));
        QVERIFY(!ThumbnailStore::isCandidatePath("notes.txt"));
    }

    void decodeScalesBeforeReturning() {
        const QString path = m_root->filePath("large.png");
        QImage source(2000, 1000, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::white);
        QVERIFY(source.save(path, "PNG"));

        std::atomic_bool cancelled = false;
        QString error;
        const QImage thumbnail = ThumbnailStore::load(path, 128, cancelled, &error);

        QVERIFY2(!thumbnail.isNull(), qPrintable(error));
        QVERIFY(thumbnail.width() <= 128);
        QVERIFY(thumbnail.height() <= 128);
        QCOMPARE(thumbnail.size(), QSize(128, 64));
    }

    void cancelledRequestDoesNoDecode() {
        const QString path = m_root->filePath("cancel.png");
        QImage source(256, 256, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::black);
        QVERIFY(source.save(path, "PNG"));

        std::atomic_bool cancelled = true;
        const QImage thumbnail = ThumbnailStore::load(path, 128, cancelled);
        QVERIFY(thumbnail.isNull());
    }

    void nonImageAndDirectoryAreRejected() {
        const QString directory = m_root->filePath("folder.jpg");
        QVERIFY(QDir().mkpath(directory));

        const QString textPath = m_root->filePath("fake.txt");
        QFile file(textPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("not an image") > 0);
        file.close();

        std::atomic_bool cancelled = false;
        QVERIFY(ThumbnailStore::load(directory, 128, cancelled).isNull());
        QVERIFY(ThumbnailStore::load(textPath, 128, cancelled).isNull());
    }

    void uriLikePathIsRejectedButLocalColonPathLoads() {
        std::atomic_bool cancelled = false;
        QString error;
        const QImage remote = ThumbnailStore::load(
            QStringLiteral("sftp://example.invalid/photo.png"),
            128,
            cancelled,
            &error);
        QVERIFY(remote.isNull());
        QVERIFY(error.isEmpty());

        const QString local = m_root->filePath("photo:local.png");
        QImage source(320, 160, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::white);
        QVERIFY(source.save(local, "PNG"));

        const QImage thumbnail = ThumbnailStore::load(local, 80, cancelled, &error);
        QVERIFY2(!thumbnail.isNull(), qPrintable(error));
        QCOMPARE(thumbnail.size(), QSize(80, 40));
    }

private:
    std::unique_ptr<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(ThumbnailStoreTest)
#include "ThumbnailStoreTest.moc"
