// SPDX-License-Identifier: GPL-3.0-only

#include "navigation/DirectorySession.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DirectorySessionProxyTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write("x"), 1);
    }

private slots:
    void exposesStableProxyForLocalNavigation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString child = QDir(temp.path()).filePath(QStringLiteral("child"));
        QVERIFY(QDir().mkdir(child));
        const QString filePath = QDir(temp.path()).filePath(QStringLiteral("notes.txt"));
        writeFile(filePath);

        DirectorySession session(temp.path());
        SessionFileModel* files = session.model();
        QVERIFY(files);
        QVERIFY(!files->remote());
        QTRY_VERIFY_WITH_TIMEOUT(!files->loading(), 5000);
        QCOMPARE(session.path(), temp.path());
        QCOMPARE(files->rowCount(), 2);

        const auto roles = files->roleNames();
        QCOMPARE(roles.value(SessionFileModel::PathRole), QByteArray("filePath"));
        QCOMPARE(roles.value(SessionFileModel::ThumbnailCandidateRole), QByteArray("thumbnailCandidate"));

        QVERIFY(session.navigate(child));
        QCOMPARE(session.model(), files);
        QTRY_VERIFY_WITH_TIMEOUT(!files->loading(), 5000);
        QCOMPARE(session.path(), child);
        QVERIFY(!files->remote());
    }

    void selectionAndFilteringUseProxyContract() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString alpha = QDir(temp.path()).filePath(QStringLiteral("Alpha.txt"));
        const QString beta = QDir(temp.path()).filePath(QStringLiteral("beta.txt"));
        writeFile(alpha);
        writeFile(beta);

        DirectorySession session(temp.path());
        SessionFileModel* files = session.model();
        QTRY_VERIFY_WITH_TIMEOUT(!files->loading(), 5000);
        QCOMPARE(files->rowCount(), 2);

        files->setFilterQuery(QStringLiteral("alpha"));
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(files->pathAt(0), alpha);

        session.selectSingle(0);
        QCOMPARE(session.selectionCount(), 1);
        QCOMPARE(session.selectedPath(), alpha);

        files->setFilterQuery(QString());
        QCOMPARE(files->rowCount(), 2);
        session.selectAll();
        QCOMPARE(session.selectionCount(), 2);
    }

    void localOnlyMigrationStillRejectsNetworkUris() {
        DirectorySession session;
        const QString before = session.path();
        QVERIFY(!session.navigate(QStringLiteral("sftp://example.com/home/user")));
        QCOMPARE(session.path(), before);
        QVERIFY(!session.model()->remote());
    }
};

QTEST_GUILESS_MAIN(DirectorySessionProxyTest)
#include "DirectorySessionProxyTest.moc"
