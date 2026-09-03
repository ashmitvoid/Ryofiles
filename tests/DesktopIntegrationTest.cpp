// SPDX-License-Identifier: GPL-3.0-only

#include "integrations/DesktopIntegration.hpp"

#include <QDir>
#include <QFile>
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

private slots:
    void initTestCase() {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());

        m_dataHome = m_root->filePath("xdg-data");
        const QString applications = QDir(m_dataHome).filePath("applications");
        QVERIFY(QDir().mkpath(applications));
        qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
        qputenv("XDG_DATA_DIRS", m_dataHome.toUtf8());

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

        DesktopIntegration desktop;
        const QVariantMap properties = desktop.propertiesForPath(directory);

        QCOMPARE(properties.value("isDirectory").toBool(), true);
        QCOMPARE(properties.value("sizeText").toString(), QStringLiteral("Not calculated"));
        QVERIFY(!properties.value("sizeBytes").isValid() || properties.value("sizeBytes").isNull());
        QCOMPARE(properties.value("mime").toString(), QStringLiteral("inode/directory"));
    }
private:
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<DesktopIntegration> m_desktop;
    QString m_dataHome;
};

QTEST_APPLESS_MAIN(DesktopIntegrationTest)
#include "DesktopIntegrationTest.moc"
