// SPDX-License-Identifier: GPL-3.0-only

#include "integrations/ClipboardController.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class ClipboardControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void normalizesSupportedNetworkLocations() {
        const QStringList normalized = ClipboardController::normalizedLocations({
            QStringLiteral("SFTP://alice@example.invalid/share/report.txt"),
            QStringLiteral("smb://nas.invalid/team"),
            QStringLiteral("sftp://alice@example.invalid/share/report.txt"),
        });

        QCOMPARE(normalized.size(), 2);
        QCOMPARE(normalized.at(0), QStringLiteral("sftp://alice@example.invalid/share/report.txt"));
        QCOMPARE(normalized.at(1), QStringLiteral("smb://nas.invalid/team"));
    }

    void rejectsUnsupportedAndSecretBearingUris() {
        const QStringList normalized = ClipboardController::normalizedLocations({
            QStringLiteral("https://example.invalid/file"),
            QStringLiteral("sftp://alice:secret@example.invalid/file"),
            QStringLiteral("ssh://alice@example.invalid/file"),
        });
        QVERIFY(normalized.isEmpty());
    }

    void keepsExistingLocalFilesAndDropsMissingOnes() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString existing = temp.filePath(QStringLiteral("report.txt"));
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
        file.close();

        const QStringList normalized = ClipboardController::normalizedLocations({
            existing,
            temp.filePath(QStringLiteral("missing.txt")),
        });
        QCOMPARE(normalized, QStringList{QDir::cleanPath(existing)});
    }
};

QTEST_GUILESS_MAIN(ClipboardControllerTest)
#include "ClipboardControllerTest.moc"
