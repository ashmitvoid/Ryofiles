// SPDX-License-Identifier: GPL-3.0-only

#include "locations/RemoteDirectoryModel.hpp"

#include <QtTest>

class RemoteDirectoryModelTest final : public QObject {
    Q_OBJECT

private slots:
    void sortsDirectoriesBeforeFilesAndNamesCaseInsensitively() {
        QVector<RemoteDirectoryEntry> entries {
            {QStringLiteral("zeta.txt"), QStringLiteral("sftp://host/zeta.txt"), {}, {}, false, false},
            {QStringLiteral("beta"), QStringLiteral("sftp://host/beta"), {}, {}, true, false},
            {QStringLiteral("Alpha.txt"), QStringLiteral("sftp://host/Alpha.txt"), {}, {}, false, false},
            {QStringLiteral("alpha"), QStringLiteral("sftp://host/alpha"), {}, {}, true, false},
        };

        const auto visible = RemoteDirectoryModel::visibleEntries(
            std::move(entries), true, {});

        QCOMPARE(visible.size(), 4);
        QCOMPARE(visible.at(0).name, QStringLiteral("alpha"));
        QCOMPARE(visible.at(1).name, QStringLiteral("beta"));
        QCOMPARE(visible.at(2).name, QStringLiteral("Alpha.txt"));
        QCOMPARE(visible.at(3).name, QStringLiteral("zeta.txt"));
    }

    void hidesHiddenEntriesWithoutAnotherNetworkScan() {
        QVector<RemoteDirectoryEntry> entries {
            {QStringLiteral(".secret"), QStringLiteral("smb://nas/share/.secret"), {}, {}, false, true},
            {QStringLiteral("visible"), QStringLiteral("smb://nas/share/visible"), {}, {}, true, false},
        };

        const auto hiddenOff = RemoteDirectoryModel::visibleEntries(entries, false, {});
        QCOMPARE(hiddenOff.size(), 1);
        QCOMPARE(hiddenOff.front().name, QStringLiteral("visible"));

        const auto hiddenOn = RemoteDirectoryModel::visibleEntries(entries, true, {});
        QCOMPARE(hiddenOn.size(), 2);
    }

    void filtersDisplayNamesCaseInsensitively() {
        QVector<RemoteDirectoryEntry> entries {
            {QStringLiteral("Project Notes.md"), QStringLiteral("dav://host/Project%20Notes.md"), {}, {}, false, false},
            {QStringLiteral("archive.zip"), QStringLiteral("dav://host/archive.zip"), {}, {}, false, false},
        };

        const auto visible = RemoteDirectoryModel::visibleEntries(
            std::move(entries), true, QStringLiteral("  NOTES  "));

        QCOMPARE(visible.size(), 1);
        QCOMPARE(visible.front().name, QStringLiteral("Project Notes.md"));
    }

    void formatsBinarySizes() {
        QCOMPARE(RemoteDirectoryModel::formatSize(42), QStringLiteral("42 B"));
        QCOMPARE(RemoteDirectoryModel::formatSize(1536), QStringLiteral("1.5 KB"));
        QCOMPARE(RemoteDirectoryModel::formatSize(2 * 1024 * 1024), QStringLiteral("2.0 MB"));
        QCOMPARE(RemoteDirectoryModel::formatSize(-1), QString());
    }

    void rejectsLocalUrisWithoutStartingARequest() {
        RemoteDirectoryModel model;
        QSignalSpy errorSpy(&model, &RemoteDirectoryModel::errorChanged);

        model.setUri(QStringLiteral("file:///tmp"));

        QVERIFY(model.uri().isEmpty());
        QVERIFY(!model.loading());
        QVERIFY(!model.error().isEmpty());
        QVERIFY(errorSpy.count() >= 1);
    }
};

QTEST_GUILESS_MAIN(RemoteDirectoryModelTest)
#include "RemoteDirectoryModelTest.moc"
