# Transparent Multi-Hop Bidirectional Sync — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one HotSync/FullSync propagate changes both directions across both hops of the `Palm — Hub — Remote` star, transparently, by giving the Palm backends a cheap collection-change token and looping the engine to a fixpoint.

**Architecture:** Three layers, all WildPalms-side (no libkalburator change). (A) the Palm device exposes a per-database modification number (`dlp_FindDBInfo`→`DBInfo.modnum`); (B) the four registered Palm backends implement the lib's `Sync::ChangeDetection` mixin via a shared `PalmChangeDetection` base + a `PalmRevisionStore`; (C) `PalmRuntime` enables `SyncEngine::setSkipUnchangedMappings` and runs the mapping set in a fixpoint loop, holding the device across passes.

**Tech Stack:** C++17, Qt6, KF6, libkalburator (pinned `493bd80`), pilot-link DLP, QtTest.

**Spec:** `docs/superpowers/specs/2026-06-14-multi-hop-bidirectional-sync-design.md`

**Key facts the engineer must know:**
- The *live* Palm backends are the **plugin submodule** classes (git submodules under `src/plugins/`), NOT the unused `src/palm/*` classes:
  - `src/plugins/calendar/palmcalendarbackend.h` — `PalmCalendarBackend : Kalburator::Sync::SyncBackend`, DB `DatebookDB`
  - `src/plugins/contacts/palmcontactsbackend.h` — `PalmContactsBackend : Kalburator::Sync::SyncBackendBase`, DB `AddressDB`
  - `src/plugins/memo/memoblobbackend.h` — `MemoBlobBackend : Kalburator::Sync::SyncBackendBase`, DB `MemoDB`
  - `src/plugins/todos/todoblobbackend.h` — `TodoBlobBackend : Kalburator::Sync::SyncBackendBase`, DB `ToDoDB`
- All four hold a private `WildPalms::PalmSync::PalmBackend *m_palmBackend;` (an `IBlobBackend` adapter) whose own `IPalmDatabaseAccess *m_device` reaches the device. Each wraps exactly ONE physical DB.
- Editing a submodule = commit + push in that submodule's own git repo, then bump the gitlink in the superproject (`feedback_libkalburator_handoff_workflow` allows editing `src/plugins/<conduit>/` freely; it is WP scope). Local builds use the submodule **working tree**, so you can build/test before committing submodules.
- Build: `cmake --build build -j 8`; test: `ctest --test-dir build -j 8`. The pin is a SHA; `WILDPALMS_LIBKALBURATOR_SOURCE_DIR=` (empty) + FetchContent.
- The engine's fast-path skip (`SyncEngine::prepareSyncFastPath`, lib `syncengine.cpp:660`) skips a mapping only when BOTH sides implement `Sync::ChangeDetection` AND both revisions equal the cached value, AND `m_skipUnchangedMappings` is true (default false). The hub side (`GenericSqliteBackend`/`LocalBackend`) already implements `ChangeDetection`; this plan adds the Palm side.

---

## File Structure

**Main repo (new):**
- `src/palm/sync/palmrevisionstore.h` / `.cpp` — QSettings-ini token store (`token`/`setToken`).
- `src/palm/sync/palmchangedetection.h` — shared mixin: `Kalburator::Sync::ChangeDetection` impl + store injection + one protected hook `currentDbRevision()`. Header-only.
- `tests/runtime/tst_palm_change_detection.cpp` — mixin + store + loop-decision unit tests.

**Main repo (modified):**
- `src/palm/kpilotlink.h` — add `databaseModnum` virtual (default -1).
- `src/palm/kpilotdevicelink.h` / `.cpp` — override `databaseModnum` (dlp_FindDBInfo).
- `src/palm/sync/ipalmdatabaseaccess.h` — add `databaseRevision` virtual (default `{}`).
- `src/palm/device/pilotlinkpalmdatabaseaccess.h` / `.cpp` — override `databaseRevision`.
- `src/runtime/palmdeviceaccess.h` / `.cpp` — override `databaseRevision` (marshaled).
- `src/palm/sync/palmbackend.h` / `.cpp` — add `databaseRevision` (forward to `m_device`).
- `src/palm/sync/mockpalmdatabaseaccess.h` / `.cpp` — override `databaseRevision` + revision bump.
- `src/runtime/palmruntime.h` / `.cpp` — own `PalmRevisionStore`, inject in `finishConnect`; `shouldContinueSync` helper; fixpoint loop in `runAllMappings`; enable skip for HotSync.
- `src/runtime/CMakeLists.txt` (and any `palm` lib CMake) — add new sources.
- `tests/runtime/CMakeLists.txt` — register the new test.

**Submodules (modified):** the four backend headers/cpps listed above (Tasks 9–12).

---

## Task 1: Device modnum on the link interface

**Files:**
- Modify: `src/palm/kpilotlink.h` (after `listDatabases()` at line 58)
- Modify: `src/palm/kpilotdevicelink.h` (near `findDatabase` decl, ~line 183)
- Modify: `src/palm/kpilotdevicelink.cpp` (after `findDatabase`, ~line 1305)

- [ ] **Step 1: Add the virtual to the abstract link interface**

In `src/palm/kpilotlink.h`, immediately after `virtual QStringList listDatabases() = 0;`:

```cpp
    /// Cheap per-database modification number (Palm DBInfo.modnum).
    /// Returns -1 when unavailable/not connected. Non-pure so non-device
    /// KPilotLink implementations need no change.
    virtual long databaseModnum(const QString &dbName) { Q_UNUSED(dbName); return -1; }
```

- [ ] **Step 2: Declare the override in KPilotDeviceLink**

In `src/palm/kpilotdevicelink.h`, right after `bool findDatabase(const QString &dbName);`:

```cpp
    /// dlp_FindDBInfo()-backed modification number; -1 on failure/not connected.
    long databaseModnum(const QString &dbName) override;
```

- [ ] **Step 3: Implement it (mirror findDatabase)**

In `src/palm/kpilotdevicelink.cpp`, immediately after the `findDatabase` definition (~line 1300):

```cpp
long KPilotDeviceLink::databaseModnum(const QString &dbName)
{
    if (!m_isConnected || m_socket < 0)
        return -1;

    struct DBInfo info;
    int rc = dlp_FindDBInfo(m_socket, 0, 0,
                            dbName.toLocal8Bit().constData(),
                            0, 0, &info);
    if (rc < 0)
        return -1;
    return static_cast<long>(info.modnum);
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target wildpalms" | tail`
Expected: builds, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/palm/kpilotlink.h src/palm/kpilotdevicelink.h src/palm/kpilotdevicelink.cpp
git commit -m "feat(palm): KPilotLink::databaseModnum via dlp_FindDBInfo"
```

---

## Task 2: `databaseRevision` on the access interface + mock

**Files:**
- Modify: `src/palm/sync/ipalmdatabaseaccess.h` (after `deleteDatabase`, ~line 48)
- Modify: `src/palm/sync/mockpalmdatabaseaccess.h` (member + decl)
- Modify: `src/palm/sync/mockpalmdatabaseaccess.cpp`
- Test: `tests/runtime/tst_palm_change_detection.cpp` (new)

- [ ] **Step 1: Add the interface method (default empty)**

In `src/palm/sync/ipalmdatabaseaccess.h`, after the `deleteDatabase` default impl:

```cpp
    /// Cheap collection-change token (the DB modification number as a string).
    /// Empty string = "cannot answer cheaply" (caller treats it as changed).
    /// Non-pure: implementers that can't answer need no override.
    virtual QString databaseRevision(const QString &dbName) const { Q_UNUSED(dbName); return {}; }
```

- [ ] **Step 2: Add a revision counter to the mock's Database struct + bump on writes**

In `src/palm/sync/mockpalmdatabaseaccess.h`, inside `struct Database { ... }` add:

```cpp
        quint64 revision = 0;   ///< bumps on every create/update/delete
```

And in the public section, after `bool deleteRecord(...) override;`:

```cpp
    QString databaseRevision(const QString &dbName) const override;
```

- [ ] **Step 3: Implement databaseRevision + bump in the mock**

In `src/palm/sync/mockpalmdatabaseaccess.cpp`:

```cpp
QString MockPalmDatabaseAccess::databaseRevision(const QString &dbName) const
{
    auto it = m_dbs.constFind(dbName);
    if (it == m_dbs.constEnd()) return {};
    return QString::number(it->revision);
}
```

Then, in each of `createRecord`, `updateRecord`, `deleteRecord` (and `writeAppBlock` if present), add `++m_dbs[dbName].revision;` at the point a mutation succeeds (after the record map is modified, before returning success). Example for `createRecord` — find the success path and add the bump:

```cpp
    // ... after inserting the record into m_dbs[dbName].records ...
    ++m_dbs[dbName].revision;
    return newId;
```

- [ ] **Step 4: Write the failing test**

Create `tests/runtime/tst_palm_change_detection.cpp`:

```cpp
#include <QtTest>
#include "mockpalmdatabaseaccess.h"

using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmRecord;

class TestPalmChangeDetection : public QObject {
    Q_OBJECT
private slots:
    void mockRevision_emptyForUnknownDb();
    void mockRevision_bumpsOnWrite();
};

void TestPalmChangeDetection::mockRevision_emptyForUnknownDb()
{
    MockPalmDatabaseAccess dev;
    QCOMPARE(dev.databaseRevision("NoSuchDB"), QString());
}

void TestPalmChangeDetection::mockRevision_bumpsOnWrite()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("DatebookDB");
    const QString r0 = dev.databaseRevision("DatebookDB");
    PalmRecord rec;                       // default record is fine for the bump
    dev.createRecord("DatebookDB", rec);
    const QString r1 = dev.databaseRevision("DatebookDB");
    QVERIFY(!r1.isEmpty());
    QVERIFY(r0 != r1);
}

QTEST_MAIN(TestPalmChangeDetection)
#include "tst_palm_change_detection.moc"
```

- [ ] **Step 5: Register the test**

In `tests/runtime/CMakeLists.txt`, copy the `tst_palm_runtime_hotsync` block (lines ~70–92: `add_executable` + `target_link_libraries` + `add_test`) and rename it to `tst_palm_change_detection`. That block already links the runtime + palm static libs (`WildPalmsRuntime`, `WildPalmsPalmSync`, …) and libkalburator — i.e. everything this test needs (`MockPalmDatabaseAccess`, `PalmBackend`, `PalmRevisionStore`, `PalmChangeDetection`, `PalmRuntime`, `shouldContinueSync`, and the `SyncResult` types). Source file: `tst_palm_change_detection.cpp`.

- [ ] **Step 6: Run — expect FAIL then PASS**

Run: `cmake --build build -j 8 && ctest --test-dir build -R tst_palm_change_detection -V`
Expected: compiles after step 3; both cases PASS. (If you wrote the test before step 3, it fails to compile on `databaseRevision` — that is the RED state.)

- [ ] **Step 7: Commit**

```bash
git add src/palm/sync/ipalmdatabaseaccess.h src/palm/sync/mockpalmdatabaseaccess.h \
        src/palm/sync/mockpalmdatabaseaccess.cpp tests/runtime/tst_palm_change_detection.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "feat(palm): IPalmDatabaseAccess::databaseRevision + mock revision bump"
```

---

## Task 3: Forward `databaseRevision` through the device adapters

**Files:**
- Modify: `src/palm/device/pilotlinkpalmdatabaseaccess.h` / `.cpp`
- Modify: `src/runtime/palmdeviceaccess.h` / `.cpp`
- Modify: `src/palm/sync/palmbackend.h` / `.cpp`

- [ ] **Step 1: PilotLinkPalmDatabaseAccess → link modnum**

Header: add `QString databaseRevision(const QString &dbName) const override;`
Cpp:

```cpp
QString PilotLinkPalmDatabaseAccess::databaseRevision(const QString &dbName) const
{
    if (!m_link) return {};
    const long m = m_link->databaseModnum(dbName);
    return m < 0 ? QString() : QString::number(m);
}
```

- [ ] **Step 2: PalmDeviceAccess → marshal to link thread (mirror availableDatabases)**

Header: add `QString databaseRevision(const QString &dbName) const override;`
Cpp (mirror the existing `availableDatabases` marshaling exactly):

```cpp
QString PalmDeviceAccess::databaseRevision(const QString &dbName) const
{
    if (!m_impl) return {};
    QString result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result, &dbName]() { result = m_impl->databaseRevision(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}
```

- [ ] **Step 3: PalmBackend → forward to its device**

Header (`src/palm/sync/palmbackend.h`, public section): add

```cpp
    /// Forwards the device's cheap per-DB change token (for ChangeDetection).
    QString databaseRevision(const QString &dbName) const;
```

Cpp:

```cpp
QString PalmBackend::databaseRevision(const QString &dbName) const
{
    return m_device ? m_device->databaseRevision(dbName) : QString();
}
```

- [ ] **Step 4: Build + add a PalmBackend forward test**

Append to `tests/runtime/tst_palm_change_detection.cpp` (declare the slot in the class):

```cpp
void TestPalmChangeDetection::palmBackendForwardsRevision()
{
    MockPalmDatabaseAccess dev;
    dev.createDatabase("AddressDB");
    WildPalms::PalmSync::PalmBackend backend(&dev);
    PalmRecord rec;
    dev.createRecord("AddressDB", rec);
    QCOMPARE(backend.databaseRevision("AddressDB"), dev.databaseRevision("AddressDB"));
    QVERIFY(!backend.databaseRevision("AddressDB").isEmpty());
}
```

Add `#include "palmbackend.h"` to the test. Run:
`cmake --build build -j 8 && ctest --test-dir build -R tst_palm_change_detection -V`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/palm/device/pilotlinkpalmdatabaseaccess.* src/runtime/palmdeviceaccess.* \
        src/palm/sync/palmbackend.* tests/runtime/tst_palm_change_detection.cpp
git commit -m "feat(palm): forward databaseRevision through device adapters"
```

---

## Task 4: `PalmRevisionStore`

**Files:**
- Create: `src/palm/sync/palmrevisionstore.h` / `.cpp`
- Modify: the CMake lib that owns `src/palm/sync/*` (search for `palmbackend.cpp` in CMake to find it; add the new `.cpp`)
- Test: `tests/runtime/tst_palm_change_detection.cpp`

- [ ] **Step 1: Write the header**

`src/palm/sync/palmrevisionstore.h`:

```cpp
#ifndef WILDPALMS_PALMSYNC_PALMREVISIONSTORE_H
#define WILDPALMS_PALMSYNC_PALMREVISIONSTORE_H

#include <QString>

namespace WildPalms::PalmSync {

/// Persists per-collection revision tokens (Palm DB modnums) across runs.
/// QSettings(ini)-backed; analogue of the lib's AkonadiRevisionStore.
class PalmRevisionStore {
public:
    explicit PalmRevisionStore(const QString &filePath);
    QString token(const QString &collectionId) const;
    void    setToken(const QString &collectionId, const QString &token);
private:
    QString m_filePath;
};

} // namespace WildPalms::PalmSync
#endif
```

- [ ] **Step 2: Write the impl**

`src/palm/sync/palmrevisionstore.cpp`:

```cpp
#include "palmrevisionstore.h"
#include <QSettings>

namespace WildPalms::PalmSync {

PalmRevisionStore::PalmRevisionStore(const QString &filePath)
    : m_filePath(filePath) {}

QString PalmRevisionStore::token(const QString &collectionId) const
{
    QSettings s(m_filePath, QSettings::IniFormat);
    return s.value(QStringLiteral("revisions/") + collectionId).toString();
}

void PalmRevisionStore::setToken(const QString &collectionId, const QString &token)
{
    QSettings s(m_filePath, QSettings::IniFormat);
    s.setValue(QStringLiteral("revisions/") + collectionId, token);
}

} // namespace WildPalms::PalmSync
```

- [ ] **Step 3: Add to CMake**

In `src/palm/sync/CMakeLists.txt` (the `WildPalmsPalmSync` target, where `palmbackend.cpp`/`palmbackend.h` are listed at lines ~17–18), add `palmrevisionstore.cpp` and `palmrevisionstore.h` to the source list.

- [ ] **Step 4: Write the failing test**

Append to `tst_palm_change_detection.cpp` (`#include "palmrevisionstore.h"`, `#include <QTemporaryDir>`):

```cpp
void TestPalmChangeDetection::revisionStore_persistsAcrossInstances()
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/palm-revisions.ini";
    {
        WildPalms::PalmSync::PalmRevisionStore s(path);
        QVERIFY(s.token("palm:calendar").isEmpty());
        s.setToken("palm:calendar", "42");
    }
    WildPalms::PalmSync::PalmRevisionStore s2(path);  // fresh instance, same file
    QCOMPARE(s2.token("palm:calendar"), QString("42"));
}
```

- [ ] **Step 5: Run — expect PASS**

Run: `cmake --build build -j 8 && ctest --test-dir build -R tst_palm_change_detection -V`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/palm/sync/palmrevisionstore.h src/palm/sync/palmrevisionstore.cpp \
        src/palm/sync/CMakeLists.txt tests/runtime/tst_palm_change_detection.cpp
git commit -m "feat(palm): PalmRevisionStore (QSettings-ini revision token store)"
```

---

## Task 5: `PalmChangeDetection` mixin

**Files:**
- Create: `src/palm/sync/palmchangedetection.h` (header-only)
- Test: `tests/runtime/tst_palm_change_detection.cpp`

- [ ] **Step 1: Write the mixin**

`src/palm/sync/palmchangedetection.h`:

```cpp
#ifndef WILDPALMS_PALMSYNC_PALMCHANGEDETECTION_H
#define WILDPALMS_PALMSYNC_PALMCHANGEDETECTION_H

#include "changedetection.h"          // Kalburator::Sync::ChangeDetection
#include "palmrevisionstore.h"
#include <QMap>
#include <QString>

namespace WildPalms::PalmSync {

/// Shared base implementing the lib's collection-level ChangeDetection for a
/// Palm backend that wraps exactly ONE physical database. Concrete backends
/// add this as a base and implement currentDbRevision() (their single DB's
/// modnum, via m_palmBackend->databaseRevision(DatabaseName)). PalmRuntime
/// injects the per-profile store via setPalmRevisionStore().
class PalmChangeDetection : public Kalburator::Sync::ChangeDetection {
public:
    void setPalmRevisionStore(PalmRevisionStore *store) { m_revStore = store; }

    QString collectionRevision(const QString &collectionId) override
    {
        Q_UNUSED(collectionId);          // one DB per backend; id-agnostic
        return currentDbRevision();
    }

    QString cachedCollectionRevision(const QString &collectionId) const override
    {
        return m_revStore ? m_revStore->token(collectionId) : QString();
    }

    void primeRevisionCache(const QMap<QString, QString> &cache) override
    {
        if (!m_revStore) return;
        for (auto it = cache.constBegin(); it != cache.constEnd(); ++it)
            m_revStore->setToken(it.key(), it.value());
    }

protected:
    /// The wrapped DB's current modnum token (empty if unavailable).
    virtual QString currentDbRevision() const = 0;

private:
    PalmRevisionStore *m_revStore = nullptr;   // not owned
};

} // namespace WildPalms::PalmSync
#endif
```

- [ ] **Step 2: Write the failing test (test subclass)**

Append to `tst_palm_change_detection.cpp` (`#include "palmchangedetection.h"`):

```cpp
namespace {
class FakeCD : public WildPalms::PalmSync::PalmChangeDetection {
public:
    QString rev;
protected:
    QString currentDbRevision() const override { return rev; }
};
}

void TestPalmChangeDetection::mixin_usesStoreAndHook()
{
    QTemporaryDir dir;
    WildPalms::PalmSync::PalmRevisionStore store(dir.path() + "/r.ini");
    FakeCD cd;
    cd.setPalmRevisionStore(&store);
    cd.rev = "7";

    QCOMPARE(cd.collectionRevision("palm:calendar"), QString("7"));   // live hook
    QVERIFY(cd.cachedCollectionRevision("palm:calendar").isEmpty());  // nothing cached
    cd.primeRevisionCache({{"palm:calendar", "7"}});
    QCOMPARE(cd.cachedCollectionRevision("palm:calendar"), QString("7"));
}

void TestPalmChangeDetection::mixin_noStoreIsSafe()
{
    FakeCD cd;                       // no store injected
    cd.rev = "3";
    QCOMPARE(cd.collectionRevision("x"), QString("3"));
    QVERIFY(cd.cachedCollectionRevision("x").isEmpty());
    cd.primeRevisionCache({{"x", "3"}});           // no-op, must not crash
    QVERIFY(cd.cachedCollectionRevision("x").isEmpty());
}
```

Declare both slots in the test class.

- [ ] **Step 3: Run — expect PASS**

Run: `cmake --build build -j 8 && ctest --test-dir build -R tst_palm_change_detection -V`
Expected: PASS.

- [ ] **Step 4: Register the header in CMake + commit**

Add `palmchangedetection.h` to the `WildPalmsPalmSync` source list in
`src/palm/sync/CMakeLists.txt` (header-only, but list it so it tracks/installs alongside
the others).

```bash
git add src/palm/sync/palmchangedetection.h src/palm/sync/CMakeLists.txt \
        tests/runtime/tst_palm_change_detection.cpp
git commit -m "feat(palm): PalmChangeDetection mixin over PalmRevisionStore"
```

---

## Task 6: Loop-decision helper `shouldContinueSync`

This pure function is the testable core of the fixpoint loop (Task 12 uses it).

**Files:**
- Modify: `src/runtime/palmruntime.h` (declare a free function or static)
- Modify: `src/runtime/palmruntime.cpp` (define it)
- Test: `tests/runtime/tst_palm_change_detection.cpp`

- [ ] **Step 1: Declare the helper**

In `src/runtime/palmruntime.h`, in the `WildPalms::Runtime` namespace (near `translateRouteSpec`), declare:

```cpp
/// Decide whether the multi-hop loop should run another pass.
/// `results` are the SyncResults of the pass that just finished.
/// Continues only if some mapping changed data (so a hop may still be
/// pending), the run is healthy, and we are under the cap.
///   passJustFinished : 1-based index of the pass that just completed
///   maxPasses        : hard cap (3 for HotSync)
bool shouldContinueSync(const QList<Kalburator::Sync::SyncResult> &results,
                        int passJustFinished, int maxPasses);
```

- [ ] **Step 2: Define it**

In `src/runtime/palmruntime.cpp` (anonymous-namespace-free, in `WildPalms::Runtime`):

```cpp
bool shouldContinueSync(const QList<Kalburator::Sync::SyncResult> &results,
                        int passJustFinished, int maxPasses)
{
    if (passJustFinished >= maxPasses) return false;        // cap reached
    bool anyChange = false;
    for (const auto &sr : results) {
        if (sr.cancelled) return false;                     // cancelled → stop
        if (!sr.success && !sr.skipped) return false;       // failure → stop
        if (sr.sourceStats.hasChanges() || sr.targetStats.hasChanges())
            anyChange = true;
    }
    return anyChange;                                       // loop only if data moved
}
```

- [ ] **Step 3: Write the failing test**

Append to `tst_palm_change_detection.cpp` (`#include "palmruntime.h"`; the SyncResult/SyncStats types come from `synctypes.h` via that header):

```cpp
void TestPalmChangeDetection::loopDecision_cases()
{
    using Kalburator::Sync::SyncResult;
    using WildPalms::Runtime::shouldContinueSync;

    auto changed = []{ SyncResult r; r.success = true; r.targetStats.created = 1; return r; };
    auto quiet   = []{ SyncResult r; r.success = true; return r; };
    auto failed  = []{ SyncResult r; r.success = false; return r; };
    auto cancel  = []{ SyncResult r; r.cancelled = true; return r; };

    // change on pass 1 (cap 3) → continue
    QVERIFY(shouldContinueSync({changed(), quiet()}, 1, 3));
    // no change → stop (fixpoint)
    QVERIFY(!shouldContinueSync({quiet(), quiet()}, 1, 3));
    // cap reached → stop even with changes
    QVERIFY(!shouldContinueSync({changed()}, 3, 3));
    // failure → stop
    QVERIFY(!shouldContinueSync({changed(), failed()}, 1, 3));
    // cancel → stop
    QVERIFY(!shouldContinueSync({changed(), cancel()}, 1, 3));
}
```

Declare the slot.

- [ ] **Step 4: Run — expect PASS**

Run: `cmake --build build -j 8 && ctest --test-dir build -R tst_palm_change_detection -V`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp tests/runtime/tst_palm_change_detection.cpp
git commit -m "feat(runtime): shouldContinueSync fixpoint-loop decision helper"
```

---

## Task 7: PalmRuntime owns + injects the revision store

**Files:**
- Modify: `src/runtime/palmruntime.h` (member)
- Modify: `src/runtime/palmruntime.cpp` (ctor init + `finishConnect` injection)

- [ ] **Step 1: Add the member (declared BEFORE m_ownedBackends so it outlives them)**

In `src/runtime/palmruntime.h`, add an include `#include "palmrevisionstore.h"` and, in the members area **above** `m_ownedBackends`:

```cpp
    std::unique_ptr<WildPalms::PalmSync::PalmRevisionStore> m_palmRevisionStore;
```

- [ ] **Step 2: Construct it next to the hub/baseline (ctor)**

In `src/runtime/palmruntime.cpp`, in the constructor body near the `m_hub`/`m_baselineStore` setup (around line 140–156), add:

```cpp
    m_palmRevisionStore = std::make_unique<WildPalms::PalmSync::PalmRevisionStore>(
        QDir(profilePath).filePath(QStringLiteral(".state/palm-revisions.ini")));
```

(Use the same `profilePath` ctor parameter the hub path uses.)

- [ ] **Step 3: Inject after each backend is registered**

In `finishConnect`, inside the `for (auto *c : conduits())` registration loop, immediately after `m_registry->registerBackendInstance(id, ownedBackend.get());` (line ~550), add:

```cpp
        if (auto *cd = dynamic_cast<WildPalms::PalmSync::PalmChangeDetection*>(ownedBackend.get()))
            cd->setPalmRevisionStore(m_palmRevisionStore.get());
```

Add `#include "palmchangedetection.h"` to `palmruntime.cpp`.

- [ ] **Step 4: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target wildpalms" | tail`
Expected: builds. (No new test here; injection is exercised by the fixpoint loop in Task 12 and by the on-device smoke test in Task 13.)

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp
git commit -m "feat(runtime): own per-profile PalmRevisionStore, inject into Palm backends"
```

---

> **Tasks 8–11 add the mixin to each of the four submodule backends.** Near-identical — only the class name, base class, and DB literal differ. Do them one at a time, building after each. Reminder: these edits land in each submodule's own git repo (commit there); local builds use the working tree, so the suite runs before the submodules are pushed and the gitlinks bumped (Task 13).

## Task 8: Calendar backend implements ChangeDetection (submodule)

**Files (submodule `src/plugins/calendar/`):**
- Modify: `src/plugins/calendar/palmcalendarbackend.h`
- Modify: `src/plugins/calendar/palmcalendarbackend.cpp`

- [ ] **Step 1: Add the base + override to the header**

In `src/plugins/calendar/palmcalendarbackend.h`:
- Add include: `#include "palmchangedetection.h"`
- Change the class declaration to also inherit the mixin:

```cpp
class PalmCalendarBackend : public Kalburator::Sync::SyncBackend,
                            public WildPalms::PalmSync::PalmChangeDetection
{
```

- In the `protected:` section (add one if needed), declare:

```cpp
protected:
    QString currentDbRevision() const override;
```

- [ ] **Step 2: Implement the hook in the cpp**

In `src/plugins/calendar/palmcalendarbackend.cpp`:

```cpp
QString PalmCalendarBackend::currentDbRevision() const
{
    return m_palmBackend ? m_palmBackend->databaseRevision(QStringLiteral("DatebookDB"))
                         : QString();
}
```

(Confirm the member is named `m_palmBackend` and is a `PalmBackend*` — header line ~90. If a `DatabaseName` constant exists, use it instead of the literal.)

- [ ] **Step 3: Build (working-tree submodule)**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target" | tail`
Expected: builds. The engine can now `dynamic_cast` the calendar backend to `Sync::ChangeDetection`.

- [ ] **Step 4: Add a per-backend revision test**

If `tests/.../tst_calendarbackendplugin.cpp` exists and constructs the backend with a mock device, add a case asserting `collectionRevision("palm:calendar")` is non-empty after a write and equals the device's `databaseRevision("DatebookDB")`. If the plugin test does not construct a device-backed backend, skip — the mixin logic is already covered by Task 5 and the device smoke test is Task 13. Do not invent a fixture.

- [ ] **Step 5: Commit (in the submodule, no push yet)**

```bash
git -C src/plugins/calendar add palmcalendarbackend.h palmcalendarbackend.cpp
git -C src/plugins/calendar commit -m "feat: implement Sync::ChangeDetection via PalmChangeDetection"
```

(Push + superproject gitlink bump happens in Task 13 after the full suite is green.)

---

## Task 9: Contacts backend implements ChangeDetection (submodule)

**Files:** `src/plugins/contacts/palmcontactsbackend.{h,cpp}`

- [ ] **Step 1: Header — base + override**

```cpp
#include "palmchangedetection.h"
// ...
class PalmContactsBackend : public Kalburator::Sync::SyncBackendBase,
                            public WildPalms::PalmSync::PalmChangeDetection
{
// ...
protected:
    QString currentDbRevision() const override;
```

- [ ] **Step 2: Cpp — hook**

```cpp
QString PalmContactsBackend::currentDbRevision() const
{
    return m_palmBackend ? m_palmBackend->databaseRevision(QStringLiteral("AddressDB"))
                         : QString();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target" | tail`
Expected: builds.

- [ ] **Step 4: Commit (submodule)**

```bash
git -C src/plugins/contacts add palmcontactsbackend.h palmcontactsbackend.cpp
git -C src/plugins/contacts commit -m "feat: implement Sync::ChangeDetection via PalmChangeDetection"
```

---

## Task 10: Memo backend implements ChangeDetection (submodule)

**Files:** `src/plugins/memo/memoblobbackend.{h,cpp}`

- [ ] **Step 1: Header — base + override**

```cpp
#include "palmchangedetection.h"
// ...
class MemoBlobBackend : public Kalburator::Sync::SyncBackendBase,
                        public WildPalms::PalmSync::PalmChangeDetection
{
// ...
protected:
    QString currentDbRevision() const override;
```

- [ ] **Step 2: Cpp — hook**

```cpp
QString MemoBlobBackend::currentDbRevision() const
{
    return m_palmBackend ? m_palmBackend->databaseRevision(QStringLiteral("MemoDB"))
                         : QString();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target" | tail`
Expected: builds.

- [ ] **Step 4: Commit (submodule)**

```bash
git -C src/plugins/memo add memoblobbackend.h memoblobbackend.cpp
git -C src/plugins/memo commit -m "feat: implement Sync::ChangeDetection via PalmChangeDetection"
```

---

## Task 11: Todo backend implements ChangeDetection (submodule)

**Files:** `src/plugins/todos/todoblobbackend.{h,cpp}`

- [ ] **Step 1: Header — base + override**

```cpp
#include "palmchangedetection.h"
// ...
class TodoBlobBackend : public Kalburator::Sync::SyncBackendBase,
                        public WildPalms::PalmSync::PalmChangeDetection
{
// ...
protected:
    QString currentDbRevision() const override;
```

- [ ] **Step 2: Cpp — hook**

```cpp
QString TodoBlobBackend::currentDbRevision() const
{
    return m_palmBackend ? m_palmBackend->databaseRevision(QStringLiteral("ToDoDB"))
                         : QString();
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target" | tail`
Expected: builds.

- [ ] **Step 4: Commit (submodule)**

```bash
git -C src/plugins/todos add todoblobbackend.h todoblobbackend.cpp
git -C src/plugins/todos commit -m "feat: implement Sync::ChangeDetection via PalmChangeDetection"
```

---

## Task 12: Enable skip + fixpoint loop in `runAllMappings`

This refactors the single-pass `runAllMappings` (`src/runtime/palmruntime.cpp:832`) into a multi-pass loop, holding the device across passes. Read the current function first (lines 832–960): it builds `ids`, creates `engineFuture = m_engine->runSync(req)`, wires one `QFutureWatcher` whose `finished` lambda aggregates results into a `PalmRunResult`, emits signals, and finishes a `promise`. The refactor extracts the dispatch into a re-armable helper.

**Files:**
- Modify: `src/runtime/palmruntime.h` (member state + private method)
- Modify: `src/runtime/palmruntime.cpp` (`runAllMappings`, `fullSync`, new `dispatchSyncPass_`)

- [ ] **Step 1: Add loop state + the private dispatch method to the header**

In `src/runtime/palmruntime.h`, add private members:

```cpp
    // Multi-hop fixpoint loop state (one in-flight run at a time).
    int     m_syncPass     = 0;     // 1-based current pass
    int     m_syncMaxPass  = 1;     // 3 for HotSync, 2 for FullSync
    QList<QString>                              m_syncIds;        // enabled mapping ids
    std::shared_ptr<QPromise<PalmRunResult>>    m_syncPromise;    // caller's promise
    PalmRunResult                               m_syncAccum;      // stats accumulated across passes
```

and a private method:

```cpp
    void dispatchSyncPass_();       // runs one engine pass; re-arms or finishes the promise
```

- [ ] **Step 2: Rewrite `runAllMappings` to initialize state + kick the first pass**

Replace the body of `runAllMappings()` (keep the `ids` collection + empty-guard + the `pauseTickle` comment block) with:

```cpp
QFuture<PalmRunResult> PalmRuntime::runAllMappings()
{
    QList<QString> ids;
    for (const auto &m : m_mappings)
        if (m.enabled) ids.append(m.id);
    if (ids.isEmpty())
        return makeSuccessFuture();

    // HotSync: skip settled mappings and loop to a fixpoint (cap 3).
    // FullSync sets these via runFullSyncPasses_ (Step 5) before calling here.
    if (m_syncMaxPass <= 1) {                  // not pre-set by FullSync
        m_engine->setSkipUnchangedMappings(true);
        m_syncMaxPass = 3;
    }

    m_syncIds    = ids;
    m_syncPass   = 0;
    m_syncAccum  = PalmRunResult{};
    m_syncAccum.success   = true;
    m_syncAccum.startTime = QDateTime::currentDateTimeUtc();

    m_syncPromise = std::make_shared<QPromise<PalmRunResult>>();
    m_syncPromise->start();
    QFuture<PalmRunResult> resultFuture = m_syncPromise->future();

    dispatchSyncPass_();
    return resultFuture;
}
```

- [ ] **Step 3: Implement `dispatchSyncPass_`**

Move the existing watcher/aggregation logic into this method. It runs one pass, and in the watcher's `finished` handler decides (via `shouldContinueSync`) whether to re-arm or finish `m_syncPromise`. Use the existing aggregation code from the old lambda (lines 889–945) to fold this pass's results into `m_syncAccum` (accumulate `perPluginStats`, set `success=false` on real failures, collapse link-lost, mark cancelled). Then:

```cpp
void PalmRuntime::dispatchSyncPass_()
{
    ++m_syncPass;

    Kalburator::Sync::SyncRequest req;
    req.mappingIds = m_syncIds;
    req.behavior   = Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored;
    auto engineFuture = m_engine->runSync(req);

    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    auto *watcher = new QFutureWatcher<void>(this);
    m_activeSyncWatcher = watcher;

    QObject::connect(watcher, &QFutureWatcher<void>::finished, this,
            [this, watcher, engineFuture]() {
        QList<Kalburator::Sync::SyncResult> results;
        if (engineFuture.resultCount() > 0)
            results = engineFuture.resultAt(0);

        // --- fold this pass into m_syncAccum (reuse the old aggregation) ---
        bool anyCancelled = engineFuture.isCanceled();
        PalmRunResult::PluginStats stats;
        int linkLostCount = 0;
        for (const auto &sr : results) {
            if (sr.cancelled) anyCancelled = true;
            if (!sr.success && !sr.cancelled && !sr.skipped) {
                m_syncAccum.success = false;
                if (sr.errorMessage.contains(QLatin1String("Palm link"), Qt::CaseInsensitive))
                    ++linkLostCount;
                else if (m_syncAccum.errorMessage.isEmpty() && !sr.errorMessage.isEmpty())
                    m_syncAccum.errorMessage = sr.errorMessage;
            }
            stats.created   += sr.targetStats.created;
            stats.updated   += sr.targetStats.updated;
            stats.deleted   += sr.targetStats.deleted;
            stats.unchanged += sr.targetStats.unchanged;
        }
        if (anyCancelled) {
            m_syncAccum.success = false;
            if (m_syncAccum.errorMessage.isEmpty())
                m_syncAccum.errorMessage = QStringLiteral("Sync cancelled");
        }
        if (!results.isEmpty())
            m_syncAccum.perPluginStats.insert(QStringLiteral("calendar"), stats);

        // per-mapping chip counts (run-end, every pass)
        for (int i = 0; i < results.size() && i < m_syncIds.size(); ++i) {
            const auto &ts = results[i].targetStats;
            Q_EMIT mappingSyncFinished(m_syncIds[i], ts.created, ts.updated, ts.deleted,
                                       results[i].success && !results[i].cancelled);
        }

        if (m_activeSyncWatcher == watcher) m_activeSyncWatcher = nullptr;
        watcher->deleteLater();

        // --- loop or finish ---
        if (WildPalms::Runtime::shouldContinueSync(results, m_syncPass, m_syncMaxPass)) {
            dispatchSyncPass_();                // device stays connected; next hop
            return;
        }

        m_syncAccum.endTime = QDateTime::currentDateTimeUtc();
        if (m_device) m_device->flushWrites();
        if (m_device) m_device->resumeTickle();
        m_activeMappingId.clear();
        m_syncMaxPass = 1;                      // reset for the next run
        Q_EMIT runFinished(m_syncAccum);
        Q_EMIT syncCompleted();
        m_syncPromise->addResult(m_syncAccum);
        m_syncPromise->finish();
        m_syncPromise.reset();
    });
    watcher->setFuture(engineFuture);
}
```

- [ ] **Step 4: Build**

Run: `cmake --build build -j 8 2>&1 | grep -E "error|Built target wildpalms" | tail`
Expected: builds.

- [ ] **Step 5: FullSync — loop without skip (fixed 2 passes)**

In `fullSync()` (line ~969), after clearing baselines and before `return runAllMappings();`, set the loop knobs so `runAllMappings` does NOT re-set them:

```cpp
    m_engine->setSkipUnchangedMappings(false);   // full re-diff every pass
    m_syncMaxPass = 2;                            // 2 passes suffice for depth-1 star
    return runAllMappings();
```

`shouldContinueSync` still early-stops pass 1 if nothing changed; otherwise it runs pass 2 and stops at the cap.

- [ ] **Step 6: Run the existing suite — no regressions**

Run: `ctest --test-dir build -j 8`
Expected: 126/126 (125 baseline + `tst_palm_change_detection`). Pay attention to the existing HotSync/FullSync/Mirror/Clobber runtime tests — they must stay green. A first sync has no cached revisions, so skip-unchanged is a no-op there.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp
git commit -m "feat(runtime): fixpoint multi-hop loop + skip-unchanged for HotSync/FullSync"
```

---

## Task 13: Integration — submodule push, gitlink bumps, on-device smoke

**Files:** superproject gitlinks for the four submodules; CLAUDE.md status.

- [ ] **Step 1: Full clean build + suite**

Run: `cmake --build build -j 8 && ctest --test-dir build -j 8`
Expected: all green.

- [ ] **Step 2: Push each submodule**

```bash
for d in calendar contacts memo todos; do git -C src/plugins/$d push; done
```

(Each pushes its `feat: implement Sync::ChangeDetection` commit to its GitHub remote.)

- [ ] **Step 3: Bump the gitlinks in the superproject**

```bash
git add src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
git commit -m "chore(submodules): bump conduit gitlinks — Palm ChangeDetection"
```

- [ ] **Step 4: Update CLAUDE.md status**

Add a short "Multi-hop bidirectional sync — LANDED" subsection under Current state: what it does (one HotSync/FullSync now propagates both hops), the three layers, the `PalmRevisionStore` at `<profile>/.state/palm-revisions.ini`, and the on-device smoke test below. Commit.

- [ ] **Step 5: On-device smoke test (user, gated on a real Palm)**

Document for the user (do not automate):
1. Fresh profile, populated Akonadi calendar bound to datebook. HotSync once.
2. **Expected:** the calendar events appear on the Palm in this single HotSync (pass 1 pulls Akonadi→hub; pass 2 pushes hub→Palm; pass 3 settles). Previously this needed two HotSyncs.
3. Edit an event on the Palm, HotSync: the edit reaches Akonadi in one sync (outbound was always one-pass).
4. A HotSync with nothing changed should be fast and log `SyncEngine: skipping unchanged mapping …` for settled mappings.

---

## Notes for the implementer

- **Submodule builds:** local builds compile the submodule **working tree**, so Tasks 8–12 build/test before the submodules are pushed (Task 13). Do not bump gitlinks until the suite is green.
- **Multiple inheritance:** `PalmChangeDetection` is a non-`QObject` mixin (like the lib's `ChangeDetection`), so adding it as a second base of a `QObject`-derived backend is safe — no moc conflict. This mirrors `RemoteCalendarBackend : SyncBackend, ChangeDetection` in the library.
- **Why the loop settles in ≤3 passes:** the engine primes the revision cache from the *pre-pass* snapshot (`syncengine.cpp:1109-1133`), so a hub collection a route changes mid-pass-1 reads as "changed" in pass 2 (delivers to Palm), then equal in pass 3 (stop). Outbound completes in pass 1.
- **`m_syncMaxPass` reset:** `runAllMappings` only sets HotSync defaults when `m_syncMaxPass <= 1`; `dispatchSyncPass_` resets it to 1 at finish so the next run re-detects its mode. `fullSync` sets it to 2 before calling.
- **Clobber/Mirror untouched:** `clobberSync` and `runMirror` do not call `runAllMappings`; they keep their single-pass paths and never enable skip.
```
