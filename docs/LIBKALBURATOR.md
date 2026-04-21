# libkalburator — status & coordination

**Date:** 2026-04-21 (updated)
**Upstream repo:** `~/dev/libkalburator/` (local-only git, not yet
published to a public forge)
**Upstream tag:** `v0.5-phase-c` (2026-04-21) — first named release.
**Target (CMake):** `Kalburator::Sync` (alias for `kalburator` static
lib), and `Kalburator::Types` for the headers-only shared vocabulary.
**License (planned):** LGPL-3.0-only — compatible with Wild Palms'
GPLv3.

## What it is

Qt6 / KF6-based calendar synchronization library extracted from
PlanStan. Multi-backend two-way sync across CalDAV, local iCal,
org-mode, Akonadi, DecSync, web subscriptions, Holidays; conflict
detection with persistent deferred-resolution workflow; crash-recovery
journaling; lossy-format transcoding preservation (RRULE +
X-property).

Wild Palms' **Full Sync Mode** is designed around consuming this
library so that WP becomes a first-class multi-backend calendar
application, not merely a Palm-device sync tool. See
`PROJECT_VISION.md` and
`plans/2026-04-20-libkalburator-integration-design.md`.

## Current upstream state (2026-04-21)

**Source of truth:** `~/dev/libkalburator/docs/phase0/README.md` is
the living index. Consult it first. Summary below is a pointer, not a
replacement.

libkalburator has landed through **Phase C.6** (`v0.5-phase-c` tagged
on `main` 2026-04-21). The library is in the shape the Phase 0
design called for. Concretely:

- **Namespaced.** Everything lives under `Kalburator::Sync::*` (and
  `Kalburator::Sync::QSyncCore::*` for the WP-originated conflict
  framework files, to resolve `ConflictResolution` / `SyncStats` /
  `SyncResult` collisions with `src/types/synctypes.h`).
- **Directory-layered.** Source lives under
  `src/{types,calendar,conflict,transcoding,journal,discovery,blob}/`
  per `~/dev/libkalburator/docs/phase0/05-repo-strategy.md`.
- **WP's `qsynccore/` is upstream.** The entire conflict framework
  (`baselinestore`, `conflictstore`, `conflictpolicy`,
  `conflictrecord`, `synccommon`, plus `conflicthandlerregistry`
  added in C.1) has been merged into libkalburator. PlanStan consumes
  it; WP still carries its own copy under `src/sync/qsynccore/`
  pending Phase E.
- **Two CMake targets.** `Kalburator::Types` (shared vocabulary,
  minimal deps) and `Kalburator::Sync` (full sync engine). No
  `KALBURATOR_PROVIDE_TYPES` flag — types are always built into
  `kalburator-types`.
- **IDMappingStore is SQLite** (`src/journal/idmappingstore.{h,cpp}`),
  in the top-level `Kalburator::Sync` namespace. It is the sole
  owner of the `sync_id_mappings` table (SyncStore's identity-mapping
  API was dissolved in C.5). The store extends Audit-2's WP fields
  (`last_synced`, `source_category`, `target_categories`, `archived`)
  plus `recurrenceId`. Shares the `.planstan-sync.db` database with
  SyncStore via idempotent `ALTER TABLE ADD COLUMN`.

**What is NOT yet upstream, relevant to Wild Palms:**

- **Blob layer.** `src/blob/` exists as an empty placeholder. The
  Phase 0 architecture (`04-merged-interface-sketch.md`) calls for
  a lower-layer `IBlobBackend` + `BlobSyncEngine` + `LocalBlobBackend`
  + `MockBlobBackend` under the calendar-typed upper layer. The
  calendar upper layer is complete; **the blob lower layer is not
  built.** This is the single largest remaining piece of the merged
  architecture, and WP's `PalmBackend` is the natural forcing function
  for its design. See the integration plan's Phase B2 below.
- **Contacts / memos backends.** Explicitly deferred past Phase 4
  per `~/dev/libkalburator/docs/phase0/00-open-questions.md` §6. WP's
  contacts and memos stay WP-internal for the foreseeable future.
  Library is calendar-only.
- **Tests inside libkalburator.** Deferred — PlanStan's test suite is
  the only regression guard today. Adding tests in libkalburator
  itself is expected to land alongside WP adoption, since WP exercises
  paths PlanStan doesn't.
- **Install target / `KalburatorConfig.cmake`.** Deferred. Both
  PlanStan and WP consume libkalburator via `add_subdirectory` so this
  doesn't block integration.

## When to read which doc

| You want to… | Read |
|---|---|
| Understand the vision & why we're doing this | `PROJECT_VISION.md` + `plans/2026-04-20-libkalburator-integration-design.md` |
| Execute the integration | `plans/2026-04-20-libkalburator-integration.md` |
| Phase E design (the active phase) | `superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` |
| See the library's current surface | `~/dev/libkalburator/src/*/` — the directory layout is stable. `~/dev/libkalburator/docs/phase0/04-merged-interface-sketch.md` is still the design sketch but some surfaces are narrower than sketched (see `04a-followups.md`). |
| See upstream status + unfinished work | `~/dev/libkalburator/docs/phase0/README.md` — this is the living index, updated with each phase landing. |
| Understand repo / license / CMake consumption plan | `~/dev/libkalburator/docs/phase0/05-repo-strategy.md` |
| See the phase-by-phase design rationale | `~/dev/libkalburator/docs/phase0/04[b-g]-phase-c*-design.md` |

## Coordination protocol

libkalburator is maintained by the same person working on PlanStan
and Wild Palms. During Wild Palms integration:

1. **Interface gaps or bugs found** in libkalburator → fix upstream
   in `~/dev/libkalburator/` first, then pick up from the WP side.
   Do **not** monkey-patch around the library inside Wild Palms — the
   whole point is shared code.
2. **Upstream API changes requested by WP** → land them upstream with
   a clear commit message (phase-tag convention: `Phase 4.X: …`),
   and add a note to `~/dev/libkalburator/docs/phase0/04a-followups.md`
   (or a new Phase-4 doc) with `[WP-driven]` provenance so future
   maintainers understand the motivation.
3. **ODR hazard window is closed.** Previous revisions of this doc
   warned against modifying shared types in either tree because three
   copies (PlanStan, libkalburator, future-WP) coexisted. That window
   closed in libkalburator's Phase C.2a: PlanStan's duplicate shared
   types were deleted, and `Kalburator::Types` is now the single
   source. Modify shared types normally in `~/dev/libkalburator/src/types/`;
   PlanStan and WP both pick up changes on the next build.
4. **Option 2 / single-source-of-truth is already done.** Previous
   revisions sequenced "(Later) PlanStan executes option 2" as a
   pending step. It landed in C.2a. There is no longer a pre-WP
   PlanStan readiness gate. WP can start Phase A today.

## Sequencing (revised 2026-04-21)

The original integration design (2026-04-20) sequenced WP work
against libkalburator Phase 3b and assumed upstream would still do
"Phase 4: namespace migration, layered split, merge qsynccore"
afterwards. In practice:

- Upstream Phases C.1–C.6 landed between 2026-04-20 and 2026-04-21,
  carrying the namespace migration (C.2b), the layered split (C.3),
  the qsynccore merge (C.1 + C.4 + C.5), and the SQLite IDMappingStore
  (C.4). What the integration plan calls **WP Phase B and the
  non-blob-layer portion of WP Phase C are already complete upstream.**
- The remaining **upstream work for WP adoption** is the blob layer
  (`IBlobBackend` + `BlobSyncEngine` + `LocalBlobBackend` + MockBackend),
  plus library-side tests, plus an eventual `KalburatorConfig.cmake`
  install target. These are the expected contents of the upstream
  **Phase 4** (Wild Palms adoption) per
  `~/dev/libkalburator/docs/phase0/README.md`. That phase has no
  design doc yet; it will be opened when WP integration concretely
  needs each piece.
- The **remaining WP-side work** is unchanged: vendor libkalburator
  (Phase A), implement host interfaces (Phase D), refactor `PalmBackend`
  onto `IBlobBackend` (Phase E, gated on the upstream blob layer),
  ship Full Sync Mode UI (Phase F), joint v1.0 (Phase G).

See `plans/2026-04-20-libkalburator-integration.md` for the phase
status and the detailed checklists.

## Build prerequisites

Match PlanStan's current pin: Qt6 6.8+, KF6 (CalendarCore, DAV, KIO,
Holidays). Optional KPim6 AkonadiCore. CMake 3.19+. C++20.

Wild Palms already depends on Qt6 and KF6CalendarCore — no new
platform deps of consequence. libkalburator's optional subsystems are
controlled via cache variables: `KALBURATOR_HAVE_ORG_IO` (leave OFF
for WP — WP doesn't ship org-io), `KALBURATOR_HAVE_AKONADI` (WP can
opt in when Akonadi support is ready).
