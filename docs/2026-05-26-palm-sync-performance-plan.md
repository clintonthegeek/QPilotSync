# Palm Sync Performance & Correctness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make real-device Palm↔CalDAV sync fast and reliable by batching Palm DB writes, keeping the device link alive during network phases, and removing two redundant-work bugs.

**Architecture:** Four independent fixes. #1 caches the Palm DB write handle in the device layer (the only hot path). #2 scopes the keep-alive tickle to Palm-DLP phases. #3 fixes a Settings-dialog construction-order bug. #4 is investigate-then-fix for a premature auto-sync. All Palm DLP is serialized on one "link thread," so device-layer state is race-free.

**Tech Stack:** C++17, Qt6, KDE Frameworks 6, pilot-link (libpisock), libkalburator (FetchContent / sibling at `../libkalburator`), QtTest.

**Spec:** `docs/2026-05-26-palm-sync-performance-design.md` — read it first.

**Setup:** Cut a branch from `main`: `git checkout -b feature/palm-sync-perf`. Build dir `build-dev` (uses the sibling libkalburator via `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`). Build a target: `cmake --build build-dev --target <t> -j"$(nproc)"`. Run a test: `ctest --test-dir build-dev -R <t> --output-on-failure`. Baseline before starting: full `ctest --test-dir build-dev` should be 101/101.

---

## Key references (verified against source)

- Hot path: `src/palm/device/pilotlinkpalmdatabaseaccess.{h,cpp}` — `createRecord`/`updateRecord`/`deleteRecord` each build a `DbScope` (`open` on ctor `:11`, `close` on dtor `:19`). Reads: `readAllRecords:55`, `readRecord:72`, `readAppBlock:151`, `recordsModifiedSince:123`, `availableDatabases:36`, `hasDatabase:42`.
- `KPilotLink` abstract base (`src/palm/kpilotlink.h:14`): `int openDatabase(QString,bool)`, `bool closeDatabase(int)`, `bool writeRecord(int,PilotRecord*)`, `bool deleteRecord(int,int)`, `QList<PilotRecord*> readAllRecords(int)`, `bool endSync()`, `void closeConnection()`, `bool isConnected() const`, `pauseTickle()/resumeTickle()` (default no-ops).
- Device interface: `src/palm/sync/ipalmdatabaseaccess.h` (all methods listed there).
- Test harness already exists: `tests/palmdevice/` with `mockkpilotlink.{h,cpp}` (a full `KPilotLink` fake with `seedDatabase/seedRecord/hasRecord/recordData`), `tst_pilotlinkpalmdatabaseaccess.cpp` (`QTEST_MAIN`, `#include "...moc"`), and a CMake helper `add_palm_device_test(NAME srcs...)` linking `Qt::Core Qt::Test WildPalmsPalmDevice WildPalmsCore pisock bluetooth usb`.
- Bridge: `toPilotRecord(PalmRecord)` / `fromPilotRecord(PilotRecord)` (`src/palm/.../palmrecord_bridge.cpp:5`). `PilotRecord::recordId()` returns the assigned id after a write.
- Tickle: `src/runtime/palmtickle.h` (`PalmTickle(KPilotLink*,int,QObject*)`, `start/stop/setInterval`, signal `connectionLost`). `PalmDeviceAccess::pauseTickle()/resumeTickle()` (`palmdeviceaccess.h:89-92`, thread-safe, auto-marshal to link thread). Current blanket pause: `palmruntime.cpp:683` (pause) / inside the `runFinished` lambda `:749-750` (resume); same pattern in `runMirror` (~`:790`/`:833`).
- Engine signals (`libkalburator/src/engine/syncengine.h`): `syncStarted(QString)`, `phaseChanged(SyncPhase)`, `SyncPhase{Idle,FetchingSource,FetchingTarget,Processing,Complete}`. PalmRuntime already connects `syncStarted`/`progressUpdated`/`fetchProgress`/`writeProgress` at `palmruntime.cpp:142-162`.
- Settings double-build: `src/settingsdialog.cpp:569` `buildAccountsAndMappingsPagesIfReady()`, called from `setAccountController():549` and `setPalmRuntime():555`. `AccountsPage` ctor `(AccountController*, PalmRuntime*, QWidget*)` (`accountspage.h:22`); its `buildUi()` does `connect(m_palmRuntime, &PalmRuntime::runStarted/runFinished, …)` at `accountspage.cpp:67-72`.
- AccountController: `loadAndConnect()` calls `m_providerManager->connectAll()` at `accountcontroller.cpp:82`; `addProvider()` calls it again at `:155`. `ProviderManager::connectAll()` returns `QFuture<void>` (`providermanager.h:58`); per-provider state in `m_providerStates` (`:90`), enum `ProviderConnectionState{Disconnected,Connecting,Connected,Error}` (`:24`).
- Auto-sync: `KF6MainWindow::onReadyForSync()` fires `onHotSync()` when `m_currentProfile->autoSyncOnConnect()`. `PalmRuntime::finishConnect()` calls `loadMappingsFromProfile():283`, generates rawfiles defaults `:355-430`, emits `readyForSync():435`. `Profile::syncMappingsJson()` (`profile.h:282`), `autoSyncOnConnect()` (`:240`).

---

## File structure

- **Modify** `src/palm/device/pilotlinkpalmdatabaseaccess.{h,cpp}` — cached write handle (P1).
- **Modify** `src/palm/sync/ipalmdatabaseaccess.h` — add `flushPendingWrites()` hook (P1).
- **Modify** `src/runtime/palmdeviceaccess.{h,cpp}` — `flushWrites()` marshaller (P1); phase-scoped tickle helper (P2).
- **Modify** `src/runtime/palmruntime.cpp` — call `flushWrites()` at run end (P1); replace blanket tickle pause with phase-scoped (P2).
- **Modify** `tests/palmdevice/mockkpilotlink.{h,cpp}` — add open/close/write call counters (P1 test).
- **Create** `tests/palmdevice/tst_pilotlinkbatching.cpp` + register in `tests/palmdevice/CMakeLists.txt` (P1).
- **Modify** `src/settingsdialog.cpp` — guard `AccountsPage` on both pointers (P3).
- **Modify** `src/app/accounts/accountspage.cpp` — null-guard the runtime connects (P3).
- **Modify** `src/runtime/providermanager.cpp` — idempotent `connectAll()` (P3, after confirmation).
- **Modify** `src/kf6/kf6mainwindow.cpp` and/or `src/runtime/palmruntime.cpp` — auto-sync gate (P4, after investigation).
- **Create** `docs/2026-05-26-tickle-phase-signal-handoff-libkalburator.md` (P2 handoff).

---

# PROBLEM 1 — Batch Palm DB writes (the 4–5 s/event fix)

Independently shippable. Highest impact.

## Task 1.1: Add call counters to MockKPilotLink

**Files:** Modify `tests/palmdevice/mockkpilotlink.h`, `tests/palmdevice/mockkpilotlink.cpp`

- [ ] **Step 1: Add public counters to the mock header**

In `mockkpilotlink.h`, add public members (near the other test helpers):

```cpp
    // Call counters for batching assertions (Problem 1).
    int openDatabaseCalls = 0;
    int closeDatabaseCalls = 0;
    int writeRecordCalls = 0;
```

- [ ] **Step 2: Increment them in the mock implementations**

In `mockkpilotlink.cpp`, at the top of the existing `openDatabase`, `closeDatabase`, and `writeRecord` overrides, increment the matching counter (e.g. `++openDatabaseCalls;` as the first line of `MockKPilotLink::openDatabase(...)`). Do not change existing behavior.

- [ ] **Step 3: Build the existing test to confirm the mock still compiles**

Run: `cmake -S . -B build-dev >/dev/null 2>&1 && cmake --build build-dev --target tst_pilotlinkpalmdatabaseaccess -j"$(nproc)" && ctest --test-dir build-dev -R tst_pilotlinkpalmdatabaseaccess --output-on-failure`
Expected: builds, existing tests still PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/palmdevice/mockkpilotlink.h tests/palmdevice/mockkpilotlink.cpp
git commit -m "test(palm): add call counters to MockKPilotLink for batching assertions"
```

## Task 1.2: Failing test for batched writes

**Files:** Create `tests/palmdevice/tst_pilotlinkbatching.cpp`; Modify `tests/palmdevice/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/palmdevice/tst_pilotlinkbatching.cpp`:

```cpp
#include <QtTest/QtTest>

#include "mockkpilotlink.h"
#include "pilotlinkpalmdatabaseaccess.h"

using WildPalms::PalmDevice::PilotLinkPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPilotLinkBatching : public QObject
{
    Q_OBJECT
private slots:
    void consecutiveWritesOpenOnce();
    void readBetweenWritesFlushes();
    void writeToDifferentDbReopens();
    void flushPendingWritesClosesHandle();
};

static PalmRecord makeRec(const QByteArray &data)
{
    PalmRecord r;
    r.recordId = 0;       // new record
    r.category = 0;
    r.data = data;
    return r;
}

void TestPilotLinkBatching::consecutiveWritesOpenOnce()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    for (int i = 0; i < 50; ++i)
        dev.createRecord(QStringLiteral("DatebookDB"), makeRec(QByteArray::number(i)));

    QCOMPARE(link.writeRecordCalls, 50);
    QCOMPARE(link.openDatabaseCalls, 1);   // opened once, not 50x
    QCOMPARE(link.closeDatabaseCalls, 0);  // still open until flush/read
}

void TestPilotLinkBatching::readBetweenWritesFlushes()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    QCOMPARE(link.openDatabaseCalls, 1);
    QCOMPARE(link.closeDatabaseCalls, 0);

    dev.readAllRecords(QStringLiteral("DatebookDB"));   // read flushes the write handle
    QCOMPARE(link.closeDatabaseCalls, 1);               // write handle closed
    // (read opens its own RO handle; that is allowed to open/close as before)

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("b"));
    QCOMPARE(link.openDatabaseCalls, 3);   // 1 write + 1 read + 1 new write handle
}

void TestPilotLinkBatching::writeToDifferentDbReopens()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    link.seedDatabase(QStringLiteral("MemoDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    dev.createRecord(QStringLiteral("MemoDB"), makeRec("b"));   // different db -> close+open
    QCOMPARE(link.openDatabaseCalls, 2);
    QCOMPARE(link.closeDatabaseCalls, 1);
}

void TestPilotLinkBatching::flushPendingWritesClosesHandle()
{
    MockKPilotLink link;
    link.seedDatabase(QStringLiteral("DatebookDB"));
    PilotLinkPalmDatabaseAccess dev(&link);

    dev.createRecord(QStringLiteral("DatebookDB"), makeRec("a"));
    QCOMPARE(link.closeDatabaseCalls, 0);
    dev.flushPendingWrites();
    QCOMPARE(link.closeDatabaseCalls, 1);
}

QTEST_MAIN(TestPilotLinkBatching)
#include "tst_pilotlinkbatching.moc"
```

- [ ] **Step 2: Register the test**

In `tests/palmdevice/CMakeLists.txt`, after the existing `add_palm_device_test(tst_pilotlinkpalmdatabaseaccess ...)` block, add:

```cmake
add_palm_device_test(tst_pilotlinkbatching
    tst_pilotlinkbatching.cpp
    mockkpilotlink.cpp
)
```

- [ ] **Step 3: Run to confirm it FAILS**

Run: `cmake -S . -B build-dev >/dev/null 2>&1 && cmake --build build-dev --target tst_pilotlinkbatching -j"$(nproc)" && ctest --test-dir build-dev -R tst_pilotlinkbatching --output-on-failure`
Expected: FAIL — `openDatabaseCalls` is 50 (per-record open), `flushPendingWrites` does not exist yet (compile error is acceptable as the "failing" state; if it won't compile because `flushPendingWrites` is undeclared, that is the expected failure to fix in Task 1.3).

## Task 1.3: Implement the cached write handle

**Files:** Modify `src/palm/sync/ipalmdatabaseaccess.h`, `src/palm/device/pilotlinkpalmdatabaseaccess.{h,cpp}`

- [ ] **Step 1: Add the interface hook**

In `src/palm/sync/ipalmdatabaseaccess.h`, add a virtual with a default no-op (so the mock and other impls need no change), just before the closing of the class (after `isConnected()`):

```cpp
    /// Flush any cached write handle (close the open DLP database). Default
    /// no-op. Real DLP impls that batch writes override this; the runtime
    /// calls it at end-of-sync so the final mapping's DB is closed before
    /// dlp_EndOfSync.
    virtual void flushPendingWrites() {}
```

- [ ] **Step 2: Add cached-handle state + helpers to the impl header**

In `src/palm/device/pilotlinkpalmdatabaseaccess.h`, add to the `private:` section (after the `DbScope` class):

```cpp
    // Problem 1: cached write handle to avoid per-record open/close.
    // Single-entry — the engine writes all of one mapping's records to one
    // DB consecutively. Touched only on the link thread (race-free).
    // mutable so const read methods can flush before reading.
    mutable int     m_writeHandle = -1;
    mutable QString m_writeDbName;
    int  ensureWriteHandle(const QString &dbName);  // open+cache or reuse
    void flushWriteHandle() const;                  // close cached handle if open
```

And add the public override (after `readAppBlock`):

```cpp
    void flushPendingWrites() override;
```

- [ ] **Step 3: Implement helpers + rewrite write/read methods**

In `src/palm/device/pilotlinkpalmdatabaseaccess.cpp`:

Add the helpers (e.g. after the constructor):

```cpp
int PilotLinkPalmDatabaseAccess::ensureWriteHandle(const QString &dbName)
{
    if (m_writeHandle >= 0 && m_writeDbName == dbName)
        return m_writeHandle;
    flushWriteHandle();
    if (!m_link) return -1;
    const int h = m_link->openDatabase(dbName, /*rw=*/true);
    if (h >= 0) {
        m_writeHandle = h;
        m_writeDbName = dbName;
    }
    return h;
}

void PilotLinkPalmDatabaseAccess::flushWriteHandle() const
{
    if (m_link && m_writeHandle >= 0)
        m_link->closeDatabase(m_writeHandle);
    m_writeHandle = -1;
    m_writeDbName.clear();
}

void PilotLinkPalmDatabaseAccess::flushPendingWrites()
{
    flushWriteHandle();
}
```

Replace `createRecord` (was `:87`):

```cpp
std::uint32_t PilotLinkPalmDatabaseAccess::createRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return 0;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return 0;
    PilotRecord bridged = toPilotRecord(record);
    if (!m_link->writeRecord(handle, &bridged)) return 0;
    return static_cast<std::uint32_t>(bridged.recordId());
}
```

Replace `updateRecord` (was `:100`):

```cpp
bool PilotLinkPalmDatabaseAccess::updateRecord(
    const QString &dbName,
    const WildPalms::PalmSync::PalmRecord &record)
{
    if (!m_link) return false;
    if (record.recordId == 0) return false;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return false;
    PilotRecord bridged = toPilotRecord(record);
    return m_link->writeRecord(handle, &bridged);
}
```

Replace `deleteRecord` (was `:113`):

```cpp
bool PilotLinkPalmDatabaseAccess::deleteRecord(const QString &dbName,
                                               std::uint32_t recordId)
{
    if (!m_link) return false;
    const int handle = ensureWriteHandle(dbName);
    if (handle < 0) return false;
    return m_link->deleteRecord(handle, static_cast<int>(recordId));
}
```

In each read/list method (`readAllRecords:55`, `readRecord:72`, `readAppBlock:151`, `recordsModifiedSince:123`, `availableDatabases:36`, `hasDatabase:42`), add `flushWriteHandle();` as the FIRST statement after the `if (!m_link) return …;` guard, so a read always sees committed data and the write handle does not collide with a read handle. Example for `readAllRecords`:

```cpp
QList<WildPalms::PalmSync::PalmRecord>
PilotLinkPalmDatabaseAccess::readAllRecords(const QString &dbName) const
{
    if (!m_link) return {};
    flushWriteHandle();                       // <-- added
    DbScope scope(m_link, dbName, /*rw=*/false);
    ...
}
```

Add a destructor flush as a safety net. Change the header's `~PilotLinkPalmDatabaseAccess() override = default;` to a declared destructor, and implement:

```cpp
PilotLinkPalmDatabaseAccess::~PilotLinkPalmDatabaseAccess()
{
    flushWriteHandle();
}
```

- [ ] **Step 4: Run the batching test — expect PASS**

Run: `cmake --build build-dev --target tst_pilotlinkbatching tst_pilotlinkpalmdatabaseaccess -j"$(nproc)" && ctest --test-dir build-dev -R "tst_pilotlinkbatching|tst_pilotlinkpalmdatabaseaccess" --output-on-failure`
Expected: both PASS (batching test green; existing access test still green — reads still open/close their own RO handle).

- [ ] **Step 5: Commit**

```bash
git add src/palm/sync/ipalmdatabaseaccess.h src/palm/device/pilotlinkpalmdatabaseaccess.h \
        src/palm/device/pilotlinkpalmdatabaseaccess.cpp \
        tests/palmdevice/tst_pilotlinkbatching.cpp tests/palmdevice/CMakeLists.txt
git commit -m "perf(palm): cache DB write handle to batch record writes (was open/close per record)"
```

## Task 1.4: Flush the final mapping's handle at end-of-sync

**Files:** Modify `src/runtime/palmdeviceaccess.{h,cpp}`, `src/runtime/palmruntime.cpp`

Rationale: flush-on-read closes the handle at the *next* mapping's first read, but the LAST mapping has no following read. Close it explicitly when the run ends, before `dlp_EndOfSync`.

- [ ] **Step 1: Add `flushWrites()` to PalmDeviceAccess**

In `src/runtime/palmdeviceaccess.h`, near `pauseTickle()/resumeTickle()` (`:89`):

```cpp
    /// Flush the device DB write handle (close it). Marshals to the link
    /// thread. Call at end-of-sync so the last mapping's DB is closed before
    /// dlp_EndOfSync.
    void flushWrites();
```

In `src/runtime/palmdeviceaccess.cpp`, mirror the existing marshalling pattern used by `readAllRecords` (`:313-319`):

```cpp
void PalmDeviceAccess::flushWrites()
{
    if (!m_impl) return;
    QMetaObject::invokeMethod(m_implOwner,
        [this]() { m_impl->flushPendingWrites(); },
        Qt::BlockingQueuedConnection);
}
```

- [ ] **Step 2: Call it when the run finishes**

In `src/runtime/palmruntime.cpp`, in the `runFinished` continuation lambda (the one that calls `m_device->resumeTickle()`, ~`:749`), add the flush immediately before `resumeTickle()`:

```cpp
    QMetaObject::invokeMethod(this, [this, r]() {
        if (m_device) m_device->flushWrites();     // <-- added: close last mapping's DB
        if (m_device) m_device->resumeTickle();
        m_activeMappingId.clear();
        Q_EMIT runFinished(r);
    });
```

Do the same in `runMirror`'s equivalent teardown (~`:833`).

- [ ] **Step 3: Build the full app + run the suite**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5 && ctest --test-dir build-dev --output-on-failure 2>&1 | tail -8`
Expected: builds clean, all tests pass (101 + the new batching test).

- [ ] **Step 4: Commit**

```bash
git add src/runtime/palmdeviceaccess.h src/runtime/palmdeviceaccess.cpp src/runtime/palmruntime.cpp
git commit -m "perf(palm): flush cached write handle at end-of-sync"
```

**Device verification (P1):** connect a Palm with calendar changes, sync; the log should show **one** `openDatabase("DatebookDB", rw)` / `dlp_OpenDB` for the whole write burst instead of one per record, and per-event latency should drop dramatically.

---

# PROBLEM 3 — Settings double-build + nullptr connects (do before P4; clears log noise)

## Task 3.1: Gate AccountsPage on both controllers (fixes the nullptr connects)

**Files:** Modify `src/settingsdialog.cpp`, `src/app/accounts/accountspage.cpp`

- [ ] **Step 1: Require both pointers before building AccountsPage**

In `src/settingsdialog.cpp` `buildAccountsAndMappingsPagesIfReady()` (`:569`), change the AccountsPage guard from `if (m_accountController && !m_accountsPage)` to require the runtime too:

```cpp
    // Accounts page needs BOTH the controller and the runtime (it connects to
    // PalmRuntime signals in buildUi). Requiring both avoids constructing it
    // with a null runtime (which produced connect(nullptr,...) warnings and a
    // page that never received run signals).
    if (m_accountController && m_palmRuntime && !m_accountsPage) {
        m_accountsPage = new WildPalms::App::Accounts::AccountsPage(
            m_accountController, m_palmRuntime, this);
        auto *item = new KPageWidgetItem(m_accountsPage, i18n("Accounts"));
        item->setIcon(QIcon::fromTheme(QStringLiteral("network-server")));
        addPage(item);
    }
```

- [ ] **Step 2: Defensive null-guard in AccountsPage**

In `src/app/accounts/accountspage.cpp` `buildUi()` (`:67-72`), wrap the two `PalmRuntime` connects so a null runtime can never `connect`:

```cpp
    if (m_palmRuntime) {
        QObject::connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runStarted,
                         this, &AccountsPage::onPalmRunStarted);
        QObject::connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runFinished,
                         this, &AccountsPage::onPalmRunFinished);
    }
```

- [ ] **Step 3: Build + run; manually confirm no nullptr warning**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5 && ctest --test-dir build-dev --output-on-failure 2>&1 | tail -5`
Expected: builds, tests pass. (The `connect(...AccountsPage): invalid nullptr` warnings will be gone at runtime; verify on the next device run.)

- [ ] **Step 4: Commit**

```bash
git add src/settingsdialog.cpp src/app/accounts/accountspage.cpp
git commit -m "fix(settings): build AccountsPage only when PalmRuntime is set (drop nullptr connects)"
```

## Task 3.2: Confirm, then dedupe, the double CalDAV discovery

**Files:** Modify `src/runtime/providermanager.cpp` (after confirmation)

- [ ] **Step 1: Instrument the two connectAll() sites**

Add a temporary `qInfo()` at each `m_providerManager->connectAll()` call site (`accountcontroller.cpp:82` in `loadAndConnect`, and `:155` in `addProvider`) tagging which one fired, e.g. `qInfo() << "[connectAll] from loadAndConnect";`. Build, run a device (or app start with a configured CalDAV account), and read the log to confirm which path fires twice and whether both run a full discovery.

- [ ] **Step 2: Make connectAll() idempotent**

In `ProviderManager::connectAll()` (`src/runtime/providermanager.cpp`), skip providers already `Connecting` or `Connected` per `m_providerStates` so a second `connectAll()` does not re-run discovery on an already-handled provider:

```cpp
    for (auto &entry : m_ownedBackends) {
        const QString id = QString::fromStdString(entry.first);
        const auto st = m_providerStates.value(id, ProviderConnectionState::Disconnected);
        if (st == ProviderConnectionState::Connecting
            || st == ProviderConnectionState::Connected)
            continue;   // already handled — don't rediscover
        // ... existing per-provider connect/discovery ...
    }
```

(Adapt to the real loop shape you find. If `m_providerStates` is not populated until after connect completes, instead set `Connecting` synchronously when you kick off discovery so the guard works on the immediate second call.)

- [ ] **Step 3: Remove the temporary logging; build + test**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -5 && ctest --test-dir build-dev --output-on-failure 2>&1 | tail -5`

- [ ] **Step 4: Commit**

```bash
git add src/runtime/providermanager.cpp src/runtime/accountcontroller.cpp
git commit -m "fix(accounts): make ProviderManager::connectAll idempotent (no double CalDAV discovery)"
```

**Device verification (P3):** one device run shows exactly one "discovered N calendars" block and one `registerCalendarUrl` per calendar, and no nullptr-connect warnings.

---

# PROBLEM 4 — Sync runs twice (rawfiles, then DAV) — investigate then fix

## Task 4.1: Determine whether DAV mappings are persisted to the profile

**Files:** none (diagnostic)

- [ ] **Step 1: Inspect the on-disk profile**

The profile lives under `~/.wildpalms/profile1/`. Find its profile JSON and inspect the `syncMappingsJson` array:

Run: `cat ~/.wildpalms/profile1/*.json 2>/dev/null | python3 -m json.tool | grep -A3 -i mapping | head -40` (adjust the filename to the actual profile file).

Determine: at app start / before the first connect, does the persisted mapping list contain the **DAV (CalDAV) mappings** (UUID ids → CalDAV calendars), or only Palm/rawfiles entries (or nothing)?

- [ ] **Step 2: Add a temporary log and reproduce**

Add `qInfo() << "[finishConnect] loaded mappings:" << m_mappings.size();` right after `loadMappingsFromProfile();` in `finishConnect()` (`palmruntime.cpp:283`) and one in `onReadyForSync()` showing whether auto-sync fires. Run a device sync and capture: at the FIRST `finishConnect`, how many mappings were loaded, and whether `onHotSync` fired before DAV mappings existed.

- [ ] **Step 3: Record the finding in the plan/spec and pick the branch**

Write the conclusion (persisted vs runtime-only) as a comment in the commit and choose:
- **Branch A (persisted but mis-ordered):** mappings exist in the profile but the first `finishConnect`/auto-sync ran before `loadMappingsFromProfile` populated them, or before the engine got them. Fix = ordering (Task 4.2A).
- **Branch B (not persisted / runtime-only):** DAV mappings are only assembled after provider discovery completes. Fix = gate auto-sync until providers are ready (Task 4.2B).

Remove the temporary logs before committing the fix.

## Task 4.2A: (if Branch A) Fix mapping-load ordering

**Files:** Modify `src/runtime/palmruntime.cpp`

- [ ] **Step 1:** Ensure `loadMappingsFromProfile()` runs and `m_engine->setSyncMappings()` is applied before `readyForSync` is emitted, and that the rawfiles-default generation (`:355-430`) only adds defaults for genuinely uncovered slots (it already checks `alreadyCovered` — verify the DAV mappings are present in `m_mappings` at that point so their slots are seen as covered). Adjust ordering so the first `readyForSync` reflects the full persisted set. Show the exact reordered code based on what Step 4.1 revealed.
- [ ] **Step 2:** Build + test + device-verify one connect = one run. Commit `fix(runtime): load persisted DAV mappings before first auto-sync`.

## Task 4.2B: (if Branch B) Gate auto-sync until providers ready

**Files:** Modify `src/kf6/kf6mainwindow.cpp`, possibly `src/runtime/accountcontroller.cpp`

- [ ] **Step 1:** Defer the auto-sync: in `KF6MainWindow::onReadyForSync()`, when `autoSyncOnConnect()` is true but account providers have not finished discovery, do NOT call `onHotSync()` immediately. Instead wait for an "accounts ready / mappings assembled" signal. If `AccountController`/`ProviderManager` lacks such a signal, add one (e.g. `AccountController::mappingsReady()` emitted after `connectAll()` completes and mappings are merged into the profile/runtime), and have `onReadyForSync()` connect a one-shot that triggers the sync once both device-ready and accounts-ready have occurred.
- [ ] **Step 2:** Build + test + device-verify one connect = one run against the full mapping set. Commit `fix(autosync): wait for account providers before auto-syncing on connect`.

**Device verification (P4):** a single connect produces exactly one engine run covering Palm + DAV mappings; no rawfiles-only first pass.

---

# PROBLEM 2 — Phase-scoped tickle (keep Palm alive during network phases)

Sequenced last: benefits from P1 (shorter Palm phases) and needs a libkalburator handoff for full precision.

## Task 2.1: Confirm why the blanket pause exists

**Files:** none (investigation)

- [ ] **Step 1:** Read `palmruntime.cpp:680-683` and `:866-869` comments and `git log -p` for those lines. Determine whether the "tickle corrupts the DLP session" problem was a cross-thread issue that the link-thread consolidation (`PalmDeviceAccess::m_linkThread`) already eliminates, or a real in-protocol hazard. Since `PalmTickle` and all DLP run on the link thread (`palmdeviceaccess.cpp:170`), a tickle can only fire when the link thread's event loop is idle (between marshalled DLP ops). Record the conclusion as a comment in the eventual commit.

## Task 2.2: Replace blanket pause with phase-scoped suspension

**Files:** Modify `src/runtime/palmruntime.cpp`

- [ ] **Step 1:** Remove the unconditional `pauseTickle()` before `runSyncFuture` (`:683`) and the unconditional `resumeTickle()` is kept only as a final safety resume in the `runFinished` lambda. Instead, drive tickle from the engine phase signals PalmRuntime already subscribes to (`:142-162`). Track, per `syncStarted(mappingId)`, whether the current mapping's source/target backend is the Palm device backend (compare the mapping's `sourceBackend`/`targetBackend` ids against the loaded Palm plugin ids — the Palm side is the one whose backend id matches a `palmPlugins()` pluginId). On `phaseChanged(SyncPhase)`:
  - `FetchingSource`: pause tickle iff Palm is the source of the current mapping; else resume.
  - `FetchingTarget`: pause tickle iff Palm is the target; else resume.
  - `Processing`: conservatively pause (apply writes to the Palm side may occur — see handoff below). 
  - `Idle`/`Complete`: resume.

  Implement a small helper `void PalmRuntime::setTicklePausedForPhase(SyncPhase phase)` that consults the current mapping's Palm-side role and calls `m_device->pauseTickle()/resumeTickle()`. Connect it to `phaseChanged`. Keep a final `resumeTickle()` in the `runFinished` teardown (already present) so the tickle is always live between runs.

  > Concrete role detection: in the `syncStarted` lambda (already at `:142`), additionally compute and store `m_currentPalmIsSource`/`m_currentPalmIsTarget` by looking up the mapping in `m_mappings` and testing whether `sourceBackend`/`targetBackend` equals a Palm plugin id.

- [ ] **Step 2:** Build + run unit tests. Add a focused unit test if feasible: extract the phase→pause decision into a pure function `bool shouldPauseTickle(SyncPhase, bool palmIsSource, bool palmIsTarget)` and unit-test its truth table (pause during Palm-side fetch + Processing; resume during remote-side fetch + Idle/Complete). Place it where `PalmRuntime` can use it and a test can call it.
- [ ] **Step 3:** Commit `feat(palm): scope keep-alive tickle to Palm DLP phases (survive long CalDAV phases)`.

## Task 2.3: Write the libkalburator handoff for precise apply-phase scoping

**Files:** Create `docs/2026-05-26-tickle-phase-signal-handoff-libkalburator.md`

- [ ] **Step 1:** Document the limitation: `phaseChanged(Processing)` covers diff+apply for both sides, so PalmRuntime cannot tell when, within apply, the Palm side vs the remote side is being written, and must conservatively keep the tickle paused through `Processing`. Request a finer engine signal (e.g. a per-backend "applying to <backendId> begin/end" or sub-phases `WritingSource`/`WritingTarget`) so the consumer can keep the tickle alive during the remote write phase too. Reference the engine write driver (`libkalburator/src/engine/syncengine.cpp:1722-1736` and the canon apply path). Per workflow, this is a libkalburator change to be made there and pinned; WildPalms only consumes it.
- [ ] **Step 2:** Commit `docs: handoff for finer engine apply-phase signal (tickle scoping)`.

**Device verification (P2):** a Palm↔CalDAV sync with a large remote calendar (hundreds of incidences) completes without the cradle dropping mid-sync.

---

## Self-review notes

- **Spec coverage:** P1 (Tasks 1.1–1.4), P2 (2.1–2.3 + handoff), P3 (3.1 nullptr, 3.2 double-discovery), P4 (4.1 investigate → 4.2A/4.2B). All four spec problems mapped.
- **Type/method consistency:** `flushPendingWrites()` defined on `IPalmDatabaseAccess` (default no-op), overridden in `PilotLinkPalmDatabaseAccess`, invoked via `PalmDeviceAccess::flushWrites()`, called from `PalmRuntime` run-end — names consistent across Tasks 1.3/1.4. `ensureWriteHandle`/`flushWriteHandle`/`m_writeHandle`/`m_writeDbName` consistent within P1.
- **Investigate-first tasks (4.1, 2.1, 3.2-step1)** are diagnostic by design per the spec's decisions; their fix steps show concrete code adapted to the finding rather than a placeholder.
- **Independence:** each PROBLEM section is independently shippable; recommended order P1 → P3 → P4 → P2 (P2 last because it leans on P1 and needs the handoff).
- **No libkalburator edits** in WildPalms tasks; P2's precise scoping is a handoff doc + future pin bump.
