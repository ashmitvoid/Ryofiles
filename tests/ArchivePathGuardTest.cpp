// SPDX-License-Identifier: GPL-3.0-only

#include "archive/ArchivePathGuard.hpp"

#include <QTest>

class ArchivePathGuardTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsAndNormalizesSafeEntryPaths();
    void rejectsUnsafeEntryPaths();
    void validatesHardlinkTargets();
    void acceptsContainedSymlinkTargets();
    void rejectsEscapingSymlinkTargets();
    void rejectsOversizedAndNullMetadata();
};

void ArchivePathGuardTest::acceptsAndNormalizesSafeEntryPaths() {
    const QList<QPair<QString, QString>> cases = {
        {QStringLiteral("file.txt"), QStringLiteral("file.txt")},
        {QStringLiteral("dir/file.txt"), QStringLiteral("dir/file.txt")},
        {QStringLiteral("./dir//file.txt"), QStringLiteral("dir/file.txt")},
        {QStringLiteral("a/./b"), QStringLiteral("a/b")},
        {QStringLiteral("-leading-dash"), QStringLiteral("-leading-dash")},
        {QStringLiteral("space and 'quote'.txt"), QStringLiteral("space and 'quote'.txt")},
        {QStringLiteral("colon:name.txt"), QStringLiteral("colon:name.txt")},
        {QStringLiteral("日本語/😀.txt"), QStringLiteral("日本語/😀.txt")},
        {QStringLiteral("literal\\backslash.txt"), QStringLiteral("literal\\backslash.txt")},
    };

    for (const auto& [input, expected] : cases) {
        const ArchivePathResult result = ArchivePathGuard::validateEntryPath(input);
        QVERIFY2(result.safe, qPrintable(QStringLiteral("%1: %2").arg(input, result.error)));
        QCOMPARE(result.normalizedPath, expected);
        QVERIFY(result.error.isEmpty());
    }
}

void ArchivePathGuardTest::rejectsUnsafeEntryPaths() {
    const QStringList cases = {
        QString(),
        QStringLiteral("."),
        QStringLiteral("././"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("../evil"),
        QStringLiteral("a/../../evil"),
        QStringLiteral("a/../b"),
        QStringLiteral("a\\..\\b"),
        QStringLiteral("..\\evil"),
        QStringLiteral("C:/evil"),
        QStringLiteral("C:\\evil"),
        QStringLiteral("\\rooted"),
        QStringLiteral("\\\\server\\share\\evil"),
        QStringLiteral("//server/share/evil"),
    };

    for (const QString& input : cases) {
        const ArchivePathResult result = ArchivePathGuard::validateEntryPath(input);
        QVERIFY2(!result.safe, qPrintable(QStringLiteral("unexpectedly accepted: %1").arg(input)));
        QVERIFY(result.normalizedPath.isEmpty());
        QVERIFY(!result.error.isEmpty());
    }
}

void ArchivePathGuardTest::validatesHardlinkTargets() {
    const ArchivePathResult safe = ArchivePathGuard::validateHardlinkTarget(
        QStringLiteral("dir/target.txt"));
    QVERIFY(safe.safe);
    QCOMPARE(safe.normalizedPath, QStringLiteral("dir/target.txt"));

    const QStringList unsafe = {
        QStringLiteral("../target"),
        QStringLiteral("dir/../target"),
        QStringLiteral("/target"),
        QStringLiteral("C:\\target"),
        QStringLiteral("\\\\server\\target"),
    };
    for (const QString& target : unsafe)
        QVERIFY2(!ArchivePathGuard::validateHardlinkTarget(target).safe, qPrintable(target));
}

void ArchivePathGuardTest::acceptsContainedSymlinkTargets() {
    struct Case {
        QString entry;
        QString target;
        QString normalized;
    };
    const QList<Case> cases = {
        {QStringLiteral("dir/link"), QStringLiteral("../target"), QStringLiteral("../target")},
        {QStringLiteral("dir/link"), QStringLiteral("./target"), QStringLiteral("target")},
        {QStringLiteral("dir/link"), QStringLiteral(".."), QStringLiteral("..")},
        {QStringLiteral("dir/deeper/link"), QStringLiteral("../../target"), QStringLiteral("../../target")},
        {QStringLiteral("link"), QStringLiteral("."), QStringLiteral(".")},
        {QStringLiteral("dir/link"), QStringLiteral("nested//target"), QStringLiteral("nested/target")},
    };

    for (const Case& test : cases) {
        const ArchivePathResult result = ArchivePathGuard::validateSymlinkTarget(
            test.entry, test.target);
        QVERIFY2(result.safe, qPrintable(QStringLiteral("%1 -> %2: %3")
                                             .arg(test.entry, test.target, result.error)));
        QCOMPARE(result.normalizedPath, test.normalized);
    }
}

void ArchivePathGuardTest::rejectsEscapingSymlinkTargets() {
    struct Case {
        QString entry;
        QString target;
    };
    const QList<Case> cases = {
        {QStringLiteral("link"), QStringLiteral("../outside")},
        {QStringLiteral("dir/link"), QStringLiteral("../../outside")},
        {QStringLiteral("dir/deeper/link"), QStringLiteral("../../../outside")},
        {QStringLiteral("dir/link"), QStringLiteral("/etc/passwd")},
        {QStringLiteral("dir/link"), QStringLiteral("C:/outside")},
        {QStringLiteral("dir/link"), QStringLiteral("C:\\outside")},
        {QStringLiteral("dir/link"), QStringLiteral("\\rooted")},
        {QStringLiteral("dir/link"), QStringLiteral("\\\\server\\share")},
        {QStringLiteral("dir/../link"), QStringLiteral("target")},
        {QStringLiteral("dir/link"), QString()},
    };

    for (const Case& test : cases) {
        const ArchivePathResult result = ArchivePathGuard::validateSymlinkTarget(
            test.entry, test.target);
        QVERIFY2(!result.safe, qPrintable(QStringLiteral("unexpectedly accepted %1 -> %2")
                                               .arg(test.entry, test.target)));
        QVERIFY(!result.error.isEmpty());
    }
}

void ArchivePathGuardTest::rejectsOversizedAndNullMetadata() {
    const QString oversized(32 * 1024 + 1, QLatin1Char('a'));
    QVERIFY(!ArchivePathGuard::validateEntryPath(oversized).safe);
    QVERIFY(!ArchivePathGuard::validateHardlinkTarget(oversized).safe);
    QVERIFY(!ArchivePathGuard::validateSymlinkTarget(QStringLiteral("dir/link"), oversized).safe);

    QString withNull = QStringLiteral("safe");
    withNull.append(QChar::Null);
    withNull.append(QStringLiteral("evil"));
    QVERIFY(!ArchivePathGuard::validateEntryPath(withNull).safe);
    QVERIFY(!ArchivePathGuard::validateHardlinkTarget(withNull).safe);
    QVERIFY(!ArchivePathGuard::validateSymlinkTarget(QStringLiteral("dir/link"), withNull).safe);
}

QTEST_MAIN(ArchivePathGuardTest)
#include "ArchivePathGuardTest.moc"
