// SPDX-License-Identifier: GPL-3.0-only

#include "git/GitActionController.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class GitActionControllerTest final : public QObject {
    Q_OBJECT

private:
    static QByteArray git(const QString& root, const QStringList& arguments, int* exitCode = nullptr) {
        QProcess process;
        process.setWorkingDirectory(root);
        process.start(QStringLiteral("git"), arguments, QIODevice::ReadOnly);
        if (!process.waitForStarted(3000))
            return {};
        process.waitForFinished(5000);
        if (exitCode)
            *exitCode = process.exitCode();
        return process.readAllStandardOutput();
    }

    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

    static void initRepository(const QString& root, bool initialCommit = true) {
        QCOMPARE(git(root, {QStringLiteral("init"), QStringLiteral("-q")}), QByteArray());
        git(root, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("ryofiles@test.invalid")});
        git(root, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Ryofiles Tests")});

        if (!initialCommit)
            return;

        writeFile(QDir(root).filePath(QStringLiteral("tracked.txt")), QByteArrayLiteral("base\n"));
        git(root, {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("tracked.txt")});
        int exitCode = -1;
        git(root, {QStringLiteral("commit"), QStringLiteral("-qm"), QStringLiteral("initial")}, &exitCode);
        QCOMPARE(exitCode, 0);
    }

private slots:
    void stageAndUnstageTrackedFile() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString path = QDir(temp.path()).filePath(QStringLiteral("tracked.txt"));
        writeFile(path, QByteArrayLiteral("changed\n"));

        GitActionController actions;
        QSignalSpy finished(&actions, &GitActionController::operationFinished);

        const QString stageId = actions.stage(temp.path(), {path});
        QVERIFY(!stageId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 7000);
        QVERIFY(finished.takeFirst().at(1).toBool());
        QCOMPARE(git(temp.path(), {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only")}).trimmed(), QByteArrayLiteral("tracked.txt"));

        const QString unstageId = actions.unstage(temp.path(), {path});
        QVERIFY(!unstageId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 7000);
        QVERIFY(finished.takeFirst().at(1).toBool());
        QVERIFY(git(temp.path(), {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--name-only")}).trimmed().isEmpty());
        QCOMPARE(git(temp.path(), {QStringLiteral("diff"), QStringLiteral("--name-only")}).trimmed(), QByteArrayLiteral("tracked.txt"));
    }

    void unstageWorksBeforeFirstCommit() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path(), false);

        const QString path = QDir(temp.path()).filePath(QStringLiteral("new.txt"));
        writeFile(path, QByteArrayLiteral("new\n"));
        git(temp.path(), {QStringLiteral("add"), QStringLiteral("--"), QStringLiteral("new.txt")});
        QCOMPARE(git(temp.path(), {QStringLiteral("ls-files")}).trimmed(), QByteArrayLiteral("new.txt"));

        GitActionController actions;
        QSignalSpy finished(&actions, &GitActionController::operationFinished);
        const QString id = actions.unstage(temp.path(), {path});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 7000);
        QVERIFY(finished.takeFirst().at(1).toBool());

        QVERIFY(git(temp.path(), {QStringLiteral("ls-files")}).trimmed().isEmpty());
        QVERIFY(QFileInfo::exists(path));
    }

    void rejectsPathsOutsideRepository() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString repository = QDir(temp.path()).filePath(QStringLiteral("repo"));
        QVERIFY(QDir().mkpath(repository));
        initRepository(repository);

        const QString outside = QDir(temp.path()).filePath(QStringLiteral("outside.txt"));
        writeFile(outside, QByteArrayLiteral("outside\n"));

        GitActionController actions;
        const QString id = actions.stage(repository, {outside});
        QVERIFY(id.isEmpty());
        QVERIFY(actions.error().contains(QStringLiteral("outside"), Qt::CaseInsensitive));
    }

    void rejectsUriInputsAtLocalBoundary() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString tracked = QDir(temp.path()).filePath(QStringLiteral("tracked.txt"));
        const QString remoteFile = QStringLiteral("sftp://alice@example.invalid/repo/tracked.txt");
        const QString remoteRoot = QStringLiteral("smb://nas.invalid/team/repo");

        GitActionController actions;

        QVERIFY(actions.stage(temp.path(), {remoteFile}).isEmpty());
        QVERIFY(actions.error().contains(QStringLiteral("local filesystem"), Qt::CaseInsensitive));
        QVERIFY(!actions.busy());

        QVERIFY(actions.unstage(remoteRoot, {tracked}).isEmpty());
        QVERIFY(actions.error().contains(QStringLiteral("local filesystem"), Qt::CaseInsensitive));
        QVERIFY(!actions.busy());

        QVERIFY(actions.requestDiff(temp.path(), QStringLiteral("file:///tmp/tracked.txt"), false).isEmpty());
        QVERIFY(actions.error().contains(QStringLiteral("local filesystem"), Qt::CaseInsensitive));
        QVERIFY(!actions.busy());

        QVERIFY(!actions.openTerminal(QStringLiteral("davs://cloud.invalid/files")));
    }

    void shellMetacharactersAreLiteralPaths() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString name = QStringLiteral("odd ; $(touch SHOULD_NOT_EXIST).txt");
        const QString path = QDir(temp.path()).filePath(name);
        writeFile(path, QByteArrayLiteral("literal\n"));

        GitActionController actions;
        QSignalSpy finished(&actions, &GitActionController::operationFinished);
        const QString id = actions.stage(temp.path(), {path});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 7000);
        QVERIFY(finished.takeFirst().at(1).toBool());

        const QByteArray files = git(temp.path(), {QStringLiteral("ls-files"), QStringLiteral("-z")});
        QVERIFY(files.contains(name.toUtf8()));
        QVERIFY(!QFileInfo::exists(QDir(temp.path()).filePath(QStringLiteral("SHOULD_NOT_EXIST"))));
    }

    void diffOutputIsBounded() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString path = QDir(temp.path()).filePath(QStringLiteral("tracked.txt"));
        QByteArray large;
        large.reserve(900000);
        for (int i = 0; i < 50000; ++i)
            large += QByteArrayLiteral("changed-line-") + QByteArray::number(i) + '\n';
        writeFile(path, large);

        GitActionController actions;
        QSignalSpy finished(&actions, &GitActionController::operationFinished);
        const QString id = actions.requestDiff(temp.path(), path, false);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 7000);
        QVERIFY(finished.takeFirst().at(1).toBool());

        QVERIFY(actions.diffTruncated());
        QVERIFY(!actions.diffText().isEmpty());
        QVERIFY(actions.diffText().toUtf8().size() <= 512 * 1024);
    }
};

QTEST_GUILESS_MAIN(GitActionControllerTest)
#include "GitActionControllerTest.moc"
