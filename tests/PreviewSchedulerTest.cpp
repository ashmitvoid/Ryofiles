// SPDX-License-Identifier: GPL-3.0-only

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

private slots:
    void boundsAreFrozen() {
        QCOMPARE(PreviewScheduler::kMaxPending, 8);
        QCOMPARE(PreviewScheduler::kMaxLongPending, 4);
        QCOMPARE(PreviewScheduler::kMaxActiveHelpers, 1);
        QCOMPARE(PreviewScheduler::kMaxLongWorkHelpers, 1);
        QCOMPARE(PreviewScheduler::kMaxTotalHelpers, 2);
        QCOMPARE(PreviewScheduler::kIdleExitMs, 10'000);
        QCOMPARE(PreviewScheduler::kInteractiveTimeoutMs, 15'000);
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
