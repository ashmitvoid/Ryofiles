// SPDX-License-Identifier: GPL-3.0-only

#include "search/DeepSearchModel.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

class DeepSearchModelTest final : public QObject {
    Q_OBJECT

private:
    static void touch(const QString& path) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    }

    static QStringList resultNames(const DeepSearchModel& model) {
        QStringList names;
        names.reserve(model.rowCount());
        for (int i = 0; i < model.rowCount(); ++i)
            names.push_back(model.data(model.index(i), DeepSearchModel::NameRole).toString());
        return names;
    }

private slots:
    void findsNestedCaseInsensitiveNames() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString nested = temp.filePath("one/two/three");
        QVERIFY(QDir().mkpath(nested));
        touch(QDir(nested).filePath("Quarterly-REPORT.txt"));
        touch(temp.filePath("unrelated.txt"));

        DeepSearchModel model;
        QSignalSpy rowsSpy(&model, &QAbstractItemModel::rowsInserted);
        model.start(temp.path(), "report", false);

        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 5000);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(rowsSpy.count() >= 1);
        QCOMPARE(model.data(model.index(0), DeepSearchModel::NameRole).toString(),
                 QStringLiteral("Quarterly-REPORT.txt"));
        QCOMPARE(model.data(model.index(0), DeepSearchModel::RelativePathRole).toString(),
                 QStringLiteral("one/two/three/Quarterly-REPORT.txt"));
        QVERIFY(model.visitedCount() >= 2);
        QVERIFY(!model.truncated());
        QVERIFY(model.error().isEmpty());
    }

    void respectsHiddenPolicy() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        touch(temp.filePath("visible-match.txt"));
        touch(temp.filePath(".hidden-match.txt"));

        DeepSearchModel model;
        model.start(temp.path(), "match", false);
        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 5000);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(resultNames(model), QStringList{QStringLiteral("visible-match.txt")});

        model.start(temp.path(), "match", true);
        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 5000);
        QCOMPARE(model.rowCount(), 2);
        const QStringList names = resultNames(model);
        QVERIFY(names.contains(QStringLiteral("visible-match.txt")));
        QVERIFY(names.contains(QStringLiteral(".hidden-match.txt")));
    }

    void neverTraversesSymlinkDirectories() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString root = temp.filePath("root");
        const QString external = temp.filePath("external");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QDir().mkpath(external));
        touch(QDir(external).filePath("ghost-match.txt"));

        const QString linkPath = QDir(root).filePath("linked");
        const QByteArray linkBytes = QFile::encodeName(linkPath);
        const QByteArray targetBytes = QFile::encodeName(external);
        QVERIFY(::symlink(targetBytes.constData(), linkBytes.constData()) == 0);

        DeepSearchModel model;
        model.start(root, "ghost-match", true);
        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 5000);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.truncated());
    }

    void restartingSearchRejectsStaleResults() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        for (int i = 0; i < 500; ++i)
            touch(temp.filePath(QStringLiteral("first-match-%1.txt").arg(i)));
        touch(temp.filePath("second-match.txt"));

        DeepSearchModel model;
        model.start(temp.path(), "first-match", false);
        model.start(temp.path(), "second-match", false);

        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 5000);
        QCOMPARE(model.query(), QStringLiteral("second-match"));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(resultNames(model), QStringList{QStringLiteral("second-match.txt")});
    }

    void clearCancelsAndRejectsQueuedResults() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        for (int i = 0; i < 1000; ++i)
            touch(temp.filePath(QStringLiteral("cancel-match-%1.txt").arg(i)));

        DeepSearchModel model;
        model.start(temp.path(), "cancel-match", false);
        model.clear();

        QCOMPARE(model.query(), QString());
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.running());
        QVERIFY(!model.active());
        QTest::qWait(150);
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.query(), QString());
    }

    void capsResultsAtTwoThousand() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        for (int i = 0; i < 2050; ++i)
            touch(temp.filePath(QStringLiteral("cap-match-%1.txt").arg(i, 4, 10, QLatin1Char('0'))));

        DeepSearchModel model;
        model.start(temp.path(), "cap-match", false);

        QTRY_VERIFY_WITH_TIMEOUT(!model.running(), 10000);
        QCOMPARE(model.rowCount(), 2000);
        QVERIFY(model.truncated());
        QVERIFY(model.visitedCount() <= 200000);
    }
};

QTEST_GUILESS_MAIN(DeepSearchModelTest)
#include "DeepSearchModelTest.moc"
