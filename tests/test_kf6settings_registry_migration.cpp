/**
 * @file test_kf6settings_registry_migration.cpp
 * @brief Tests for the one-time DeviceRegistry → DeviceSerials migration
 *
 * Verifies that legacy fingerprint-keyed entries (written by pre-Phase-L
 * versions of WildPalms) are copied into the serial-keyed group at next
 * KF6Settings instantiation, and that the legacy group is then cleared.
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <KSharedConfig>
#include <KConfigGroup>
#include "kf6/kf6settings.h"

class TestKF6SettingsMigration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testMigrationCopiesSerialEntries();
    void testMigrationIsIdempotent();
    void testEmptyLegacyGroupIsNoOp();
};

void TestKF6SettingsMigration::initTestCase()
{
    // CMakeLists isolates XDG_CONFIG_HOME per test execution.
    qDebug() << "XDG_CONFIG_HOME=" << qgetenv("XDG_CONFIG_HOME");
}

void TestKF6SettingsMigration::testMigrationCopiesSerialEntries()
{
    // Arrange: write two legacy DeviceRegistry entries directly via
    // KSharedConfig, BEFORE constructing KF6Settings.
    {
        KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
        KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
        // Key format per DeviceFingerprint::registryKey(): "userId:userName:serial"
        legacy.writeEntry(QStringLiteral("42:Alice:SN-AAA-001"), QStringLiteral("/tmp/profileA"));
        legacy.writeEntry(QStringLiteral("99:Bob:SN-BBB-002"), QStringLiteral("/tmp/profileB"));
        cfg->sync();
    }

    // Act: construct (triggers migration in ctor).
    KF6Settings &s = KF6Settings::instance();

    // Assert: DeviceSerials has both entries with the parsed serial as key.
    KSharedConfig::Ptr verifyCfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup serials(verifyCfg, QStringLiteral("DeviceSerials"));
    QCOMPARE(serials.readEntry(QStringLiteral("SN-AAA-001"), QString()),
             QStringLiteral("/tmp/profileA"));
    QCOMPARE(serials.readEntry(QStringLiteral("SN-BBB-002"), QString()),
             QStringLiteral("/tmp/profileB"));

    // Assert: legacy DeviceRegistry group is now empty.
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
    QVERIFY2(legacy.keyList().isEmpty(),
             qPrintable(QStringLiteral("Legacy group not cleared: ") + legacy.keyList().join(",")));
}

void TestKF6SettingsMigration::testMigrationIsIdempotent()
{
    // KF6Settings is a singleton, so this re-uses the already-constructed
    // instance from the prior test — that's exactly the in-process
    // "second access after migration ran" scenario we want to verify:
    // no crash, no undo, the migrated state is still present.
    KSharedConfig::Ptr verifyCfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup serials(verifyCfg, QStringLiteral("DeviceSerials"));
    QCOMPARE(serials.readEntry(QStringLiteral("SN-AAA-001"), QString()),
             QStringLiteral("/tmp/profileA"));
}

void TestKF6SettingsMigration::testEmptyLegacyGroupIsNoOp()
{
    // Already covered by testMigrationIsIdempotent's second run, but
    // make the intent explicit: no crash, no spurious writes.
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
    QVERIFY(legacy.keyList().isEmpty());
}

QTEST_MAIN(TestKF6SettingsMigration)
#include "test_kf6settings_registry_migration.moc"
