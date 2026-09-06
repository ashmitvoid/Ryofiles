// SPDX-License-Identifier: GPL-3.0-only

#include "preview/PreviewProtocol.hpp"
#include "preview/PreviewScheduler.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class FontPreviewTest final : public QObject {
    Q_OBJECT

private:
    static QString helperPath() {
        return QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("ryofiles-preview-helper"));
    }

    static QString testFontPath() {
        return QStringLiteral("/usr/share/fonts/TTF/DejaVuSans.ttf");
    }

    static PreviewResult execute(
        PreviewScheduler& scheduler,
        QObject& owner,
        const QJsonObject& request) {
        PreviewResult result;
        result.error = QStringLiteral("Font preview test timed out");
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
            result.error = QStringLiteral("Font preview request was rejected by scheduler");
            return result;
        }

        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!completed)
            scheduler.cancelOwner(&owner);
        return result;
    }

private slots:
    void fontBoundsAreFrozen() {
        QCOMPARE(PreviewProtocol::kMaxFontInputBytes, 32LL * 1024 * 1024);
        QCOMPARE(PreviewProtocol::kMaxFontRenderDimension, 1280);
        QCOMPARE(PreviewProtocol::kMaxFontPixelSize, 96);
        QCOMPARE(PreviewProtocol::kMaxFontSampleChars, 96);
        QCOMPARE(PreviewProtocol::kMaxFontWritingSystems, 16);
    }

    void rendersDejaVuMetadataAndBoundedSample() {
        QVERIFY2(QFile::exists(testFontPath()), qPrintable(testFontPath()));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("font-preview")},
                {QStringLiteral("path"), testFontPath()},
                {QStringLiteral("maxWidth"), 640},
                {QStringLiteral("maxHeight"), 300},
                {QStringLiteral("pixelSize"), 64},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.payload.value(QStringLiteral("family")).toString().contains(
            QStringLiteral("DejaVu Sans"), Qt::CaseInsensitive));
        QVERIFY(!result.payload.value(QStringLiteral("style")).toString().isEmpty());
        QVERIFY(result.payload.value(QStringLiteral("weight")).toInt() > 0);
        QVERIFY(result.payload.value(QStringLiteral("unitsPerEm")).toDouble() > 0.0);

        const QJsonArray systems = result.payload.value(QStringLiteral("writingSystems")).toArray();
        QVERIFY(!systems.isEmpty());
        QVERIFY(systems.size() <= PreviewProtocol::kMaxFontWritingSystems);

        QCOMPARE(
            result.payload.value(QStringLiteral("sampleFormat")).toString(),
            QStringLiteral("png"));
        const int width = result.payload.value(QStringLiteral("sampleWidth")).toInt();
        const int height = result.payload.value(QStringLiteral("sampleHeight")).toInt();
        QVERIFY(width > 0 && width <= 640);
        QVERIFY(height > 0 && height <= 300);
        QVERIFY(!result.payload.value(QStringLiteral("sampleText")).toString().isEmpty());

        const QByteArray png = QByteArray::fromBase64(
            result.payload.value(QStringLiteral("sampleBase64")).toString().toLatin1());
        QVERIFY(!png.isEmpty());
        QVERIFY(png.size() <= PreviewProtocol::kMaxEncodedImageBytes);
        QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
    }

    void rejectsFontSymlinkWithoutFollowing() {
        QVERIFY2(QFile::exists(testFontPath()), qPrintable(testFontPath()));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString linkPath = directory.filePath(QStringLiteral("linked.ttf"));
        QVERIFY(QFile::link(testFontPath(), linkPath));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("font-preview")},
                {QStringLiteral("path"), linkPath},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("regular file")));
    }

    void rejectsMalformedFont() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("broken.ttf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("not a font container");
        QCOMPARE(file.write(contents), contents.size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("font-preview")},
                {QStringLiteral("path"), path},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("TrueType/OpenType")));
    }

    void rejectsOversizedFontBeforeRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("oversized.ttf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(PreviewProtocol::kMaxFontInputBytes + 1));
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("font-preview")},
                {QStringLiteral("path"), path},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("input limit")));
    }
};

QTEST_GUILESS_MAIN(FontPreviewTest)
#include "FontPreviewTest.moc"
