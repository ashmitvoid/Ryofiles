// SPDX-License-Identifier: GPL-3.0-only

#include "git/GitStatusController.hpp"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class GitStatusControllerTest final : public QObject {
    Q_OBJECT

private:
    QString m_git;

    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

    bool git(const QString& workingDirectory, const QStringList& arguments) const {
        QProcess process;
        process.setWorkingDirectory(workingDirectory);
        process.start(m_git, arguments, QIODevice::ReadOnly);
        if (!process.waitForStarted(2000))
            return false;
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(200);
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            qWarning().noquote() << process.readAllStandardError();
            return false;
        }
        return true;
    }

    void initRepository(const QString& path, const QString& branch = QStringLiteral("main")) const {
        QVERIFY(QDir().mkpath(path));
        QVERIFY(git(path, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), branch}));
        QVERIFY(git(path, {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("ryofiles@example.test")}));
        QVERIFY(git(path, {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Ryofiles Test")}));
    }

    void commitAll(const QString& path, const QString& message = QStringLiteral("initial")) const {
        QVERIFY(git(path, {QStringLiteral("add"), QStringLiteral("-A")}));
        QVERIFY(git(path, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message}));
    }

    static void waitForRevision(GitStatusController& controller, quint64 previous = 0) {
        QTRY_VERIFY_WITH_TIMEOUT(controller.revision() > previous && !controller.loading(), 7000);
    }

private slots:
    void initTestCase() {
        m_git = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (m_git.isEmpty())
            QSKIP("git executable is required for Git awareness tests");
    }

    void detectsRepositoryAndBranch() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        GitStatusController controller;
        controller.setPath(temp.path());
        waitForRevision(controller);

        QVERIFY(controller.repository());
        QVERIFY(controller.gitAvailable());
        QCOMPARE(controller.rootPath(), QDir(temp.path()).absolutePath());
        QCOMPARE(controller.branchName(), QStringLiteral("main"));
        QVERIFY(!controller.detached());
        QVERIFY(controller.error().isEmpty());
    }

    void reportsDirectAndAggregatedStatuses() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString tracked = temp.filePath("tracked.txt");
        const QString ignored = temp.filePath("ignored.log");
        const QString nestedDir = temp.filePath("nested");
        const QString nestedFile = QDir(nestedDir).filePath("child.txt");
        QVERIFY(QDir().mkpath(nestedDir));
        writeFile(tracked, "base\n");
        writeFile(nestedFile, "nested\n");
        writeFile(temp.filePath(".gitignore"), "ignored.log\n");
        commitAll(temp.path());

        GitStatusController controller;
        controller.setPath(temp.path());
        waitForRevision(controller);
        QCOMPARE(controller.changedCount(), 0);

        writeFile(tracked, "modified\n");
        quint64 revision = controller.revision();
        controller.refresh();
        waitForRevision(controller, revision);
        QCOMPARE(controller.statusForPath(tracked), QStringLiteral("modified"));

        QVERIFY(git(temp.path(), {QStringLiteral("add"), QStringLiteral("tracked.txt")}));
        revision = controller.revision();
        controller.refresh();
        waitForRevision(controller, revision);
        QCOMPARE(controller.statusForPath(tracked), QStringLiteral("staged"));

        writeFile(tracked, "staged then modified again\n");
        writeFile(temp.filePath("new-file.txt"), "new\n");
        writeFile(ignored, "ignored\n");
        writeFile(nestedFile, "nested modified\n");
        revision = controller.revision();
        controller.refresh();
        waitForRevision(controller, revision);

        QCOMPARE(controller.statusForPath(tracked), QStringLiteral("mixed"));
        QCOMPARE(controller.statusForPath(temp.filePath("new-file.txt")), QStringLiteral("untracked"));
        QCOMPARE(controller.statusForPath(ignored), QStringLiteral("ignored"));
        QCOMPARE(controller.statusForPath(nestedDir), QStringLiteral("modified"));
        QCOMPARE(controller.statusLabelForPath(tracked), QStringLiteral("STAGED + MODIFIED"));
        QVERIFY(controller.changedCount() >= 4);
    }

    void scopesStatusesToCurrentSubdirectory() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString sourceDir = temp.filePath("src");
        QVERIFY(QDir().mkpath(sourceDir));
        const QString sourceFile = QDir(sourceDir).filePath("main.cpp");
        const QString rootFile = temp.filePath("README.md");
        writeFile(sourceFile, "int main() {}\n");
        writeFile(rootFile, "readme\n");
        commitAll(temp.path());

        writeFile(sourceFile, "int main() { return 0; }\n");
        writeFile(rootFile, "changed outside current folder\n");

        GitStatusController controller;
        controller.setPath(sourceDir);
        waitForRevision(controller);

        QCOMPARE(controller.statusForPath(sourceFile), QStringLiteral("modified"));
        QCOMPARE(controller.statusForPath(rootFile), QString());
        QCOMPARE(controller.changedCount(), 1);
    }

    void supportsGitWorktreeMarkerFiles() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString mainRepo = temp.filePath("main-repo");
        const QString worktree = temp.filePath("worktree");
        initRepository(mainRepo);
        writeFile(QDir(mainRepo).filePath("tracked.txt"), "base\n");
        commitAll(mainRepo);

        QVERIFY(git(mainRepo, {
            QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-q"),
            QStringLiteral("-b"), QStringLiteral("side"), worktree
        }));
        QVERIFY(QFileInfo(QDir(worktree).filePath(".git")).isFile());

        GitStatusController controller;
        controller.setPath(worktree);
        waitForRevision(controller);

        QVERIFY(controller.repository());
        QCOMPARE(controller.rootPath(), QDir(worktree).absolutePath());
        QCOMPARE(controller.branchName(), QStringLiteral("side"));
    }

    void rejectsStalePathResults() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString one = temp.filePath("one");
        const QString two = temp.filePath("two");
        initRepository(one, QStringLiteral("one-branch"));
        initRepository(two, QStringLiteral("two-branch"));

        GitStatusController controller;
        controller.setPath(one);
        controller.setPath(two);
        waitForRevision(controller);

        QCOMPARE(controller.path(), QDir(two).absolutePath());
        QCOMPARE(controller.rootPath(), QDir(two).absolutePath());
        QCOMPARE(controller.branchName(), QStringLiteral("two-branch"));
    }

    void indexWatcherRefreshesStagedStateWithoutPolling() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        initRepository(temp.path());

        const QString tracked = temp.filePath("watch.txt");
        writeFile(tracked, "base\n");
        commitAll(temp.path());
        writeFile(tracked, "changed\n");

        GitStatusController controller;
        controller.setPath(temp.path());
        waitForRevision(controller);
        QCOMPARE(controller.statusForPath(tracked), QStringLiteral("modified"));

        const quint64 revision = controller.revision();
        QVERIFY(git(temp.path(), {QStringLiteral("add"), QStringLiteral("watch.txt")}));

        QTRY_VERIFY_WITH_TIMEOUT(controller.revision() > revision, 7000);
        QTRY_COMPARE_WITH_TIMEOUT(controller.statusForPath(tracked), QStringLiteral("staged"), 7000);
    }
};

QTEST_GUILESS_MAIN(GitStatusControllerTest)
#include "GitStatusControllerTest.moc"
