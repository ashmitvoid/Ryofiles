// SPDX-License-Identifier: GPL-3.0-only

#include "integrations/DesktopIntegration.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DesktopIntegrationTest final : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString& path, const QByteArray& content) {
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
        QCOMPARE(file.write(content), content.size());
    }

private slots:
    void filePropertiesReportDirectMetadata() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());

        const QString path = QDir(temp.path()).filePath("sample.txt");
        writeFile(path, "hello");

        DesktopIntegration desktop;
        const QVariantMap properties = desktop.propertiesForPath(path);

        QCOMPARE(properties.value("name").toString(), QStringLiteral("sample.txt"));
        QCOMPARE(properties.value("path").toString(), path);
        QCOMPARE(properties.value("sizeBytes").toLongLong(), 5LL);
        QVERIFY(properties.value("sizeText").toString() != QStringLiteral("Not calculated"));
        QVERIFY(!properties.value("mime").toString().isEmpty());
        QCOMPARE(properties.value("isDirectory").toBool(), false);
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
};

QTEST_APPLESS_MAIN(DesktopIntegrationTest)
#include "DesktopIntegrationTest.moc"
