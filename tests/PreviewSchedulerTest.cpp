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

class PreviewSchedulerTest final : public QObject {
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
        result.error = QStringLiteral("Preview test timed out");
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
            result.error = QStringLiteral("Preview request was rejected by scheduler");
            return result;
        }

        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        if (!completed)
            scheduler.cancelOwner(&owner);
        return result;
    }

    static QByteArray onePagePdf() {
        QByteArray pdf("%PDF-1.4\n");
        QList<qint64> offsets(6, 0);

        auto appendObject = [&pdf, &offsets](int number, const QByteArray& body) {
            offsets[number] = pdf.size();
            pdf += QByteArray::number(number) + " 0 obj\n";
            pdf += body;
            if (!body.endsWith('\n'))
                pdf += '\n';
            pdf += "endobj\n";
        };

        appendObject(1, "<< /Type /Catalog /Pages 2 0 R >>\n");
        appendObject(2, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n");
        appendObject(
            3,
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 240 180] "
            "/Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>\n");

        const QByteArray stream = "BT /F1 18 Tf 30 90 Td (Hello Ryofiles) Tj ET\n";
        appendObject(
            4,
            "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n"
                + stream + "endstream\n");
        appendObject(5, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");

        const qint64 xrefOffset = pdf.size();
        pdf += "xref\n0 6\n";
        pdf += "0000000000 65535 f \n";
        for (int number = 1; number <= 5; ++number) {
            pdf += QByteArray::number(offsets[number]).rightJustified(10, '0');
            pdf += " 00000 n \n";
        }
        pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n";
        pdf += QByteArray::number(xrefOffset);
        pdf += "\n%%EOF\n";
        return pdf;
    }

private slots:
    void boundsAreFrozen() {
        QCOMPARE(PreviewScheduler::kMaxPending, 8);
        QCOMPARE(PreviewScheduler::kMaxLongPending, 4);
        QCOMPARE(PreviewScheduler::kMaxActiveHelpers, 1);
        QCOMPARE(PreviewScheduler::kMaxLongWorkHelpers, 1);
        QCOMPARE(PreviewScheduler::kMaxTotalHelpers, 2);
        QCOMPARE(PreviewScheduler::kIdleExitMs, 10'000);
        QCOMPARE(PreviewScheduler::kInteractiveTimeoutMs, 15'000);
        QCOMPARE(PreviewProtocol::kMaxPdfRenderDimension, 2048);
        QCOMPARE(PreviewProtocol::kMaxEncodedImageBytes, 2 * 1024 * 1024);
    }

    void queueAdmissionIsBounded() {
        PreviewScheduler scheduler(QStringLiteral("/definitely/missing/ryofiles-preview-helper"));
        QObject interactiveOwner;
        QObject longOwner;
        const QJsonObject request {
            {QStringLiteral("op"), QStringLiteral("stat")},
            {QStringLiteral("path"), QStringLiteral("/tmp/preview-bound-test")},
        };

        for (int index = 0; index < PreviewScheduler::kMaxPending; ++index) {
            QVERIFY(scheduler.submit(
                PreviewScheduler::Lane::InteractivePreview,
                &interactiveOwner,
                request,
                [](PreviewResult) {}));
        }
        QVERIFY(!scheduler.submit(
            PreviewScheduler::Lane::InteractivePreview,
            &interactiveOwner,
            request,
            [](PreviewResult) {}));
        QCOMPARE(
            scheduler.pendingCount(PreviewScheduler::Lane::InteractivePreview),
            PreviewScheduler::kMaxPending);

        for (int index = 0; index < PreviewScheduler::kMaxLongPending; ++index) {
            QVERIFY(scheduler.submit(
                PreviewScheduler::Lane::ExplicitLongWork,
                &longOwner,
                request,
                [](PreviewResult) {}));
        }
        QVERIFY(!scheduler.submit(
            PreviewScheduler::Lane::ExplicitLongWork,
            &longOwner,
            request,
            [](PreviewResult) {}));
        QCOMPARE(
            scheduler.pendingCount(PreviewScheduler::Lane::ExplicitLongWork),
            PreviewScheduler::kMaxLongPending);
    }

    void probesRegularFileThroughHelper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("hello.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("hello"), 5);
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("stat")},
                {QStringLiteral("path"), path},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payload.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
        QCOMPARE(result.payload.value(QStringLiteral("size")).toString(), QStringLiteral("5"));
        QCOMPARE(result.payload.value(QStringLiteral("mime")).toString(), QStringLiteral("text/plain"));
    }

    void describesSymlinkWithoutFollowingTarget() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString targetPath = directory.filePath(QStringLiteral("target.txt"));
        const QString linkPath = directory.filePath(QStringLiteral("link.txt"));
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::WriteOnly));
        QVERIFY(target.write("secret") > 0);
        target.close();
        QVERIFY(QFile::link(targetPath, linkPath));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("stat")},
                {QStringLiteral("path"), linkPath},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payload.value(QStringLiteral("kind")).toString(), QStringLiteral("symlink"));
        QVERIFY(result.payload.value(QStringLiteral("mime")).toString().isEmpty());
    }

    void rendersPdfPageFromNoFollowDescriptor() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("sample.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = onePagePdf();
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("pdf-page")},
                {QStringLiteral("path"), path},
                {QStringLiteral("page"), 0},
                {QStringLiteral("maxWidth"), 640},
                {QStringLiteral("maxHeight"), 480},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payload.value(QStringLiteral("page")).toInt(), 0);
        QCOMPARE(result.payload.value(QStringLiteral("pageCount")).toInt(), 1);
        QVERIFY(result.payload.value(QStringLiteral("pixelWidth")).toInt() > 0);
        QVERIFY(result.payload.value(QStringLiteral("pixelHeight")).toInt() > 0);
        QCOMPARE(result.payload.value(QStringLiteral("imageFormat")).toString(), QStringLiteral("png"));

        const QByteArray png = QByteArray::fromBase64(
            result.payload.value(QStringLiteral("imageBase64")).toString().toLatin1());
        QVERIFY(!png.isEmpty());
        QVERIFY(png.size() <= PreviewProtocol::kMaxEncodedImageBytes);
        QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
    }

    void readsPdfMetadataWithoutRenderingPage() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("metadata.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = onePagePdf();
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("pdf-page")},
                {QStringLiteral("path"), path},
                {QStringLiteral("page"), 0},
                {QStringLiteral("renderPage"), false},
            });

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.payload.value(QStringLiteral("page")).toInt(), 0);
        QCOMPARE(result.payload.value(QStringLiteral("pageCount")).toInt(), 1);
        QVERIFY(!result.payload.value(QStringLiteral("fileSize")).toString().isEmpty());
        QVERIFY(!result.payload.contains(QStringLiteral("pixelWidth")));
        QVERIFY(!result.payload.contains(QStringLiteral("pixelHeight")));
        QVERIFY(!result.payload.contains(QStringLiteral("imageFormat")));
        QVERIFY(!result.payload.contains(QStringLiteral("imageBase64")));
    }

    void pdfRenderRejectsSymlinkInput() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString targetPath = directory.filePath(QStringLiteral("target.pdf"));
        const QString linkPath = directory.filePath(QStringLiteral("link.pdf"));
        QFile target(targetPath);
        QVERIFY(target.open(QIODevice::WriteOnly));
        const QByteArray bytes = onePagePdf();
        QCOMPARE(target.write(bytes), bytes.size());
        target.close();
        QVERIFY(QFile::link(targetPath, linkPath));

        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("pdf-page")},
                {QStringLiteral("path"), linkPath},
                {QStringLiteral("page"), 0},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("regular file")));
    }

    void rejectsRelativePaths() {
        PreviewScheduler scheduler(helperPath());
        QObject owner;
        const PreviewResult result = execute(
            scheduler,
            owner,
            QJsonObject {
                {QStringLiteral("op"), QStringLiteral("stat")},
                {QStringLiteral("path"), QStringLiteral("relative.txt")},
            });

        QVERIFY(!result.ok);
        QVERIFY(result.error.contains(QStringLiteral("absolute local path")));
    }
};

QTEST_GUILESS_MAIN(PreviewSchedulerTest)
#include "PreviewSchedulerTest.moc"
