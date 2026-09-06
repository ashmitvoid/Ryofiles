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

class MediaProbeTest final : public QObject {
    Q_OBJECT

private:
    static QString helperPath() {
        return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("ryofiles-preview-helper"));
    }

    static PreviewResult execute(
        PreviewScheduler& scheduler,
        QObject& owner,
        const QJsonObject& request) {
        PreviewResult result;
        result.error = QStringLiteral("Media preview test timed out");
        bool completed = false;
        QEventLoop loop;

        const bool submitted = scheduler.submit(
            PreviewScheduler::Lane::InteractivePreview,
            &owner,
            request,
            [&result, &completed, &loop](PreviewResult value) {
                result = std::move(value);
                completed = true;
                loop.quit();
            });
        if (!submitted) {
            result.error = QStringLiteral("Media preview request was rejected by scheduler");
            return result;
        }

        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!completed)
            scheduler.cancelOwner(&owner);
        return result;
    }

    static QByteArray pcmWave() {
        constexpr quint32 sampleRate = 8000;
        constexpr quint16 channels = 1;
        constexpr quint16 bitsPerSample = 8;
        constexpr quint32 sampleCount = 800;
        constexpr quint32 dataSize = sampleCount * channels * bitsPerSample / 8;

        QByteArray wave;
        auto appendLe16 = [&wave](quint16 value) {
            wave.append(static_cast<char>(value & 0xff));
            wave.append(static_cast<char>((value >> 8) & 0xff));
        };
        auto appendLe32 = [&wave](quint32 value) {
            wave.append(static_cast<char>(value & 0xff));
            wave.append(static_cast<char>((value >> 8) & 0xff));
            wave.append(static_cast<char>((value >> 16) & 0xff));
            wave.append(static_cast<char>((value >> 24) & 0xff));
        };

        wave.append("RIFF", 4);
        appendLe32(36 + dataSize);
        wave.append("WAVE", 4);
        wave.append("fmt ", 4);
        appendLe32(16);
        appendLe16(1);
        appendLe16(channels);
        appendLe32(sampleRate);
        appendLe32(sampleRate * channels * bitsPerSample / 8);
        appendLe16(channels * bitsPerSample / 8);
        appendLe16(bitsPerSample);
        wave.append("data", 4);
        appendLe32(dataSize);
        wave.append(QByteArray(dataSize, static_cast<char>(128)));
        return wave;
    }

    static QByteArray tinyGif() {
        return QByteArray::fromBase64(
            "R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==");
    }

private slots:
    void mediaBoundsAreFrozen() {
        QCOMPARE(PreviewProtocol::kMaxMediaReadBytes, 64LL * 1024 * 1024);
        QCOMPARE(PreviewProtocol::kMaxMediaProbeBytes, 4LL * 1024 * 1024);
        QCOMPARE(PreviewProtocol::kMaxMediaPosterDimension, 1280);
        QCOMPARE(PreviewProtocol::kMaxMediaPackets, 512);
    }

    void probesGeneratedWaveMetadata() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("tone.wav"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray wave = pcmWave();
        QCOMPARE(file.write(wave), wave.size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("media-probe")},
                {QStringLiteral("path"), path},
                {QStringLiteral("poster"), false},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.payload.value(QStringLiteral("hasAudio")).toBool());
        QVERIFY(!result.payload.value(QStringLiteral("hasVideo")).toBool());
        QCOMPARE(result.payload.value(QStringLiteral("mediaKind")).toString(), QStringLiteral("audio"));
        QCOMPARE(result.payload.value(QStringLiteral("sampleRate")).toInt(), 8000);
        QCOMPARE(result.payload.value(QStringLiteral("channels")).toInt(), 1);
        QVERIFY(!result.payload.value(QStringLiteral("audioCodec")).toString().isEmpty());
        QVERIFY(result.payload.value(QStringLiteral("durationMs")).toDouble() >= 90.0);
        QVERIFY(result.payload.value(QStringLiteral("bytesRead")).toString().toLongLong()
            <= PreviewProtocol::kMaxMediaReadBytes);
        QVERIFY(result.payload.value(QStringLiteral("posterBase64")).toString().isEmpty());
    }

    void rendersBoundedPosterFromVideoStream() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("pixel.gif"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray gif = tinyGif();
        QCOMPARE(file.write(gif), gif.size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("media-probe")},
                {QStringLiteral("path"), path},
                {QStringLiteral("poster"), true},
                {QStringLiteral("maxWidth"), 320},
                {QStringLiteral("maxHeight"), 240},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.payload.value(QStringLiteral("hasVideo")).toBool());
        QCOMPARE(result.payload.value(QStringLiteral("mediaKind")).toString(), QStringLiteral("video"));
        QCOMPARE(result.payload.value(QStringLiteral("posterFormat")).toString(), QStringLiteral("png"));
        QVERIFY(result.payload.value(QStringLiteral("posterWidth")).toInt() > 0);
        QVERIFY(result.payload.value(QStringLiteral("posterWidth")).toInt() <= 320);
        QVERIFY(result.payload.value(QStringLiteral("posterHeight")).toInt() > 0);
        QVERIFY(result.payload.value(QStringLiteral("posterHeight")).toInt() <= 240);

        const QByteArray png = QByteArray::fromBase64(
            result.payload.value(QStringLiteral("posterBase64")).toString().toLatin1());
        QVERIFY(!png.isEmpty());
        QVERIFY(png.size() <= PreviewProtocol::kMaxEncodedImageBytes);
        QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
    }

    void rejectsMediaSymlinkWithoutFollowing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString targetPath = directory.filePath(QStringLiteral("target.wav"));
        const QString linkPath = directory.filePath(QStringLiteral("link.wav"));
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::WriteOnly));
        const QByteArray wave = pcmWave();
        QCOMPARE(target.write(wave), wave.size());
        target.close();
        QVERIFY(QFile::link(targetPath, linkPath));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("media-probe")},
                {QStringLiteral("path"), linkPath},
                {QStringLiteral("poster"), false},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("regular file")));
    }

    void rejectsMalformedMedia() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("broken.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not a media container"), 21);
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("media-probe")},
                {QStringLiteral("path"), path},
                {QStringLiteral("poster"), false},
            });

        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(MediaProbeTest)
#include "MediaProbeTest.moc"
