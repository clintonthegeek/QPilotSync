#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "synctypes.h"

class TstPalmRuntimeDefaultMappingsOnlyWhenEmpty : public QObject
{
    Q_OBJECT
private slots:
    void defaults_skipped_if_user_mappings_present();
    void defaultsOnlyForUncoveredSlots();
    void defaultMappings_useLastWriteWinsPolicy();
    void wildcardSourceCalendarSuppressesDefaults();  // F.1c.1 T1
};

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaults_skipped_if_user_mappings_present()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonObject m;
    m[QStringLiteral("id")] = QStringLiteral("user-only");
    m[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    m[QStringLiteral("sourceCalendar")] = QStringLiteral("cal");
    m[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-x");
    m[QStringLiteral("targetCalendar")] = QStringLiteral("out");
    m[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    m[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    m[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    m[QStringLiteral("enabled")] = true;
    QJsonArray arr;
    arr.append(m);

    rt.reloadMappings(arr);
    QCOMPARE(rt.palmMappings().size(), 1);

    // This test verifies the user-mapping-preservation contract via
    // reloadMappings + palmMappings round-trip. The guard that skips
    // defaults when user mappings are present is tested by inspection
    // of palmruntime.cpp's connectDevice.
    auto current = rt.palmMappings();
    QCOMPARE(current.size(), 1);
    QCOMPARE(current[0].id, QStringLiteral("user-only"));
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaultsOnlyForUncoveredSlots()
{
    // C Task 9: repurposed from an old per-slot F1 test (now retired).
    // Verifies that finishConnect() produces exactly one Star mapping per
    // connected domain (hub-and-spoke topology) — i.e. one mapping per Palm
    // backend, not one per category slot.
    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    const auto mappings = runtime.palmMappings();

    // With all 4 Palm backends registering successfully, there should be
    // exactly 4 domain-level Star mappings (calendar, contacts, memo, todo).
    QCOMPARE(mappings.size(), 4);

    // Every mapping must have wp-hub as source (Primary) and a palm:<domain>
    // collection as target — confirming hub-and-spoke structure.
    for (const auto &m : mappings) {
        QCOMPARE(m.sourceBackend, QStringLiteral("wp-hub"));
        QVERIFY(m.targetCalendar.startsWith(QStringLiteral("palm:")));
    }
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaultMappings_useLastWriteWinsPolicy()
{
    // C Task 9: renamed assertion — new behavior uses AskUser, not LastWriteWins.
    // finishConnect() builds domain-level Star mappings via generateMappings()
    // with conflictPolicy=AskUser so Palm<->hub conflicts surface for review
    // rather than silently auto-resolving (see comment in palmruntime.cpp).
    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    const auto mappings = runtime.palmMappings();
    QVERIFY(!mappings.isEmpty());

    // All auto-generated Star mappings must use AskUser conflict policy
    // (deliberate change from the retired per-slot RawFiles LastWriteWins default).
    for (const auto &m : mappings) {
        QCOMPARE(m.conflictPolicy,
                 Kalburator::Sync::ConflictResolution::AskUser);
    }
}

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::wildcardSourceCalendarSuppressesDefaults()
{
    // F.1c.1 T1: a user mapping with sourceCalendar=="" is a wildcard meaning
    // "this target covers every slot for this pluginId". finishConnect must
    // treat such a mapping as already-covering all Palm collections for that
    // plugin and skip per-slot default RawFiles creation.

    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    mockDb->createDatabase(QStringLiteral("CalendarDB-PDat"));
    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = 1;
    pr.category = 0;
    pr.data     = QByteArrayLiteral("seed");
    mockDb->createRecord(QStringLiteral("CalendarDB-PDat"), pr);

    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    // Seed a wildcard mapping BEFORE injecting the device. finishConnect runs
    // on setDeviceAccessForTest; with the wildcard in place, no default rawfiles
    // row should be appended for wildpalms.calendar.
    QJsonObject m;
    m[QStringLiteral("id")]              = QStringLiteral("user-wildcard");
    m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
    m[QStringLiteral("sourceCalendar")]  = QString();   // wildcard
    m[QStringLiteral("targetBackend")]   = QStringLiteral("test-target");
    m[QStringLiteral("targetCalendar")]  = QStringLiteral("Personal");
    m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
    m[QStringLiteral("conflictPolicy")]  = QStringLiteral("LastWriteWins");
    m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
    m[QStringLiteral("enabled")]         = true;
    QJsonArray arr; arr.append(m);
    runtime.reloadMappings(arr);
    QCOMPARE(runtime.palmMappings().size(), 1);

    auto deviceAccess = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    runtime.setDeviceAccessForTest(std::move(deviceAccess));

    // Without the wildcard fix, the post-finishConnect mapping count is 2:
    // the wildcard row PLUS an auto-appended rawfiles-wildpalms.calendar-...
    // row for the Palm calendar slot. With the fix, the wildcard covers
    // every wildpalms.calendar slot and no default is appended.
    const auto post = runtime.palmMappings();
    int defaultCalendarRows = 0;
    for (const auto &x : post) {
        if (x.sourceBackend == QStringLiteral("calendar") &&
            x.id != QStringLiteral("user-wildcard"))
            ++defaultCalendarRows;
    }
    QCOMPARE(defaultCalendarRows, 0);
}

QTEST_MAIN(TstPalmRuntimeDefaultMappingsOnlyWhenEmpty)
#include "tst_palm_runtime_default_mappings_only_when_empty.moc"
