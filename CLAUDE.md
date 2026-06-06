# WildPalms — agent handoff

Cross-repo Qt6/KF6 desktop app for syncing Palm OS PIM data with an Akonadi/cloud-backed hub. Sync engine is libkalburator (sibling repo); per-conduit logic lives in four GitHub-hosted submodules under `src/plugins/{calendar,contacts,memo,todos}/`.

For deeper history check `~/dev/CLAUDE.md` (the global dev-root instructions) and `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` (auto-memory).

---

## Current branch and state (as of 2026-06-05)

**Branch:** `feature/three-tier-sync` at origin `270bfa8`.
**libkalburator pin:** `v0.65` (`CMakeLists.txt:63`).
**Build dir convention:** legacy `build/` (no `CMakePresets.json`).
**ctest:** **117/120 pass.**

### The three known failures (all pre-existing, all deferred)

Same root cause: `dynamic_cast<SyncBackend*>` returns null because Plan 3 reparented non-calendar backends to `SyncBackendBase`. The test harnesses use the older cast.

- `tst_palm_runtime_route_first_sync`
- `tst_palm_runtime_route_recategorization`
- `tst_runtime_carddav_e2e`

Triage doc: `docs/2026-06-04-v0.63-pin-bump-test-regressions.md`. Do NOT count these as your regressions.

---

## What just landed: clobber-sync feature

A "Clobber Palm from PC" Tools-menu mode that wipes selected Palm-side PIM databases and re-pushes from the hub in one operation.

**Key references:**
- Spec: `docs/superpowers/specs/2026-06-05-clobber-sync-design.md`
- Plan: `docs/superpowers/plans/2026-06-05-clobber-sync.md`
- libkalburator handoff RFC: `docs/2026-06-05-libkalburator-clobber-sync-handoff.md`
- libkalburator response: `~/dev/libkalburator/docs/2026-06-05-clobber-sync-response.md`

**Plan progress:**

| Task | Status | Commit |
|---|---|---|
| 1: libkalburator handoff RFC | Done | `f7330f9` |
| 2: pin bump v0.64→v0.65 | Done | `1184368` |
| 3: mapping classification helpers | Done | `77542cf` |
| 4: ClobberDialog | Done | `84b62c1` |
| 5–8: per-conduit wipeCollection overrides | Done in submodules | gitlinks at `675aaf7` |
| 9: gitlink bumps + WP-side scaffolding | Done | `675aaf7` |
| 10: PalmRuntime::clobberSync | Done | `39247e0` |
| 11: kf6 menu rewire | Done | `270bfa8` |
| 12: device-backed hardware verification | **PENDING** | — |

### Outstanding work on this feature

**1. Hardware verification (Plan Task 12) — not yet run.**
The full freshen-Palm loop on a real Palm device (the user's original ask) is unverified. Procedure documented at the end of the plan doc. Until this runs, treat the feature as "lands clean on CI but unverified on hardware."

**2. Phase B consistency pass — deferred.**
Tasks 5 (calendar) and 6/7/8 (contacts/memo/todos) ship inconsistent implementations:
- **Calendar** uses the fast path: `PalmBackend::wipePalmDatabase(name)` → `deleteDatabase` + `createDatabase` + `invalidateCache`.
- **Contacts, Memo, ToDo** use the per-record loop equivalent: `loadPalmRecords(name)` + per-record `deletePalmRecord(name, id)`.

Both are functionally correct (meet `IBlobBackend::wipeCollection`'s contract: collection emptied, still exists), but inconsistency is a smell. The follow-up brings 6/7/8 onto the `wipePalmDatabase` helper.

Also deferred: WP-side `tst_<conduit>blobbackend.cpp` tests for contacts/memo/todos similar to the calendar one that landed in `675aaf7`. The Task 5 agent landed that pattern; the other three agents couldn't because of how I scoped their prompts (they thought they couldn't touch WP `tests/`).

**3. `dlp_DeleteDB` + `dlp_CreateDB` hardware fast path.**
The `IPalmDatabaseAccess::deleteDatabase` default impl is loop-delete; the pilot-link concrete impl doesn't override it yet. For real-device clobber, wiring true `dlp_DeleteDB`/`dlp_CreateDB` (with correct creator/type IDs — `date`/`DATA`, `addr`/`DATA`, `memo`/`DATA`, `todo`/`DATA`) into `KPilotLink` / `KPilotDeviceLink` is a small follow-up that lets the device avoid N round-trips per wipe.

---

## Build + test

```bash
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.65
cmake --build build -j 8
ctest --test-dir build -j 8
```

Submodules must be initialized once per fresh clone or worktree:
```bash
git submodule update --init --recursive
```

---

## Cross-repo discipline (important)

`feedback_libkalburator_handoff_workflow` (auto-memory) — do NOT edit libkalburator from this repo. Write handoff docs in `WildPalms/docs/` and let libkalburator land the changes. The PlanStan-green gate (`feedback_planstan_pretest_for_upstream`) requires every libkalburator commit to pass PlanStan's ctest baseline before tagging.

Submodules ARE part of WildPalms's scope: edit freely in `src/plugins/<conduit>/`, commit there, push to the submodule's GitHub remote, then bump the gitlink in the superproject.

---

## Open architectural gap unrelated to clobber

**No hub↔remote-only sync exists.** Today a user editing a record in the WildPalms UI cannot propagate that edit to a cloud spoke (CalDAV/CardDAV) without connecting a Palm — `hotSync` and `fullSync` both require a connected Palm. This is a real gap in the three-tier-sync architecture's "hub buffers Palm edits and propagates to remotes when reachable" promise. Out of scope for clobber-sync but worth its own design pass.

---

## Ready to work on next (no hardware required)

Hardware verification of clobber-sync (Plan Task 12) is the user's bottleneck until they're back at a real Palm. Until then, these are concrete, no-hardware-needed tasks:

### A. Phase B consistency for clobber-sync (small, ~1-2 hours)

Bring contacts/memo/todos onto the `PalmBackend::wipePalmDatabase` helper that calendar already uses; add WP-side `wipeCollection_clears_palm_<conduit>_database` tests for each, modeled on the one in `tests/plugins/calendar/tst_calendarblobbackend.cpp`. Each conduit needs:

- One submodule commit replacing the per-record loop with `m_palmBackend->wipePalmDatabase(<dbName>)`.
- One WP-side test in `tests/plugins/<conduit>/tst_<conduit>backendplugin.cpp`.
- One gitlink bump in the superproject (can batch all three).

### B. Finalize and ship the FilteredCollectionBackend RFC

`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` has been in active editing all session (+177/-48 unstaged on the 354-line file). It's an RFC for libkalburator to add a generic `RecordFilter` + `FilteredCollectionBackend` primitive for property-based slicing of a hub collection — needed for WP's hub-and-spoke remote routing by `categories`. PlanStan benefits too. Status header reads "Proposal / RFC — requesting a small, focused addition." When the user is ready, finishing the edits and shipping as a handoff (same pattern as the clobber-sync RFC) unblocks libkalburator from starting the work.

### C. Triage the three pre-existing v0.63 test failures

`tst_palm_runtime_route_first_sync`, `tst_palm_runtime_route_recategorization`, `tst_runtime_carddav_e2e`. The triage doc at `docs/2026-06-04-v0.63-pin-bump-test-regressions.md` enumerates hypotheses; the leading one (and what the Task 10 clobber agent confirmed by working around it) is that `BlobSyncBackendWrapper::wrap()` doesn't satisfy the post-Plan-3 `SyncBackendBase` contract. Fixing the wrapper would likely fix all three tests in one shot and remove the workaround Task 10 had to do in its own test path.

### D. Brainstorm the hub↔remote-only sync gap

Surfaced during clobber-sync brainstorming, this is the real bug that a user who edits a record in the WildPalms UI cannot propagate that edit to a cloud spoke without connecting a Palm. Three-tier-sync architecture promises "hub buffers Palm edits and propagates to remotes when reachable" — the propagation half is wired only through hot/full-sync today. Worth its own spec → plan flow.

### E. Optional — `dlp_DeleteDB`/`dlp_CreateDB` hardware fast path

`IPalmDatabaseAccess::deleteDatabase` default impl is loop-delete; the pilot-link concrete impl in `src/palm/kpilotlink*.cpp` doesn't override yet. Wiring real DLP calls (with creator IDs `date`/`addr`/`memo`/`todo`, type `DATA`) into `KPilotLink` is implementation work that can ship without device validation, then get validated alongside Task 12. Speeds up real-device clobber from O(N) round-trips to O(1).

---

## Open handoff/RFC docs worth tracking

These either need a libkalburator response or sit on the WP-edit pile:

| Doc | Direction | Status |
|---|---|---|
| `2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` | WP → lib | Open RFC; **actively edited (item B above)**; unstaged +177 lines |
| `2026-05-27-libkalburator-topology-authority-proposal.md` | WP → lib | Open RFC; the hub editability authority/demotion question. No response yet from lib AFAICT. |
| `2026-05-26-calendar-writer-palmwire-parse-handoff-libkalburator.md` | WP → lib | Labeled "Blocker for CalDAV→Palm calendar sync"; status not re-verified this session |

The DAV-config-integration handoff (`2026-05-26-dav-config-integration-handoff-from-libkalburator.md`) is closed — `AccountFormWidget` already bridges `IProviderConfigWidget` (verified `src/app/accounts/accountformwidget.cpp:117-119, 157-159`).

---

## Long-running unstaged file

`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` carries +177/-48 of WIP edits (item B above). Leave it on the working tree if not actively shipping; don't accidentally commit it as part of unrelated work.
