# libkalburator — status & coordination

**Date:** 2026-04-20
**Upstream repo:** `~/dev/libkalburator/` (local-only git, not yet
published to a public forge)
**Target (CMake):** `Kalburator::Sync` (alias for `kalburator`
static lib)
**License (planned):** LGPL-3.0-only — compatible with Wild Palms'
GPLv3.

## What it is

Qt6 / KF6-based calendar synchronization library extracted from
PlanStan. Multi-backend two-way sync across CalDAV, local iCal,
org-mode, Akonadi, DecSync, web subscriptions, Holidays; conflict
detection with persistent deferred-resolution workflow; crash-recovery
journaling; lossy-format transcoding preservation (RRULE + X-property).

Wild Palms' **Full Sync Mode** is designed around consuming this
library so that WP becomes a first-class multi-backend calendar
application, not merely a Palm-device sync tool. See
`PROJECT_VISION.md` and
`plans/2026-04-20-libkalburator-integration-design.md`.

## Current phase on the upstream side

**Phase 3 complete** (2026-04-20). PlanStan now consumes libkalburator
in-tree via `add_subdirectory` with `KALBURATOR_PROVIDE_TYPES=OFF`.
PlanStan's old `libs/sync/` has been deleted.

**What this means for Wild Palms right now:** the library exists, is
buildable, and is battle-tested by one real consumer (PlanStan). It is
**not yet** the fully-reconciled shape described in the Phase 0 design
docs. Specifically:

- **Not yet layered.** The `blob/` vs `calendar/` directory split
  from `~/dev/libkalburator/docs/phase0/05-repo-strategy.md` is not
  implemented. All sync code sits flat in `src/sync/`, calendar-typed
  only. There is no `IBlobBackend` / `BlobSyncEngine` yet.
- **Not yet namespaced.** `Kalburator::Sync::*` is the target
  namespace but has not been applied to the source. Types currently
  live in PlanStan's original namespaces / global scope.
- **Wild Palms' `qsynccore/` not yet merged in.** The Phase 0 design
  called for lifting `baselinestore`, `conflictstore`,
  `idmappingstore`, `conflictpolicy`, `conflictrecord`, `synccommon`
  from `src/sync/qsynccore/` into libkalburator. That work was
  deferred — libkalburator currently has PlanStan's own conflict
  code (`conflictmanager`, `iconflictresolver`, `iconflictpresenter`),
  **not** Wild Palms' more complete framework.
- **Contacts / memos explicitly deferred past Phase 4.** See
  `~/dev/libkalburator/docs/phase0/00-open-questions.md` §6. Wild
  Palms' Palm contacts and memos stay Wild-Palms-internal for now.
  The library is calendar-only for the foreseeable future.

See `~/dev/libkalburator/docs/phase0/04b-phase3-status.md` for the
definitive upstream status.

## When to read which doc

| You want to… | Read |
|---|---|
| Understand the vision & why we're doing this | `PROJECT_VISION.md` + `plans/2026-04-20-libkalburator-integration-design.md` |
| Execute the integration | `plans/2026-04-20-libkalburator-integration.md` |
| See the library's current surface | `~/dev/libkalburator/docs/phase0/04-merged-interface-sketch.md` + actual `~/dev/libkalburator/src/sync/*.h` |
| See upstream status / open questions | `~/dev/libkalburator/docs/phase0/04b-phase3-status.md` + `00-open-questions.md` + `04a-followups.md` |
| Understand repo/license/CMake consumption plan | `~/dev/libkalburator/docs/phase0/05-repo-strategy.md` |

## Coordination protocol

libkalburator is currently maintained by the same person working on
PlanStan. During Wild Palms integration:

1. **Interface gaps or bugs found** in libkalburator → fix upstream
   in `~/dev/libkalburator/` first, then pick up from the WP side.
   Do **not** monkey-patch around the library inside Wild Palms — the
   whole point is shared code.
2. **Upstream API changes requested by WP** → add them to
   `~/dev/libkalburator/docs/phase0/04a-followups.md` with a
   provenance note `[WP-driven]` so future maintainers understand the
   motivation.
3. **ODR hazard window.** PlanStan currently holds duplicate copies of
   `BackendConfiguration`, `LogicalCalendar`, `CalendarMetadataManager`,
   `IIncidenceSource`, `ICalendarCollection`, `ISyncConfigStore` —
   libkalburator has its own `src/types/` copies too. Until PlanStan
   completes its "option 2" layering, **do not modify these headers
   in either tree.** If WP integration surfaces a genuinely needed
   change, coordinate it as a single commit across all three repos.
4. **Sequencing with PlanStan's option 2.** Per the brainstorm on
   2026-04-20, WP integration runs **before** PlanStan executes
   option 2. WP consuming libkalburator with
   `KALBURATOR_PROVIDE_TYPES=ON` (the default) is the real test of
   whether the exported types are self-sufficient. Any gaps WP finds
   will be fixed before PlanStan commits to single-source-of-truth.

## Build prerequisites

Match PlanStan's current pin: Qt6 6.8+, KF6 (CalendarCore, DAV, KIO,
Holidays). Optional KPim6 AkonadiCore. CMake 3.19+. C++20.

Wild Palms already depends on Qt6 and KF6CalendarCore — no new
platform deps of consequence.
