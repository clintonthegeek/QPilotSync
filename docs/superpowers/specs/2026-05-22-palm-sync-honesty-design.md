# Palm-Sync Honesty — design

**Date:** 2026-05-22
**Status:** Design approved through brainstorming. Spec ready for plan.
**Phase:** F.2 + bug-cluster (related to mass-delete-guard followup)
**Predecessors:**
- F.1b ✅ (`2026-05-22-f1b-file-menu-design.md`)
- Mass-delete guard ✅ (`2026-05-22-mass-delete-guard.md` + libkalburator `v0.54-mass-delete-guard`)
- Rawfiles path alignment ✅ (commit `a8f686f`)

---

## 1. Why this exists

Two manual reproductions of the F.1b followup `rm -rf rawfiles` scenario
made the engine destroy 84 Palm contacts on the first run and silently
no-op every subsequent calendar/todo sync. Instrumenting the engine
exposed four interlocking bugs:

1. **Hash instability** (`PalmRecord::toWireBytes()`): the wire-bytes
   serialization includes mutable per-sync metadata (`attributes`
   carrying the dirty bit, `lastModified`). Two reads of the same
   unchanged Palm record produce different wire bytes → different
   `sha256` content hashes → `perRecordDiff::equalRecords()` returns
   false → engine classifies every record as a conflict.
2. **Silent conflict drop in Unmonitored AskUser**
   (`syncengine.cpp:2253-2272`): when the per-mapping policy is
   `AskUser` and the sync mode is `Unmonitored` (the WildPalms
   default for auto-mappings), each conflict is emitted via
   `conflictDetected` signal — but WildPalms has no listener. The
   conflict goes into `unresolvedConflicts` and `conflictsDeferred`,
   `finalSource` / `finalTarget` stay empty, both `applyBatch` calls
   are skipped, and the engine reports "completed" with no work
   applied and no user-visible signal.
3. **Asymmetric dbName round-trip in `decodeRecordId` — affects `ToDoDB` only**
   (corrected post-landing 2026-05-22). `encodeCollectionId` lowercases the
   bare dbName (`MemoDB`→`palm:memo`, `DatebookDB`→`palm:datebook`,
   `AddressDB`→`palm:address`, `ToDoDB`→`palm:todo`). `decodeRecordId`
   uppercases the first letter and appends `DB`, so `palm:todo` round-trips
   to `TodoDB` — *not* `ToDoDB` — and the device rejects it. The other
   three names round-trip cleanly. **Original spec claim that calendar
   and memo were broken was wrong**: their `decodeRecordId` path
   produces `DatebookDB` / `MemoDB`, which the device accepts. In
   practice todos already used the explicit `deletePalmRecord("ToDoDB", rid)`
   form, so the broken path was never exercised in production either.
   The sub-project B work on calendar+memo was therefore a *consistency*
   change (all four plugins now hardcode the canonical dbName at the
   call site), not a correctness fix. See retrospective note in §11.
4. **Mass-delete guard unreachable**: the guard sits inside
   `applyBatch`. While Bug 1 and Bug 2 are active, the engine never
   reaches `applyBatch` for the unchanged-records-vs-empty-target
   scenario (every record becomes a deferred conflict). The
   guard cannot fire end-to-end against the original-destruction
   reproduction.

The destruction observed in commit `a8f686f` was a "perfect storm":
fresh baselines (so hashes still matched what the Palm reported on
the second read), Bug 3 happening to allow the destructive deletes on
contacts but silently swallow them on calendar/memo, and no
mass-delete guard. The bugs together produce *unpredictable*
behavior: either nothing happens (Bug 1 + Bug 2) or 84 records
disappear silently (Bug 3 on a plugin with working `deleteRecord`).

This spec fixes all four as one coherent F.2 deliverable, plus
formalizes the contract for conflict surfacing in WildPalms.

## 2. Scope

### 2.1 In scope (five sub-projects)

- **A. Hash stability** — `PalmRecord::contentHash()` returns a
  deterministic SHA-256 over the stable identity fields (recordId,
  category, data); plugin backends switch to it. Wire-bytes
  serialization unchanged (still used for transport).
- **B. Canonical `deleteRecord`** — calendar/memo plugin backends
  switch to the dbName-aware `deletePalmRecord` path with hardcoded
  canonical DB names matching contacts/todos.
- **C. Default mapping policy = `LastWriteWins`** —
  `PalmRuntime::finishConnect` auto-mappings ship with
  `conflictPolicy = LastWriteWins`. User-configured mappings via
  `MappingEditorDialog` keep their explicit policy.
- **D. Conflict surfacing wired** — WildPalms listens to
  `SyncEngine::conflictDetected`, persists to `SyncConflictStore`,
  shows a status-bar badge with the unresolved count, opens
  `ConflictReviewDialog` on click. Resolutions persist and apply on
  the next sync.
- **E. Mass-delete guard verification** — an end-to-end integration
  test (libkalburator + WildPalms) reproducing the original
  destruction scenario (baselined+matched+target-emptied) and
  proving the guard fires.

### 2.2 Out of scope

- Phase F.3 (per-Palm-category routing UX), F.4 (Radicale E2E +
  user docs) — separate F-phase sub-projects.
- New plugin types (Plucker, WebCalendar) — already shipped per E.13
  / E.14.
- Re-architecting the engine's diff/merge model. The 5 fixes here
  operate within the existing model.
- Migration of users with the destruction-corrupted profile (84
  contacts gone). Test profiles only; no migration story.
- Hardening account password storage to KWallet (deferred to F.1c
  per F.1a §2).

## 3. Confirmed design choices

Established during 2026-05-22 brainstorming:

1. **Per-mapping conflict policy is preserved** as the user-facing
   knob. Only the *default* changes (AskUser → LastWriteWins for
   auto-mappings). Existing AskUser mappings remain valid.
2. **Conflict surfacing UI**: status-bar badge ("N conflicts pending")
   + on-demand non-modal `ConflictReviewDialog` (already exists at
   `src/widgets/dialogs/conflictreviewdialog.{h,cpp}`). No modal
   blocking; deferred queue model.
3. **Hash stability fix lives in the Palm-side plugin layer**
   (`PalmRecord` + per-plugin backends), not in the libkalburator
   engine. Backend peculiarities degrade in the backend per project
   convention.
4. **`deleteRecord` fix mirrors contacts/todos pattern**: hardcoded
   canonical dbName per plugin, not a fix to `decodeRecordId`. The
   decoder stays whatever it is; plugins don't depend on it.
5. **One unified spec, multiple plans**: writing-plans produces five
   independently-executable plans (one per sub-project A–E). Plans
   ship in dependency order A→B→C→D→E. Each plan = one PR / coherent
   commit series.
6. **Mass-delete guard threshold stays as-is** (>10 absolute OR >25%
   relative). No change to the libkalburator API or contract.

## 4. Architecture per sub-project

### 4.1 Sub-project A — Hash stability

**Root cause analysis** (from instrumented sync, log
`/tmp/wp-diag.log`):

`PalmRecord::toWireBytes()` (in `src/palm/sync/palmrecord.h`)
serializes:

```cpp
ds << static_cast<quint32>(recordId)
   << static_cast<quint8>(category)
   << static_cast<quint8>(attributes)   // ← dirty bit, busy bit
   << data
   << lastModified;                     // ← mutable per-sync
```

Both `attributes` (AttrDirty / AttrBusy flip on the device between
syncs) and `lastModified` (re-derived from device DB mtime on every
read) vary between reads of the same content-unchanged record. The
sha256 of these bytes therefore varies. The engine's `perRecordDiff`
compares Palm-side `contentHash` to the baselined `contentHash` and
sees them as different.

Confirmed empirically: 582 calendar records and 18 todo records ALL
hashed differently on the second read of the unchanged profile —
zero matches, 100% mis-classified as conflicts.

**Fix:** introduce a new method `PalmRecord::contentHash()` returning
a deterministic SHA-256 over only the stable identity fields:

```cpp
QString contentHash() const
{
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << static_cast<quint32>(recordId)
       << static_cast<quint8>(category)
       << data;
    return QCryptographicHash::hash(buf, QCryptographicHash::Sha256).toHex();
}
```

(Helper lives on `PalmRecord` so all plugin backends use the same
algorithm. The existing free function `sha256Hex` per-plugin is
removed; plugin backends call `pr.contentHash()` instead of
`sha256Hex(pr.toWireBytes())`.)

**Why include `recordId` + `category` but not `attributes` /
`lastModified`:**
- `recordId` is part of identity (different ids = different records,
  even with same payload).
- `category` is user-meaningful state (moving a contact from Personal
  to Unfiled is a content change).
- `attributes` carries sync-protocol bits (dirty, busy) that the
  device mutates; not content.
- `lastModified` is a sync signal (set on write, refreshed on read);
  not content.

**`toWireBytes()` is preserved unchanged.** It's still used for
in-process transport (plugin → engine → plugin via the
transformation pipeline). Round-trip property unaffected.

**Files affected:**
- `src/palm/sync/palmrecord.h` — add `contentHash()`; add
  `#include <QCryptographicHash>`.
- `src/plugins/calendar/palmcalendarbackend.cpp` — replace
  `sha256Hex(br.data)` with `pr.contentHash()` in the three call
  sites at lines 125, 147, 222. Delete local `sha256Hex` helper at
  line 21.
- `src/plugins/contacts/palmcontactsbackend.cpp` — same three
  sites (122, 141, 214); delete local `sha256Hex` at line 19.
- `src/plugins/memo/memoblobbackend.cpp` and
  `src/plugins/todos/todoblobbackend.cpp` — same pattern (these
  use the same `sha256Hex(br.data)` idiom).

**Test:** a `tst_palmrecord_contenthash` test that:
1. Constructs a `PalmRecord` with fixed `data`, `recordId`,
   `category`.
2. Mutates `attributes` and `lastModified` repeatedly.
3. Asserts `contentHash()` stays equal across all mutations.
4. Mutates `data` → asserts hash changes.
5. Mutates `category` → asserts hash changes.

### 4.2 Sub-project B — Canonical `deleteRecord`

**Current state** (corrected post-landing 2026-05-22 — see §1 Bug 3 retraction):

| Plugin | Implementation | Works? | Round-trip via `decodeRecordId` |
|--------|----------------|--------|----------------------------------|
| Contacts | `m_palmBackend->deletePalmRecord("AddressDB", rid)` | ✅ | `AddressDB` → `palm:address` → `AddressDB` ✅ |
| Todos    | `m_palmBackend->deletePalmRecord("ToDoDB", rid)` | ✅ | `ToDoDB` → `palm:todo` → **`TodoDB`** ❌ (asymmetric) |
| Calendar | (was) `m_palmBackend->deleteRecord(recordId)` → decoder yields `DatebookDB` | ✅ | `DatebookDB` → `palm:datebook` → `DatebookDB` ✅ |
| Memo     | (was) `m_palmBackend->deleteRecord(recordId)` → decoder yields `MemoDB` | ✅ | `MemoDB` → `palm:memo` → `MemoDB` ✅ |

**Original spec table claimed calendar/memo were ❌; this was wrong.**
The only dbName that doesn't round-trip is `ToDoDB` (camelCase capital `D`
becomes lowercase `d` on decode). Todos already used the explicit
`deletePalmRecord("ToDoDB", rid)` call site, so even the one broken
round-trip was never exercised in production. Sub-project B brought
calendar+memo into line with the explicit-dbName pattern for
**consistency**, not because they were destructive. See §11.

**Fix:** rewrite `PalmCalendarBackend::deleteRecord` and
`MemoBlobBackend::deleteRecord` to mirror the contacts/todo pattern:

```cpp
// PalmCalendarBackend::deleteRecord
bool PalmCalendarBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid)) return false;
    return m_palmBackend->deletePalmRecord(QStringLiteral("DatebookDB"), rid);
}

// MemoBlobBackend::deleteRecord
bool MemoBlobBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid)) return false;
    return m_palmBackend->deletePalmRecord(QStringLiteral("MemoDB"), rid);
}
```

`decodeId` exists in both plugin backends already (used by
`updateRecord`).

**Files affected:**
- `src/plugins/calendar/palmcalendarbackend.cpp` — rewrite
  `deleteRecord` at line 197.
- `src/plugins/memo/memoblobbackend.cpp` — rewrite `deleteRecord`
  at line 185.

**Test:** extend each plugin's existing backend test to cover the
delete path against a stub Palm device that records the call. Assert
`deletePalmRecord` is called with the canonical dbName and the
expected numeric record id.

**Safety:** this change makes calendar and memo destructive on the
delete path. The mass-delete guard (already shipped in libkalburator
`v0.54`) is the safety net. Sub-project E proves it works end-to-end
before this change can damage real data.

### 4.3 Sub-project C — Default mapping policy = `LastWriteWins`

**Current state:** `PalmRuntime::finishConnect` at
`src/runtime/palmruntime.cpp:336-344` constructs auto-mappings
without setting `conflictPolicy`, so it inherits the
`SyncMapping::conflictPolicy` default of `AskUser`
(libkalburator `src/types/synctypes.h:231`).

**Fix:** add one line:

```cpp
Kalburator::Sync::SyncMapping m;
m.id             = QStringLiteral("default-%1-%2").arg(id, safeColId);
m.sourceBackend  = id;
m.targetBackend  = pcId;
m.sourceCalendar = palmCol.id;
m.targetCalendar = safeColId;
m.mode           = Kalburator::Sync::SyncMode::TwoWay;
m.conflictPolicy = Kalburator::Sync::ConflictResolution::LastWriteWins;  // NEW
m.enabled        = true;
m_mappings.append(m);
```

**Why `LastWriteWins`:**
- The PC and Palm both carry `lastModified`. For PC, RawFilesBackend
  records use file mtime (per existing libkalburator behavior). For
  Palm, `PalmRecord::lastModified` is set from the device's
  modification timestamp.
- For the modify-delete case (one side has a record, the other
  doesn't), libkalburator's `LastWriteWins` (`syncengine.cpp:2286`)
  says "the modifier always wins via >= comparison" — the missing
  side has an invalid timestamp.
- For both-modified, the side with the newer timestamp wins.
- Avoids per-conflict prompts in the common case (recently-edited
  Palm record, never-edited PC version).

**User-configured override:** `MappingEditorDialog` already exposes
`conflictPolicy` as a per-mapping setting. Users who explicitly
choose AskUser for a mapping get the AskUser flow (sub-project D
handles surfacing).

**Files affected:**
- `src/runtime/palmruntime.cpp` — one-line change at line 343 (or
  wherever the `m.mode = TwoWay` line is).

**Test:** add to `tst_palm_runtime_default_mappings_only_when_empty`
(or sibling): assert that newly-created auto-mappings have
`conflictPolicy == LastWriteWins`.

### 4.4 Sub-project D — Conflict surfacing wired

**Background:** the engine already emits
`SyncEngine::conflictDetected(const ConflictInfo &)` (per
`syncengine.h:615`) and persists deferred conflicts via
`SyncConflictStore` (per `syncengine.h:406-409`, set on the engine
during `PalmRuntime` construction). What's missing is the
WildPalms-side listener + UI badge + on-demand dialog.

**`ConflictReviewDialog`** already exists at
`src/widgets/dialogs/conflictreviewdialog.{h,cpp}` (shipped earlier
in M5/F.1a era). It accepts a `SyncConflictStore *` and shows the
pending list. Sub-project D wires it up; no new dialog needs to be
written.

**Wiring sketch:**

1. **Conflict signal forwarded by PalmRuntime.** Rather than expose
   the embedded `SyncEngine`, `PalmRuntime` re-emits the engine's
   `conflictDetected` signal. In `PalmRuntime`'s ctor (right after
   `m_engine` construction):

   ```cpp
   connect(m_engine.get(),
           &Kalburator::Sync::SyncEngine::conflictDetected,
           this, &PalmRuntime::conflictDetected);
   ```

   With a public PalmRuntime signal:

   ```cpp
   // palmruntime.h
   signals:
       void conflictDetected(const Kalburator::Sync::ConflictInfo &info);
   ```

   `KF6MainWindow::loadProfile` (after constructing PalmRuntime)
   connects to this signal:

   ```cpp
   connect(m_palmRuntime.get(),
           &WildPalms::Runtime::PalmRuntime::conflictDetected,
           this, &KF6MainWindow::onConflictDetected);
   ```

   No engine accessor needed; the abstraction boundary holds.

2. **`KF6MainWindow::onConflictDetected(const ConflictInfo &)`** slot:
   - Increments `m_pendingConflictCount`.
   - Updates the status-bar badge widget (see step 3).
   - The conflict is already persisted to `SyncConflictStore` by the
     engine itself; the slot only updates UI state.

3. **Status-bar badge.** A new `QLabel` (or custom `QPushButton`
   styled flat) added to `KF6MainWindow::statusBar()` showing
   `"⚠ %1 conflicts pending".arg(count)` when count > 0, hidden when
   count == 0. Wired with `clicked` signal that opens the
   `ConflictReviewDialog`.

4. **`ConflictReviewDialog` (existing).** Non-modal,
   `Qt::WA_DeleteOnClose`. Constructor takes the
   `SyncConflictStore *` (borrowed from `m_palmRuntime`). User
   resolves conflicts inside the dialog; resolutions write back to
   the store. On dialog close, refresh the badge count.

5. **Badge reset on sync completion.** After a successful sync
   (engine's `syncCompleted` / `allSyncsCompleted` signal),
   re-read the unresolved count from `SyncConflictStore` and update
   the badge. Resolved conflicts disappear; new ones may appear.

6. **On startup.** `KF6MainWindow::loadProfile` also reads the
   `SyncConflictStore` once to seed the badge (in case a prior
   session left unresolved conflicts).

**Resolution → next-sync apply.** Existing libkalburator behavior:
`SyncConflictStore` is consulted at the start of each mapping's
sync; resolved entries override the diff classification for those
record ids. No new wiring needed for the resolution-apply side.

**Files affected:**
- `src/runtime/palmruntime.h` / `.cpp` — add `conflictDetected`
  signal; connect from embedded engine in ctor.
- `src/kf6/kf6mainwindow.h` / `.cpp` — `m_pendingConflictCount`
  member, `onConflictDetected` slot, badge widget, dialog opening
  on click, badge reset on sync-completed.
- `src/widgets/sidebar/` or `src/kf6/` — new lightweight
  `ConflictBadgeWidget` if a styled QPushButton + QLabel pair grows
  too complex (probably 30 lines; embed in `KF6MainWindow` if
  smaller).

**Test:**
- `tst_kf6mainwindow_conflict_badge` — drive a fake conflict signal,
  assert badge appears with correct count; trigger sync-completed
  signal, assert count updates from `SyncConflictStore`.
- Existing `ConflictReviewDialog` tests cover dialog internals.

### 4.5 Sub-project E — Mass-delete guard verification

**Goal:** prove the guard fires end-to-end against the *original*
destruction scenario, not just against unit-test mocks.

The libkalburator suite already has `tst_mass_delete_guard.cpp`
(5 cases, shipped in `v0.54-mass-delete-guard`) exercising the
threshold/allow/deny via `MockBackend`. What's missing is a
WildPalms-level integration test where:
- A PalmRuntime is constructed with a real `BaselineStore` over a
  tempdir.
- The Palm side is stubbed (`MockPalmDatabaseAccess` already exists)
  with 20+ records matching the baseline.
- The PC side (RawFilesBackend over a tempdir) is empty.
- A `MassDeleteGuardPresenter` subclass (test fixture, similar to
  `TestableForgetWindow` from F.1b T10) overrides `promptUser` and
  records invocations.
- Sync is triggered; assert the guard's `promptUser` is called with
  proposed=20, baseline=20.
- Repeat with the fixture returning false; assert no Palm-side
  deletes happened.

**Files affected:**
- Create: `tests/runtime/tst_palm_mass_delete_guard_e2e.cpp` — the
  integration test.
- Modify: `tests/runtime/CMakeLists.txt` — register the test.

This test depends on sub-projects A + B + C being landed (otherwise
the engine doesn't propose deletes — Bug 1 + Bug 2 silent-defer
returns). Putting E last in the implementation order ensures the
preconditions are met.

## 5. Sub-project dependency + plan ordering

| # | Sub-project | Depends on | Independently reverts? |
|---|-------------|------------|------------------------|
| A | Hash stability | — | yes |
| B | Canonical deleteRecord | — | yes; safe because guard already exists |
| C | Default policy LastWriteWins | A (hash stability needed to make LWW comparisons meaningful) | yes |
| D | Conflict surfacing | A, B, C (otherwise no conflicts surface anyway) | yes |
| E | Guard E2E verification | A, B, C (otherwise no deletes ever propose) | yes (test-only) |

**Implementation order:** A → B → C → D → E. Each ships as its own
plan / PR. Each is reversible. Each leaves the codebase functional.

## 6. Error handling

Per sub-project:

| Failure | Surfacing | Recovery |
|---------|-----------|----------|
| A: contentHash on malformed record | Returns empty QString (sha256 of empty buf); diff treats as conflict (no false positives) | Sub-project D's conflict UI handles it |
| B: deletePalmRecord returns false | Engine logs failure, mass-delete guard threshold counts the attempted-but-failed deletes | Next sync retries (record still exists on Palm) |
| C: LastWriteWins with both timestamps invalid | Falls through to libkalburator's existing tie-break (source wins, per current syncengine.cpp behavior) | No degradation vs current |
| D: SyncConflictStore corrupted | Existing libkalburator error handling (returns empty list); badge stays at 0; user sees no conflicts but sync continues | User can wipe `.state/syncconflicts.db` to reset |
| E: test infrastructure broken | ctest reports failure; doesn't block production sync | Fix test infrastructure |

## 7. Testing

Each sub-project has its own tests (per §4). Aggregate verification:

- **After A:** existing plugin backend tests still pass; new
  `tst_palmrecord_contenthash` covers stable-hash invariant.
- **After B:** existing plugin backend tests extended to cover delete
  path; no regression in other plugins.
- **After C:** new mapping default verified; existing
  `MappingEditorDialog` tests show user override still works.
- **After D:** badge tests cover count display + dialog opening;
  existing `tst_conflictreviewdialog` covers dialog internals.
- **After E:** end-to-end guard verification passes against the
  original-destruction scenario reproduced in isolation.

**Cumulative regression:** after E, run the full WildPalms ctest
suite. Expectation: 80+ tests pass (current baseline +
~10 new tests across the five sub-projects).

**PlanStan gate:** sub-projects A, C, D, E may touch libkalburator
indirectly (sub-project A could change baseline format if we're not
careful). Per project memory, every libkalburator commit must pass
PlanStan's baseline (86/26/112 per the memory; current absolute
count differs but principle holds — no NEW failures). Sub-project A
is the only one touching libkalburator-consumed contracts directly;
plan A must include the PlanStan gate. Sub-projects B/C/D/E are
WildPalms-only.

## 8. Success criteria

1. **Hash stable.** Two consecutive reads of an unchanged Palm record
   produce the same `contentHash`. Verified by
   `tst_palmrecord_contenthash`.
2. **All plugin deletes work.** Each of the four sync plugins
   (calendar/contacts/memo/todos) can delete a Palm record by id.
   Verified by per-plugin backend tests against a stub device.
3. **Auto-mappings don't prompt.** A fresh sync with a new profile
   (Palm has data, PC is empty) writes PC files without firing the
   conflict UI. Verified by manual smoke test + existing e2e tests
   passing.
4. **Conflicts visible when they happen.** If a user explicitly sets
   a mapping's policy to AskUser and a conflict occurs, the
   status-bar badge appears with the correct count and clicking it
   opens the review dialog. Verified by
   `tst_kf6mainwindow_conflict_badge`.
5. **Guard fires against the original-destruction scenario.** With
   sub-projects A+B+C landed, the canonical reproduction (baseline
   matches Palm, PC emptied, sync run) triggers the mass-delete
   guard's `confirmMassDelete` callback with the expected counts.
   Verified by `tst_palm_mass_delete_guard_e2e`.
6. **No regressions.** All existing WildPalms ctest pass; PlanStan
   baseline holds against any libkalburator change in sub-project A.
7. **No silent destruction possible.** Manually `rm -rf rawfiles`
   on a freshly-synced test profile and re-sync: either the guard
   fires (warns user, allows skip) or the sync legitimately proposes
   <11 deletes that pass under threshold. In no case do >10 Palm
   records vanish without a user prompt.

## 9. Open implementation points (for plans to resolve)

- **A: which dataset to use for `data`?** `PalmRecord::data` is the
  raw record payload as the device returns it. If pilot-link mutates
  the data bytes between reads (e.g., trailing-NUL handling), the
  hash still varies. Plan A includes a manual verification step:
  hash two reads of the same unchanged record and confirm equality
  before committing. If not equal, the plan adds a normalisation
  step (e.g., trim trailing NULs) before hashing.
- **A: baseline migration.** Existing profiles' baselines were
  computed with the old `sha256Hex(toWireBytes())` algorithm. After
  the fix, fresh reads use the new `PalmRecord::contentHash()`
  algorithm. The two will not match → every record on the first
  post-fix sync looks "modified." With auto-mappings on
  LastWriteWins (sub-project C), this triggers LWW resolution
  (Palm side typically wins, since `lastModified` was updated when
  the sync ack'd the records). For real users this could mirror
  Palm to PC and overwrite local PC edits. **Mitigation**: the
  combined deliverable (A + C) recommends users (or a release-note
  warning) delete `.state/.wildpalms-blob-baselines.db` after the
  upgrade; baselines will rebuild on the next sync. Scope §2.2
  declares the destruction-corrupted test profile out of scope for
  formal migration; this open point notes the same concern applies
  to any real user with a pre-fix baseline. Plan A's commit
  message + release notes call this out explicitly.
- **B: do we also need to fix `decodeRecordId`?** No. The plugin
  fix at the calling site is the minimal correct change. Touching
  the decoder would risk other callers we haven't surveyed. Plan B
  adds a `qWarning` to the decoder if it produces a non-canonical
  name, but doesn't change its return value.
- **C: should the default also flip the sync mode to Monitored?**
  No. Monitored mode requires synchronous UI interaction during sync,
  which doesn't match the current async sync model. Unmonitored +
  LastWriteWins means conflicts auto-resolve without prompting; if
  the user opts into AskUser, they get the deferred-queue UX from
  sub-project D.
- **D: status-bar badge widget**: `QPushButton` styled flat vs.
  custom widget. Plan D picks the simplest that fits KDE style.
- **E: should the test exercise the real MassDeleteGuardPresenter
  with a stub `promptUser`?** Yes — that's the whole point. The
  fixture subclass overrides `promptUser` to record + return
  preset; same pattern as `TestableForgetWindow` from F.1b T10.

## 10. References

- Mass-delete guard spec & plan: `docs/superpowers/plans/2026-05-22-mass-delete-guard.md`
- F.1b: `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md`
- F.1a: `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md`
- Integration plan: `docs/plans/2026-04-20-libkalburator-integration.md`
  (Phase F.2 is "real IConflictPresenter" — sub-project D
  satisfies it.)
- DATA_LOSS_HANDLING.md — incident log
- Engine code: `~/dev/libkalburator/src/engine/syncengine.cpp`
  (lines 2253-2272 = silent-defer site; lines 2461-2566 = applyBatch
  + guard).
- `PalmRecord`: `src/palm/sync/palmrecord.h`
- `ConflictReviewDialog`: `src/widgets/dialogs/conflictreviewdialog.{h,cpp}`

## 11. Retrospective (post-landing, 2026-05-22)

### 11.1 Sub-project B overstated the bug

The original spec characterized calendar and memo as having a broken
`deleteRecord` that silently no-op'd against the device. Empirical
inspection of the round-trip behaviour during landing (see
`PalmBackend::encodeCollectionId` and `PalmBackend::decodeRecordId`)
shows this was incorrect:

- `DatebookDB` and `MemoDB` round-trip cleanly through encode/decode.
  The pre-fix `deleteRecord(recordId)` path produced the canonical
  dbName the device expects, and deletes did fire.
- `AddressDB` also round-trips cleanly, which is why contacts'
  hardcoded explicit form looked identical to the implicit form.
- `ToDoDB` is the *only* dbName with an asymmetric round-trip
  (`palm:todo` decodes to `TodoDB`, not `ToDoDB`). The device
  rejects `TodoDB`. But todos already used the explicit
  `deletePalmRecord("ToDoDB", rid)` form, so the broken decoder
  path was never reachable in production.

Net effect: the destruction observed in commit `a8f686f` was
**not** attributable to the calendar/memo `deleteRecord` path.
That bug pattern is a real latent fragility (anyone adding a new
plugin whose dbName has internal capitalization would hit it), but
the contacts destruction had a different cause — Bug 1 (hash
instability) combined with the silent-defer site (Bug 2) plus the
absence of the mass-delete guard.

### 11.2 Why we still kept sub-project B

The work was retained for two reasons:

1. **Defense in depth**: hardcoding the canonical dbName at every
   delete site eliminates any future regression risk from edits to
   `decodeRecordId`. All four plugins now follow the same pattern.
2. **Test coverage**: the canonical-dbName tests added in B
   (`183a69a`, `4718ad4`) document the contract explicitly. Future
   plugin authors can copy-paste the pattern with a passing test
   already in place.

Both are real value, but neither is the destructive-bug fix the
spec originally claimed. Plans and PR descriptions referencing the
"calendar/memo deleteRecord destruction" should be read in light of
this correction.

### 11.3 What's still unsolved

The decoder's asymmetric round-trip for `ToDoDB` is left as-is, per
the original §9 decision ("we don't fix the decoder"). The
follow-up risk: a future plugin or refactor that calls
`m_palmBackend->deleteRecord(recordId)` against a todo recordId
will silently no-op. Plan B's `qWarning` in the decoder (when it
produces a non-canonical name) remains the mitigation. No code
change is recommended at this time.
