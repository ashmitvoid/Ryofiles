// SPDX-License-Identifier: GPL-3.0-only

#include "navigation/TabManager.hpp"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TabManagerSplitTest final : public QObject {
    Q_OBJECT

private:
    static QString makeDir(const QString& root, const QString& name) {
        const QString path = QDir(root).filePath(name);
        if (!QDir().mkpath(path))
            return {};
        return path;
    }

private slots:
    void splitCreatesIndependentSessions() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString left = makeDir(temp.path(), QStringLiteral("left"));
        const QString right = makeDir(temp.path(), QStringLiteral("right"));
        const QString nested = makeDir(right, QStringLiteral("nested"));
        QVERIFY(!left.isEmpty());
        QVERIFY(!right.isEmpty());
        QVERIFY(!nested.isEmpty());

        TabManager tabs;
        QVERIFY(tabs.primarySession());
        QVERIFY(tabs.primarySession()->navigate(left));
        DirectorySession* primary = tabs.primarySession();

        tabs.openSplit(right);
        QVERIFY(tabs.split());
        QVERIFY(tabs.secondarySession());
        QVERIFY(tabs.secondarySession() != primary);
        QCOMPARE(tabs.primarySession()->path(), left);
        QCOMPARE(tabs.secondarySession()->path(), right);
        QCOMPARE(tabs.currentSession(), primary);

        QVERIFY(tabs.secondarySession()->navigate(nested));
        QCOMPARE(tabs.secondarySession()->path(), nested);
        QCOMPARE(tabs.primarySession()->path(), left);
    }

    void activePaneRoutesCurrentSession() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString left = makeDir(temp.path(), QStringLiteral("left"));
        const QString right = makeDir(temp.path(), QStringLiteral("right"));
        QVERIFY(!left.isEmpty());
        QVERIFY(!right.isEmpty());

        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(left));
        tabs.openSplit(right);

        DirectorySession* primary = tabs.primarySession();
        DirectorySession* secondary = tabs.secondarySession();
        QSignalSpy currentChanged(&tabs, &TabManager::currentSessionChanged);

        QCOMPARE(tabs.activePane(), 0);
        QCOMPARE(tabs.currentSession(), primary);

        tabs.setActivePane(1);
        QCOMPARE(tabs.activePane(), 1);
        QCOMPARE(tabs.currentSession(), secondary);
        QCOMPARE(currentChanged.count(), 1);

        tabs.setActivePane(0);
        QCOMPARE(tabs.activePane(), 0);
        QCOMPARE(tabs.currentSession(), primary);
        QCOMPARE(currentChanged.count(), 2);
    }

    void splitStateIsPerTab() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString left = makeDir(temp.path(), QStringLiteral("left"));
        const QString right = makeDir(temp.path(), QStringLiteral("right"));
        const QString other = makeDir(temp.path(), QStringLiteral("other"));
        QVERIFY(!left.isEmpty());
        QVERIFY(!right.isEmpty());
        QVERIFY(!other.isEmpty());

        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(left));
        tabs.openSplit(right);
        tabs.setActivePane(1);

        DirectorySession* firstPrimary = tabs.primarySession();
        DirectorySession* firstSecondary = tabs.secondarySession();

        tabs.newTab(other);
        QCOMPARE(tabs.rowCount(), 2);
        QVERIFY(!tabs.split());
        QCOMPARE(tabs.activePane(), 0);
        QCOMPARE(tabs.currentSession()->path(), other);

        tabs.previousTab();
        QVERIFY(tabs.split());
        QCOMPARE(tabs.activePane(), 1);
        QCOMPARE(tabs.primarySession(), firstPrimary);
        QCOMPARE(tabs.secondarySession(), firstSecondary);
        QCOMPARE(tabs.currentSession(), firstSecondary);
    }

    void swapPanesSwapsCompleteSessions() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString left = makeDir(temp.path(), QStringLiteral("left"));
        const QString right = makeDir(temp.path(), QStringLiteral("right"));
        QVERIFY(!left.isEmpty());
        QVERIFY(!right.isEmpty());

        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(left));
        tabs.primarySession()->setViewMode(DirectorySession::CompactView);
        tabs.openSplit(right);
        tabs.secondarySession()->setViewMode(DirectorySession::GridView);

        DirectorySession* originalPrimary = tabs.primarySession();
        DirectorySession* originalSecondary = tabs.secondarySession();

        tabs.swapPanes();
        QCOMPARE(tabs.primarySession(), originalSecondary);
        QCOMPARE(tabs.secondarySession(), originalPrimary);
        QCOMPARE(tabs.primarySession()->path(), right);
        QCOMPARE(tabs.secondarySession()->path(), left);
        QCOMPARE(tabs.primarySession()->viewMode(), static_cast<int>(DirectorySession::GridView));
        QCOMPARE(tabs.secondarySession()->viewMode(), static_cast<int>(DirectorySession::CompactView));
    }

    void closedSplitTabRestoresBothPanes() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString left = makeDir(temp.path(), QStringLiteral("left"));
        const QString right = makeDir(temp.path(), QStringLiteral("right"));
        const QString other = makeDir(temp.path(), QStringLiteral("other"));
        QVERIFY(!left.isEmpty());
        QVERIFY(!right.isEmpty());
        QVERIFY(!other.isEmpty());

        TabManager tabs;
        QVERIFY(tabs.primarySession()->navigate(left));
        tabs.openSplit(right);
        tabs.setActivePane(1);
        tabs.newTab(other);
        tabs.previousTab();

        tabs.closeCurrentTab();
        QCOMPARE(tabs.rowCount(), 1);
        QVERIFY(!tabs.split());
        QCOMPARE(tabs.currentSession()->path(), other);

        tabs.reopenClosedTab();
        QCOMPARE(tabs.rowCount(), 2);
        QVERIFY(tabs.split());
        QCOMPARE(tabs.primarySession()->path(), left);
        QCOMPARE(tabs.secondarySession()->path(), right);
        QCOMPARE(tabs.activePane(), 1);
        QCOMPARE(tabs.currentSession(), tabs.secondarySession());
    }
};

QTEST_GUILESS_MAIN(TabManagerSplitTest)
#include "TabManagerSplitTest.moc"
