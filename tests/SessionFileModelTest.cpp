// SPDX-License-Identifier: GPL-3.0-only

#include "fs/DirectoryModel.hpp"
#include "locations/RemoteDirectoryModel.hpp"
#include "locations/SessionFileModel.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SessionFileModelTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write("x"), 1);
    }

private slots:
    void exposesStableQmlRoleContract() {
        SessionFileModel model;
        const auto roles = model.roleNames();

        QCOMPARE(roles.value(SessionFileModel::NameRole), QByteArray("name"));
        QCOMPARE(roles.value(SessionFileModel::PathRole), QByteArray("filePath"));
        QCOMPARE(roles.value(SessionFileModel::DirectoryRole), QByteArray("isDir"));
        QCOMPARE(roles.value(SessionFileModel::SizeTextRole), QByteArray("sizeText"));
        QCOMPARE(roles.value(SessionFileModel::ModifiedTextRole), QByteArray("modifiedText"));
        QCOMPARE(roles.value(SessionFileModel::HiddenRole), QByteArray("isHidden"));
        QCOMPARE(roles.value(SessionFileModel::ThumbnailCandidateRole), QByteArray("thumbnailCandidate"));
    }

    void delegatesLocalEntriesAndCommonMethods() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = QDir(temp.path()).filePath(QStringLiteral("photo.png"));
        writeFile(path);

        DirectoryModel local(false, nullptr);
        local.setPath(temp.path());

        SessionFileModel model;
        model.useLocal(&local);
        QVERIFY(!model.remote());
        QCOMPARE(model.rowCount(), 0);

        local.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 5000);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.pathAt(0), path);
        QVERIFY(!model.isDirectoryAt(0));
        QCOMPARE(model.indexOfPath(path), 0);
        QVERIFY(model.data(model.index(0, 0), SessionFileModel::ThumbnailCandidateRole).toBool());
    }

    void switchesToRemoteWithoutInventingThumbnailWork() {
        DirectoryModel local(false, nullptr);
        RemoteDirectoryModel remote;
        SessionFileModel model;

        model.useLocal(&local);
        QVERIFY(!model.remote());

        model.useRemote(&remote);
        QVERIFY(model.remote());
        QVERIFY(!model.loading());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(model.filterQuery(), QString());
        QVERIFY(!model.showHidden());
    }

    void delegatesFilterAndHiddenStateToActiveBackend() {
        DirectoryModel local(false, nullptr);
        RemoteDirectoryModel remote;
        SessionFileModel model;

        model.useLocal(&local);
        model.setFilterQuery(QStringLiteral(" local "));
        model.setShowHidden(true);
        QCOMPARE(local.filterQuery(), QStringLiteral("local"));
        QVERIFY(local.showHidden());

        model.useRemote(&remote);
        QCOMPARE(model.filterQuery(), QString());
        QVERIFY(!model.showHidden());

        model.setFilterQuery(QStringLiteral(" remote "));
        model.setShowHidden(true);
        QCOMPARE(remote.filterQuery(), QStringLiteral("remote"));
        QVERIFY(remote.showHidden());

        QCOMPARE(local.filterQuery(), QStringLiteral("local"));
        QVERIFY(local.showHidden());
    }
};

QTEST_GUILESS_MAIN(SessionFileModelTest)
#include "SessionFileModelTest.moc"
