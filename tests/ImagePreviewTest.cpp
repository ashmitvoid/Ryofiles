// SPDX-License-Identifier: GPL-3.0-only

#include "preview/PreviewProtocol.hpp"
#include "preview/PreviewScheduler.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class ImagePreviewTest final : public QObject {
    Q_OBJECT

private:
    static QString helperPath() {
        return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("ryofiles-preview-helper"));
    }

    static PreviewResult execute(
        PreviewScheduler& scheduler,
        PreviewScheduler::Lane lane,
        QObject& owner,
        const QJsonObject& request) {
        PreviewResult result;
        result.error = QStringLiteral("Image preview test timed out");
        bool completed = false;
        QEventLoop loop;

        const bool submitted = scheduler.submit(
            lane,
            &owner,
            request,
            [&result, &completed, &loop](PreviewResult value) {
                result = std::move(value);
                completed = true;
                loop.quit();
            });
        if (!submitted) {
            result.error = QStringLiteral("Image preview request was rejected by scheduler");
            return result;
        }

        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!completed)
            scheduler.cancelOwner(&owner);
        return result;
    }

    static QByteArray jpegFixture() {
        return QByteArray::fromBase64(
            QByteArrayLiteral("/9j/4AAQSkZJRgABAQAAAQABAAD/4QEeRXhpZgAATU0AKgAAAAgABgEPAAIAAAAQAAAAVgEQAAIAAAAIAAAAZgESAAMAAAABAAYAAAExAAIAAAAOAAAAbgEyAAIAAAAUAAAAfIdpAAQAAAABAAAAkAAAAABSeW9maWxlcyBDYW1lcmEATW9kZWwgWABSeW9maWxlcyBUZXN0ADIwMjY6MDk6MDYgMTQ6MDA6MDAAAAaCmgAFAAAAAQAAAN6CnQAFAAAAAQAAAOaIJwADAAAAAQDIAACQAwACAAAAFAAAAO6SCgAFAAAAAQAAAQKkNAACAAAADAAAAQoAAAAAAAAAAQAAAH0AAAAcAAAACjIwMjY6MDk6MDYgMTM6NTk6NTgAAAAAMgAAAAFSeW8gTGVucyA1MAD/2wBDAAgGBgcGBQgHBwcJCQgKDBQNDAsLDBkSEw8UHRofHh0aHBwgJC4nICIsIxwcKDcpLDAxNDQ0Hyc5PTgyPC4zNDL/2wBDAQkJCQwLDBgNDRgyIRwhMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjIyMjL/wAARCAADAAQDASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwDlqKKK8c9Q/9k="));
    }

    static QByteArray gifFixture() {
        return QByteArray::fromBase64(
            QByteArrayLiteral("R0lGODlhAgACAIEAAP8AAAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACgAAACwAAAAAAgACAAAIBgABCAQQEAAh+QQBDAABACwAAAAAAgACAIEA/wAAAAAAAAAAAAAIBgABCAQQEAA7"));
    }

private slots:
    void imageBoundsAreFrozen() {
        QCOMPARE(PreviewProtocol::kMaxImageMetadataInputBytes, 64LL * 1024 * 1024);
        QCOMPARE(PreviewProtocol::kMaxImageMetadataChars, 256);
        QCOMPARE(PreviewProtocol::kMaxAnimationInputBytes, 64LL * 1024 * 1024);
        QCOMPARE(PreviewProtocol::kMaxAnimationSourceDimension, 4096);
        QCOMPARE(PreviewProtocol::kMaxAnimationFrames, 120);
        QCOMPARE(PreviewProtocol::kMinAnimationDelayMs, 50);
        QCOMPARE(PreviewProtocol::kMaxAnimationPlayMs, 30'000);
    }

    void readsExifWithoutPathReopen() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("fixture.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(jpegFixture()), jpegFixture().size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            PreviewScheduler::Lane::InteractivePreview,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-probe")},
                {QStringLiteral("path"), path},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payload.value(QStringLiteral("pixelWidth")).toInt(), 4);
        QCOMPARE(result.payload.value(QStringLiteral("pixelHeight")).toInt(), 3);
        QCOMPARE(result.payload.value(QStringLiteral("cameraMake")).toString(), QStringLiteral("Ryofiles Camera"));
        QCOMPARE(result.payload.value(QStringLiteral("cameraModel")).toString(), QStringLiteral("Model X"));
        QCOMPARE(result.payload.value(QStringLiteral("lensModel")).toString(), QStringLiteral("Ryo Lens 50"));
        QCOMPARE(result.payload.value(QStringLiteral("iso")).toString(), QStringLiteral("200"));
        QVERIFY(result.payload.value(QStringLiteral("metadataAvailable")).toBool());
    }

    void refusesImageSymlink() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("source.jpg"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(jpegFixture()), jpegFixture().size());
        file.close();
        const QString link = directory.filePath(QStringLiteral("linked.jpg"));
        QVERIFY(QFile::link(source, link));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            PreviewScheduler::Lane::InteractivePreview,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-probe")},
                {QStringLiteral("path"), link},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("regular file")));
    }

    void skipsDeepMetadataPastBound() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("large.jpg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QCOMPARE(file.write(jpegFixture()), jpegFixture().size());
        QVERIFY(file.resize(PreviewProtocol::kMaxImageMetadataInputBytes + 1));
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            PreviewScheduler::Lane::InteractivePreview,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-probe")},
                {QStringLiteral("path"), path},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.payload.value(QStringLiteral("metadataLimited")).toBool());
        QVERIFY(!result.payload.value(QStringLiteral("metadataAvailable")).toBool());
    }

    void animatedGifRequiresExplicitLongWorkRequests() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("two-frame.gif"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(gifFixture()), gifFixture().size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject probeOwner;
        const PreviewResult probe = execute(
            scheduler,
            PreviewScheduler::Lane::InteractivePreview,
            probeOwner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-probe")},
                {QStringLiteral("path"), path},
            });
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY(probe.payload.value(QStringLiteral("animated")).toBool());
        QVERIFY(probe.payload.value(QStringLiteral("animationSupported")).toBool());
        QCOMPARE(probe.payload.value(QStringLiteral("frameCount")).toInt(), 2);

        QObject animationOwner;
        const QString session = QStringLiteral("image-test-session");
        const PreviewResult first = execute(
            scheduler,
            PreviewScheduler::Lane::ExplicitLongWork,
            animationOwner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-animation-start")},
                {QStringLiteral("path"), path},
                {QStringLiteral("session"), session},
                {QStringLiteral("maxWidth"), 640},
                {QStringLiteral("maxHeight"), 480},
            });
        QVERIFY2(first.ok, qPrintable(first.error));
        QCOMPARE(first.payload.value(QStringLiteral("session")).toString(), session);
        QCOMPARE(first.payload.value(QStringLiteral("frame")).toInt(), 0);
        QCOMPARE(first.payload.value(QStringLiteral("frameCount")).toInt(), 2);
        QVERIFY(!first.payload.value(QStringLiteral("imageBase64")).toString().isEmpty());

        const PreviewResult second = execute(
            scheduler,
            PreviewScheduler::Lane::ExplicitLongWork,
            animationOwner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("image-animation-next")},
                {QStringLiteral("session"), session},
            });
        QVERIFY2(second.ok, qPrintable(second.error));
        QCOMPARE(second.payload.value(QStringLiteral("frame")).toInt(), 1);
        QCOMPARE(second.payload.value(QStringLiteral("frameCount")).toInt(), 2);
        QVERIFY(!second.payload.value(QStringLiteral("imageBase64")).toString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ImagePreviewTest)
#include "ImagePreviewTest.moc"
