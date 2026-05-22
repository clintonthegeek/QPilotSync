# Mass-Delete Guard — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a generic mass-delete safety gate in libkalburator that pauses the sync and asks a consumer-supplied callback before propagating a bulk delete (threshold: >10 absolute OR >25% of baseline per mapping); wire WildPalms's KF6 UI to surface a QMessageBox when the gate fires.

**Architecture:** New pure-virtual `Kalburator::Sync::IMassDeleteGuard` interface (one method, returns bool). `SyncEngine` gains a setter and a worker-thread call site inside the write-apply pipeline. WildPalms ships a concrete `KF6MainWindow`-owned implementation that marshals to the GUI thread (via `Qt::BlockingQueuedConnection`) and pops a `QMessageBox::question`. When the guard returns false, the engine drops the delete list for that mapping this round and proceeds with the creates/updates; next sync re-proposes the same deletes.

**Tech Stack:** Qt6, KF6, C++17, libkalburator + WildPalms via FetchContent.

**Reference issue:** the F.1b followup commit `a8f686f` (rawfiles path alignment) caused 84 Palm contacts to be deleted on a fresh test profile because the engine silently propagated "PC dir empty but baseline has N records" as N delete operations against the Palm. Calendar (582) and memo were "safe" only because `PalmCalendarBackend::deleteRecord` and `PalmMemoBackend::deleteRecord` use a non-canonical dbName decode that the device rejects (silent no-op). Fixing those bugs would increase blast radius — hence the guard lands first.

**Cross-repo workflow:**

Both repos live locally. Build configurations:
- libkalburator standalone: `~/dev/libkalburator/build-dev/`
- WildPalms with libkalburator pinned from Codeberg: default
- WildPalms with libkalburator from local checkout: configure with `-DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator`
- PlanStan: `~/dev/PlanStan/build/` (gates libkalburator commits)

**PlanStan baseline (per memory):** 86 pass / 26 fail / 112 total. Every libkalburator commit must hold this. Any new failure blocks the commit.

**Files inventory:**

libkalburator (new):
- `src/engine/imassdeleteguard.h` — pure-virtual interface (one method)
- `tests/engine/tst_mass_delete_guard.cpp` — engine integration test

libkalburator (modified):
- `src/engine/syncengine.h` — `setMassDeleteGuard` setter + member
- `src/engine/syncengine.cpp` — threshold check inside `applyBatch` lambda; call guard if exceeded
- `src/CMakeLists.txt` — install the new header
- `tests/engine/CMakeLists.txt` — register the new test

WildPalms (new):
- `src/runtime/massdeleteguardpresenter.h` / `.cpp` — concrete `IMassDeleteGuard` for KF6
- `tests/runtime/tst_massdeleteguardpresenter.cpp` — covers the threshold/skip semantics

WildPalms (modified):
- `src/kf6/kf6mainwindow.cpp` — construct + register the presenter with PalmRuntime's engine
- `src/runtime/palmruntime.h` / `.cpp` — accept a guard via setter and forward to the embedded SyncEngine
- `src/CMakeLists.txt` — register the new presenter files
- `tests/runtime/CMakeLists.txt` — register the new test
- `CMakeLists.txt` — once libkalburator tag is cut, bump `WILDPALMS_LIBKALBURATOR_GIT_TAG`

---

## Task 1: libkalburator — IMassDeleteGuard interface header

**Files:**
- Create: `~/dev/libkalburator/src/engine/imassdeleteguard.h`

- [ ] **Step 1: Write the header**

```cpp
// ~/dev/libkalburator/src/engine/imassdeleteguard.h
#ifndef KALBURATOR_SYNC_IMASSDELETEGUARD_H
#define KALBURATOR_SYNC_IMASSDELETEGUARD_H

#include <QString>

namespace Kalburator::Sync {

/// Synchronous gate for bulk-delete operations during sync.
///
/// SyncEngine consults the registered guard before allowing a single
/// mapping's write phase to propagate a large number of deletes to its
/// target backend. The library trips the gate when the proposed delete
/// list exceeds either of two thresholds (per mapping, per sync):
///   - absolute: more than 10 deletes; OR
///   - relative: more than 25% of the mapping's current baseline count.
///
/// If no guard is registered, deletes proceed unconditionally (backward
/// compatible with consumers that don't opt in).
///
/// If the guard returns false, the engine drops the delete list for that
/// mapping this round and proceeds with creates/updates only. Baselines
/// are unchanged; the next sync re-proposes the same deletes.
///
/// Threading: the engine calls `confirmMassDelete` from a worker thread.
/// Concrete implementations that need to interact with a GUI must
/// marshal to the UI thread themselves (Qt::BlockingQueuedConnection
/// or equivalent).
class IMassDeleteGuard
{
public:
    virtual ~IMassDeleteGuard() = default;

    /// Return true to allow the deletes; false to skip them this round.
    /// @param mappingId       SyncMapping::id (e.g. "default-contacts-palm_contact_0")
    /// @param targetBackendId Backend the deletes would apply to
    /// @param proposedDeletes Number of records the engine wants to delete
    /// @param baselineCount   Number of records in the mapping's baseline
    virtual bool confirmMassDelete(const QString &mappingId,
                                   const QString &targetBackendId,
                                   int proposedDeletes,
                                   int baselineCount) = 0;
};

} // namespace Kalburator::Sync

#endif // KALBURATOR_SYNC_IMASSDELETEGUARD_H
```

- [ ] **Step 2: Install the header**

In `~/dev/libkalburator/src/CMakeLists.txt`, find the existing engine headers install block (search for `iconflictpresenter.h` and look for nearby `install(FILES ...)` calls). Add `engine/imassdeleteguard.h` to the same install set. If you can't find a single install block, add a fresh one:

```cmake
install(FILES engine/imassdeleteguard.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Kalburator/engine)
```

- [ ] **Step 3: Configure + build libkalburator standalone**

```bash
cd ~/dev/libkalburator
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
```

Expected: success. The new header is independent so build is fast.

- [ ] **Step 4: Commit (libkalburator)**

```bash
cd ~/dev/libkalburator
git add src/engine/imassdeleteguard.h src/CMakeLists.txt
git commit -m "engine: add IMassDeleteGuard interface for sync-time bulk-delete gating"
```

This is a header-only change — no behavior, no tests yet. The PlanStan baseline check can be deferred until after Task 3 (where behavior actually changes).

---

## Task 2: libkalburator — SyncEngine setter + member

**Files:**
- Modify: `~/dev/libkalburator/src/engine/syncengine.h`
- Modify: `~/dev/libkalburator/src/engine/syncengine.cpp`

- [ ] **Step 1: Forward-declare + add accessor to header**

In `~/dev/libkalburator/src/engine/syncengine.h`, near the top with other forward declarations (`class ConflictHandlerRegistry;` etc., around line 36), add:

```cpp
class IMassDeleteGuard;
```

In the `public:` section, near where `conflictRegistry()` is exposed (around line 420), add:

```cpp
    /**
     * Register a synchronous gate consulted before mass deletes are
     * propagated during sync. Non-owning; consumer must outlive the
     * SyncEngine. Pass nullptr to clear. See imassdeleteguard.h for
     * threshold semantics. Default: no guard (deletes proceed
     * unconditionally — backward compatible).
     */
    void setMassDeleteGuard(IMassDeleteGuard *guard);
    IMassDeleteGuard *massDeleteGuard() const;
```

In the `private:` member section (where `m_conflictRegistry` lives, around line 731), add:

```cpp
    IMassDeleteGuard *m_massDeleteGuard = nullptr;
```

- [ ] **Step 2: Implement setter + accessor in the .cpp**

In `~/dev/libkalburator/src/engine/syncengine.cpp`, add `#include "imassdeleteguard.h"` near the other engine-local includes at the top. Then add two trivial definitions (place them near the `setConflictHandler` / other public setters — search for `void SyncEngine::set` to find the cluster):

```cpp
void SyncEngine::setMassDeleteGuard(IMassDeleteGuard *guard)
{
    m_massDeleteGuard = guard;
}

IMassDeleteGuard *SyncEngine::massDeleteGuard() const
{
    return m_massDeleteGuard;
}
```

- [ ] **Step 3: Build standalone**

```bash
cd ~/dev/libkalburator
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
```

Expected: success. No behavior change yet.

- [ ] **Step 4: Commit**

```bash
cd ~/dev/libkalburator
git add src/engine/syncengine.h src/engine/syncengine.cpp
git commit -m "engine: SyncEngine — register/expose IMassDeleteGuard"
```

Still header-shape only; no destruction behavior changed. Defer PlanStan check.

---

## Task 3: libkalburator — engine threshold + gate call (FAILING TEST FIRST)

**Files:**
- Create: `~/dev/libkalburator/tests/engine/tst_mass_delete_guard.cpp`
- Modify: `~/dev/libkalburator/tests/engine/CMakeLists.txt`
- Modify: `~/dev/libkalburator/src/engine/syncengine.cpp`

This is the behavioral commit. The test uses a stub `IMassDeleteGuard` + two `MockBlobBackend`s (one as source, one as target) preloaded so that a sync run will propose enough deletes to trip the absolute threshold (>10). It also seeds baselines so the threshold logic has real numbers to compare against. The guard is wired in, asserted to be called with expected arguments, and the test verifies that returning false causes the deletes to NOT apply while creates/updates still do.

- [ ] **Step 1: Read the existing engine test setup to learn the fixture style**

```bash
sed -n '1,60p' ~/dev/libkalburator/tests/engine/tst_engine_silent_success_guard.cpp 2>&1 | tail -60
```

Note the include pattern, the use of `MockBlobBackend` (in `~/dev/libkalburator/src/blob/mockblobbackend.h`), `BackendController`/`SyncEngine` setup, and the test-runner macro (likely `QTEST_MAIN` or a libkalburator-specific equivalent — match it exactly).

- [ ] **Step 2: Write the failing test**

```cpp
// ~/dev/libkalburator/tests/engine/tst_mass_delete_guard.cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "engine/syncengine.h"
#include "engine/imassdeleteguard.h"
#include "blob/mockblobbackend.h"
#include "sync/backendcontroller.h"   // adjust if the include path differs
#include "sync/syncmapping.h"          // adjust if the include path differs
#include "storage/baselinestore.h"
#include "blob/backendrecord.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Storage;

namespace {

class StubGuard : public IMassDeleteGuard {
public:
    int  invocations = 0;
    QString lastMappingId;
    QString lastTargetBackend;
    int     lastProposed = -1;
    int     lastBaseline = -1;
    bool    nextReturn   = true;

    bool confirmMassDelete(const QString &mappingId,
                           const QString &targetBackendId,
                           int proposedDeletes,
                           int baselineCount) override {
        ++invocations;
        lastMappingId     = mappingId;
        lastTargetBackend = targetBackendId;
        lastProposed      = proposedDeletes;
        lastBaseline      = baselineCount;
        return nextReturn;
    }
};

} // namespace

class TstMassDeleteGuard : public QObject
{
    Q_OBJECT
private slots:
    void guardNotCalledBelowThreshold();
    void guardCalledAboveAbsoluteThreshold();
    void guardCalledAboveRelativeThreshold();
    void guardAllowProceedsWithDeletes();
    void guardDenySkipsDeletesKeepsCreatesUpdates();
    void noGuardRegisteredAllowsAllDeletes();
};

// Helper: build a SyncEngine wired to src/tgt MockBlobBackends + a
// SyncMapping in TwoWay mode + an in-memory BaselineStore. The fixture
// preloads PC-side baselines so the test can simulate "PC dir lost N
// records since last sync."
//
// The exact wiring depends on the engine's public surface — read
// tst_engine_silent_success_guard.cpp for the canonical pattern (it
// already sets up controllers, mappings, mocks, and baselines).
//
// Each test below builds its own fixture so tests are independent.

void TstMassDeleteGuard::guardNotCalledBelowThreshold()
{
    // Setup: baseline has 5 records on target; source still has the same
    // 5 + nothing else; so the diff yields 0 deletes. Guard must NOT be
    // called for delete-zero scenarios.
    // [Test body using the engine fixture pattern from
    //  tst_engine_silent_success_guard.cpp — see Step 1.]
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

void TstMassDeleteGuard::guardCalledAboveAbsoluteThreshold()
{
    // Setup: baseline has 20 records on target; source has 0 records;
    // diff yields 20 deletes — exceeds absolute threshold (>10) → guard
    // fires with proposed=20, baseline=20.
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

void TstMassDeleteGuard::guardCalledAboveRelativeThreshold()
{
    // Setup: baseline has 30 records; source has 21; diff yields 9
    // deletes. 9 < 10 absolute, but 9/30 = 30% > 25% relative → guard
    // fires.
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

void TstMassDeleteGuard::guardAllowProceedsWithDeletes()
{
    // Setup: high deletes; guard.nextReturn = true. Verify target
    // backend's records reflect the deletes after sync completes.
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

void TstMassDeleteGuard::guardDenySkipsDeletesKeepsCreatesUpdates()
{
    // Setup: high deletes AND a few creates. Guard.nextReturn = false.
    // After sync: target's existing records are unchanged (deletes
    // skipped) but the new creates were applied.
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

void TstMassDeleteGuard::noGuardRegisteredAllowsAllDeletes()
{
    // Setup: same as guardAboveAbsoluteThreshold but engine.setMassDeleteGuard(nullptr).
    // Verify target records are deleted (backward-compatible default).
    QSKIP("Implementation deferred to Step 4 once fixture pattern confirmed.");
}

QTEST_MAIN(TstMassDeleteGuard)
#include "tst_mass_delete_guard.moc"
```

- [ ] **Step 3: Register the test**

In `~/dev/libkalburator/tests/engine/CMakeLists.txt`, add an entry following the same pattern as `tst_engine_silent_success_guard` (which is the closest existing analogue). Look for that block and add a parallel one for `tst_mass_delete_guard.cpp`. If the existing block uses a helper function (e.g. `kalburator_add_engine_test(...)`), use the same helper.

- [ ] **Step 4: Fill in the test bodies using the canonical fixture pattern**

Use the fixture from `tst_engine_silent_success_guard.cpp` as the template (it's the closest analogue). Replace the test file's `QSKIP`d slots and the standalone fixture struct with this canonical setup. Edit the file to look like:

```cpp
// ~/dev/libkalburator/tests/engine/tst_mass_delete_guard.cpp
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimeZone>

#include <KCalendarCore/Event>

#include "backendregistry.h"
#include "baselinestore.h"
#include "conflictmanager.h"
#include "domainoperationsregistry.h"
#include "domainregistry.h"
#include "engine/imassdeleteguard.h"
#include "mockbackend.h"
#include "pluginmanager.h"
#include "stock_plugins.h"
#include "syncengine.h"
#include "syncconflictstore.h"
#include "synctypes.h"
#include "transformationregistry.h"

#include "stubsynchost.h"

using namespace Kalburator::Sync;
using namespace Kalburator::Sync::Test;

namespace {

constexpr auto kSourceBackendId = "source-mock";
constexpr auto kTargetBackendId = "target-mock";
constexpr auto kCollectionId    = "stub-collection";
constexpr auto kCalendarId      = "calendar-1";
constexpr auto kMappingId       = "mapping-1";
constexpr int  kSyncTimeoutMs   = 30000;

SyncMapping makeTwoWayMapping()
{
    SyncMapping m;
    m.id             = QString::fromLatin1(kMappingId);
    m.sourceBackend  = QString::fromLatin1(kSourceBackendId);
    m.sourceCalendar = QString::fromLatin1(kCalendarId);
    m.targetBackend  = QString::fromLatin1(kTargetBackendId);
    m.targetCalendar = QString::fromLatin1(kCalendarId);
    m.mode           = SyncMode::TwoWay;
    m.conflictPolicy = ConflictResolution::SourceWins;
    m.enabled        = true;
    return m;
}

class StubGuard : public IMassDeleteGuard {
public:
    int  invocations = 0;
    QString lastMappingId;
    QString lastTargetBackend;
    int     lastProposed = -1;
    int     lastBaseline = -1;
    bool    nextReturn   = true;

    bool confirmMassDelete(const QString &mappingId,
                           const QString &targetBackendId,
                           int proposedDeletes,
                           int baselineCount) override {
        ++invocations;
        lastMappingId     = mappingId;
        lastTargetBackend = targetBackendId;
        lastProposed      = proposedDeletes;
        lastBaseline      = baselineCount;
        return nextReturn;
    }
};

} // namespace

class TstMassDeleteGuard : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        Kalburator::Sync::BackendRegistry pmRegistry;
        Kalburator::PluginManager pm(&pmRegistry);
        Kalburator::registerStockPlugins(pm);
    }
    void cleanupTestCase() {
        Kalburator::Shape::TransformationRegistry::instance().clear();
        Kalburator::Shape::DomainRegistry::instance().clear();
        Kalburator::Shape::DomainOperationsRegistry::instance().clear();
    }
    void init();
    void cleanup();

    void guardNotCalledBelowThreshold();
    void guardCalledAboveAbsoluteThreshold();
    void guardCalledAboveRelativeThreshold();
    void guardDenySkipsDeletesKeepsCreatesUpdates();
    void noGuardRegisteredAllowsAllDeletes();

private:
    void seedEvents(MockBackend *backend, int count);
    bool runOneSync();

    std::unique_ptr<QTemporaryDir>         m_tmpDir;
    std::unique_ptr<BackendRegistry>       m_registry;
    std::unique_ptr<MockBackend>           m_source;
    std::unique_ptr<MockBackend>           m_target;
    std::unique_ptr<StubSyncHost>          m_host;
    std::unique_ptr<Kalburator::Storage::BaselineStore> m_baselines;
    std::unique_ptr<SyncConflictStore>     m_conflictStore;
    std::unique_ptr<ConflictManager>       m_conflictManager;
    std::unique_ptr<SyncEngine>            m_engine;
    StubGuard                              m_guard;
    SyncResult                             m_lastResult;
};

void TstMassDeleteGuard::init()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());

    m_registry = std::make_unique<BackendRegistry>();
    m_source   = std::make_unique<MockBackend>();
    m_target   = std::make_unique<MockBackend>();
    m_registry->registerBackendInstance(QString::fromLatin1(kSourceBackendId), m_source.get());
    m_registry->registerBackendInstance(QString::fromLatin1(kTargetBackendId), m_target.get());

    m_source->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));
    m_target->createCalendar(QString::fromLatin1(kCollectionId),
                             QString::fromLatin1(kCalendarId),
                             QStringLiteral("Calendar 1"));

    m_host = std::make_unique<StubSyncHost>(m_registry.get());
    auto *hostCal = new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone());
    hostCal->setId(QString::fromLatin1(kCalendarId));
    m_host->stubCollection()->addCalendarWithId(QString::fromLatin1(kCalendarId), hostCal);

    const QString dbPath = m_tmpDir->filePath(QStringLiteral(".sync.db"));
    m_baselines     = std::make_unique<Kalburator::Storage::BaselineStore>(dbPath);
    m_conflictStore = std::make_unique<SyncConflictStore>(dbPath);

    m_conflictManager = std::make_unique<ConflictManager>();
    m_conflictManager->setSyncConflictStore(m_conflictStore.get());

    m_engine = std::make_unique<SyncEngine>(m_registry.get(), m_host.get());
    m_engine->setBaselineStore(m_baselines.get());
    m_engine->setSyncConflictStore(m_conflictStore.get());
    m_engine->setConflictManager(m_conflictManager.get());
    m_engine->setCollection(m_host->stubCollection());

    m_guard = StubGuard{};
    m_lastResult = SyncResult{};
}

void TstMassDeleteGuard::cleanup()
{
    m_engine.reset();
    m_conflictManager.reset();
    m_conflictStore.reset();
    m_baselines.reset();
    m_host.reset();
    m_target.reset();
    m_source.reset();
    m_registry.reset();
    m_tmpDir.reset();
}

void TstMassDeleteGuard::seedEvents(MockBackend *backend, int count)
{
    for (int i = 1; i <= count; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("Event %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        backend->addIncidence(QString::fromLatin1(kCalendarId), e);
    }
}

bool TstMassDeleteGuard::runOneSync()
{
    auto future = m_engine->runSyncFuture(SyncEngine::SyncBehavior::Unmonitored);
    if (!QTest::qWaitFor([&] { return future.isFinished(); }, kSyncTimeoutMs)) return false;
    if (future.isCanceled()) return false;
    const auto results = future.resultAt(0);
    if (results.isEmpty()) return false;
    m_lastResult = results.last();
    return true;
}

// ---- Tests --------------------------------------------------------------

void TstMassDeleteGuard::guardNotCalledBelowThreshold()
{
    // Seed both sides with 5 events; first sync establishes baseline.
    seedEvents(m_source.get(), 5);
    seedEvents(m_target.get(), 5);
    m_engine->setMassDeleteGuard(&m_guard);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    // No deletes proposed: guard not called.
    QCOMPARE(m_guard.invocations, 0);
}

void TstMassDeleteGuard::guardCalledAboveAbsoluteThreshold()
{
    // First sync: seed 20 events both sides, establish baseline of 20.
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    // Second sync: remove all source events. Engine proposes 20 deletes
    // against target. 20 > 10 absolute → guard fires.
    for (int i = 1; i <= 20; ++i)
        m_source->removeIncidence(QString::fromLatin1(kCalendarId),
                                  QStringLiteral("uid-%1").arg(i));
    m_guard.nextReturn = true;
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    QCOMPARE(m_guard.lastMappingId, QString::fromLatin1(kMappingId));
    QCOMPARE(m_guard.lastTargetBackend, QString::fromLatin1(kTargetBackendId));
    QCOMPARE(m_guard.lastProposed, 20);
    QCOMPARE(m_guard.lastBaseline, 20);
}

void TstMassDeleteGuard::guardCalledAboveRelativeThreshold()
{
    // First sync: seed 30 both sides, baseline = 30.
    seedEvents(m_source.get(), 30);
    seedEvents(m_target.get(), 30);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    // Second sync: remove 9 source events. 9 < 10 absolute, but
    // 9/30 = 30% > 25% relative → guard fires.
    for (int i = 1; i <= 9; ++i)
        m_source->removeIncidence(QString::fromLatin1(kCalendarId),
                                  QStringLiteral("uid-%1").arg(i));
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    QCOMPARE(m_guard.lastProposed, 9);
    QCOMPARE(m_guard.lastBaseline, 30);
}

void TstMassDeleteGuard::guardDenySkipsDeletesKeepsCreatesUpdates()
{
    // First sync: 20 both sides.
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    // Second sync: remove all 20 source events AND add 2 new source events.
    for (int i = 1; i <= 20; ++i)
        m_source->removeIncidence(QString::fromLatin1(kCalendarId),
                                  QStringLiteral("uid-%1").arg(i));
    for (int i = 100; i < 102; ++i) {
        auto e = KCalendarCore::Event::Ptr(new KCalendarCore::Event());
        e->setUid(QStringLiteral("uid-%1").arg(i));
        e->setSummary(QStringLiteral("New %1").arg(i));
        e->setDtStart(QDateTime::currentDateTimeUtc());
        m_source->addIncidence(QString::fromLatin1(kCalendarId), e);
    }
    m_guard.nextReturn = false;
    m_engine->setMassDeleteGuard(&m_guard);
    QVERIFY(runOneSync());
    QCOMPARE(m_guard.invocations, 1);
    // Target still has the original 20 (deletes skipped)
    // and gained 2 new events from source (creates applied).
    const auto tgtUids = m_target->allUids(QString::fromLatin1(kCalendarId));
    QCOMPARE(tgtUids.size(), 22);
}

void TstMassDeleteGuard::noGuardRegisteredAllowsAllDeletes()
{
    seedEvents(m_source.get(), 20);
    seedEvents(m_target.get(), 20);
    m_engine->setSyncMappings({makeTwoWayMapping()});
    QVERIFY(runOneSync());
    for (int i = 1; i <= 20; ++i)
        m_source->removeIncidence(QString::fromLatin1(kCalendarId),
                                  QStringLiteral("uid-%1").arg(i));
    // No setMassDeleteGuard call — default behaviour: deletes proceed.
    QVERIFY(runOneSync());
    QCOMPARE(m_target->allUids(QString::fromLatin1(kCalendarId)).size(), 0);
}
```

Notes on quirks:
- The `MockBackend::removeIncidence(calId, uid)` and `MockBackend::allUids(calId)` calls used above match the existing `tst_engine_silent_success_guard.cpp` usage. If `removeIncidence` doesn't exist by that name, search `mockbackend.h` for the equivalent (`deleteIncidence`, `remove`, etc.) and adjust.
- `makeTwoWayMapping()` uses `SyncMode::TwoWay`. If `SyncMode::TwoWay` doesn't exist, check `synctypes.h` for the canonical name (`Bidirectional`, etc.) and update.
- The threshold test for "below" needs zero proposed deletes; if the engine's first sync establishes baselines that DON'T match the test's expectations (e.g. baseline-harvest is delayed), one extra sync may be needed to settle. Add a second `runOneSync()` if needed.

- [ ] **Step 5: Build + run, confirm tests FAIL**

```bash
cd ~/dev/libkalburator
cmake --build build-dev --target tst_mass_delete_guard 2>&1 | tail -10
ctest --test-dir build-dev -R tst_mass_delete_guard --output-on-failure 2>&1 | tail -30
```

Expected: 4 of the 6 tests FAIL (the ones that actually check guard.invocations > 0 or that deletes are skipped). The "no guard registered" + "below threshold" tests should pass against the current code.

If ALL 6 pass, the test fixture isn't actually exercising the delete path — re-examine the setup.

- [ ] **Step 6: Add the production guard call inside applyBatch**

In `~/dev/libkalburator/src/engine/syncengine.cpp`, find the `applyBatch` lambda (around line 2456-2508 — searches: `auto applyBatch`). The lambda's body has two branches based on `writer->threading()`; both call `classifyForWriter(toWrite, blobBackend, colId, ...)` to populate a `WriterBatch` and then `writer->apply(colId, batch.creates, batch.updates, batch.deletes)`.

Refactor so the guard check happens AFTER classify and BEFORE apply, in both branches:

Replace the WorkerThread branch's `writer->apply(...)` call with a call to a new local helper that consults the guard. Same for the BackendThread branch. Concretely, add this helper above the `applyBatch` lambda (or inline-lambda inside it):

```cpp
auto applyWithGuard = [this, &writer = writer, &mappingId = request.mapping.id,
                      &targetBackendIdRef]
    (WriterBatch &batch, const QString &colId, const QString &backendId)
{
    // Mass-delete guard: fire if proposed deletes exceed
    // absolute (>10) OR relative (>25% of baseline) threshold.
    if (!batch.deletes.isEmpty() && m_engine && m_engine->massDeleteGuard()) {
        const int proposed = batch.deletes.size();
        int baselineCount = 0;
        if (m_baselineStore) {
            baselineCount = m_baselineStore
                ->baselinesForMappingV3(mappingId).size();
        }
        const bool overAbs = proposed > 10;
        const bool overRel = baselineCount > 0
            && (proposed * 100 / baselineCount) > 25;
        if (overAbs || overRel) {
            const bool allow = m_engine->massDeleteGuard()
                ->confirmMassDelete(mappingId, backendId, proposed, baselineCount);
            if (!allow) {
                qDebug() << "SyncEngineWorker: mass-delete gate denied"
                         << proposed << "deletes for mapping" << mappingId;
                batch.deletes.clear();
            }
        }
    }
    return writer->apply(colId, batch.creates, batch.updates, batch.deletes);
};
```

Note: this helper is placed inside the lexical scope of `applyBatch`'s outer function, so it captures `m_engine`, `m_baselineStore`, and `request.mapping.id` correctly. The `targetBackendId` for the per-call argument is the result of `backend->backendId()` (or the equivalent — search the engine for an existing `backendId()` getter on `SyncBackend`).

Then in both branches of `applyBatch`:

WorkerThread branch (current line ~2480-2490):
```cpp
WriterBatch batch;
QString classifyErr1;
QMetaObject::invokeMethod(backend, [blobBackend, colId, &batch, &classifyErr1, toWrite]() {
    batch = classifyForWriter(toWrite, blobBackend, colId, &classifyErr1);
}, Qt::BlockingQueuedConnection);
if (!classifyErr1.isEmpty()) {
    ok = false;
    writeError = classifyErr1;
} else {
    // CHANGED: route through guard
    ok = applyWithGuard(batch, colId, backend->backendId());
}
```

BackendThread branch (current ~2491-2502):
```cpp
QString classifyErr2;
QMetaObject::invokeMethod(backend, [&writer, &applyWithGuard, blobBackend, colId, toWrite, &ok, &classifyErr2, backend]() {
    WriterBatch batch = classifyForWriter(toWrite, blobBackend, colId, &classifyErr2);
    if (classifyErr2.isEmpty())
        ok = applyWithGuard(batch, colId, backend->backendId());
}, Qt::BlockingQueuedConnection);
```

(If `SyncBackend` doesn't have a `backendId()` getter, fall back to a constructor-captured map of `colId → backendId` from the surrounding scope. Read the surrounding ~40 lines of syncengine.cpp to find the canonical accessor.)

- [ ] **Step 7: Build + run the tests — all should PASS now**

```bash
cd ~/dev/libkalburator
cmake --build build-dev --target tst_mass_delete_guard 2>&1 | tail -10
ctest --test-dir build-dev -R tst_mass_delete_guard --output-on-failure 2>&1 | tail -30
```

Expected: all 6 PASS. Paste the per-test output.

- [ ] **Step 8: Run the libkalburator FULL ctest to confirm no regressions**

```bash
cd ~/dev/libkalburator
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: no NEW failures relative to the prior baseline. Note any pre-existing failures separately.

- [ ] **Step 9: PlanStan baseline gate**

```bash
cmake -S ~/dev/PlanStan -B ~/dev/PlanStan/build -DPLANSTAN_DEV_BUILD=ON 2>&1 | tail -5
cmake --build ~/dev/PlanStan/build -j$(nproc) 2>&1 | tail -5
cd ~/dev/PlanStan/build && WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland ctest -j$(nproc) 2>&1 | tail -10
```

Required: result must be `86 pass / 26 fail / 112 total` or better (no NEW failures). If a new failure appears, DO NOT commit — diagnose first.

- [ ] **Step 10: Commit the test + engine change together**

```bash
cd ~/dev/libkalburator
git add src/engine/syncengine.cpp tests/engine/tst_mass_delete_guard.cpp tests/engine/CMakeLists.txt
git commit -m "engine: gate mass deletes through IMassDeleteGuard threshold

Trip the guard when a single mapping's proposed deletes exceed 10
absolute OR 25% of baseline. If the guard returns false, drop the
deletes from this round's apply batch and proceed with creates/updates;
next sync re-proposes the same deletes (baseline unchanged). If no
guard is registered, deletes proceed unconditionally (backward
compatible). Verified against the WildPalms data-loss reproduction
where 84 contacts would have been deleted from a Palm without a
prompt; tests cover threshold, allow, deny, and no-guard scenarios.
PlanStan baseline holds (86/26/112)."
```

---

## Task 4: libkalburator — tag + push

**Files:** none modified

- [ ] **Step 1: Tag the new version**

Pick the next version per the existing tag pattern (current pin is `v0.53-phase-q1-no-singleton`):

```bash
cd ~/dev/libkalburator
git tag -a v0.54-mass-delete-guard -m "engine: IMassDeleteGuard interface + threshold gate"
```

- [ ] **Step 2: Push commits + tag**

```bash
cd ~/dev/libkalburator
git push origin main
git push origin v0.54-mass-delete-guard
```

Expected: both push successfully to `codeberg.org:clintonthegeek/libkalburator.git`.

---

## Task 5: WildPalms — switch to local libkalburator for development

**Files:** none modified (just a reconfigure)

- [ ] **Step 1: Reconfigure WildPalms's build-dev to point at the local libkalburator checkout**

```bash
cd /home/clinton/dev/WildPalms
cmake -S . -B build-dev -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$HOME/dev/libkalburator 2>&1 | tail -10
```

Expected: "libkalburator: using local source at /home/clinton/dev/libkalburator" in the configure output.

- [ ] **Step 2: Rebuild**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev -j$(nproc) 2>&1 | tail -5
```

Expected: success. The new `imassdeleteguard.h` is available to WildPalms.

- [ ] **Step 3: Run the existing WildPalms ctest suite to confirm clean baseline**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 80/80 pass (matches the F.1b baseline).

No commit yet — this is local dev configuration.

---

## Task 6: WildPalms — MassDeleteGuardPresenter — failing tests

**Files:**
- Create: `/home/clinton/dev/WildPalms/tests/runtime/tst_massdeleteguardpresenter.cpp`
- Modify: `/home/clinton/dev/WildPalms/tests/runtime/CMakeLists.txt`

The presenter is the WildPalms-side concrete `IMassDeleteGuard`. It implements the gate by popping a `QMessageBox::question` on the GUI thread. For testing, we need to make the prompt overridable so tests can preset the answer without spawning a real dialog.

Pattern: a `protected: virtual bool promptUser(...)` seam (like F.1b's `confirmForgetProfile` seam) lets the test override the prompt and supply a preset response.

- [ ] **Step 1: Write the test file**

```cpp
// /home/clinton/dev/WildPalms/tests/runtime/tst_massdeleteguardpresenter.cpp
#include <QtTest/QtTest>

#include "../../src/runtime/massdeleteguardpresenter.h"
#include "../wildpalms_qtest_main.h"

class TstMassDeleteGuardPresenter : public QObject
{
    Q_OBJECT
private slots:
    void confirmMassDeleteRoutesThroughPromptUser();
    void confirmReturnsPromptResult();
    void promptArgsCarryAllFields();
};

namespace {

class PresenterFixture : public MassDeleteGuardPresenter {
public:
    using MassDeleteGuardPresenter::MassDeleteGuardPresenter;

    int    invocations = 0;
    QString lastMapping;
    QString lastBackend;
    int    lastProposed = -1;
    int    lastBaseline = -1;
    bool   nextAnswer   = true;

protected:
    bool promptUser(const QString &mappingId,
                    const QString &targetBackendId,
                    int proposedDeletes,
                    int baselineCount) override {
        ++invocations;
        lastMapping  = mappingId;
        lastBackend  = targetBackendId;
        lastProposed = proposedDeletes;
        lastBaseline = baselineCount;
        return nextAnswer;
    }
};

} // namespace

void TstMassDeleteGuardPresenter::confirmMassDeleteRoutesThroughPromptUser()
{
    PresenterFixture p(nullptr);
    p.nextAnswer = true;
    const bool result = p.confirmMassDelete(
        QStringLiteral("default-contacts-palm_contact_0"),
        QStringLiteral("rawfiles-contacts-palm_contact_0"),
        84, 84);
    QCOMPARE(p.invocations, 1);
    QVERIFY(result);
}

void TstMassDeleteGuardPresenter::confirmReturnsPromptResult()
{
    PresenterFixture p(nullptr);
    p.nextAnswer = false;
    const bool result = p.confirmMassDelete(
        QStringLiteral("X"), QStringLiteral("Y"), 50, 100);
    QVERIFY(!result);
}

void TstMassDeleteGuardPresenter::promptArgsCarryAllFields()
{
    PresenterFixture p(nullptr);
    p.confirmMassDelete(
        QStringLiteral("MAP"),
        QStringLiteral("BACK"),
        12, 40);
    QCOMPARE(p.lastMapping,  QStringLiteral("MAP"));
    QCOMPARE(p.lastBackend,  QStringLiteral("BACK"));
    QCOMPARE(p.lastProposed, 12);
    QCOMPARE(p.lastBaseline, 40);
}

WILDPALMS_QTEST_MAIN(TstMassDeleteGuardPresenter)
#include "tst_massdeleteguardpresenter.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

In `/home/clinton/dev/WildPalms/tests/runtime/CMakeLists.txt`, after the `tst_profilepropertiesdialog_rename` block, add:

```cmake
add_executable(tst_massdeleteguardpresenter tst_massdeleteguardpresenter.cpp)
target_link_libraries(tst_massdeleteguardpresenter
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::I18n
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
)
add_test(NAME tst_massdeleteguardpresenter
         COMMAND tst_massdeleteguardpresenter)
set_tests_properties(tst_massdeleteguardpresenter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Build, expect compile error**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_massdeleteguardpresenter 2>&1 | tail -15
```

Expected: compile error — `massdeleteguardpresenter.h` not found. Red phase as designed.

- [ ] **Step 4: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add tests/runtime/tst_massdeleteguardpresenter.cpp tests/runtime/CMakeLists.txt
git commit -m "test: failing tests for MassDeleteGuardPresenter (red phase)"
```

---

## Task 7: WildPalms — MassDeleteGuardPresenter implementation

**Files:**
- Create: `/home/clinton/dev/WildPalms/src/runtime/massdeleteguardpresenter.h`
- Create: `/home/clinton/dev/WildPalms/src/runtime/massdeleteguardpresenter.cpp`
- Modify: `/home/clinton/dev/WildPalms/src/CMakeLists.txt`

- [ ] **Step 1: Write the header**

```cpp
// /home/clinton/dev/WildPalms/src/runtime/massdeleteguardpresenter.h
#ifndef WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H
#define WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H

#include <QObject>
#include <QString>

#include "engine/imassdeleteguard.h"

class QWidget;

namespace WildPalms::Runtime {

/// IMassDeleteGuard that surfaces a QMessageBox::question on the GUI
/// thread. SyncEngine calls confirmMassDelete from a worker thread; the
/// presenter marshals via Qt::BlockingQueuedConnection to its parent
/// window before showing the prompt, so the dialog lands on the right
/// thread and the worker waits for the user's answer.
class MassDeleteGuardPresenter
    : public QObject
    , public Kalburator::Sync::IMassDeleteGuard
{
    Q_OBJECT
public:
    explicit MassDeleteGuardPresenter(QWidget *parent);
    ~MassDeleteGuardPresenter() override;

    /// IMassDeleteGuard
    bool confirmMassDelete(const QString &mappingId,
                           const QString &targetBackendId,
                           int proposedDeletes,
                           int baselineCount) override;

protected:
    /// Test seam: invoked on the GUI thread. Production override pops
    /// a QMessageBox; tests override to return a preset value.
    virtual bool promptUser(const QString &mappingId,
                            const QString &targetBackendId,
                            int proposedDeletes,
                            int baselineCount);

private:
    QWidget *m_parentWidget;
};

} // namespace WildPalms::Runtime

#endif // WILDPALMS_RUNTIME_MASSDELETEGUARDPRESENTER_H
```

- [ ] **Step 2: Write the implementation**

```cpp
// /home/clinton/dev/WildPalms/src/runtime/massdeleteguardpresenter.cpp
#include "massdeleteguardpresenter.h"

#include <KLocalizedString>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>

namespace WildPalms::Runtime {

MassDeleteGuardPresenter::MassDeleteGuardPresenter(QWidget *parent)
    : QObject(parent)
    , m_parentWidget(parent)
{
}

MassDeleteGuardPresenter::~MassDeleteGuardPresenter() = default;

bool MassDeleteGuardPresenter::confirmMassDelete(
    const QString &mappingId,
    const QString &targetBackendId,
    int proposedDeletes,
    int baselineCount)
{
    bool result = false;

    // Marshal to the GUI thread. If we're already on it (e.g. unit
    // tests), call promptUser directly.
    if (QThread::currentThread() == this->thread()) {
        result = promptUser(mappingId, targetBackendId,
                            proposedDeletes, baselineCount);
    } else {
        QMetaObject::invokeMethod(this,
            [&]() {
                result = promptUser(mappingId, targetBackendId,
                                    proposedDeletes, baselineCount);
            },
            Qt::BlockingQueuedConnection);
    }
    return result;
}

bool MassDeleteGuardPresenter::promptUser(
    const QString &mappingId,
    const QString &targetBackendId,
    int proposedDeletes,
    int baselineCount)
{
    Q_UNUSED(mappingId);
    const QString text = i18n(
        "WildPalms is about to delete %1 records from <b>%2</b>.\n\n"
        "The baseline for this mapping has %3 records, and the sync "
        "engine has concluded the PC side wants those records removed. "
        "If this is unexpected (for example you moved or deleted files "
        "outside WildPalms), choose <b>No</b> to skip the deletes "
        "this round.",
        proposedDeletes, targetBackendId, baselineCount);

    QMessageBox box(m_parentWidget);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(i18n("Confirm Mass Delete"));
    box.setText(text);
    box.setTextFormat(Qt::RichText);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

} // namespace WildPalms::Runtime
```

- [ ] **Step 3: Register source files**

In `/home/clinton/dev/WildPalms/src/CMakeLists.txt`, find the existing `runtime/...` source entries (around lines 80-90; search for `runtime/profileregistry.cpp` to anchor) and add:

```cmake
    runtime/massdeleteguardpresenter.cpp
    runtime/massdeleteguardpresenter.h
```

- [ ] **Step 4: Build + run the presenter test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_massdeleteguardpresenter 2>&1 | tail -10
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R tst_massdeleteguardpresenter --output-on-failure
```

Expected: 3 cases PASS. Paste the per-test output.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/runtime/massdeleteguardpresenter.h src/runtime/massdeleteguardpresenter.cpp src/CMakeLists.txt
git commit -m "runtime: MassDeleteGuardPresenter — QMessageBox-backed IMassDeleteGuard

Concrete IMassDeleteGuard that pops a Qt warning dialog on the GUI
thread when the engine trips its mass-delete threshold. Worker-thread
marshalling is handled via Qt::BlockingQueuedConnection so the sync
waits on the user. A protected promptUser() seam keeps the prompt
overridable from tests."
```

---

## Task 8: WildPalms — PalmRuntime + KF6MainWindow wiring

**Files:**
- Modify: `/home/clinton/dev/WildPalms/src/runtime/palmruntime.h`
- Modify: `/home/clinton/dev/WildPalms/src/runtime/palmruntime.cpp`
- Modify: `/home/clinton/dev/WildPalms/src/kf6/kf6mainwindow.h`
- Modify: `/home/clinton/dev/WildPalms/src/kf6/kf6mainwindow.cpp`

`PalmRuntime` owns the `SyncEngine`. KF6MainWindow owns the `PalmRuntime`. The presenter is owned by KF6MainWindow (lifetime matches the window). KF6MainWindow constructs the presenter in its ctor (or in `loadProfile`) and forwards it to PalmRuntime via a setter; PalmRuntime forwards to the engine.

- [ ] **Step 1: Add the setter to PalmRuntime's header**

In `/home/clinton/dev/WildPalms/src/runtime/palmruntime.h`, add a forward declaration near the top (with other `class` decls):

```cpp
namespace Kalburator::Sync { class IMassDeleteGuard; }
```

In the `public:` section, after the existing setters (search for `setConflictHandler`), add:

```cpp
    /// Forward to the embedded SyncEngine. Non-owning; consumer must
    /// outlive the runtime. nullptr clears.
    void setMassDeleteGuard(Kalburator::Sync::IMassDeleteGuard *guard);
```

- [ ] **Step 2: Implement the setter in .cpp**

In `/home/clinton/dev/WildPalms/src/runtime/palmruntime.cpp`, add `#include <imassdeleteguard.h>` near the other libkalburator includes (next to `#include <baselinestore.h>`). Then add the setter implementation (place it near other `PalmRuntime::set*` methods — `setConflictHandler` is a natural neighbor):

```cpp
void PalmRuntime::setMassDeleteGuard(Kalburator::Sync::IMassDeleteGuard *guard)
{
    if (m_syncEngine) {
        m_syncEngine->setMassDeleteGuard(guard);
    }
}
```

If the engine isn't constructed by the time someone calls the setter (unlikely but possible — depends on ctor order), the call is a no-op. If you find that `m_syncEngine` is constructed later than the setter would be invoked, also cache the pointer in `m_pendingMassDeleteGuard` and apply it during engine construction. Check the existing ctor + lazy-engine patterns; mirror them.

- [ ] **Step 3: Construct + register the presenter in KF6MainWindow**

In `/home/clinton/dev/WildPalms/src/kf6/kf6mainwindow.h`, add a forward declaration:

```cpp
namespace WildPalms::Runtime { class MassDeleteGuardPresenter; }
```

In the `private:` member section, add (next to `m_profileMenuController`):

```cpp
    std::unique_ptr<WildPalms::Runtime::MassDeleteGuardPresenter> m_massDeleteGuard;
```

In `/home/clinton/dev/WildPalms/src/kf6/kf6mainwindow.cpp`, add `#include "../runtime/massdeleteguardpresenter.h"` near the other runtime-namespace includes.

In the ctor — after `m_profileMenuController` is constructed — add:

```cpp
    m_massDeleteGuard = std::make_unique<WildPalms::Runtime::MassDeleteGuardPresenter>(this);
```

In `KF6MainWindow::loadProfile`, AFTER `m_palmRuntime` is constructed (search for `m_palmRuntime = std::make_unique<WildPalms::Runtime::PalmRuntime>` around line ~509), add:

```cpp
    m_palmRuntime->setMassDeleteGuard(m_massDeleteGuard.get());
```

This wires the presenter to the engine for every freshly-loaded profile.

- [ ] **Step 4: Build the full project**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev -j$(nproc) 2>&1 | tail -10
```

Expected: success.

- [ ] **Step 5: Run the full WildPalms ctest suite**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 81/81 pass (80 existing + 1 new presenter test).

- [ ] **Step 6: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
    src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "runtime+kf6: wire MassDeleteGuardPresenter into PalmRuntime's engine

KF6MainWindow constructs the presenter once and registers it with
PalmRuntime on every loadProfile call, so all sync sessions on the
active profile flow through the gate. PalmRuntime forwards the
registration to its embedded SyncEngine."
```

---

## Task 9: WildPalms — manual reproduction of the F.1b data-loss scenario

**Files:** none modified

The data loss in commit `a8f686f` was caused by deleting `<profile>/.state/rawfiles` while baselines persisted. Reproduce that exact scenario; verify the guard now fires.

- [ ] **Step 1: Delete the orphaned `.state/rawfiles` again to recreate the trigger**

```bash
rm -rf ~/.wildpalms/profile1/.state/rawfiles
ls -la ~/.wildpalms/profile1/
```

Confirm the directory is gone but `~/.wildpalms/profile1/.state/.wildpalms-blob-baselines.db` is still present (the baselines from before are still there — 684 entries across calendar/contacts/todos).

- [ ] **Step 2: Launch WildPalms and trigger a HotSync**

```bash
/home/clinton/dev/WildPalms/build-dev/wildpalms 2>&1 | tee /tmp/wp-guard-verify.log
```

Plug in the Palm, hit HotSync. Watch for a dialog titled "Confirm Mass Delete" warning about contacts (84 records). Click **No**.

- [ ] **Step 3: Verify the log shows the gate fired**

```bash
grep -E "mass-delete gate|Confirm Mass Delete|deleteRecord" /tmp/wp-guard-verify.log | head -20
```

Expected: `SyncEngineWorker: mass-delete gate denied 84 deletes for mapping default-contacts-palm_contact_0`. Expected `deleteRecord()` count should be 0 (the deletes were dropped).

- [ ] **Step 4: Verify the Palm side is intact**

Manually verify on the device that all 84 contacts that would have been deleted are still present.

This task produces no commit — it's pre-merge verification.

---

## Task 10: WildPalms — bump libkalburator pin

**Files:**
- Modify: `/home/clinton/dev/WildPalms/CMakeLists.txt`

Once libkalburator's tag `v0.54-mass-delete-guard` is pushed (Task 4), update the FetchContent pin and rebuild from the remote to confirm the integration works without `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`.

- [ ] **Step 1: Edit the pin**

In `/home/clinton/dev/WildPalms/CMakeLists.txt`, around line 62, change:

```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.53-phase-q1-no-singleton" CACHE STRING
```

to:

```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.54-mass-delete-guard" CACHE STRING
```

- [ ] **Step 2: Reconfigure without the local-source override + rebuild**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev -UWILDPALMS_LIBKALBURATOR_SOURCE_DIR 2>&1 | tail -10
cmake --build /home/clinton/dev/WildPalms/build-dev -j$(nproc) 2>&1 | tail -10
```

Expected: `libkalburator: fetching v0.54-mass-delete-guard from Codeberg` followed by a clean build.

- [ ] **Step 3: Run the test suite against the fetched tag**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 81/81 pass.

- [ ] **Step 4: Commit the pin bump**

```bash
cd /home/clinton/dev/WildPalms
git add CMakeLists.txt
git commit -m "build: bump libkalburator pin to v0.54-mass-delete-guard"
```

---

## Task 11: Docs — note the guard + cross-reference the data-loss incident

**Files:**
- Modify: `/home/clinton/dev/WildPalms/docs/DATA_LOSS_HANDLING.md`

There's an existing `DATA_LOSS_HANDLING.md`. Add a section recording this incident + the guard.

- [ ] **Step 1: Open and read the existing file**

```bash
cat /home/clinton/dev/WildPalms/docs/DATA_LOSS_HANDLING.md | head -60
```

Match the existing structure (heading style, sections).

- [ ] **Step 2: Append a new section**

Append to the bottom of the file:

```markdown
## 2026-05-22 — Mass-delete guard

**Incident:** While fixing the F.1b rawfiles path mismatch (commit
`a8f686f`), a follow-up instruction to `rm -rf <profile>/.state/rawfiles`
caused the next HotSync to propagate 84 ghost-deletes against the
Palm device. The baselines DB recorded 684 PC-side records that no
longer had files on disk; the engine concluded the user deleted them
on the PC side and asked the Palm to mirror.

Only contacts actually deleted (84 records). Calendar (582) and memo
were "safe" only because `PalmCalendarBackend::deleteRecord` and
`PalmMemoBackend::deleteRecord` use a non-canonical dbName decode that
the device rejects (silent no-op). Fixing those bugs would expand the
blast radius.

**Mitigation:** `IMassDeleteGuard` in libkalburator. SyncEngine
consults the registered guard before propagating bulk deletes when
the per-mapping threshold (>10 absolute OR >25%% of baseline) is
exceeded. WildPalms's `MassDeleteGuardPresenter` pops a QMessageBox
warning. If the user declines, the deletes are dropped for this round
(baselines unchanged; next sync re-proposes the same deletes).

**Follow-up:** the calendar/memo silent-delete bug remains. It must
be fixed in a separate change set so the broken-by-accident safety
net is removed in a controlled way (the guard now provides the real
safety net).

**References:**
- Plan: `docs/superpowers/plans/2026-05-22-mass-delete-guard.md`
- libkalburator: `src/engine/imassdeleteguard.h`,
  `src/engine/syncengine.cpp` (`applyBatch` lambda)
- WildPalms: `src/runtime/massdeleteguardpresenter.{h,cpp}`
```

- [ ] **Step 3: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add docs/DATA_LOSS_HANDLING.md
git commit -m "docs: record 2026-05-22 mass-delete incident + IMassDeleteGuard mitigation"
```

---

## Verification checklist

After all tasks complete:

- [ ] libkalburator: `ctest --test-dir build-dev` includes a passing `tst_mass_delete_guard`.
- [ ] PlanStan baseline (86/26/112) holds against the updated libkalburator.
- [ ] WildPalms: `ctest --test-dir build-dev` is green (81/81 — adds `tst_massdeleteguardpresenter`).
- [ ] Manual repro (Task 9) shows the gate firing and zero Palm-side deletes when the user clicks No.
- [ ] `git log --oneline` in both repos shows the planned task series; tags are pushed.
- [ ] Note in `DATA_LOSS_HANDLING.md` links the plan + the affected source files.
