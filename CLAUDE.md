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

## Long-running unstaged file

`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` carries +177/-48 of WIP edits stashed-and-popped repeatedly throughout this session. Leave it on the working tree; don't accidentally commit it as part of unrelated work.
