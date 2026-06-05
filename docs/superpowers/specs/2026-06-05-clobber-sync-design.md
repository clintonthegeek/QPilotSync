# Clobber-sync — design

**Date:** 2026-06-05
**Branch:** `feature/three-tier-sync`
**Status:** approved by user; pending writing-plans skill + cross-repo
handoff to libkalburator.

## 1. Problem

The user needs a repeatable Palm-test workflow: wipe the Palm device's
PIM databases, then sync from WildPalms as if it were a brand-new
device, over and over, without recreating the WildPalms profile each
cycle. Today the only way to skip the "Palm is empty → mass delete →
propagate to desktop" interpretation is to make a fresh profile.

The user also believes the underlying capability — "freshen Palm
contents from the more authoritative desktop/cloud source" — has
general user value beyond testing. The Palm is a toy people play with;
they'll want to clobber it from authoritative state regularly.

A separate, broader gap is acknowledged but **out of scope**: today
there is no hub↔remote-only sync (without Palm). This means a user who
edits a record in the WildPalms UI cannot propagate that edit to a
cloud spoke without connecting a Palm. That is a real bug in the
three-tier-sync architecture's "hub buffers Palm edits and propagates
to remotes when reachable" promise, but it is not what clobber-sync
solves and conflating them would muddy both.

## 2. Decisions

Locked during brainstorming:

| Decision | Choice |
|---|---|
| Engine wipes Palm DBs itself? | Yes — single operation, not external script. |
| Granularity | Per-conduit checkboxes in v1; engine API takes mapping IDs so per-mapping is v2 with zero engine change. |
| Relationship to `copyPCToPalm()` | Subsumes/replaces. `copyPCToPalm()` is deleted. |
| Invocation surface | GUI only (Tools menu). No CLI/test binary. |
| Hub scope | Palm-direct only. Hub trusted as-is at click-time. No hub↔remote pre-refresh. |
| `ExecutionOverride::direction` when `clobber=true` | Silently ignored, behavior is always source→target. |
| Menu label | "Clobber Palm from PC" — chosen to emphasize severity. |

## 3. Architecture

```
WildPalms Tools menu  ─►  "Clobber Palm from PC"
                                │
                                ▼
                        ClobberDialog (per-conduit checkboxes)
                                │
                                │ selected mapping IDs
                                ▼
                  PalmRuntime::clobberSync(mappingIds)
                                │
                                │ 1. Cancel any in-flight sync, pause tickle.
                                │ 2. Build SyncRequest with
                                │      executionOverride={clobber=true}.
                                ▼
              SyncEngine::runSync(SyncRequest)   ← existing entry point,
                                │                  semantics extended for clobber.
                                │
                                │ For each mapping in the request:
                                │   a. Skip baseline load.
                                │   b. Skip mass-delete-guard hook.
                                │   c. targetBackend->wipeCollection(targetColId).
                                │   d. Push source records to (now empty) target.
                                │   e. Write fresh baseline at end-of-sync.
                                ▼
            PalmCalendarBackend::wipeCollection() etc.
                                │
                                ▼
               dlp_DeleteDB + recreate empty (per-conduit DB names)
```

**Boundary discipline** (per the `feedback_library_vs_backend_responsibility`
memory): libkalburator additions are domain-neutral. The library knows
"clobber mode" + "wipe a collection," not "Palm." All Palm-specific
behavior lives in the WildPalms-owned conduit submodules' backend
overrides.

## 4. Components

### libkalburator additions (delivered via handoff)

**`ExecutionOverride` extension** (`src/types/synctypes.h`):

```cpp
struct ExecutionOverride {
    Direction direction = Direction::Default;
    bool clobber = false;  // NEW
};
```

Semantics under `clobber == true` for a given mapping:

1. Engine skips baseline load (treats it as first sync for this mapping).
2. Engine skips the mass-delete-guard hook (no deletes are computed; the
   wipe replaces the diff).
3. Engine calls `targetBackend->wipeCollection(targetCollectionId)` before
   the push phase.
4. Engine pushes source records to the now-empty target.
5. Engine writes a fresh baseline at end-of-sync as normal.
6. `direction` is silently ignored under clobber — effective direction
   is always source → target.

**`SyncRequest` semantics relaxed** (`src/engine/syncrequest.h`):

Today `executionOverride` only applies when `isSingleMapping()`. The
clobber flag specifically broadens to apply on subset dispatch
(multi-mapping) too; each mapping in the subset runs with clobber
semantics independently. `direction` override stays single-mapping-only.

This lets WP send one `SyncRequest` containing all selected mapping IDs
with `clobber=true` and the engine runs them all with shared tickle and
a single end-of-sync.

**`IBlobBackend::wipeCollection`** (`src/blob/iblobbackend.h`):

```cpp
/// Delete every record in the collection, leaving it empty but usable.
/// Default impl: iterate loadRecords + deleteRecord (slow but always works).
/// Backends MAY override with a fast path (drop+recreate, TRUNCATE, etc.).
virtual bool wipeCollection(const QString &collectionId);
```

Default implementation provided inline so every existing backend
compiles unchanged. Palm-side backends override for the fast path.

### WildPalms additions

**`PalmRuntime::clobberSync(QList<QString> mappingIds)`**
(`src/runtime/palmruntime.{h,cpp}`):

Mirrors `hotSync()` / `fullSync()`. Cancels any in-flight sync, pauses
tickle, builds `SyncRequest{mappingIds, executionOverride={clobber=true}}`,
dispatches through `m_engine->runSync()`, reports per-mapping aggregated
stats in `PalmRunResult`. Extends the existing per-plugin reporting
(which today hardcodes `"calendar"`) to handle multi-domain.

**Mapping classification helpers** on `PalmRuntime`:

```cpp
bool isPalmDirectMapping(const SyncMapping &m) const;
QList<QString> palmDirectMappingsForDomain(const QString &domain) const;
```

Used only by `ClobberDialog` to populate its checkboxes. The engine
never consumes these.

**`ClobberDialog`** (`src/runtime/clobberdialog.{h,cpp}` —
or `src/kf6/` if it needs KF6 widgets):

Modal `QDialog`. One section per Palm-direct conduit that has at least
one enabled Palm-direct mapping:

- ☐ Calendar — *N mappings*
- ☐ Contacts — *N mappings*
- ☐ Memo — *N mappings*
- ☐ ToDo — *N mappings*

Confirm/Cancel buttons. On Confirm, a final `QMessageBox::warning`
prompt — "This will delete *N* Palm databases and replace them with
desktop data. Continue?" — gates the return. Returns the selected
mapping IDs to the caller. The dialog has zero engine knowledge.

**Palm-side `wipeCollection` overrides** in the conduit submodules:

| Conduit | Class | Classic DB | Enhanced DB |
|---|---|---|---|
| wildpalms-conduit-calendar | `PalmCalendarBackend` | `DatebookDB` | `CalendarDB-PDat` |
| wildpalms-conduit-contacts | `PalmContactsBackend` | `AddressDB` | `ContactsDB-PAdd` |
| wildpalms-conduit-memo | `PalmMemoBackend` | `MemoDB` | `MemosDB-PMem` |
| wildpalms-conduit-todos | `PalmToDoBackend` | `ToDoDB` | `TasksDB-PTod` |

Each override:

1. Calls `dlp_DeleteDB(handle, cardno, dbname)` for the classic name.
2. Same for the enhanced name (ignore "not found" errors — most Palms
   only have one).
3. Re-creates the empty database via `dlp_CreateDB(...)` so the
   subsequent push has a valid target. Same DB type/creator IDs the
   built-in PIM apps would create on first open.

Mirrors the proven `scripts/palm-wipe-pim.sh` sequence inside the backend
class. After the migration, the script becomes optional (still useful
for headless `palm-wipe-pim.sh && ./run-some-tests.sh` flows, but no
longer the only way).

**Tools menu wiring** (`src/kf6/kf6mainwindow.cpp`):

- Remove the existing "Copy PC to Palm" action.
- Add "Clobber Palm from PC" action.
- Action handler: instantiate `ClobberDialog`, on accept call
  `m_palmRuntime->clobberSync(selectedMappingIds)`.

**Deletion of `PalmRuntime::copyPCToPalm()`** and any
internal callers. It's WP-internal — no external consumers — so it
goes outright. The `runMirror(MirrorDir::PCToPalm, ...)` machinery
underneath stays available for whatever future caller wants
non-clobber mirror semantics, but the public method is gone.

## 5. Data flow — clobber-sync run

```
t0  user opens Tools menu → "Clobber Palm from PC"
t1  ClobberDialog asks runtime for Palm-direct mappings, grouped by domain
t2  user ticks Calendar + ToDo, clicks Confirm
t3  warning prompt: "Delete 2 Palm databases..." → user clicks Yes
t4  dialog returns selected mapping IDs to kf6 main window
t5  kf6 main window calls runtime.clobberSync(ids)
t6  runtime cancels any in-flight sync, pauses tickle, builds SyncRequest
t7  runtime calls engine.runSync(request)
t8  engine, per mapping:
      - looks up target backend (PalmCalendarBackend, PalmToDoBackend)
      - calls target->wipeCollection(collectionId)
        → backend: dlp_DeleteDB + dlp_CreateDB on the Palm
      - reads source records from hub
      - pushes to target (now empty) using "first sync" semantics
        (no baseline diff, just create-everything)
      - writes fresh baseline
t9  engine emits SyncResult per mapping → runtime aggregates
t10 runtime resumes tickle, emits runFinished/syncCompleted
t11 dashboard updates per-mapping stats
```

## 6. Error handling

**Mid-run failure between wipe and push** is the highest-stakes case.
The Palm has been wiped but not yet repopulated. If we abort, the Palm
is empty.

- **Treatment:** report failure loudly in `PalmRunResult` and the log
  widget. Do NOT attempt to "undo" the wipe — that's the engine's
  natural state after a wipe-without-push and the user can re-run
  clobber. Document this explicitly in the warning prompt:
  > "If the push fails after the wipe, the Palm will be left empty.
  > Re-running Clobber Palm from PC will retry."

**Mass-delete guard never sees clobber.** By construction (skipped at
step 8 above), the guard cannot misfire on the post-wipe diff. The
classic "Palm is empty → mass delete on desktop" failure mode is
structurally impossible under clobber.

**Direction set under clobber.** Silently ignored. WP never sets it
that way, but other libkalburator consumers might. Documented in the
field doc-comment.

**Wipe fails (Palm disconnect mid-DLP-call).** `wipeCollection` returns
false; engine aborts the mapping with a clear error; per-mapping
SyncResult reports the wipe failure. Other mappings in the same
request still proceed (one mapping's failure doesn't cancel the others)
— same per-mapping isolation hot-sync already has.

**Profile state inconsistency** (baseline cleared but later steps
fail). The baseline is only written at end-of-sync (step 4 above), so
a mid-run abort leaves the baseline absent → next sync correctly
re-enters first-sync semantics. No half-state.

## 7. Testing

**Unit / integration tests in libkalburator** (shipped with the handoff):

- `tst_syncengine_clobber_single_mapping`: clobber=true with a single
  mapping; assert `wipeCollection` is called, baseline is skipped on
  load and written at end, all source records arrive at target.
- `tst_syncengine_clobber_multi_mapping`: clobber=true with subset
  dispatch; assert each mapping runs independently with clobber semantics.
- `tst_syncengine_clobber_mass_delete_guard_silenced`: install a
  `IMassDeleteGuard` mock that fails the test if consulted; run a clobber
  that would have tripped the guard pre-clobber; assert the mock is
  never called.
- `tst_iblobbackend_default_wipeCollection`: assert the default
  iterate-and-delete impl works on `MockBlobBackend`.

**WP-side tests:**

- `tst_palm_runtime_clobber_sync` (new, in `tests/runtime/`):
  end-to-end with `MockPalmDatabaseAccess` + a pre-populated hub
  collection; assert the mock receives wipe + repopulate, hub records
  arrive on the mock Palm, and `PalmRunResult` reports per-mapping
  stats correctly.
- `tst_clobber_dialog` (new, in `tests/runtime/` or `tests/ui/`):
  populates dialog with fake Palm-direct mappings; clicks checkboxes;
  asserts returned mapping IDs match selections; asserts the warning
  prompt triggers; asserts Cancel returns an empty selection.
- Existing `tst_palm_runtime_route_first_sync` / etc. should be
  unaffected (clobber is opt-in, default `false`).

**Manual / live test workflow** (the original ask):

1. Start with desktop holding canonical data; Palm has any state.
2. Tools → "Clobber Palm from PC" → tick all four conduits → Confirm.
3. Verify Palm Calendar / Contacts / Memo / ToDo apps show desktop's
   data, nothing else.
4. Edit something on Palm.
5. Tools → "Clobber Palm from PC" → tick the relevant conduit → Confirm.
6. Verify Palm reverts to desktop state; Palm-side edit is gone; no
   mass-delete prompt fired; desktop data is untouched.
7. Repeat 4–6 ad nauseam.

The `palm-wipe-pim.sh` script remains available for headless flows,
but the in-app loop is now the primary path.

## 8. Non-goals

- **Hub↔remote-only sync** (excluding Palm). Real architectural gap,
  out of scope.
- **CLI / headless clobber.** GUI is sufficient for the test loop.
- **Per-mapping checkbox UI in v1.** Engine API supports it; dialog
  defers it to v2.
- **Driving clobber from inside `palm-wipe-pim.sh`.** Script stays
  separate; clobber is the in-app path.
- **Undo-wipe / pre-wipe backup.** The user can use
  `palm-wipe-pim.sh --backup <dir>` separately if they want a safety
  net. Clobber does not bundle backup.
- **Clobbering routed (hub↔remote) mappings.** Dialog only offers
  Palm-direct mappings. Routed mappings continue to sync via
  hot-sync / full-sync.
- **Adapting the three currently-failing runtime tests**
  (`tst_palm_runtime_route_first_sync` etc.) that fail under v0.63+
  with `success=false`. Those are tracked separately in
  `docs/2026-06-04-v0.63-pin-bump-test-regressions.md`.

## 9. Cross-repo coordination

Per the `feedback_libkalburator_handoff_workflow` and
`feedback_planstan_pretest_for_upstream` memories:

1. **WildPalms writes** `docs/2026-06-NN-libkalburator-clobber-sync-handoff.md`
   describing the three libkalburator surface additions
   (`ExecutionOverride::clobber`, `SyncRequest` relax, `IBlobBackend::wipeCollection`),
   their semantics, and the test set.
2. **libkalburator implements** the surface, lands the four library-side
   tests, and **PlanStan-green gate** passes against the change.
3. **libkalburator cuts a tag** (likely `v0.65`).
4. **WildPalms bumps pin** v0.64 → v0.65, implements:
   - `PalmRuntime::clobberSync` + classification helpers
   - `ClobberDialog`
   - `wipeCollection` overrides in the four conduit submodules
     (each via its own commit in the submodule, then a gitlink bump
     in the WP superproject — same pattern as the recent Plan-3
     reparenting submodule bumps)
   - Tools-menu rewire; delete `copyPCToPalm()`
   - WP-side tests
5. **Device-backed verification** on a real Palm before merging to
   `main`.

Steps 1–3 are the blocker; step 4 cannot start until the libkalburator
API exists. Step 4 can be done in parallel with libkalburator
implementation only to the degree of writing the dialog and helpers
that don't touch the new APIs.

## 10. Open questions

- **Database creator IDs for `dlp_CreateDB`.** The classic PIM apps
  (Datebook, Address, etc.) use specific creator IDs. We need to
  match them so the built-in apps re-recognize the recreated DB.
  Reference: `palm-wipe-pim.sh` doesn't recreate (relies on the PIM
  apps to recreate on next open); we'll need to encode the creator
  IDs in each Palm-side backend. Should be straightforward but worth
  flagging.
- **Migration of the existing `tst_palm_mass_delete_guard_e2e`.**
  It exists to prove the guard fires when expected. We should add a
  companion test that proves the guard does NOT fire under clobber,
  not retire the existing one.
- **Naming after v1.** "Clobber Palm from PC" is the v1 label;
  if user adoption suggests a friendlier label later
  ("Refresh Palm from Desktop", "Restore Palm to Desktop"), it's a
  one-line UI string change.
