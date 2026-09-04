// SPDX-License-Identifier: GPL-3.0-only
#include "portal/PortalRoutingManager.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(bytes) == bytes.size();
}

struct Fixture {
    QTemporaryDir directory;
    QString configPath;
    QString statePath;

    Fixture()
        : configPath(directory.filePath(QStringLiteral("hyprland-portals.conf")))
        , statePath(directory.filePath(QStringLiteral("state/portal-routing.json"))) {
    }

    PortalRoutingManager manager() const {
        return PortalRoutingManager({configPath, statePath});
    }
};

const QByteArray kRyokuConfig =
    "# Screen sharing remains owned by Hyprland.\n"
    "[preferred]\n"
    "default=hyprland;gtk\n"
    "org.freedesktop.impl.portal.ScreenCast=hyprland\n"
    "org.freedesktop.impl.portal.Screenshot=hyprland\n"
    "  org.freedesktop.impl.portal.FileChooser = gtk\n"
    "\n"
    "[other]\n"
    "example=value\n";

} // namespace

class PortalRoutingManagerTest final : public QObject {
    Q_OBJECT

private slots:
    void enablePreservesEveryOtherRyokuRoute();
    void disableRestoresExactOriginalBytes();
    void enableAndDisableAreIdempotent();
    void missingFileChooserEntryIsInsertedAndRemoved();
    void externalChangesAfterEnableAreNeverClobbered();
    void externallyConfiguredRyofilesIsNeverClaimedOrRemoved();
    void duplicateFileChooserEntriesAreRejected();
    void symlinkedConfigIsRejected();
    void corruptedStateBlocksManagedMutation();
};

void PortalRoutingManagerTest::enablePreservesEveryOtherRyokuRoute() {
    Fixture fixture;
    QVERIFY(fixture.directory.isValid());
    QVERIFY(writeFile(fixture.configPath, kRyokuConfig));

    PortalRoutingManager manager = fixture.manager();
    const auto result = manager.enable();
    QVERIFY2(result.ok, qPrintable(result.message));
    QVERIFY(result.changed);

    const QByteArray enabled = readFile(fixture.configPath);
    const QByteArray expected =
        "# Screen sharing remains owned by Hyprland.\n"
        "[preferred]\n"
        "default=hyprland;gtk\n"
        "org.freedesktop.impl.portal.ScreenCast=hyprland\n"
        "org.freedesktop.impl.portal.Screenshot=hyprland\n"
        "org.freedesktop.impl.portal.FileChooser=ryofiles;gtk\n"
        "\n"
        "[other]\n"
        "example=value\n";
    QCOMPARE(enabled, expected);

    const auto status = manager.status();
    QVERIFY2(status.ok, qPrintable(status.message));
    QVERIFY(status.enabled);
    QVERIFY(status.managed);
    QCOMPARE(status.backendList, QStringLiteral("ryofiles;gtk"));
    QVERIFY(QFileInfo::exists(fixture.statePath));
}

void PortalRoutingManagerTest::disableRestoresExactOriginalBytes() {
    Fixture fixture;
    QVERIFY(writeFile(fixture.configPath, kRyokuConfig));
    PortalRoutingManager manager = fixture.manager();

    QVERIFY(manager.enable().ok);
    const auto disabled = manager.disable();
    QVERIFY2(disabled.ok, qPrintable(disabled.message));
    QVERIFY(disabled.changed);
    QCOMPARE(readFile(fixture.configPath), kRyokuConfig);
    QVERIFY(!QFileInfo::exists(fixture.statePath));

    const auto status = manager.status();
    QVERIFY(status.ok);
    QVERIFY(!status.enabled);
    QVERIFY(!status.managed);
    QCOMPARE(status.backendList, QStringLiteral("gtk"));
}

void PortalRoutingManagerTest::enableAndDisableAreIdempotent() {
    Fixture fixture;
    QVERIFY(writeFile(fixture.configPath, kRyokuConfig));
    PortalRoutingManager manager = fixture.manager();

    const auto firstEnable = manager.enable();
    QVERIFY(firstEnable.ok);
    QVERIFY(firstEnable.changed);
    const QByteArray onceEnabled = readFile(fixture.configPath);

    const auto secondEnable = manager.enable();
    QVERIFY(secondEnable.ok);
    QVERIFY(!secondEnable.changed);
    QCOMPARE(readFile(fixture.configPath), onceEnabled);

    const auto firstDisable = manager.disable();
    QVERIFY(firstDisable.ok);
    QVERIFY(firstDisable.changed);
    QCOMPARE(readFile(fixture.configPath), kRyokuConfig);

    const auto secondDisable = manager.disable();
    QVERIFY(secondDisable.ok);
    QVERIFY(!secondDisable.changed);
    QCOMPARE(readFile(fixture.configPath), kRyokuConfig);
}

void PortalRoutingManagerTest::missingFileChooserEntryIsInsertedAndRemoved() {
    Fixture fixture;
    const QByteArray original =
        "[preferred]\n"
        "default=hyprland;gtk\n"
        "org.freedesktop.impl.portal.ScreenCast=hyprland\n"
        "org.freedesktop.impl.portal.Screenshot=hyprland\n"
        "\n"
        "[other]\n"
        "example=value\n";
    QVERIFY(writeFile(fixture.configPath, original));
    PortalRoutingManager manager = fixture.manager();

    const auto enabled = manager.enable();
    QVERIFY2(enabled.ok, qPrintable(enabled.message));
    QVERIFY(readFile(fixture.configPath).contains(
        "org.freedesktop.impl.portal.FileChooser=ryofiles\n"));

    const auto disabled = manager.disable();
    QVERIFY2(disabled.ok, qPrintable(disabled.message));
    QCOMPARE(readFile(fixture.configPath), original);
}

void PortalRoutingManagerTest::externalChangesAfterEnableAreNeverClobbered() {
    Fixture fixture;
    QVERIFY(writeFile(fixture.configPath, kRyokuConfig));
    PortalRoutingManager manager = fixture.manager();
    QVERIFY(manager.enable().ok);

    QByteArray externallyChanged = readFile(fixture.configPath);
    externallyChanged.replace(
        "org.freedesktop.impl.portal.FileChooser=ryofiles;gtk",
        "org.freedesktop.impl.portal.FileChooser=ryofiles;kde");
    QVERIFY(writeFile(fixture.configPath, externallyChanged));

    const auto disabled = manager.disable();
    QVERIFY(!disabled.ok);
    QVERIFY(disabled.message.contains(QStringLiteral("refusing to clobber")));
    QCOMPARE(readFile(fixture.configPath), externallyChanged);
    QVERIFY(QFileInfo::exists(fixture.statePath));
}

void PortalRoutingManagerTest::externallyConfiguredRyofilesIsNeverClaimedOrRemoved() {
    Fixture fixture;
    const QByteArray externalConfig =
        "[preferred]\n"
        "default=hyprland;gtk\n"
        "org.freedesktop.impl.portal.FileChooser=ryofiles;gtk\n";
    QVERIFY(writeFile(fixture.configPath, externalConfig));
    PortalRoutingManager manager = fixture.manager();

    const auto enabled = manager.enable();
    QVERIFY(enabled.ok);
    QVERIFY(!enabled.changed);
    QCOMPARE(readFile(fixture.configPath), externalConfig);
    QVERIFY(!QFileInfo::exists(fixture.statePath));

    const auto status = manager.status();
    QVERIFY(status.ok);
    QVERIFY(status.enabled);
    QVERIFY(!status.managed);

    const auto disabled = manager.disable();
    QVERIFY(!disabled.ok);
    QCOMPARE(readFile(fixture.configPath), externalConfig);
}

void PortalRoutingManagerTest::duplicateFileChooserEntriesAreRejected() {
    Fixture fixture;
    const QByteArray invalid =
        "[preferred]\n"
        "org.freedesktop.impl.portal.FileChooser=gtk\n"
        "org.freedesktop.impl.portal.FileChooser=kde\n";
    QVERIFY(writeFile(fixture.configPath, invalid));
    PortalRoutingManager manager = fixture.manager();

    const auto result = manager.enable();
    QVERIFY(!result.ok);
    QCOMPARE(readFile(fixture.configPath), invalid);
    QVERIFY(!QFileInfo::exists(fixture.statePath));
}

void PortalRoutingManagerTest::symlinkedConfigIsRejected() {
    Fixture fixture;
    const QString realPath = fixture.directory.filePath(QStringLiteral("real.conf"));
    QVERIFY(writeFile(realPath, kRyokuConfig));
    QVERIFY(QFile::link(realPath, fixture.configPath));
    QVERIFY(QFileInfo(fixture.configPath).isSymLink());

    PortalRoutingManager manager = fixture.manager();
    const auto result = manager.enable();
    QVERIFY(!result.ok);
    QVERIFY(result.message.contains(QStringLiteral("symlinked")));
    QCOMPARE(readFile(realPath), kRyokuConfig);
}

void PortalRoutingManagerTest::corruptedStateBlocksManagedMutation() {
    Fixture fixture;
    QVERIFY(writeFile(fixture.configPath, kRyokuConfig));
    PortalRoutingManager manager = fixture.manager();
    QVERIFY(manager.enable().ok);
    const QByteArray enabledConfig = readFile(fixture.configPath);

    QVERIFY(writeFile(fixture.statePath, QByteArrayLiteral("not-json\n")));

    const auto status = manager.status();
    QVERIFY(!status.ok);
    const auto disabled = manager.disable();
    QVERIFY(!disabled.ok);
    QCOMPARE(readFile(fixture.configPath), enabledConfig);
}

QTEST_GUILESS_MAIN(PortalRoutingManagerTest)
#include "PortalRoutingManagerTest.moc"
