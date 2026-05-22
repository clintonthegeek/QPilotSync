# libkalburator integration — phased plan

**Date:** 2026-04-20 (drafted), 2026-04-21 (revised after upstream
Phase C.6 and again after Phase B4)
**Status:** Phases A, B, B2, C, D, **E** all **done** (as of 2026-05-21).
E.19 (docs supersession) landed 2026-05-21, closing the Phase E loop.
Phase F (Full Sync Mode UX polish + profile-creation wizard) is the
next active phase; not yet started.
See `../superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
for the Phase E sub-phase status table.
**Design doc:** `2026-04-20-libkalburator-integration-design.md`
(with 2026-04-21 reconciliation section).
**Pointer:** `../LIBKALBURATOR.md`
**Upstream living index:** `~/dev/libkalburator/docs/phase0/README.md`

## Phase overview

| Phase | Scope | Side | Status | Gated on |
|---|---|---|---|---|
| A | Vendor libkalburator into WP build | WP | ✅ **Done** 2026-04-21 (commit `52bc31b`). | — |
| B | Upstream: merge WP's `qsynccore/` into libkalburator | libkalburator | ✅ **Done upstream** as Phases B (partial) + C.1 + C.4 + C.5, tagged `v0.5-phase-c` on 2026-04-21. | A (retroactively) |
| B2 | Upstream: implement the blob layer (`IBlobBackend`, `BlobSyncEngine`, `LocalBlobBackend`, `MockBlobBackend`) | libkalburator | ✅ **Done** 2026-04-21. Tagged `v0.6-phase-b2-blob-layer` at `9cae6ff`. Minimum-viable engine (mirror + twoWayNaive); baseline / conflict / upper-layer wiring catalogued as deferred in `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md`. | A |
| C | Upstream: layered directory split + `Kalburator::Sync::*` namespace | libkalburator | ✅ **Done upstream** as Phases C.2a + C.2b + C.3, tagged `v0.5-phase-c` on 2026-04-21. | B |
| D | WP implements host interfaces (`ICalendarHost`, `ICalendarCollection`, `ISyncConfigStore`) | WP | ✅ **Done** 2026-04-21 (commit `ff40d0f`). | A |
| E | WP refactors `PalmBackend` onto `IBlobBackend`; ships `PalmCalendarBackend` adapter; rewrites WP plugin ABI; collapses Client/Full-Sync Modes into unified runtime | both | ✅ **Done 2026-05-21.** E.0–E.15b landed 2026-04-21..2026-04-26. E.16 landed (partial 2026-04-28; deferrals (a)(b)(c)(e) closed 2026-05-21 via the conflict-handler port + WebCalendar deletion + per-DB cache + namespace rename; only (d) — LocalBlobBackend cross-id-mapping — remains as a libkalburator follow-up). E.17 subsumed by the engine-merger campaign merged to main 2026-05-21. E.18 ❌ cancelled (POSE64 timing infeasible). E.19 landed 2026-05-21 (`docs/PLUGIN_ABI.md` written; `docs/ARCHITECTURE_2026.md` + `docs/SYNC_ENGINE_ARCHITECTURE.md` + `docs/LIBKALBURATOR.md` refreshed; legacy docs archived). See `../superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. | B2, D |
| F | Full Sync Mode UI polish + profile-creation wizard (the mode collapse itself landed in E.16 per Phase-E spec decision #3) | WP | **In progress.** F.1a ✅ + F.1b ✅ + F.2 ✅ done 2026-05-22. F.1c / F.3 / F.4 pending. | E |
| G | Joint v1.0 declaration (libkalburator) | both | **Not started.** | F |

**Status reconciliation (2026-05-21, post Phase E close):**

- Phases A, B, B2, C, D, **E** are all done. E closed 2026-05-21 with
  E.19's three-pass docs supersession (legacy archive + engine-merger
  artifact extraction + new `PLUGIN_ABI.md` / refreshed
  `ARCHITECTURE_2026.md` / `SYNC_ENGINE_ARCHITECTURE.md` /
  `LIBKALBURATOR.md`).
- E.16's deferrals (a) conflict-handler port, (b) WebCalendar deletion,
  (c) per-DB cache, (e) namespace rename all landed 2026-05-21.
  Deferral (d) — suspected `LocalBlobBackend` cross-id-mapping bug —
  is now tracked upstream in libkalburator at
  `~/dev/libkalburator/docs/2026-05-21-localblobbackend-cross-id-mapping.md`
  and is a follow-up coordinated with PlanStan, not blocking Phase F.
- E.18 ❌ cancelled. POSE64 emulator timing is too unstable for an
  automated integration harness. Coverage is the per-plugin e2e
  ctests + periodic manual smoke against real Palm hardware.
- Phase F is split into three F.1 sub-projects per 2026-05-21 brainstorm:
  F.1a (profile persistence + app registry) ✅ landed 2026-05-22 in
  17 commits (spec: `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md`,
  plan: `docs/superpowers/plans/2026-05-21-f1a-profile-registry.md`).
  F.1b (new File menu: Switch / Import / Forget) ✅ landed 2026-05-22
  in 16 commits (spec: `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md`,
  plan: `docs/superpowers/plans/2026-05-22-f1b-file-menu.md`).
  F.1c (the multi-page wizard) — next.
- F.2 (real `IConflictPresenter` + the broader palm-sync-honesty
  bug cluster) ✅ landed 2026-05-22 in five sub-project commit
  series (A: hash stability, B: canonical deleteRecord, C: default
  LWW policy, D: conflict-surfacing UI, E: mass-delete-guard E2E
  verification). Spec: `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md`.
  Plans: `docs/superpowers/plans/2026-05-22-palm-sync-{A,B,C,D,E}-*.md`.
  Two follow-ups closed 2026-05-22 in commit `13a5df2`:
  (1) `ConflictReviewDialog` now receives a populated
  `Kalburator::Conflict::ConflictStore` mirrored from engine-side
  `Sync::ConflictInfo` via a new `PalmRuntime::toConflictRecord`
  helper, so the status-bar badge click actually shows the
  recorded conflicts; (2) spec §1 Bug 3 + §4.2 corrected — only
  `ToDoDB` has the asymmetric encode/decode round-trip, and todos
  already used the explicit-dbName path, so the calendar/memo work
  in sub-project B was a consistency change, not a correctness
  fix. See spec §11 retrospective.
- Phase F also covers F.3 (calendar-binding UX polish), F.4
  (Radicale E2E + user docs).
- Phase G (joint v1.0 cut) depends on Phase F and on PlanStan's own
  readiness against the same libkalburator pin. Not started.

### libkalburator / PlanStan coordination

WildPalms and PlanStan both consume libkalburator. WP work *may* land
changes in libkalburator, but every libkalburator commit:

- must pass PlanStan's ctest baseline before being tagged
  ([see PlanStan pretest policy](../../.claude/projects/-home-clinton-dev-WildPalms/memory/feedback_planstan_pretest_for_upstream.md))
- must be documented in `~/dev/libkalburator/docs/phase0/` (the
  per-phase status doc + `04w-deferred-work.md`)
- must not encode WP-specific peculiarities in upstream types — those
  belong in the WP-side backend / plugin (see
  [library-vs-backend responsibility note](../../.claude/projects/-home-clinton-dev-WildPalms/memory/feedback_library_vs_backend_responsibility.md))

libkalburator's current state (HEAD `0e56652`, phase-p T17) is
documented in `~/dev/libkalburator/docs/phase0/04aj-phase-p-status.md`
and `04w-deferred-work.md`. Phase Q (Full Session Refactor, deferred
from O.6) is the next campaign-sized chunk on that side.

---

## Phase A — vendor libkalburator into Wild Palms ✅ DONE 2026-04-21

**Status:** Shipped in commit `52bc31b` (`build: vendor libkalburator
via add_subdirectory`). WP builds against libkalburator as a no-op
dependency; no behaviour change. 13/13 ctest tests pass (12 pre-existing
+ 1 new smoke test).

**Goal:** Wild Palms builds against libkalburator as a no-op
dependency. No behaviour change. Smoke test: a Full-Sync-Mode-shaped
test that links `Kalburator::Sync` and instantiates a mock backend.

### Tasks

- [x] Add `~/dev/libkalburator/` as a dependency in WP's top-level
  `CMakeLists.txt`. Follow PlanStan's pattern:
  `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libkalburator
  libkalburator EXCLUDE_FROM_ALL)`. The `KALBURATOR_PROVIDE_TYPES`
  flag no longer exists (removed in upstream C.2a); `Kalburator::Types`
  is always built as a separate target alongside `Kalburator::Sync`.
  Link whichever of the two WP needs (for Phase A, likely
  `Kalburator::Sync` since the smoke test instantiates a backend).
- [x] Set `KALBURATOR_HAVE_ORG_IO=OFF` for now (WP doesn't ship
  PlanStan's org-io library).
- [x] Set `KALBURATOR_HAVE_AKONADI=OFF` initially; flip to ON once the
  rest of the integration is stable.
- [x] Write a minimal linking test: a CMake target in `tests/` that
  links `Kalburator::Sync`, instantiates a mock backend and
  a `ConflictManager`, calls a simple accessor, asserts non-empty.
  Purpose: prove the link line and include paths are correct. Use
  `using namespace Kalburator::Sync;` in the test TU per the pattern
  PlanStan uses in its own consumers (all library types live in that
  namespace now). **Shipped as `tests/test_libkalburator_smoke.cpp`.
  Class name is `MockBackend` (not `MockCalendarBackend` as this plan
  originally said); asserts `backendType()` matches `BackendTypeName`
  and `ConflictManager`'s default `workflowMode`/`hybridThreshold`.**
- [x] Verify the WP build still passes end-to-end (existing tests +
  the new smoke test). No WP source changes.
- [x] Commit: `build: vendor libkalburator via add_subdirectory`.

### Exit criteria

- [x] `cmake --build build` succeeds with libkalburator subdir.
- [x] New smoke test passes.
- [x] No existing WP test regressed.

### Notes captured during execution

- Transitive deps WP now carries through `Kalburator::Sync`: `Qt6::Sql`,
  `Qt6::Xml`, `KF6::DAV`, `KF6::KIOCore`, `KF6::Holidays` (PlanStan
  already required these — confirmed present on the system).
- libkalburator's `find_package(Qt6 6.8 …)` effectively raises WP's
  minimum Qt from 6.2 to 6.8. System Qt is 6.11, so no user impact,
  but the root `CMakeLists.txt`'s `find_package(Qt6 6.2 …)` is now
  misleading; consider raising to 6.8 in a follow-up or when Phase E
  starts consuming kalburator from WP source.
- The smoke test target is added directly in `tests/CMakeLists.txt`
  rather than via the `add_wildpalms_test()` helper: helper links
  `WildPalmsCore` + `pisock` + `bluetooth` + `usb`, none of which the
  smoke test needs. Keeping it minimal isolates linkage regressions
  to the library itself.

### Non-goals for Phase A

- Using any library functionality in WP's actual code paths.
- Deleting any WP sync code.
- ~~Namespace / layering concerns (those are upstream Phase C).~~
  Namespace and layering are already done upstream — WP consumers
  just use `Kalburator::Sync::*` or pull it in with a using-directive,
  same pattern PlanStan follows.

---

## Phase B — merge WP's `qsynccore/` into libkalburator ✅ DONE UPSTREAM

**Status:** Landed upstream across Phases B (partial) + C.1 + C.4 +
C.5 in `~/dev/libkalburator/`, tagged `v0.5-phase-c` on 2026-04-21.
All six target files from WP's `qsynccore/` are now in libkalburator
(`baselinestore`, `conflictpolicy`, `conflictrecord`, `conflictstore`,
`idmappingstore`, `synccommon`), plus a new `conflicthandlerregistry`
added in C.1. Landed scope differs from what this section originally
specified:

- `IDMappingStore` was **rewritten as SQLite** during C.4, not copied
  as-is. It now shares `.planstan-sync.db` with `SyncStore` via
  idempotent `ALTER TABLE ADD COLUMN` and merges Audit-2's WP fields
  (`last_synced`, `source_category`, `target_categories`, `archived`)
  plus `recurrenceId` (needed because PS's PK includes `recurrence_id`
  for iCal exceptions). It lives at `src/journal/idmappingstore.{h,cpp}`
  in the top-level `Kalburator::Sync` namespace (promoted out of
  `::QSyncCore` during the SQLite rewrite).
- `SyncStore`'s identity-mapping API was **dissolved** in C.5 —
  `IDMappingStore` is the sole owner of the `sync_id_mappings` table.
- `BaselineStore` is not yet upstream. WP's `BaselineStore` stays
  WP-side for now; PlanStan's baseline logic remains embedded in
  `SyncStore`. The reconciliation between the two (the Phase B
  pre-work open question 1) is still open. Not blocking for WP
  integration: Phase E can consume whichever baseline layer exists
  when it runs.
- `ConflictStore` + `ConflictManager` coexist upstream — PlanStan's
  `ConflictManager` is kept as the orchestrator; WP's `ConflictStore`
  is the persistent deferred-resolution layer beneath it.
- `ConnectionBehavior` was kept off the generic `ConflictPolicy`;
  WP's `PalmBackendConfig` (Phase E) will hold it.

The exit criteria below are met. The original task list is preserved
for the historical record of what was planned.

**Original Phase B scope, as originally written:**

libkalburator gains WP's conflict framework. This was
**upstream work in `~/dev/libkalburator/`**, not inside Wild Palms.
WP is the donor repo; its `qsynccore/` is the source of truth.

### Pre-work

Resolve the four open questions in the design doc §"Open questions
to resolve before Phase B":

1. `BaselineStore` schema reconciliation (WP's explicit vs PlanStan's
   embedded).
2. `IDMappingStore` coverage audit.
3. Conflict handler dispatch when a conduit is active.
4. `AsyncFileWriter` unification.

Capture each resolution in
`~/dev/libkalburator/docs/phase0/04a-followups.md` with `[WP-driven]`
provenance before touching code.

### Tasks (upstream)

- [ ] Copy `qsynccore/` files into `~/dev/libkalburator/src/sync/`
  (temporarily flat — the layered split is Phase C).
- [ ] Strip `ConnectionBehavior` from `ConflictPolicy` — it's
  Palm-specific per the Phase 0 audit
  (`~/dev/libkalburator/docs/phase0/02-inventory-wildpalms.md`
  §"Conflict-policy audit"). WP-side subclass will re-add it.
- [ ] Reconcile `ConflictStore` + `ConflictManager` — both exist now,
  former from WP, latter from PlanStan. Design decides which survives.
  Leaning: WP's `ConflictStore` survives (persistent deferred
  resolution is richer); PlanStan's `ConflictManager` becomes a thin
  orchestrator on top.
- [ ] Reconcile `BaselineStore` + PlanStan's embedded baseline logic
  in `SyncStore`. WP's wins per design doc.
- [ ] Add `IDMappingStore`.
- [ ] Update libkalburator's own tests to cover the merged conflict
  framework.
- [ ] **Update PlanStan** — since PlanStan consumes libkalburator
  in-tree, this is a coordinated commit: PlanStan's call sites move
  from `ConflictManager`-only to the merged API.
- [ ] ~~Tag libkalburator as `v0.4-phase-b-qsynccore-merged`.~~ Tag
      landed was `v0.5-phase-c` after the C.1–C.6 sequence; there is
      no separate `v0.4` tag. The "phase B merge" was never cut as a
      standalone release because upstream rolled straight through to
      C.6.

### Exit criteria

- libkalburator builds and its own tests pass.
- PlanStan builds against the new libkalburator snapshot; existing
  `ctest` baseline (87 pass / 27 fail as of Phase 3b) preserved or
  improved.
- WP still builds (WP doesn't consume the conflict framework yet —
  that's Phase E).

---

## Phase B2 — upstream blob layer (new, 2026-04-21)

**Status:** Not started. Carved out of the original Phase C when
upstream's C.3 executed the directory layering without building the
blob layer. This is now the single largest upstream deliverable for
WP adoption.

**Goal:** libkalburator's `src/blob/` directory (currently an empty
placeholder) gains the lower-layer abstractions described in
`~/dev/libkalburator/docs/phase0/04-merged-interface-sketch.md`:
`IBlobBackend`, `BlobSyncEngine`, `LocalBlobBackend`, `MockBlobBackend`.
The calendar-typed upper layer already exists; Phase B2 is about
building the sibling generic-blob lower layer, and factoring anything
generic downward out of the calendar layer where appropriate.

This is **upstream work in `~/dev/libkalburator/`**.

### Pre-work

Open an upstream design doc in `~/dev/libkalburator/docs/phase0/`
(suggested name: `06-phase4-wp-adoption-plan.md`, or
`04h-blob-layer-design.md` if you prefer to keep the 04-prefix
pattern for design specs). The doc should specify:

- The `IBlobBackend` surface (file/record-oriented CRUD, opaque byte
  records plus `BackendRecord` metadata).
- The `BlobSyncEngine` responsibilities vs. the existing
  `SyncCoordinator` (the latter is calendar-typed; likely the former
  is composed by the latter rather than inherited).
- What moves **down** from `ICalendarBackend` into `IBlobBackend`
  (generic pieces: identity, credentials, transport errors, general
  CRUD shape) vs. what stays **up** in the calendar layer (incidence
  decoding, recurrence, categories, etc.).
- How `LocalBlobBackend` and PlanStan's `LocalBackend` relate. Either
  PlanStan's `LocalBackend` refactors onto `LocalBlobBackend` + a
  calendar adapter, or the two coexist with documented overlap.
- Test coverage expectations — Phase B2 is a natural moment to
  land the first library-side tests.

### Tasks (upstream)

- [ ] Land the design doc.
- [ ] Implement `IBlobBackend` in `src/blob/iblobbackend.h`.
- [ ] Implement `BackendRecord` value type in
      `src/blob/backendrecord.h` (or under `src/types/` if preferred —
      judgment call based on whether it's part of the shared
      vocabulary).
- [ ] Implement `BlobSyncEngine` in `src/blob/blobsyncengine.{h,cpp}`.
- [ ] Implement `LocalBlobBackend` in `src/blob/localblobbackend.{h,cpp}`.
- [ ] Implement `MockBlobBackend` in `src/blob/mockblobbackend.{h,cpp}`
      for consumer tests.
- [ ] Wire `SyncCoordinator` to compose `BlobSyncEngine*` for the
      generic parts of its flow.
- [ ] First library-side test target: bring up `tests/blob/` with a
      round-trip test using `MockBlobBackend` and `LocalBlobBackend`.
- [ ] PlanStan-side: confirm the existing calendar sync flow is
      unaffected (no PlanStan call sites should need to change if the
      composition is done right; if changes are needed, land them in
      the same commit).
- [ ] Tag libkalburator `v0.6-phase-b2-blob-layer` on completion.

### Exit criteria

- `src/blob/` contains real implementations, not a placeholder.
- libkalburator builds standalone; new `tests/blob/` target passes.
- PlanStan builds and ctest baseline is preserved.
- WP can move to Phase E (which is gated on `IBlobBackend` existing).

### Why this is B2 and not an extension of C

The original Phase C scoped "Introduce IBlobBackend + BlobSyncEngine"
as the first of its tasks. Upstream did C.3 (directory layering) and
C.2 (namespacing) without touching the blob work. Calling the
remaining work "Phase C, still open" would be inconsistent with
`~/dev/libkalburator/docs/phase0/README.md`'s phase map, which
records C as done at C.6. A fresh phase number avoids the
bookkeeping conflict and makes this doc line up with the upstream
status file.

---

## Phase C — upstream layering + namespacing ✅ DONE UPSTREAM

**Status:** Landed upstream across Phases C.2a + C.2b + C.3 in
`~/dev/libkalburator/`, tagged `v0.5-phase-c` on 2026-04-21. Source
is split into `src/{types,calendar,conflict,transcoding,journal,discovery,blob}/`
per `05-repo-strategy.md`. All types live in `Kalburator::Sync::*`
(qsynccore files under `Kalburator::Sync::QSyncCore::*` to resolve
`ConflictResolution` / `SyncStats` / `SyncResult` collisions with
`src/types/synctypes.h`). PlanStan's duplicate shared-type headers
were deleted in C.2a; `Kalburator::Types` is the single source
(closing the ODR hazard window this plan originally warned about).

**The blob-layer portion of the original Phase C scope did not land
in C.3 and has been carved out to Phase B2** (above) for accuracy
with the upstream phase map.

The exit criteria below are met for the namespace + layering
portion. The original task list is preserved for the historical
record of what was planned.

**Original Phase C scope, as originally written:**

libkalburator source is split into `blob/`, `calendar/`,
`conflict/`, `transcoding/`, `journal/`, `types/` per
`~/dev/libkalburator/docs/phase0/05-repo-strategy.md` §"Directory
layout". All types live in `Kalburator::Sync::*`. This is the last
major upstream shape change before v1.0.

This was **upstream work in `~/dev/libkalburator/`**.

### Tasks (upstream)

- [ ] Introduce `IBlobBackend` + `BlobSyncEngine`. Move what's
  generic down from `ICalendarBackend` / `CalendarSyncEngine`. Keep
  the latter as an upper-layer composition over the former.
- [ ] Split source tree into `src/{blob,calendar,conflict,transcoding,journal,types}/`.
- [ ] Namespace every type: `Kalburator::Sync::ICalendarBackend` etc.
- [ ] Decide inheritance: `ICalendarSyncCoordinator` **composes** a
  `BlobSyncEngine*` per `00-open-questions.md` §1 resolution.
- [ ] Update PlanStan call sites — every include + every qualified
  name. This is the biggest PlanStan-side churn of the whole
  integration.
- [ ] Update libkalburator's CMake to export `Kalburator::Sync`
  (current) and prepare for future `Kalburator::Transport`,
  `Kalburator::Discovery` targets.
- [ ] ~~Tag libkalburator as `v0.5-phase-c-layered`.~~ Tag landed
      was `v0.5-phase-c` (no `-layered` suffix).

### Exit criteria

- `Kalburator::Sync::*` everywhere; no bare PlanStan-namespace types
  in library headers.
- PlanStan builds clean against v0.5.
- WP's Phase-A smoke test still builds (its usage is shallow enough
  that namespace changes mostly break harmlessly in test code).

### Why this is gated on Phase B

Merging `qsynccore/` while simultaneously reshaping the directory
tree creates a review nightmare: any conflict is ambiguous between
"is this the merge or the move?" Do B first, flat; do C after, with
the merged file-set as input.

---

## Phase D — WP host interface implementations ✅ DONE 2026-04-21

**Status:** Shipped in commit `ff40d0f` (`feat(fullsync): WP
implementations of libkalburator host interfaces`). 16/16 ctest
pass (13 pre-existing + 3 new fullsync).

**Goal:** WP has concrete implementations of the host interfaces the
library expects, covered by WP-side unit tests that don't involve any
real backend.

### Tasks

- [x] Create `src/fullsync/calendarcollection_wp.{h,cpp}` —
  thin wrapper over `QHash<QString, MemoryCalendar*>`. Implements
  the 6-method `ICalendarCollection` surface from `04a-followups.md`.
  *(Filename deviates from plan: leading `i` prefix dropped because
  the file contains a concrete impl, not an interface declaration.)*
- [x] Create `src/fullsync/syncconfigstore_wp.{h,cpp}` —
  QSettings-backed. 8-method `ISyncConfigStore` surface. Round-trips
  `LogicalCalendar` + `SyncMapping` via the library's own JSON
  helpers (`Kalburator::Sync::syncMappingToJson` etc.).
- [x] Create `src/fullsync/synchost_wp.{h,cpp}` —
  owns the `CalendarCollection_WP` + `SyncConfigStore_WP`;
  `applyIncidence*` dispatch targets are counters (real model wiring
  lands in Phase F). `incidenceSource()` / `incidenceRegistry()`
  return `nullptr` (same stub pattern PlanStan's smoke test uses).
  *(Plan called this `icalendarhost_wp`; actual interface is
  `ISyncHost`, so filename matches the interface name.)*
- [x] Create `src/fullsync/conflictresolver_wp.{h,cpp}` +
  `conflictpresenter_wp.{h,cpp}` — resolver auto-accepts
  `ConflictResolution::SourceWins`; presenter counts
  `refreshConflicts()` calls. Both flagged TODO for Phase F
  real-dialog wiring.
- [x] Unit tests for each implementation using an in-memory
  `QSettings` (via `QTemporaryDir`) — not `MockCalendarBackend` as
  originally written, since Phase D doesn't yet exercise any backend.
  `test_fullsync_calendarcollection`, `test_fullsync_syncconfigstore`,
  `test_fullsync_synchost` (covers host + both conflict stubs).
- [x] Commit: `feat(fullsync): WP implementations of libkalburator host interfaces`.

### Exit criteria

- [x] All three host interfaces (plus conflict resolver/presenter)
  have a WP implementation with passing unit tests.
- [x] No real calendar model wired up yet — Phase D is scaffolding.
- [x] Kalburator::Sync linkage remains quarantined to
  `WildPalmsFullSync` — `WildPalmsCore` (Client Mode) is still
  library-free.

### Notes captured during execution

- New static lib `WildPalmsFullSync` at `src/fullsync/`. PUBLIC-links
  `Kalburator::Sync`. Keeps Full Sync Mode code distinct from Client
  Mode so WP's existing Palm-driver build path doesn't pick up the
  KF6::DAV / KIOCore / Holidays link line until Phase F needs it.
- `add_fullsync_test()` helper added to `tests/CMakeLists.txt` —
  counterpart to the existing `add_wildpalms_test()`. Links
  `WildPalmsFullSync` + `Kalburator::Sync` only; deliberately not
  `WildPalmsCore`/`pisock`.
- ADL gotcha: libkalburator already defines `syncMappingToJson` /
  `syncMappingFromJson` in `Kalburator::Sync`. An early draft of
  `SyncConfigStore_WP` had its own anonymous-namespace copies, which
  were ambiguous to overload resolution when called with a
  `Kalburator::Sync::SyncMapping`. Removed and now use the library's.
- `CalendarCollection_WP` also exposes two host-side accessors not
  on the library interface (`calendarColor(id)`, `isCalendarVisible(id)`)
  that Phase F UI work will read. Deliberately kept out of
  `ICalendarCollection` upstream since the library doesn't need them.

---

## Phase E — PalmBackend + plugin ABI rewrite + runtime unification

**Status:** Largely landed. E.0..E.15b ✅ done 2026-04-21..2026-04-26.
E.16 🟡 partial 2026-04-28. E.17 mostly subsumed by the engine-merger
campaign (merged 2026-05-21). E.18 ❌ cancelled (POSE64 not viable).
E.19 in progress (legacy docs archived 2026-05-21; replacement docs
still TODO).

**Gated on:** Phase B2 (upstream blob layer — done) + Phase D (host
interfaces on the WP side — done). Sub-phase-level dependencies are
tracked in the spec's sub-phase table.

**Goal:** WP's existing Palm-device sync code moves from the
now-obsolete `Sync::SyncBackend` base to libkalburator's
`Kalburator::Sync::IBlobBackend`. Add a `PalmCalendarBackend` adapter
that presents Palm Datebook records as `Incidence::Ptr`. Rewrite WP's
plugin ABI (no third-party plugins exist; ABI is negotiable) and
collapse Client Mode / Full Sync Mode into a unified
`SyncCoordinator`-driven runtime.

**Detailed design + sub-phase tracking:**
`docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`.
That spec is the single source of truth for sub-phase status; this
section is a high-level pointer only. Do **not** duplicate sub-phase
checkboxes here — update the spec.

### Sub-phases landed (as of 2026-04-21)

- ✅ **E.0** — Phase-E spec itself. Commit `2a484ca`.
- ✅ **E.1** — `BlobBaselineStore` (upstream libkalburator Phase B3).
  Tag `v0.7-phase-b3-baseline`. Plan:
  `../superpowers/plans/2026-04-21-phase-e1-blob-baseline-store.md`.
- ✅ **E.2** — `BlobSyncEngine::twoWayWithBaseline` + `ConflictStore`
  wiring (upstream libkalburator Phase B4). Tag
  `v0.8-phase-b4-engine-conflicts`. Plan:
  `../superpowers/plans/2026-04-21-phase-e2-engine-conflict-wiring.md`.

### Sub-phases landed (continued)

- ✅ **E.3–E.7** — `PalmBackend : IBlobBackend`, `PalmCalendarBackend`,
  `PalmConflictHandler`, `PalmBackendConfig`, typed adapters. Landed
  across 2026-04-21 (E.3–E.6) and 2026-04-23 (E.7).
- ✅ **E.8** — new plugin ABI (`IPlugin`, `IBackendPlugin`,
  `IPluginAction`) + `BackendPluginManager` / `PluginActionManager`.
  Landed 2026-04-23.
- ✅ **E.9** — Memo rewritten as first `IBackendPlugin`
  (`MemoBackendPlugin` + `MemoBlobBackend` + `MemoMarkdown`).
  `WILDPALMS_MEMO_PLUGIN_V2` toggle; legacy `MemoConduit` remains
  until E.16. Landed 2026-04-23.
- ✅ **E.10** — Calendar rewritten as second `IBackendPlugin`
  (`CalendarBackendPlugin` + `CalendarBlobBackend` +
  `CalendarConflictHandler` + `CategoryAppInfoReader` +
  `IcsTranscoder`). First real consumer of `PalmBackend` and
  `PalmCalendarBackend`. AppInfo-block parsing landed.
  `WILDPALMS_CALENDAR_PLUGIN_V2` toggle; legacy `CalendarConduit`
  remains until E.16. Landed 2026-04-24.
- ✅ **E.11** — ToDo rewritten as third `IBackendPlugin`
  (`TodoBackendPlugin` + `TodoBlobBackend` + `TodoConflictHandler` +
  `TodoIcsTranscoder`). Second consumer of `CategoryAppInfoReader`
  (promoted into `WildPalmsPalmCalendar` static lib;
  `parseDatebookAppInfo` renamed `parseCategoryAppInfo`). Virtual
  sub-collections `palm:todo/<N>`; completion-asymmetric overlay.
  TaskView reused untouched. `WILDPALMS_TODO_PLUGIN_V2` toggle;
  legacy `TodoConduit` remains until E.16. Landed 2026-04-25.
- ✅ **E.12** — Contacts rewritten as fourth `IBackendPlugin`
  (`ContactsBackendPlugin` + `ContactsBlobBackend` +
  `ContactsConflictHandler` + `ContactsVcardTranscoder`). vCard 4.0
  on the wire; virtual sub-collections `palm:contact/<slot>`. One
  conflict overlay: per-slot field-union for `phone[]`/`custom[]` when
  single-valued fields agree. Third consumer of the shared
  `parseCategoryAppInfo` (validates the E.11 promotion). No main view
  in this phase (legacy `ContactView` stays with legacy
  `ContactConduit` until E.16). `WILDPALMS_CONTACTS_PLUGIN_V2` toggle.
  Landed 2026-04-25.

### Sub-phases landed (continued)

- ✅ **E.13** — WebCalendar rewritten as `IBackendPlugin`
  (`WebcalBackendPlugin` + `WebcalBlobBackend` + `WebcalFeed`). New
  `Kalburator::Sync::IcsFeedFetcher` upstream. Per-feed slot allocation
  to `palm:calendar/<slot>` (strictly 1:1). Cache-on-failure +
  `lastFetchSucceeded(slot)` gate. Landed 2026-04-26.
- ✅ **E.14** — Plucker rewritten as `IBackendPlugin`
  (`PluckerBackendPlugin` + `PluckerBlobBackend` + `PluckerFetcher`).
  Source-only — runtime install drain via E.15a's `IPluginAction`. Two
  collections: `plucker:channels` and `plucker:bootstrap`. Settings
  widget is the channel-management UI. Landed 2026-04-26.
- ✅ **E.15a** — Install rewritten as `IPluginAction`
  (`InstallActionPlugin`). New `IPalmFileInstaller` abstraction +
  `InstallSourceCollector` aggregating folder + cross-plugin blob
  backends. Landed 2026-04-26.
- ✅ **E.15b** — `src/fullsync/` folded into `src/runtime/`;
  `WildPalmsFullSync` static lib folded into `WildPalmsRuntime`.
  Mechanical, no behaviour change. Landed 2026-04-26.

### Sub-phases pending or partial

- 🟡 **E.16** — *Partial 2026-04-28.* SyncRunner orchestrates V2 plugins
  for all six Tools-menu sync modes (HotSync / FullSync / CopyPalmToPC
  / CopyPCToPalm / Backup / Restore). Real-device smoke: 621 records
  flowed Palm→PC on first HotSync against an m505. Status of deferrals
  (also tracked in the Phase-E spec's E.16 row):
  (a) ✅ **Done 2026-05-21.** `KalburatorInteractiveConflictHandler`
      derives from `Kalburator::Conflict::ConflictHandler`. All 12 WP
      consumers (incl. plugin submodules) migrated from
      `QSyncCore::*` to `Kalburator::Conflict::*`. The conflict-system
      bridge (`conflictdialogbridge` + `palmruntimebridgeinstall`)
      collapsed: ConflictDialog called directly from the handler;
      KalburatorInteractiveConflictHandler instantiated directly from
      kf6mainwindow. The qsynccore conflict types deleted from
      `src/sync/qsynccore/`. The dead-but-needed JSON BaselineStore /
      IDMappingStore (used by `SyncState::pendingConflictCount` for
      legacy per-conduit conflict tracking, distinct from the SQLite
      Kalburator::Storage stores used by the new sync engine) relocated
      to `src/sync/journal/` under namespace `WildPalms::Sync`.
      Original ConnectionBehavior consult-via-PalmBackendConfig feature
      deferred (not required to land the migration).
  (b) ✅ **Resolved by removal 2026-05-21.** Rather than fix the
      cross-thread parenting bug, the WebCalendar plugin was deleted
      outright (submodule + tests + PalmRuntime registration + Plucker
      runAfter entry). The feature goes away; the bug becomes moot.
      Re-add as a fresh plugin later if Web feed subscription is
      wanted, using a thread-local fetcher pattern from day one.
  (c) ✅ **Done 2026-05-21.** Multi-collection per-DB re-read perf:
      `PalmBackend::loadPalmRecords` now caches per-database; mutators
      invalidate for their dbName. Cuts the 4× AddressDB / 4× ToDoDB
      open+read pattern down to 1× per sync cycle. Regression covered
      by `tst_palmbackend::loadPalmRecordsCachesAcrossCalls` and
      `cacheInvalidatedOnMutation`.
  (d) `LocalBlobBackend` cross-id-space mapping — likely duplicate-on-
      second-sync, untested. Needs e2e against `LocalBlobBackend`.
  (e) ✅ **Done 2026-05-21** (commit `f615926`).
      `WildPalms::FullSync` → `WildPalms::Runtime` namespace rename.
- 🟡 **E.17** — Mostly subsumed by the engine-merger campaign (phases
  J/K/L/M, merged 2026-05-21). Remaining straggler call-sites tracked
  inline in the Phase-E spec's E.17 row.
- ❌ **E.18** — **Cancelled 2026-05-21.** Original scope was integration
  tests against a POSE64 emulator sandbox; POSE64's DLP timing is too
  unstable for a reliable harness, and the effort to stabilise it is
  out of proportion with the value. Coverage is provided by per-plugin
  e2e ctests already in the suite, plus periodic manual smoke runs
  against real Palm hardware. Revisit if a viable emulator harness
  appears later.
- 🟡 **E.19** — Partial 2026-05-21. Legacy conduit / SDK docs and the
  three pre-Phase-E TODO files moved to `docs/archived/`. Still TODO:
  write `docs/PLUGIN_ABI.md` describing the new ABI; refresh
  `docs/ARCHITECTURE_2026.md` + `docs/SYNC_ENGINE_ARCHITECTURE.md`;
  flip this plan's Phase E row to ✅ done when the above ship and the
  E.16 conflict-handler rebind has landed.

### Exit criteria

- WP's Palm sync goes through libkalburator for transport.
- `src/sync/` is deleted; plugin ABI is the new one.
- Client Mode and Full Sync Mode run on the same code path.
- All existing WP sync tests pass (conduit behaviour preserved via
  mapping defaults); new integration tests for category routing and
  conflict resolution pass.

---

## Phase F — Full Sync Mode UX polish

**Scope change (2026-04-21):** Phase F as originally written carried
both the runtime mode-collapse and the UX polish on top. Phase E's
spec decision #3 moved the mode-collapse earlier into E.16 (it's
machinery, not UX, and doing it mid-plugin-ABI-rewrite is simpler
than layering it later). Phase F is now UX-only: wizard, dialogs,
conflict-review UI, docs.

**Goal:** Users can create Full Sync Mode profiles. The UI exposes
multi-backend calendar management, conflict resolution, and sync
actions. WP's conflict presenter is real (not the Phase D stub).

### Tasks

- [ ] Design doc for the Full Sync Mode UX (separate doc, not this
  plan). Scope: profile-creation wizard, backend-configuration
  dialogs, calendar-binding UI, conflict-resolution dialog. The
  underlying machinery (unified `SyncCoordinator`-driven runtime,
  `SyncMapping` lists per profile) is already in place from E.16.
- [ ] Implement per the design doc.
- [ ] Wire `ICalendarHost_WP`'s apply-* methods to WP's actual
  calendar model (E.17 covers the mechanical moves; F wires UI
  triggers for them).
- [ ] Real `IConflictPresenter` — replaces Phase D auto-accept stub.
- [ ] Profile-creation UX: choose mapping targets (local files /
  CalDAV / Akonadi) when creating a new profile.
- [ ] End-to-end test: Full Sync Mode profile syncs Palm + CalDAV
  (against local Radicale test server); conflict surfaces in UI;
  user resolves; result is correct on both sides.
- [ ] Documentation: `FULL_SYNC_MODE_GUIDE.md` user-facing guide.

### Exit criteria

- Full Sync Mode is shippable per design doc §"Success criteria".
- Client Mode profiles (carried over from E.17's migration) unaffected.

---

## Phase G — joint v1.0 declaration

**Goal:** libkalburator v1.0 cut. Semantic-versioning stability
promise kicks in. Shared across PlanStan + WP.

### Tasks

- [ ] Confirm PlanStan + WP both pass their own integration tests
  against the same libkalburator commit.
- [ ] Tag libkalburator `v1.0.0`.
- [ ] WP and PlanStan each pin to `v1.0.0`.
- [ ] Decide on public-forge hosting per
  `~/dev/libkalburator/docs/phase0/05-repo-strategy.md` §"Repository
  location" — GitHub / KDE Invent / Codeberg.
- [ ] Write `CHANGELOG.md`, `CONTRIBUTING.md`, README for public
  consumption.
- [ ] Apply SPDX LGPL-3.0-only headers to all source files (upstream).
- [ ] Announce to KDE-PIM mailing list if appropriate.

---

## Progress tracking

Update this document in the same commit that lands phase work. Do
not leave exit-criteria checkboxes stale — if a phase ships, mark it
and summarise the actual scope delivered. Per
`~/dev/PlanStan/CLAUDE.md` §"Tracking Plans, Proposals, and Phase
Status": status docs that lie about progress are worse than no
status docs.

When a phase completes, also update:

- `../LIBKALBURATOR.md` — the status pointer at docs root.
- `~/dev/libkalburator/docs/phase0/04b-phase3-status.md` — bump to
  new status if the phase was upstream work.
- `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`
  — the cross-repo proposal's Status line.
