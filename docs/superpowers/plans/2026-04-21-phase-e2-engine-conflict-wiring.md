# Phase E.2 — BlobSyncEngine ↔ BlobBaselineStore + ConflictStore (upstream libkalburator) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend `Kalburator::Sync::BlobSyncEngine` with `twoWayWithBaseline(...)` — a 3-way-merge sync operation that consults `BlobBaselineStore` (landed in E.1) for deletion detection, dispatches conflicts to per-backend `ConflictHandler`s via `ConflictHandlerRegistry`, and persists unresolved conflicts via `ConflictStore`. Tag `v0.8-phase-b4-engine-conflicts` when complete.

**Architecture:** Nine-case 3-way diff per (record ID) across (baseline, side-a, side-b). Each case maps to exactly one action: no-op, copy A→B, copy B→A, delete on A, delete on B, or raise conflict. Conflicts are handed to `handlers->handlerFor(sourceBackendId)->handleConflict(record, policy)` and the decision is applied — or deferred by persisting the `ConflictRecord` in `ConflictStore`.

**Tech Stack:** C++20, Qt6 (Core, Sql, Test), SQLite. Registry and stores are QSyncCore namespace.

**Repo:** All work in `~/dev/libkalburator/`. Mandatory PlanStan ctest gate before each commit.

**Design decision — registry ownership:** `ConflictHandlerRegistry` is passed into `twoWayWithBaseline` as a borrowed pointer. The engine does *not* own its own registry; the caller (typically `SyncCoordinator` or, in Wild Palms, a runtime constructed in E.15) owns one and passes it in. This matches the existing `SyncCoordinator::conflictRegistry()` pattern and keeps the engine stateless. **Not** adding `BlobSyncEngine::registerConflictHandler(...)` as originally sketched in the spec — the caller's registry is authoritative.

---

## File Structure

| Path | Role | Created / Modified |
|---|---|---|
| `~/dev/libkalburator/docs/phase0/04j-engine-conflict-wiring-design.md` | Design doc | Create |
| `~/dev/libkalburator/src/blob/blobsyncengine.h` | Add `twoWayWithBaseline` + `BlobSyncResult` extension | Modify |
| `~/dev/libkalburator/src/blob/blobsyncengine.cpp` | Implementation | Modify |
| `~/dev/libkalburator/tests/blob/tst_blobsyncengine.cpp` | Add 3-way-merge test cases | Modify |
| `~/dev/libkalburator/docs/phase0/README.md` | Phase map row + status | Modify |
| `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md` | Strike `BlobSyncEngine ↔ ConflictStore`, `registerConflictHandler` from deferred | Modify |
| `~/dev/libkalburator/docs/phase0/04j-engine-conflict-wiring-design.md` | Outcome section | Modify |

---

## Task 1: Design doc

**Files:** Create `~/dev/libkalburator/docs/phase0/04j-engine-conflict-wiring-design.md`.

- [ ] **Step 1: Write the design doc.**

```markdown
# Phase B4 — BlobSyncEngine ↔ BlobBaselineStore + ConflictStore

**Date:** 2026-04-21
**Status:** Approved for implementation.
**Phase tag on completion:** `v0.8-phase-b4-engine-conflicts`.

## Motivation

Phase B3 landed `BlobBaselineStore` but did not wire it into the engine.
`BlobSyncEngine::twoWayNaive` is stateless and cannot propagate deletions
or detect true conflicts. This phase closes that gap and completes the
B2-deferred items that block Wild Palms Phase E's integration tests.

## Surface

```cpp
namespace Kalburator::Sync {

BlobSyncResult twoWayWithBaseline(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy);

} // namespace Kalburator::Sync
```

`BlobSyncResult` gains `int conflicts` field (unresolved count).

## The nine-case 3-way diff

For each record ID seen on either side or in the baseline:

| baseline | side a | side b | action |
|---|---|---|---|
| present (hash B) | present (hash A) | present (hash B) | modified on A only → copy A to B |
| present (hash B) | present (hash B) | present (hash B') | modified on B only → copy B to A |
| present (hash B) | present (hash B) | present (hash B) | no change |
| present (hash B) | present (hash A) | present (hash B'), A≠B' | **conflict** → handler->handleConflict |
| present (hash B) | missing | present | deleted on A → delete on B |
| present (hash B) | present | missing | deleted on B → delete on A |
| missing | present | missing | new on A → create on B |
| missing | missing | present | new on B → create on A |
| missing | present (hash X) | present (hash X) | concurrent identical create → no-op (record new baseline) |
| missing | present (hash X) | present (hash Y), X≠Y | **conflict** → handler->handleConflict |

At end of successful run, `baseline.commitBaselines(mappingId, finalHashMap)`
reflects the synced state.

## Conflict handling

When a conflict is detected:
1. Build a `ConflictRecord` from the two `BackendRecord`s.
2. Look up `handler = handlers->handlerFor(a->backendId())`. If nullptr,
   fall back to `handlers->defaultHandler()`. If that is also nullptr,
   treat the conflict as deferred (persist and skip).
3. Call `handler->handleConflict(record, policy)` which returns a
   `ConflictDecision`.
4. Apply the decision: UseSource / UseTarget / Skip / (others treated
   as skip for now; Duplicate/UseBoth/Merge/DeleteBoth are out of
   scope for B4).
5. If the decision is `Skip` or `Pending`, persist the record via
   `conflicts->addConflict(record)` and increment `conflicts` count.

## Tests

Extend `tests/blob/tst_blobsyncengine.cpp` with new slots:
- `twoWayWithBaseline_noChanges` — all hashes match baseline.
- `twoWayWithBaseline_modifiedOnAOnly` — source update propagates.
- `twoWayWithBaseline_modifiedOnBOnly` — target update propagates.
- `twoWayWithBaseline_deletedOnA` — deletion propagates to B.
- `twoWayWithBaseline_deletedOnB` — deletion propagates to A.
- `twoWayWithBaseline_newOnA` — creation propagates to B.
- `twoWayWithBaseline_newOnB` — creation propagates to A.
- `twoWayWithBaseline_conflictInvokesHandler` — registered handler
  receives the ConflictRecord.
- `twoWayWithBaseline_conflictSkipPersists` — Skip decision routes
  to ConflictStore.
- `twoWayWithBaseline_baselineCommittedAtEnd` — post-sync baseline
  reflects synced state.

Test handler is a subclass of `ConflictHandler` that records invocations
and returns a configurable decision.

## Outcome

(Filled in during Task 14.)
```

- [ ] **Step 2: Commit.**

```bash
cd ~/dev/libkalburator
git add docs/phase0/04j-engine-conflict-wiring-design.md
git commit -m "Phase B4: design doc for engine ↔ conflict wiring

twoWayWithBaseline consumes B3's BlobBaselineStore for 3-way diff;
per-backend ConflictHandler dispatch via ConflictHandlerRegistry;
unresolved conflicts persisted to ConflictStore.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Extend header + BlobSyncResult + stub implementation

**Files:**
- Modify: `~/dev/libkalburator/src/blob/blobsyncengine.h`
- Modify: `~/dev/libkalburator/src/blob/blobsyncengine.cpp`

- [ ] **Step 1: Add forward declarations + method to `blobsyncengine.h`.**

Edit the top-of-file includes and forward-decls to add:

```cpp
#include "conflictpolicy.h"  // For Kalburator::Sync::QSyncCore::ConflictPolicy
```

Inside `namespace Kalburator::Sync {`, add near the existing forward decls:

```cpp
class BlobBaselineStore;

namespace QSyncCore {
    class ConflictHandlerRegistry;
    class ConflictStore;
}
```

Extend `BlobSyncStats` / `BlobSyncResult` — add `conflicts` field to stats:

```cpp
struct BlobSyncStats {
    int created   = 0;
    int updated   = 0;
    int deleted   = 0;
    int unchanged = 0;
    int errors    = 0;
    int conflicts = 0;  // NEW
};
```

Inside the `class BlobSyncEngine` body, after `twoWayNaive`, add:

```cpp
    /// Three-way sync consulting a baseline store. Propagates deletions
    /// correctly and dispatches conflicts to per-backend handlers.
    /// On successful completion, commits new baselines reflecting the
    /// synced state.
    BlobSyncResult twoWayWithBaseline(
        IBlobBackend *a,
        IBlobBackend *b,
        const QString &collectionId,
        const QString &mappingId,
        BlobBaselineStore *baseline,
        QSyncCore::ConflictHandlerRegistry *handlers,
        QSyncCore::ConflictStore *conflicts,
        const QSyncCore::ConflictPolicy &policy);
```

- [ ] **Step 2: Add includes + stub implementation in `blobsyncengine.cpp`.**

Add at the top of `blobsyncengine.cpp`:

```cpp
#include "blobbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictrecord.h"
#include "conflictstore.h"
```

At the bottom of the file (still inside `namespace Kalburator::Sync`), add a stub:

```cpp
BlobSyncResult BlobSyncEngine::twoWayWithBaseline(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy)
{
    Q_UNUSED(a); Q_UNUSED(b); Q_UNUSED(collectionId); Q_UNUSED(mappingId);
    Q_UNUSED(baseline); Q_UNUSED(handlers); Q_UNUSED(conflicts);
    Q_UNUSED(policy);

    BlobSyncResult result;
    result.success = false;
    result.errorMessage = QStringLiteral("twoWayWithBaseline: not implemented yet");
    return result;
}
```

- [ ] **Step 3: Build.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
```

Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.h src/blob/blobsyncengine.cpp
git commit -m "Phase B4 task 1: twoWayWithBaseline header + stub

Signature added; implementation returns not-implemented. BlobSyncStats
gains conflicts count.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: No-change case — `twoWayWithBaseline_noChanges`

**Files:**
- Modify: `~/dev/libkalburator/tests/blob/tst_blobsyncengine.cpp`
- Modify: `~/dev/libkalburator/src/blob/blobsyncengine.cpp`

- [ ] **Step 1: Add test.** Read `tests/blob/tst_blobsyncengine.cpp` first to find the existing TEST slot pattern; the test class name is likely `TestBlobSyncEngine`. Add helper + slot.

Add after the last existing slot declaration in the private slots section:

```cpp
    void twoWayWithBaseline_noChanges();
```

Add at the top of the file after existing includes:

```cpp
#include "blobbaselinestore.h"
#include "conflicthandlerregistry.h"
#include "conflictpolicy.h"
#include "conflictstore.h"
#include <QTemporaryDir>
```

Add the test body at the end of the file (before `QTEST_MAIN`):

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_noChanges()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Both sides have the same record with matching hash; baseline matches.
    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    BackendRecord rec = makeRecord(QStringLiteral("r1"),
                                   QStringLiteral("payload"));
    rec.contentHash = QStringLiteral("hash-1");
    a.createRecord(QStringLiteral("col"), rec);
    b.createRecord(QStringLiteral("col"), rec);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(r.sourceStats.unchanged + r.targetStats.unchanged, 2);
    QCOMPARE(r.sourceStats.created + r.targetStats.created, 0);
    QCOMPARE(r.sourceStats.updated + r.targetStats.updated, 0);
    QCOMPARE(r.sourceStats.deleted + r.targetStats.deleted, 0);
    QCOMPARE(r.sourceStats.conflicts + r.targetStats.conflicts, 0);
}
```

Check that the existing test file's `MockBlobBackend` constructor accepts a `backendId` parameter. If `MockBlobBackend()` is the only ctor, adjust the two lines to just `MockBlobBackend a;` and skip the id arg — most likely it does accept one since the engine needs distinct backend IDs for dispatch. Inspect the test file to confirm.

- [ ] **Step 2: Build + run; expect failure (returns not-implemented).**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobsyncengine
```

- [ ] **Step 3: Implement the skeleton of `twoWayWithBaseline`.**

Replace the stub in `blobsyncengine.cpp` with:

```cpp
BlobSyncResult BlobSyncEngine::twoWayWithBaseline(
    IBlobBackend *a,
    IBlobBackend *b,
    const QString &collectionId,
    const QString &mappingId,
    BlobBaselineStore *baseline,
    QSyncCore::ConflictHandlerRegistry *handlers,
    QSyncCore::ConflictStore *conflicts,
    const QSyncCore::ConflictPolicy &policy)
{
    Q_UNUSED(policy);

    BlobSyncResult result;
    if (!a || !b || !baseline) {
        result.success = false;
        result.errorMessage = QStringLiteral(
            "twoWayWithBaseline: null backend or baseline store");
        return result;
    }

    // Load all records from both sides + the baseline snapshot.
    const QList<BackendRecord> recsA = a->loadRecords(collectionId);
    const QList<BackendRecord> recsB = b->loadRecords(collectionId);

    QHash<QString, BackendRecord> byIdA;
    for (const auto &r : recsA) byIdA.insert(r.id, r);
    QHash<QString, BackendRecord> byIdB;
    for (const auto &r : recsB) byIdB.insert(r.id, r);

    QHash<QString, QString> baselineHashes;
    const QStringList baseIds = baseline->baselineRecordIds(mappingId);
    for (const QString &id : baseIds) {
        baselineHashes.insert(id, baseline->baselineHash(mappingId, id));
    }

    // Union of all record IDs seen.
    QSet<QString> allIds;
    for (const QString &id : byIdA.keys()) allIds.insert(id);
    for (const QString &id : byIdB.keys()) allIds.insert(id);
    for (const QString &id : baselineHashes.keys()) allIds.insert(id);

    QMap<QString, QString> finalHashes;  // What to commit as new baseline.

    for (const QString &id : allIds) {
        const bool hasA = byIdA.contains(id);
        const bool hasB = byIdB.contains(id);
        const bool hasBase = baselineHashes.contains(id);

        if (hasA && hasB && hasBase) {
            const auto &ra = byIdA.value(id);
            const auto &rb = byIdB.value(id);
            const QString bHash = baselineHashes.value(id);

            if (ra.contentHash == bHash && rb.contentHash == bHash) {
                // Case: no change.
                result.sourceStats.unchanged++;
                result.targetStats.unchanged++;
                finalHashes.insert(id, ra.contentHash);
            }
            // Other sub-cases land in subsequent tasks.
        }
        // Other top-level cases land in subsequent tasks.
    }

    // Commit the new baseline snapshot.
    if (!finalHashes.isEmpty()) {
        baseline->commitBaselines(mappingId, finalHashes);
    }

    Q_UNUSED(handlers);
    Q_UNUSED(conflicts);
    return result;
}
```

- [ ] **Step 4: Build + test pass.**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R tst_blobsyncengine
```

Expected: `twoWayWithBaseline_noChanges` passes. Other existing blob tests unchanged.

- [ ] **Step 5: Commit.**

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.cpp tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 2: twoWayWithBaseline no-change case

Implements the skeleton: load both sides + baseline, build union of
IDs, handle the 'all hashes match' no-op case. Subsequent tasks fill
in the other 8 cases.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Unilateral modification cases (a-only and b-only)

**Files:** same two files.

- [ ] **Step 1: Add tests.**

Slot declarations:

```cpp
    void twoWayWithBaseline_modifiedOnAOnly();
    void twoWayWithBaseline_modifiedOnBOnly();
```

Slot bodies:

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_modifiedOnAOnly()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    // Baseline = "v1". A has "v2" (modified). B still has "v1".
    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v2"));
    ra.contentHash = QStringLiteral("hash-v2");
    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    rb.contentHash = QStringLiteral("hash-v1");
    a.createRecord(QStringLiteral("col"), ra);
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    // B should now have the updated record.
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("hash-v2"));
    QCOMPARE(r.targetStats.updated, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_modifiedOnBOnly()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    ra.contentHash = QStringLiteral("hash-v1");
    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v2"));
    rb.contentHash = QStringLiteral("hash-v2");
    a.createRecord(QStringLiteral("col"), ra);
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    auto aRecs = a.loadRecords(QStringLiteral("col"));
    QCOMPARE(aRecs.size(), 1);
    QCOMPARE(aRecs.first().contentHash, QStringLiteral("hash-v2"));
    QCOMPARE(r.sourceStats.updated, 1);
}
```

- [ ] **Step 2: Build + run; expect 2 new failures.**

- [ ] **Step 3: Extend the main loop in `twoWayWithBaseline`** — replace the `if (hasA && hasB && hasBase)` block with:

```cpp
        if (hasA && hasB && hasBase) {
            const auto &ra = byIdA.value(id);
            const auto &rb = byIdB.value(id);
            const QString bHash = baselineHashes.value(id);

            if (ra.contentHash == bHash && rb.contentHash == bHash) {
                // No change.
                result.sourceStats.unchanged++;
                result.targetStats.unchanged++;
                finalHashes.insert(id, ra.contentHash);
            } else if (ra.contentHash != bHash && rb.contentHash == bHash) {
                // Modified on A only → propagate to B.
                BackendRecord updated = ra;
                if (b->updateRecord(updated)) {
                    result.targetStats.updated++;
                    finalHashes.insert(id, ra.contentHash);
                } else {
                    result.targetStats.errors++;
                }
            } else if (ra.contentHash == bHash && rb.contentHash != bHash) {
                // Modified on B only → propagate to A.
                BackendRecord updated = rb;
                if (a->updateRecord(updated)) {
                    result.sourceStats.updated++;
                    finalHashes.insert(id, rb.contentHash);
                } else {
                    result.sourceStats.errors++;
                }
            }
            // Conflict case (both modified) lands in Task 6.
        }
```

- [ ] **Step 4: Build + test.** Expected: both new tests pass. Commit.

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.cpp tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 3: unilateral modification cases

A-only and B-only updates propagate to the other side. Baseline
records the post-sync hash.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Deletion propagation cases (5 and 6)

**Files:** same two.

- [ ] **Step 1: Add tests.**

Slots:

```cpp
    void twoWayWithBaseline_deletedOnA();
    void twoWayWithBaseline_deletedOnB();
```

Bodies (A-deleted case):

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_deletedOnA()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    // Baseline records that r1 was synced. A now missing, B still has.
    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    rb.contentHash = QStringLiteral("hash-v1");
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.targetStats.deleted, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_deletedOnB()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    ra.contentHash = QStringLiteral("hash-v1");
    a.createRecord(QStringLiteral("col"), ra);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 0);
    QCOMPARE(r.sourceStats.deleted, 1);
}
```

- [ ] **Step 2: Build + run; expect failures.**

- [ ] **Step 3: Extend the main loop** — add branches for missing-on-one-side cases. Insert after the `hasA && hasB && hasBase` block:

```cpp
        else if (!hasA && hasB && hasBase) {
            // Deleted on A since baseline → delete on B.
            const auto &rb = byIdB.value(id);
            if (b->deleteRecord(rb.id)) {
                result.targetStats.deleted++;
                // Don't add to finalHashes — record is gone.
            } else {
                result.targetStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        }
        else if (hasA && !hasB && hasBase) {
            // Deleted on B since baseline → delete on A.
            const auto &ra = byIdA.value(id);
            if (a->deleteRecord(ra.id)) {
                result.sourceStats.deleted++;
            } else {
                result.sourceStats.errors++;
                finalHashes.insert(id, baselineHashes.value(id));
            }
        }
```

- [ ] **Step 4: Build + test.** Commit.

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.cpp tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 4: deletion propagation

When a record exists in baseline but is missing from one side, it's
treated as a deletion and propagated to the other side. This is the
core capability baselines enable.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: New-record propagation cases (7 and 8)

**Files:** same two.

- [ ] **Step 1: Add tests.**

Slots:

```cpp
    void twoWayWithBaseline_newOnA();
    void twoWayWithBaseline_newOnB();
```

Bodies (new-on-A):

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_newOnA()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    // No baseline; A has record, B doesn't.
    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    ra.contentHash = QStringLiteral("hash-v1");
    a.createRecord(QStringLiteral("col"), ra);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.targetStats.created, 1);
}

void TestBlobSyncEngine::twoWayWithBaseline_newOnB()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v1"));
    rb.contentHash = QStringLiteral("hash-v1");
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    ConflictHandlerRegistry reg;
    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(a.loadRecords(QStringLiteral("col")).size(), 1);
    QCOMPARE(r.sourceStats.created, 1);
}
```

- [ ] **Step 2: Extend the loop** — add branches for !hasBase:

```cpp
        else if (hasA && !hasB && !hasBase) {
            // New on A → create on B.
            const auto &ra = byIdA.value(id);
            const QString newId = b->createRecord(collectionId, ra);
            if (!newId.isEmpty()) {
                result.targetStats.created++;
                finalHashes.insert(id, ra.contentHash);
            } else {
                result.targetStats.errors++;
            }
        }
        else if (!hasA && hasB && !hasBase) {
            // New on B → create on A.
            const auto &rb = byIdB.value(id);
            const QString newId = a->createRecord(collectionId, rb);
            if (!newId.isEmpty()) {
                result.sourceStats.created++;
                finalHashes.insert(id, rb.contentHash);
            } else {
                result.sourceStats.errors++;
            }
        }
```

- [ ] **Step 3: Build + test + commit.**

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.cpp tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 5: new-record propagation

Records present on exactly one side with no baseline get created on
the other side.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Conflict detection + handler dispatch

**Files:** same two.

- [ ] **Step 1: Add a test ConflictHandler class + test.**

In `tst_blobsyncengine.cpp`, above the test class declaration, add:

```cpp
class TestHandler : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    int invocations = 0;
    Kalburator::Sync::QSyncCore::ConflictDecision decision =
        Kalburator::Sync::QSyncCore::ConflictDecision::UseSource;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &) override
    {
        invocations++;
        return decision;
    }
};
```

Slot:

```cpp
    void twoWayWithBaseline_conflictInvokesHandler();
```

Body:

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_conflictInvokesHandler()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;
    using Kalburator::Sync::QSyncCore::ConflictDecision;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    // Baseline = h-v1. Both sides diverged.
    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v2a"));
    ra.contentHash = QStringLiteral("hash-v2a");
    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v2b"));
    rb.contentHash = QStringLiteral("hash-v2b");
    a.createRecord(QStringLiteral("col"), ra);
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::UseSource;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(handler.invocations, 1);

    // UseSource → B gets A's version.
    auto bRecs = b.loadRecords(QStringLiteral("col"));
    QCOMPARE(bRecs.size(), 1);
    QCOMPARE(bRecs.first().contentHash, QStringLiteral("hash-v2a"));
}
```

- [ ] **Step 2: Extend the `hasA && hasB && hasBase` block** with the else-case for divergent hashes (add after the modified-on-B-only branch):

```cpp
            else {
                // Both modified → conflict.
                QSyncCore::ConflictRecord cr;
                cr.conflictId = QStringLiteral("%1:%2").arg(mappingId, id);
                cr.conduitId = mappingId;
                cr.source.id.backendId = a->backendId();
                cr.source.id.recordId = ra.id;
                cr.source.contentHash = ra.contentHash;
                cr.source.content = ra.data;
                cr.source.lastModified = ra.lastModified;
                cr.target.id.backendId = b->backendId();
                cr.target.id.recordId = rb.id;
                cr.target.contentHash = rb.contentHash;
                cr.target.content = rb.data;
                cr.target.lastModified = rb.lastModified;

                QSyncCore::ConflictHandler *h = handlers
                    ? handlers->handlerFor(a->backendId())
                    : nullptr;

                QSyncCore::ConflictDecision decision =
                    QSyncCore::ConflictDecision::Pending;
                if (h) {
                    decision = h->handleConflict(cr, policy);
                }

                if (decision == QSyncCore::ConflictDecision::UseSource) {
                    if (b->updateRecord(ra)) {
                        result.targetStats.updated++;
                        finalHashes.insert(id, ra.contentHash);
                    } else {
                        result.targetStats.errors++;
                    }
                } else if (decision == QSyncCore::ConflictDecision::UseTarget) {
                    if (a->updateRecord(rb)) {
                        result.sourceStats.updated++;
                        finalHashes.insert(id, rb.contentHash);
                    } else {
                        result.sourceStats.errors++;
                    }
                } else {
                    // Skip / Pending / unsupported → defer.
                    if (conflicts) {
                        conflicts->addConflict(cr);
                    }
                    result.sourceStats.conflicts++;
                    // Keep baseline at current so we revisit next sync.
                    finalHashes.insert(id, bHash);
                }
            }
```

Note: the `ConflictRecord::source.id` / `target.id` structure depends on `RecordSnapshot` / `RecordId` actual shape in `~/dev/libkalburator/src/conflict/conflictrecord.h`. Inspect that file first and adjust the field assignments to match.

- [ ] **Step 3: Build.** If field names don't match, read `conflictrecord.h` and fix. Common likely differences: `cr.source.id` may be a single string field, not a struct with `backendId`/`recordId`. Check `RecordSnapshot` definition.

- [ ] **Step 4: Test + commit.**

```bash
cd ~/dev/libkalburator
git add src/blob/blobsyncengine.cpp tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 6: conflict detection + handler dispatch

Two-sided modification since baseline raises a conflict. Handler is
looked up via ConflictHandlerRegistry keyed by source backend ID.
UseSource / UseTarget decisions apply immediately; Skip / Pending
route to ConflictStore and leave the baseline untouched so the
conflict recurs next sync.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8: Deferred-conflict persistence test

**Files:** same two.

- [ ] **Step 1: Add test.**

Slot:

```cpp
    void twoWayWithBaseline_conflictSkipPersists();
```

Body:

```cpp
void TestBlobSyncEngine::twoWayWithBaseline_conflictSkipPersists()
{
    using Kalburator::Sync::BlobBaselineStore;
    using Kalburator::Sync::QSyncCore::ConflictHandlerRegistry;
    using Kalburator::Sync::QSyncCore::ConflictStore;
    using Kalburator::Sync::QSyncCore::ConflictPolicy;
    using Kalburator::Sync::QSyncCore::ConflictDecision;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    MockBlobBackend a(QStringLiteral("a"));
    MockBlobBackend b(QStringLiteral("b"));
    a.createCollection(makeCollection(QStringLiteral("col")));
    b.createCollection(makeCollection(QStringLiteral("col")));

    BackendRecord ra = makeRecord(QStringLiteral("r1"), QStringLiteral("v2a"));
    ra.contentHash = QStringLiteral("hash-v2a");
    BackendRecord rb = makeRecord(QStringLiteral("r1"), QStringLiteral("v2b"));
    rb.contentHash = QStringLiteral("hash-v2b");
    a.createRecord(QStringLiteral("col"), ra);
    b.createRecord(QStringLiteral("col"), rb);

    BlobBaselineStore base(dir.filePath(QStringLiteral(".planstan-sync.db")));
    QVERIFY(base.setBaseline(QStringLiteral("m1"),
                             QStringLiteral("r1"),
                             QStringLiteral("hash-v1")));

    ConflictHandlerRegistry reg;
    TestHandler handler;
    handler.decision = ConflictDecision::Skip;
    reg.registerHandler(QStringLiteral("a"), &handler);

    ConflictStore store;
    ConflictPolicy policy;

    BlobSyncEngine engine;
    BlobSyncResult r = engine.twoWayWithBaseline(
        &a, &b, QStringLiteral("col"), QStringLiteral("m1"),
        &base, &reg, &store, policy);

    QVERIFY2(r.success, qUtf8Printable(r.errorMessage));
    QCOMPARE(handler.invocations, 1);
    QCOMPARE(r.sourceStats.conflicts, 1);
    QCOMPARE(store.pendingConflicts().size(), 1);

    // Neither side was touched.
    QCOMPARE(a.loadRecords(QStringLiteral("col")).first().contentHash,
             QStringLiteral("hash-v2a"));
    QCOMPARE(b.loadRecords(QStringLiteral("col")).first().contentHash,
             QStringLiteral("hash-v2b"));
}
```

- [ ] **Step 2: Test should pass** (Task 7's impl handles Skip by routing to ConflictStore). If it fails, inspect `ConflictStore::pendingConflicts()` return shape and `ConflictStore::addConflict` behavior.

- [ ] **Step 3: Commit.**

```bash
cd ~/dev/libkalburator
git add tests/blob/tst_blobsyncengine.cpp
git commit -m "Phase B4 task 7: Skip decision persists to ConflictStore

Characterises deferred resolution — conflicts routed to ConflictStore
are available for later batch review. Records are untouched until
resolution.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: Full libkalburator ctest

- [ ] **Step 1:**

```bash
cd ~/dev/libkalburator
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Expected: all 4 existing test executables (including extended tst_blobsyncengine) pass.

---

## Task 10: PlanStan ctest gate

- [ ] **Step 1:**

```bash
cmake --build ~/dev/PlanStan/build -j"$(nproc)"
cd ~/dev/PlanStan/build
WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORM=wayland ctest -j"$(nproc)" 2>&1 | tail -5
```

Expected: no new failures vs Phase B3 run (81 pass / 6 actual fail / 18 not-run). The 6 known-failing integration/sync_error_recovery tests are pre-existing.

---

## Task 11: Phase index + design doc updates

**Files:**
- Modify: `~/dev/libkalburator/docs/phase0/README.md`
- Modify: `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md`
- Modify: `~/dev/libkalburator/docs/phase0/04j-engine-conflict-wiring-design.md`

- [ ] **Step 1: 04j Outcome section.**

Append:

```markdown
## Outcome (2026-04-21)

Landed as planned. `twoWayWithBaseline` handles all nine diff cases.
Eight new test slots in `tst_blobsyncengine.cpp` covering no-change,
unilateral A/B modifications, deletion propagation both ways,
creation propagation both ways, and conflict detection with
UseSource / Skip resolutions exercised.

`BlobSyncStats` gained `conflicts` count field. `ConflictHandlerRegistry`
is passed into the engine rather than owned by it — engine stays
stateless, caller (SyncCoordinator or WP runtime) owns the registry.

PlanStan ctest held baseline. No new failures.
```

- [ ] **Step 2: 04h deferred-list updates.**

Strike the following items (add ✅ landed-in-B4 note):

- `ConflictStore` integration inside `BlobSyncEngine`
- `AutomaticConflictHandler` wired into `BlobSyncEngine`
- `BlobSyncEngine::registerConflictHandler(backendId, handler)` — note that we landed the registry-passed-in variant, not the method variant.

- [ ] **Step 3: README.md phase map row.**

Add after Phase B3 row:

```markdown
| **Phase B4** — engine ↔ conflict wiring | done 2026-04-21 | `04j-engine-conflict-wiring-design.md` | BlobSyncEngine::twoWayWithBaseline consumes B3's BlobBaselineStore for 3-way diff. Per-backend ConflictHandler dispatch via external ConflictHandlerRegistry (passed in, not owned). Unresolved conflicts persist to ConflictStore. Eight new test slots in tst_blobsyncengine. Tag: `v0.8-phase-b4-engine-conflicts`. Unblocks WP Phase E.3+. |
```

Update **Current status** "Done" list: add B4 bullet.
Update **Next:** line: Phase 4 (Wild Palms adoption) can now proceed to WP Phase E.3+ on the WP side.
Bump "Last updated" date.

- [ ] **Step 4: Commit.**

```bash
cd ~/dev/libkalburator
git add docs/phase0/
git commit -m "Phase B4 task 8: phase index + design doc updates

README phase map gains B4 row. 04h deferred-list strikes the three
conflict-wiring items (ConflictStore integration, AutomaticConflictHandler,
registerConflictHandler — landed as registry-passed-in variant). 04j
gains Outcome section.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: Tag v0.8-phase-b4-engine-conflicts

- [ ] **Step 1:**

```bash
cd ~/dev/libkalburator
git tag -a v0.8-phase-b4-engine-conflicts -m "Phase B4: engine ↔ conflict wiring

BlobSyncEngine::twoWayWithBaseline — 3-way-merge sync operation
consulting B3's BlobBaselineStore for deletion detection. Per-backend
ConflictHandler dispatch via externally-owned ConflictHandlerRegistry.
Unresolved conflicts persist to ConflictStore for deferred review.

Eight new test slots covering all diff cases + conflict dispatch.
PlanStan ctest baseline held."

git tag -l | grep v0.8
```

---

## Self-Review

Spec coverage:
- `twoWayWithBaseline` signature + 9-case diff — Tasks 3-7. ✓
- `ConflictHandlerRegistry` dispatch — Task 7. ✓
- `ConflictStore` integration — Task 7-8. ✓
- `BlobBaselineStore` consumed — Task 3 and onward. ✓
- PlanStan ctest gate — Task 10. ✓
- README + deferred-list updates — Task 11. ✓
- Tag — Task 12. ✓

Placeholder scan: no TBD/TODO. Code blocks contain full content. The one "inspect this file first" note (Task 7 Step 3 on `ConflictRecord` field names) is necessary because the precise field structure isn't certain from the spec alone — the executor must confirm field shape against `conflictrecord.h`.

Type consistency:
- `BlobSyncResult::conflicts` count vs `sourceStats.conflicts` — chose the stats-based field. Single place.
- `ConflictDecision::UseSource` vs `Source` — matches the enum in `synccommon.h`/`conflictrecord.h`.
- `registerHandler(backendId, handler)` matches existing `ConflictHandlerRegistry` API.
