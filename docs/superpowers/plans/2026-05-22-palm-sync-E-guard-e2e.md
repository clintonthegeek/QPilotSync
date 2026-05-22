# Sub-Project E — Mass-Delete Guard E2E Verification — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the mass-delete guard fires against the original-destruction scenario by writing a WildPalms-level integration test using `MockPalmDatabaseAccess` + `RawFilesBackend` + a test-fixture `MassDeleteGuardPresenter` subclass.

**Architecture:** Test-only. Construct a PalmRuntime over tempdirs (real BaselineStore, real RawFilesBackend, mock PalmDatabaseAccess), seed 20+ matching records on the mock Palm + populate baselines via a first sync, then empty the PC rawfiles dir + run a second sync with a guard fixture registered. Assert the guard's `promptUser` is invoked with the expected counts, AND assert that returning false from the guard preserves the Palm-side records.

**Tech Stack:** Qt6, C++17, libkalburator (`v0.54-mass-delete-guard`), MockPalmDatabaseAccess (existing test util).

**Spec:** `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` §4.5

**Build / test commands:**
- `cmake --build build-dev -j$(nproc)`
- `ctest --test-dir build-dev -R tst_palm_mass_delete_guard_e2e --output-on-failure`

**File inventory:**

New:
- `tests/runtime/tst_palm_mass_delete_guard_e2e.cpp` — the integration test.

Modified:
- `tests/runtime/CMakeLists.txt` — register the new test.

**Dependency:** Plans A + B + C. Without A (hash stability), the second sync's diff classifies everything as conflicts and the guard never reaches `applyBatch`. Without B (canonical deleteRecord) the deletes wouldn't actually apply on the mock either (well — the mock might be more forgiving — but in production this matters). Without C (LWW default), the silent-defer behavior masks the test path. Plan E lands LAST in the dependency chain.

**Cross-repo:** none. The guard interface + threshold logic shipped in libkalburator `v0.54-mass-delete-guard` (current WildPalms pin).

---

## Task 1: Write the E2E test

**Files:**
- Create: `tests/runtime/tst_palm_mass_delete_guard_e2e.cpp`

- [ ] **Step 1: Read the existing palm-runtime test fixture pattern**

```bash
sed -n '1,80p' /home/clinton/dev/WildPalms/tests/runtime/tst_palm_runtime_hotsync.cpp
```

Note: how PalmRuntime is constructed, how MockPalmDatabaseAccess is wired, how the runtime drives a sync, how to wait for completion. This is the canonical pattern; the new test mirrors it.

Also confirm the existing MassDeleteGuardPresenter test seam:

```bash
sed -n '1,80p' /home/clinton/dev/WildPalms/tests/runtime/tst_massdeleteguardpresenter.cpp
```

(The `PresenterFixture` pattern — subclass + override `promptUser` to record calls + return preset values.)

- [ ] **Step 2: Write the test file**

```cpp
// tests/runtime/tst_palm_mass_delete_guard_e2e.cpp
//
// Sub-project E of palm-sync-honesty: end-to-end verification that
// the libkalburator mass-delete guard fires when a sync against a
// fresh WildPalms profile would otherwise destroy >10 Palm records.
//
// Scenario reproduces the original a8f686f data-loss event:
//   1. Fresh profile, fresh baselines.
//   2. Mock Palm seeded with N>=20 contact records.
//   3. First sync populates PC rawfiles + baselines.
//   4. PC rawfiles dir emptied (simulates user rm -rf).
//   5. Second sync: engine sees baselines + Palm side both still
//      have the records but PC is empty → propose N deletes to
//      mirror PC's empty state to Palm.
//   6. Mass-delete guard fires (N > 10 absolute threshold).
//   7. Guard returns false → deletes skipped, Palm records preserved.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "../../src/runtime/palmruntime.h"
#include "../../src/runtime/massdeleteguardpresenter.h"
#include "../../src/palm/sync/mockpalmdatabaseaccess.h"
#include "../../src/palm/sync/palmrecord.h"
#include "../wildpalms_qtest_main.h"

#include <imassdeleteguard.h>

namespace {

class RecordingGuard : public WildPalms::Runtime::MassDeleteGuardPresenter {
public:
    using MassDeleteGuardPresenter::MassDeleteGuardPresenter;

    int     invocations  = 0;
    QString lastMappingId;
    QString lastBackendId;
    int     lastProposed = -1;
    int     lastBaseline = -1;
    bool    nextAnswer   = false;  // default: deny

protected:
    bool promptUser(const QString &mappingId,
                    const QString &targetBackendId,
                    int proposedDeletes,
                    int baselineCount) override {
        ++invocations;
        lastMappingId = mappingId;
        lastBackendId = targetBackendId;
        lastProposed  = proposedDeletes;
        lastBaseline  = baselineCount;
        return nextAnswer;
    }
};

void seedContacts(WildPalms::PalmSync::MockPalmDatabaseAccess *device, int count)
{
    for (int i = 1; i <= count; ++i) {
        WildPalms::PalmSync::PalmRecord pr;
        pr.recordId = static_cast<std::uint32_t>(1000 + i);
        pr.category = 0;
        pr.data     = QStringLiteral("contact %1").arg(i).toUtf8();
        device->writeRecord(QStringLiteral("AddressDB"), pr);
    }
}

} // namespace

class TstPalmMassDeleteGuardE2E : public QObject
{
    Q_OBJECT
private slots:
    void guardFiresAgainstPCEmptiedScenario();
    void guardDenyPreservesPalmRecords();
};

void TstPalmMassDeleteGuardE2E::guardFiresAgainstPCEmptiedScenario()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    auto device = std::make_shared<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    seedContacts(device.get(), 20);

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    // Register a fixture guard that records calls and DENIES deletes.
    RecordingGuard guard(nullptr);
    guard.nextAnswer = false;
    runtime.setMassDeleteGuard(&guard);

    // Drive connect → first sync. Adapt to whatever helper the
    // existing palm-runtime tests use (likely a fake KPilotLink
    // wrapping the mock device, then runtime.connectDevice + wait).
    // Read tst_palm_runtime_hotsync.cpp for the canonical pattern
    // and replicate it here. The first sync must complete cleanly
    // and populate baselines for default-contacts-palm_contact_0.

    // After first sync: PC rawfiles dir should contain 20 contact files.
    const QString pcDir = tmp.path()
        + QStringLiteral("/rawfiles/contacts/palm_contact_0");
    QDir(pcDir).removeRecursively();   // simulate user rm -rf
    QDir().mkpath(pcDir);              // dir must exist or RawFilesBackend
                                       // sees "no collection"

    // Reset guard counters before the second sync.
    guard.invocations = 0;

    // Drive second sync. Same pattern as first.

    // Assert guard was invoked exactly once for the contacts mapping
    // and asked to confirm 20 deletes against the 20-record baseline.
    QCOMPARE(guard.invocations, 1);
    QCOMPARE(guard.lastMappingId,
             QStringLiteral("default-contacts-palm_contact_0"));
    QCOMPARE(guard.lastProposed, 20);
    QCOMPARE(guard.lastBaseline, 20);
}

void TstPalmMassDeleteGuardE2E::guardDenyPreservesPalmRecords()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    auto device = std::make_shared<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    seedContacts(device.get(), 20);

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    RecordingGuard guard(nullptr);
    guard.nextAnswer = false;  // deny
    runtime.setMassDeleteGuard(&guard);

    // First sync, then rm -rf, then second sync — see slot above for
    // the pattern.

    // After second sync: because guard denied, no Palm-side deletes
    // happened. Mock device still has 20 contacts.
    const auto remaining = device->readAllRecords(QStringLiteral("AddressDB"));
    QCOMPARE(remaining.size(), 20);
}

WILDPALMS_QTEST_MAIN(TstPalmMassDeleteGuardE2E)
#include "tst_palm_mass_delete_guard_e2e.moc"
```

**Note on fixture replication:** the slots above leave "Drive connect → first sync" and "Drive second sync" as conceptual placeholders because the exact helper to drive a PalmRuntime sync depends on the existing test utilities. Read `tst_palm_runtime_hotsync.cpp` and copy its `connectDevice` + `runtime.hotSync()` + `QSignalSpy waitForFinished` pattern into both slots before committing. If `MockPalmDatabaseAccess` isn't enough on its own to drive PalmRuntime (it needs a KPilotLink-shaped wrapper), use whatever helper class the existing test does (`MockKPilotLink` or similar — present in tests).

- [ ] **Step 3: Register the test in CMakeLists**

In `tests/runtime/CMakeLists.txt`, after the existing `tst_massdeleteguardpresenter` block, add:

```cmake
add_executable(tst_palm_mass_delete_guard_e2e
    tst_palm_mass_delete_guard_e2e.cpp)
target_link_libraries(tst_palm_mass_delete_guard_e2e
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        KF6::I18n
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_mass_delete_guard_e2e
         COMMAND tst_palm_mass_delete_guard_e2e)
set_tests_properties(tst_palm_mass_delete_guard_e2e PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

Match the link list of `tst_palm_runtime_hotsync` if the above doesn't link cleanly — it's the most-similar existing test target.

- [ ] **Step 4: Build the test (expect compile success, runtime probably needs fixture-filling first)**

```bash
cmake --build build-dev --target tst_palm_mass_delete_guard_e2e 2>&1 | tail -10
```

Expected: success IF the fixture placeholders are filled in. If still placeholder-shaped, the compile succeeds but the test fails at runtime with `runtime.connectDevice` either not existing or not being called.

- [ ] **Step 5: Run the test**

```bash
ctest --test-dir build-dev -R tst_palm_mass_delete_guard_e2e --output-on-failure 2>&1 | tail -25
```

Expected: both slots PASS. Paste per-test PASS output.

If the test FAILS with "guard.invocations == 0" — that means Sub-projects A/B/C aren't fully landed (the engine isn't reaching applyBatch with deletes). Verify by reading the engine debug output (consider re-adding the temporary `[MDG-DIAG]` qDebug lines from the earlier investigation if needed — but those should be reverted from a clean libkalburator).

If the test FAILS with "remaining.size() != 20" — that means the guard wasn't actually consulted (deletes happened). Inspect the guard wiring.

- [ ] **Step 6: Commit**

```bash
git add tests/runtime/tst_palm_mass_delete_guard_e2e.cpp \
    tests/runtime/CMakeLists.txt
git commit -m "test: mass-delete guard E2E verification against original-destruction scenario

End-to-end integration test using MockPalmDatabaseAccess +
PalmRuntime + RecordingGuard fixture (subclass of
MassDeleteGuardPresenter with promptUser override). Seeds 20 Palm
contact records, runs first sync to populate baselines + PC files,
deletes the PC files, runs second sync, asserts that the guard's
promptUser is invoked with proposed=20 baseline=20 and that
guard-denial preserves the Palm-side records.

Closes sub-project E of the palm-sync-honesty design. With sub-
projects A+B+C landed, the original a8f686f data-loss scenario
is now provably gated by the mass-delete guard."
```

---

## Task 2: Full regression check

**Files:** none modified

- [ ] **Step 1: Full ctest**

```bash
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 80+ tests pass. New `tst_palm_mass_delete_guard_e2e` adds 2 cases. Cumulative count after all five sub-projects (A+B+C+D+E) should be ~88 tests.

- [ ] **Step 2: Spec coverage cross-check**

Grep the spec's success criteria (§8 of the design doc) against the test suite:

```bash
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | grep -E "tst_palmrecord_contenthash|tst_palmcalendarbackend|tst_memoblobbackend|tst_palm_runtime_default_mappings|tst_kf6mainwindow_conflict_badge|tst_palm_mass_delete_guard_e2e"
```

Expected: all six listed tests pass (one per sub-project A-E plus the contenthash test that's listed separately).

---

## Verification checklist

- [ ] `tst_palm_mass_delete_guard_e2e` has 2 passing test slots.
- [ ] The fixture uses real `PalmRuntime` + real `BaselineStore` + real `RawFilesBackend` + mock Palm device + `MassDeleteGuardPresenter` fixture subclass.
- [ ] Guard's `promptUser` invoked exactly once for the contacts mapping with proposed=20, baseline=20.
- [ ] Guard-deny preserves the 20 Palm records.
- [ ] Full ctest baseline holds.
- [ ] Spec §4.5 requirements satisfied.

**Sub-project series complete.** With all five plans (A→B→C→D→E) landed:
- Palm record hashes are stable across reads.
- All four sync plugins can canonically delete Palm records.
- Default auto-mappings use LastWriteWins instead of silently deferring.
- AskUser conflicts (per-mapping opt-in) surface via status-bar badge + ConflictReviewDialog.
- Mass-delete guard provably fires against the original-destruction scenario.

**Phase F.2 (real IConflictPresenter) satisfied.** Update the integration plan (`docs/plans/2026-04-20-libkalburator-integration.md`) to mark F.2 ✅ after all five plans land.
