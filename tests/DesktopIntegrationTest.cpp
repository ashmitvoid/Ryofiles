// SPDX-License-Identifier: GPL-3.0-only

#include "integrations/DesktopIntegration.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class DesktopIntegrationTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

    static QByteArray readFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

private slots:
    void initTestCase() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());

        m_dataHome = m_root->filePath("xdg-data");
        m_configHome = m_root->filePath("xdg-config");
        m_actionLog = m_root->filePath("ryoku-action.log");

        const QString applications = QDir(m_dataHome).filePath("applications");
        const QString scripts = QDir(m_configHome).filePath("hypr/scripts");
        QVERIFY(QDir().mkpath(applications));
        QVERIFY(QDir().mkpath(scripts));

        qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
        qputenv("XDG_DATA_DIRS", QByteArrayLiteral("/usr/local/share:/usr/share"));
        qputenv("XDG_CONFIG_HOME", m_configHome.toUtf8());
        qputenv("RYOFILES_TEST_ACTION_LOG", m_actionLog.toUtf8());

        QFile desktopFile(QDir(applications).filePath("ryofiles-test-viewer.desktop"));
        QVERIFY(desktopFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));

        const QByteArray desktopEntry =
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=Ryofiles Test Viewer\n"
            "Exec=/usr/bin/true %f\n"
            "MimeType=text/plain;\n"
            "NoDisplay=false\n"
            "Terminal=false\n";

        QCOMPARE(desktopFile.write(desktopEntry), desktopEntry.size());
        desktopFile.close();

        writeFile(
            QDir(scripts).filePath("stash-install.sh"),
            "#!/usr/bin/env bash\n"
            "printf 'install|%s|%s\\n' \"${RYOKU_STASH_KEEP:-}\" \"$1\" >> \"$RYOFILES_TEST_ACTION_LOG\"\n"
            "case \"$1\" in *fail*) exit 7 ;; esac\n"
            "exit 0\n");
        writeFile(
            QDir(scripts).filePath("stash-compress.sh"),
            "#!/usr/bin/env bash\n"
            "printf 'compress|%s|%s\\n' \"${RYOKU_STASH_KEEP:-}\" \"$1\" >> \"$RYOFILES_TEST_ACTION_LOG\"\n"
            "case \"$1\" in *fail*) exit 9 ;; esac\n"
            "exit 0\n");

        m_desktop = std::make_unique<DesktopIntegration>();
    }

    void filePropertiesReportDirectMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath("sample.txt");
        writeFile(path, "hello");

        const QVariantMap properties = m_desktop->propertiesForPath(path);

        QCOMPARE(properties.value("name").toString(), QStringLiteral("sample.txt"));
        QCOMPARE(properties.value("path").toString(), path);
        QCOMPARE(properties.value("sizeBytes").toLongLong(), 5LL);
        QVERIFY(properties.value("sizeText").toString() != QStringLiteral("Not calculated"));
        QVERIFY(!properties.value("mime").toString().isEmpty());
        QCOMPARE(properties.value("isDirectory").toBool(), false);
    }

    void openWithDiscoversAndLaunchesDesktopApplication() {
        const QString path = m_root->filePath("open-with.txt");
        writeFile(path, "hello");

        QTRY_VERIFY_WITH_TIMEOUT(m_desktop->applicationsReady(), 5000);

        const QVariantList applications = m_desktop->applicationsForPath(path);
        QString appId;

        for (const QVariant& item : applications) {
            const QVariantMap app = item.toMap();
            if (app.value("name").toString() == QStringLiteral("Ryofiles Test Viewer")) {
                appId = app.value("id").toString();
                break;
            }
        }

        QVERIFY(!appId.isEmpty());
        QVERIFY(m_desktop->openWith(appId, path));
    }

    void directoryPropertiesNeverCalculateRecursiveSize() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString directory = QDir(temp.path()).filePath("tree");
        const QString nested = QDir(directory).filePath("nested/deeper");
        QVERIFY(QDir().mkpath(nested));

        writeFile(QDir(directory).filePath("a.bin"), QByteArray(1024, 'a'));
        writeFile(QDir(nested).filePath("b.bin"), QByteArray(4096, 'b'));

        const QVariantMap properties = m_desktop->propertiesForPath(directory);

        QCOMPARE(properties.value("isDirectory").toBool(), true);
        QCOMPARE(properties.value("sizeText").toString(), QStringLiteral("Not calculated"));
        QVERIFY(!properties.value("sizeBytes").isValid() || properties.value("sizeBytes").isNull());
        QCOMPARE(properties.value("mime").toString(), QStringLiteral("inode/directory"));
    }

    void ryokuSuffixContractsMatchUpstream() {
        QVERIFY(DesktopIntegration::isRyokuInstallablePath(QStringLiteral("app.AppImage")));
        QVERIFY(DesktopIntegration::isRyokuInstallablePath(QStringLiteral("bundle.pkg.tar.zst")));
        QVERIFY(DesktopIntegration::isRyokuInstallablePath(QStringLiteral("archive.TAR.XZ")));
        QVERIFY(DesktopIntegration::isRyokuInstallablePath(QStringLiteral("package.rpm")));
        QVERIFY(!DesktopIntegration::isRyokuInstallablePath(QStringLiteral("notes.txt")));

        QVERIFY(DesktopIntegration::isRyokuCompressiblePath(QStringLiteral("movie.MKV")));
        QVERIFY(DesktopIntegration::isRyokuCompressiblePath(QStringLiteral("photo.jpeg")));
        QVERIFY(DesktopIntegration::isRyokuCompressiblePath(QStringLiteral("image.BMP")));
        QVERIFY(!DesktopIntegration::isRyokuCompressiblePath(QStringLiteral("archive.zip")));
    }

    void ryokuEligibilityKeepsOnlyExistingLocalRegularFiles() {
        const QString appImage = m_root->filePath("eligible.AppImage");
        const QString package = m_root->filePath("eligible.pkg.tar.zst");
        const QString video = m_root->filePath("eligible.MP4");
        const QString unsupported = m_root->filePath("unsupported.txt");
        const QString disguisedDirectory = m_root->filePath("folder.tar.gz");

        writeFile(appImage, "app");
        writeFile(package, "pkg");
        writeFile(video, "video");
        writeFile(unsupported, "text");
        QVERIFY(QDir().mkpath(disguisedDirectory));

        QCOMPARE(
            DesktopIntegration::ryokuInstallablePaths({
                appImage,
                package,
                video,
                unsupported,
                disguisedDirectory,
                QStringLiteral("sftp://example.invalid/tool.AppImage"),
                appImage,
            }),
            QStringList({appImage, package}));

        QCOMPARE(
            DesktopIntegration::ryokuCompressiblePaths({
                package,
                video,
                QStringLiteral("file:///tmp/photo.jpg"),
                m_root->filePath("missing.png"),
                video,
            }),
            QStringList({video}));

        QVERIFY(m_desktop->canRyokuInstall({appImage, unsupported}));
        QVERIFY(m_desktop->canRyokuCompress({video, unsupported}));
        QVERIFY(!m_desktop->canRyokuInstall({unsupported}));
        QVERIFY(!m_desktop->canRyokuCompress({QStringLiteral("smb://host.invalid/photo.jpg")}));
    }

    void ryokuMixedSelectionRunsEligibleSubsetInSelectionOrder() {
        QFile::remove(m_actionLog);

        const QString unsupported = m_root->filePath("mixed notes.txt");
        const QString first = m_root->filePath("mixed first.AppImage");
        const QString second = m_root->filePath("mixed second.tar.xz");
        writeFile(unsupported, "notes");
        writeFile(first, "first");
        writeFile(second, "second");

        QSignalSpy started(m_desktop.get(), &DesktopIntegration::ryokuActionStarted);
        QSignalSpy finished(m_desktop.get(), &DesktopIntegration::ryokuActionFinished);

        QVERIFY(m_desktop->installWithRyoku({
            unsupported,
            first,
            QStringLiteral("sftp://example.invalid/remote.AppImage"),
            second,
            first,
        }));

        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, 5000);
        QCOMPARE(started.at(0).at(0).toString(), QStringLiteral("install"));
        QCOMPARE(started.at(0).at(1).toInt(), 2);

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        const QList<QVariant> result = finished.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("install"));
        QCOMPARE(result.at(1).toInt(), 2);
        QCOMPARE(result.at(2).toInt(), 0);

        const QList<QByteArray> lines = readFile(m_actionLog).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0), QByteArray("install|1|") + first.toUtf8());
        QCOMPARE(lines.at(1), QByteArray("install|1|") + second.toUtf8());
    }

    void ryokuInstallRunsSequentiallyWithLiteralArgumentsAndKeepsSources() {
        QFile::remove(m_actionLog);

        const QString literal = m_root->filePath("one; touch PWNED; #.AppImage");
        const QString failing = m_root->filePath("two fail.deb");
        const QString last = m_root->filePath("three.tar.gz");
        writeFile(literal, "one");
        writeFile(failing, "two");
        writeFile(last, "three");

        QSignalSpy finished(m_desktop.get(), &DesktopIntegration::ryokuActionFinished);
        QVERIFY(m_desktop->installWithRyoku({literal, failing, last}));
        QVERIFY(m_desktop->ryokuActionBusy());
        QVERIFY(!m_desktop->compressWithRyoku({m_root->filePath("eligible.MP4")}));
        QVERIFY(m_desktop->ryokuActionError().contains(QStringLiteral("already running"), Qt::CaseInsensitive));

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!m_desktop->ryokuActionBusy());

        const QList<QVariant> result = finished.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("install"));
        QCOMPARE(result.at(1).toInt(), 2);
        QCOMPARE(result.at(2).toInt(), 1);
        QVERIFY(result.at(3).toString().contains(QStringLiteral("1 of 3")));
        QCOMPARE(m_desktop->ryokuActionError(), result.at(3).toString());

        const QList<QByteArray> lines = readFile(m_actionLog).trimmed().split('\n');
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0), QByteArray("install|1|") + literal.toUtf8());
        QCOMPARE(lines.at(1), QByteArray("install|1|") + failing.toUtf8());
        QCOMPARE(lines.at(2), QByteArray("install|1|") + last.toUtf8());
        QVERIFY(!QFileInfo(m_root->filePath("PWNED")).exists());
    }

    void ryokuCompressDoesNotInjectInstallKeepEnvironment() {
        QFile::remove(m_actionLog);

        const QString video = m_root->filePath("compress me.mp4");
        const QString image = m_root->filePath("photo.webp");
        writeFile(video, "video");
        writeFile(image, "image");

        QSignalSpy finished(m_desktop.get(), &DesktopIntegration::ryokuActionFinished);
        QVERIFY(m_desktop->compressWithRyoku({video, image}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);

        const QList<QVariant> result = finished.takeFirst();
        QCOMPARE(result.at(0).toString(), QStringLiteral("compress"));
        QCOMPARE(result.at(1).toInt(), 2);
        QCOMPARE(result.at(2).toInt(), 0);
        QCOMPARE(result.at(3).toString(), QString());
        QCOMPARE(m_desktop->ryokuActionError(), QString());

        const QList<QByteArray> lines = readFile(m_actionLog).trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0), QByteArray("compress||") + video.toUtf8());
        QCOMPARE(lines.at(1), QByteArray("compress||") + image.toUtf8());
    }

private:
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<DesktopIntegration> m_desktop;
    QString m_dataHome;
    QString m_configHome;
    QString m_actionLog;
};

QTEST_GUILESS_MAIN(DesktopIntegrationTest)
#include "DesktopIntegrationTest.moc"
