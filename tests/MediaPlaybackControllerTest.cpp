// SPDX-License-Identifier: GPL-3.0-only

#include "preview/MediaPreviewWorkflow.hpp"
#include "preview/PreviewFileAccess.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void appendLe16(QByteArray* bytes, quint16 value) {
    bytes->append(static_cast<char>(value & 0xff));
    bytes->append(static_cast<char>((value >> 8) & 0xff));
}

void appendLe32(QByteArray* bytes, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8)
        bytes->append(static_cast<char>((value >> shift) & 0xff));
}

QByteArray tinyWav() {
    constexpr quint32 sampleRate = 8000;
    constexpr quint16 channels = 1;
    constexpr quint16 bitsPerSample = 16;
    constexpr quint32 sampleCount = 160;
    constexpr quint32 dataBytes = sampleCount * channels * (bitsPerSample / 8);

    QByteArray bytes;
    bytes.append("RIFF", 4);
    appendLe32(&bytes, 36 + dataBytes);
    bytes.append("WAVE", 4);
    bytes.append("fmt ", 4);
    appendLe32(&bytes, 16);
    appendLe16(&bytes, 1);
    appendLe16(&bytes, channels);
    appendLe32(&bytes, sampleRate);
    appendLe32(&bytes, sampleRate * channels * (bitsPerSample / 8));
    appendLe16(&bytes, channels * (bitsPerSample / 8));
    appendLe16(&bytes, bitsPerSample);
    bytes.append("data", 4);
    appendLe32(&bytes, dataBytes);
    bytes.append(QByteArray(dataBytes, '\0'));
    return bytes;
}

QString writeWav(QTemporaryDir* directory, const QString& name) {
    const QString path = directory->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    const QByteArray bytes = tinyWav();
    if (file.write(bytes) != bytes.size())
        return {};
    file.close();
    return path;
}

} // namespace

class MediaPlaybackControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void fileAccessOpensRegularWithoutFollowing() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = writeWav(&directory, QStringLiteral("sample.wav"));
        QVERIFY(!path.isEmpty());

        QString error;
        auto opened = PreviewFileAccess::openRegularNoFollow(path, &error);
        QVERIFY2(opened, qPrintable(error));
        QVERIFY(opened.file->isOpen());
        QVERIFY(opened.size > 44);
        QVERIFY(opened.inode != 0);
    }

    void fileAccessRejectsRelativeAndSymlink() {
        QString error;
        auto relative = PreviewFileAccess::openRegularNoFollow(
            QStringLiteral("relative.wav"), &error);
        QVERIFY(!relative);
        QVERIFY(error.contains(QStringLiteral("absolute local path")));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = writeWav(&directory, QStringLiteral("target.wav"));
        const QString link = directory.filePath(QStringLiteral("link.wav"));
        QVERIFY(QFile::link(target, link));

        error.clear();
        auto symlink = PreviewFileAccess::openRegularNoFollow(link, &error);
        QVERIFY(!symlink);
        QVERIFY(error.contains(QStringLiteral("regular file")));
    }

    void sourceReleasesOnSelectionChangeAndDeactivate() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString first = writeWav(&directory, QStringLiteral("first.wav"));
        const QString second = writeWav(&directory, QStringLiteral("second.wav"));
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        MediaPlaybackController controller;
        controller.setActive(true);
        controller.setPath(first);
        controller.play();
        QVERIFY2(controller.prepared(), qPrintable(controller.error()));

        controller.setPath(second);
        QVERIFY(!controller.prepared());
        QCOMPARE(controller.position(), 0);

        controller.play();
        QVERIFY2(controller.prepared(), qPrintable(controller.error()));
        controller.setActive(false);
        QVERIFY(!controller.prepared());
    }

    void symlinkCannotBecomePlaybackSource() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString target = writeWav(&directory, QStringLiteral("target.wav"));
        const QString link = directory.filePath(QStringLiteral("link.wav"));
        QVERIFY(QFile::link(target, link));

        MediaPlaybackController controller;
        controller.setActive(true);
        controller.setPath(link);
        controller.play();

        QVERIFY(!controller.prepared());
        QVERIFY(controller.error().contains(QStringLiteral("regular file")));
    }

    void newestControllerOwnsTheSinglePlaybackSource() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString firstPath = writeWav(&directory, QStringLiteral("first.wav"));
        const QString secondPath = writeWav(&directory, QStringLiteral("second.wav"));

        MediaPlaybackController first;
        MediaPlaybackController second;
        first.setActive(true);
        second.setActive(true);
        first.setPath(firstPath);
        second.setPath(secondPath);

        first.play();
        QVERIFY2(first.prepared(), qPrintable(first.error()));
        second.play();
        QVERIFY2(second.prepared(), qPrintable(second.error()));
        QVERIFY(!first.prepared());
    }
};

QTEST_GUILESS_MAIN(MediaPlaybackControllerTest)
#include "MediaPlaybackControllerTest.moc"
