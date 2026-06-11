# WildPalms — agent handoff

Cross-repo Qt6/KF6 desktop app for syncing Palm OS PIM data with an Akonadi/cloud-backed hub. Sync engine is libkalburator (sibling repo); per-conduit logic lives in four GitHub-hosted submodules under `src/plugins/{calendar,contacts,memo,todos}/`.

For deeper history check `~/dev/CLAUDE.md` (the global dev-root instructions) and `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` (auto-memory).

---

## Current branch and state (as of 2026-06-11)

**Branch:** local `main` at `8c7571c` — **117 commits ahead of `origin/main`, unpushed**
(push only at user request; both the v0.67 response §5 and the Plan 8 handoff §5 ask for a
push so lib gates can run against WP's real baseline — flagged to user).
**libkalburator pin:** `v0.69` (`CMakeLists.txt:63`).
**Build dir convention:** legacy `build/` (no `CMakePresets.json`). Stray dirs `build-dev/`, `build-c/`, `build-fetchcontent/`, `build-appimage/` may exist on disk from prior experiments; ignore unless cleaning house.
**ctest:** **123/123 pass.**

### Configuration Substrate (Sub-project A) — LANDED 2026-06-11

Executed `docs/superpowers/plans/2026-06-11-config-substrate.md` in full (12 tasks,
commits `ca29caf`→`8c7571c`). The hardcoded-four-conduits assumption is gone:

- **Conduit descriptor.** `PimPlugin` is promoted to the conduit descriptor
  (`conduitId`/`domain`/`primaryDbName`/`matchesCollection`/`categorySlotNames`/…);
  the four stock plugins implement it (submodule commits pushed, gitlinks bumped).
  PalmRuntime, the wizard, and `kf6mainwindow` enumerate `conduits()` instead of
  `dynamic_cast` chains (one residual base-cast). `conduitcatalog::createStockConduits`
  is the single source of truth. `domainfilter.{h,cpp}` deleted (folded into
  `matchesCollection`). New seam `PalmRuntime::appendConduitForTest` + `tst_fifth_conduit`
  prove a 5th conduit participates with zero WP source change.
- **Names-first routes.** Rows persist `palm:<domain>/name:<categoryName>`;
  `translateRouteSpec(row, conduits)` returns a `RouteTranslation{spec, status}` and
  never silently drops a well-formed row (`RouteStatus` Active/WaitingForDevice/
  NoFreeSlot/NotARoute; `PalmRuntime::routeStatuses()`). The graph view writes/reads the
  name form (fixed a latent `palm:contact/` + `palm:memo/` domain bug). **No migration —
  pre-existing profiles' slot-form rows won't translate; recreate via the wizard.**
- **Category reconciler.** `Profile::desiredCategoryNames` + `initialSyncPending`;
  `reconcileCategories()` (pure, case-insensitive, preserves AppInfo tail);
  `IPalmDatabaseAccess::writeAppBlock` (dbName-level, link-thread marshaled);
  `finishConnect` reconciles desired categories → device before backends read AppInfo.
- **Everything-is-a-provider.** `LocalFolderContribution` — first credential-less source
  in the same registry as DAV/Akonadi.

**Hardware-verification queue (gated on a real Palm):** the **first live AppInfo write**
(`PilotLinkPalmDatabaseAccess::writeAppBlock` → reconciler creating category slots on the
device) joins clobber-sync Task 12. The reconciler is exercised on fakes (which return
empty AppInfo → no-op) and in `tst_category_reconciler`, but never against hardware yet.
**User smoke tests pending:** recreate a wizard profile (slot-form rows retired), HotSync,
and confirm the reconciler creates the expected category slots; the contacts main-view
page now appears (descriptor-driven `kf6mainwindow` wiring fixed the old omission).

### First live HotSync through the wizard profile — route dispatch FIXED (`fa67c83`, 2026-06-11)

User's first device HotSync against a wizard-created profile: the 4 hub↔palm mappings ran;
all 3 account-backed route mappings (hub→DAV) failed with `dispatchSync: backend not found`.
Root cause: the wizard persisted `targetBackend = <bare account uuid>`, but
`ProviderManager::registerProviderBackends` registers provider-collection backends as
`"<providerId>:<collectionId>"` — the convention SyncMappingsGraphView documents/writes and
`AccountController::mappingIndicesFor` cascade-matches (`"<uuid>:"` prefix), meaning the bare
uuid also exempted wizard rows from cascade-delete. Fix at the wizard write site
(`kf6mainwindow.cpp`); `translateRouteSpec` passes it through verbatim so the composed id
reaches the engine unchanged. **No migration: profiles created before `fa67c83` carry broken
rows — recreate via the wizard (user smoke test pending: HotSync should now run all 7
mappings; todo route correctly targets the VTODO-capable "Next Actions").**

### Unified DAV account kind — landed 2026-06-11 (`74cb635`)

Add Account previously offered "CalDAV (calendar)" and "CardDAV (contacts)" as separate
kinds — same WebDAV credentials split across two accounts, defeating the provider
abstraction's point. `registerStandardContributions` now registers ONLY
`multiproto-dav` (lib's `MultiProtocolDavBackendContribution`, full CalDAV+CardDAV
surface, per-leg graceful degradation for one-protocol servers) + `akonadi`. The
single-protocol contributions are deliberately unregistered; **no backward compat by
user decision — account kinds "caldav"/"carddav" no longer resolve.** Dropdown label:
"DAV server (calendar + contacts)". All account-kind strings in tests swept to
`multiproto-dav` (the lib's `CalDavProvider`/`CardDavProvider` e2e tests construct
providers directly and are untouched). **User smoke test pending: re-add the Nextcloud
account as the unified DAV kind — one credential entry should yield calendar + todo +
contacts bindings in the wizard.** Related: lib's pending WP-A1 RFC (calendarsOnly
per-account mode selection) targets this same surface.

### Plan 8 consumer wave — DONE this session (2026-06-10)

libkalburator's `docs/2026-06-10-plan8-wildpalms-consumer-wave-handoff.md` executed in full;
WP response doc: `docs/2026-06-10-plan8-consumer-wave-response-wildpalms.md`. **WP is
runSyncFuture-clean; lib step 3 (overload deletion) is unblocked from WP's side.**

- A.0 pin v0.66→v0.69 (`d68fa5d`); A.1 `PalmSyncHost` collapsed onto v0.69 registry-backed
  ISyncHost defaults (`e5d2820`); A.2 `SyncHost_WP` kept per recommendation.
- B.1/B.2 (`4dc3537`): both `runSyncFuture` call sites migrated to `runSync(SyncRequest)`.
  Result delivery moved out of `.then()` (Qt6 drops continuations on cancel → runFinished
  never fired → UI hang) into the cancellation watcher's finished slot; caller futures are
  promise-backed and always finish; cancelled runs now report success=false /
  "Sync cancelled". Single-mapping path synthesizes the cancelled result because the
  canonical wrapper future carries NO result after cancel (lib FINDINGS).
- New tests: `cancelSync_midRun_{mirror,hotSync}_emitsRunFinished` (slow-loadRecords mock
  for a deterministic cancel window).
- **Do NOT bump the pin past the lib's step-3 tag until the lib confirms** — actually WP is
  clean now, so the step-3 compile break won't bite; but watch for the lib's announcement.

### Heads-up: upcoming lib RFCs aimed at WP (from v0.67 response §Consumer actions)

calendarsOnly mode selection (WP-A1), IProvider failure-signal contract (WP-A7),
BaselineStore-v2 retirement (WP-C5), recurrenceCapabilities migration (WP-C6, also visible
as a deprecation warning in our build). Docs live lib-side under
`docs/campaign/architectural-redress/2026-06-10-audit-follow-up-specs.md`.

### First live run of the accounts-first wizard (2026-06-09, post-landing)

User exercised the new wizard against a real Nextcloud account (12 calendars). Outcomes:

1. **Teardown segfault — FIXED at `1be66a3`.** `KF6MainWindow::loadProfile` replacing the previous `AccountController` crashed: `~ProviderManager()` (member) calls `disconnectAll()`, provider emits, `providerStateChanged` reached the ctor lambda which wrote into the already-destroyed `m_states` (declared after `m_providerManager`; context disconnect only happens later in `~QObject`). Fix: sever connections in the dtor body. Regression test: `tst_account_controller::destruction_does_not_deliver_provider_signals`.
2. **Todo conduit unbindable to CalDAV task lists — RESOLVED, shipped at lib v0.67; WP pinned past it (v0.69).** RFC: `docs/2026-06-09-libkalburator-collectioninfo-contenttypes-handoff.md`; lib response: `~/dev/libkalburator/docs/2026-06-10-v067-response.md` (CLOSED). The calendar filter follow-up is DONE at `ba481a8` — contentTypes are authoritative for the calendar conduit when reported, bare `type=="calendar"` only as fallback (Akonadi). **Remaining: user smoke-tests the wizard — todo dropdowns should list the 10 VTODO-capable Nextcloud calendars (7 tasks-only + 3 mixed); datebook dropdown shrinks to the 5 VEVENT-capable; accounts no longer stick at "Connecting" (v0.67's second fix).**
3. **UX observations (not bugs, candidate roadmap items):** (a) Bindings page binds ONE collection per conduit — syncing *all* the user's calendars needs the existing category-mapping machinery (or wizard checkboxes to merge several calendars into the datebook); (b) same multi-collection story applies to address books → contact categories.

### Previously deferred failures — RESOLVED at v0.66 (2026-06-06)

The three deferred failures (`tst_palm_runtime_route_first_sync`, `tst_palm_runtime_route_recategorization`, `tst_runtime_carddav_e2e`) are all green as of the v0.66 pin bump. libkalburator landed the engine-side fix same-day (their `16afeb0`, tagged `v0.66`) in response to the WP RFC.

Triage trail (kept for future reference):
- WP-side analysis: `docs/2026-06-04-v0.63-pin-bump-test-regressions.md`.
- Handoff RFC: `docs/2026-06-06-libkalburator-dispatchsync-backendbyid-regression.md`.
- Library response: `~/dev/libkalburator/docs/2026-06-06-dispatchsync-backendbyid-response.md`.

`PalmSyncHost::backendById`'s `dynamic_cast<SyncBackend*>` is the type-correct impl and stays as-is; the engine no longer calls it on the dispatch path. Regression test added library-side at `tests/blob/tst_engine_baseonly_backend.cpp`.

---

## What just landed: accounts-first wizard

The New Profile wizard now uses an **accounts-first flow** (Name → Accounts → Bindings → Review). Page 2 creates accounts via `AddAccountDialog` and immediately `connect()`s each provider so collections are discovered once per account. Page 3's per-conduit dropdowns list only real `(account, collection)` pairs filtered by domain — no sentinel items, no dropdown self-mutation.

`KALBURATOR_HAVE_AKONADI` now defaults **ON**; Akonadi appears in the Add Account dialog without extra build flags.

**Deleted:** `AddAccountsPage`, `DiscoveryPage`, `DiscoveryRow`, `PendingAccount`, and the `__add_new__:` sentinel machinery.

**Key references:**
- Spec: `docs/superpowers/specs/2026-06-09-accounts-first-wizard-design.md`
- Plan: `docs/superpowers/plans/2026-06-09-accounts-first-wizard.md`

---

## Previous landing: clobber-sync feature (kept for reference)

A "Clobber Palm from PC" **Sync**-menu mode (between "Copy Palm → PC" and the Backup separator) that wipes selected Palm-side PIM databases and re-pushes from the hub in one operation. Triggered from `KF6MainWindow::onClobberPalmFromPC` → `ClobberDialog` → `PalmRuntime::clobberSync`.

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
| Phase B consistency (contacts/memo/todos on `wipePalmDatabase`) | **Done 2026-06-06** | `24b0fa5` |

Clobber-sync Task 12 (hardware verification) remains pending — gated on a real Palm device.

---

## Build + test

```bash
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.66
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

## Roadmap — what to work on next

Accounts-first wizard is **done** (landed this session). Hardware verification of clobber-sync (Plan Task 12) remains gated on a real Palm. Everything else below ships without hardware. Items roughly ordered by combined urgency / preparedness; user picks.

### 1. ~~FilteredCollectionBackend RFC~~ — CLOSED (shipped lib v0.59; doc updated 2026-06-11)

The RFC was **accepted and shipped in libkalburator v0.59** as
`Kalburator::Sinks::FilteredCollectionBackend` + `Kalburator::Shape::RecordFilter`;
WP consumes the **library** class directly (no WP-local hand-rolled FCB — that prior
note was stale), pinned `v0.69`. `buildRouteLogicalCalendars` builds one FCB per
category route (`wp-route-<id>`, `RecordFilter{categories, Contains, <name>}`);
substrate A's names-first `translateRouteSpec` feeds the parsed category name as the
filter value. The handoff doc
(`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md`) now carries a
**Resolution (CLOSED)** section recording the shipped API + delta, and was committed
(no longer an unstaged working-tree file).

### 2. Hub↔remote-only sync gap (item D in prior sessions)

**State:** scoped only conceptually — no spec, no plan.

**The bug:** today a user editing a record in the WildPalms UI cannot propagate that edit to a cloud spoke (CalDAV/CardDAV) without connecting a Palm — `hotSync` and `fullSync` both require a connected Palm. The three-tier-sync architecture promises "hub buffers Palm edits and propagates to remotes when reachable"; the propagation half is wired only through hot/full-sync today.

**Next move:** brainstorm → spec → plan flow. Could parallel the clobber-sync flow (RFC for any lib-side primitives needed, then WP-side wiring).

### 3. `dlp_DeleteDB` + `dlp_CreateDB` hardware fast path (item E in prior sessions)

**State:** small implementation task, ships without device validation, validates alongside Task 12 whenever the user is back at a Palm.

**What it does:** wire real `dlp_DeleteDB` / `dlp_CreateDB` calls (creator IDs `date`/`addr`/`memo`/`todo`, type `DATA`) into `KPilotLink` / `KPilotDeviceLink`'s `IPalmDatabaseAccess::deleteDatabase` and `createDatabase` overrides. Currently the pilot-link concrete impl falls back to the loop-delete default. Real-device clobber goes from O(N) round-trips to O(1).

### 4. Resolved this session (kept for reference, no work to do)

- ~~Accounts-first wizard~~ — done (2026-06-09). `AddAccountsPage`/`DiscoveryPage`/`DiscoveryRow`/`PendingAccount`/`__add_new__:` sentinel all deleted; accounts-first flow live; `KALBURATOR_HAVE_AKONADI` defaults ON.
- ~~Phase B consistency~~ — done at `24b0fa5`. All four conduits on `wipePalmDatabase`.
- ~~v0.63 deferred test triage~~ — done at `da91e46`; libkalburator landed the fix at `v0.66`; WP pin-bumped at `d7a3a0d`; ctest 120/120.
- ~~Clobber menu wire-in~~ — done at `59eac17`; "Sync → Clobber Palm from PC" now visible.

### 5. FYI — small follow-ups from the libkalburator v0.66 response

Not blocking anything, but worth tracking:

- `tst_palm_mass_delete_guard_e2e` has a pre-existing nondeterministic heap-teardown abort (~5/15 isolated-run failures, identical on pre-Plan-6 and Plan-6 libkalburator — so it's a WP-side double-free in fixture/runtime teardown, not a library regression). Worth a debug pass with the systematic-debugging skill someday.
- `tests/runtime/CMakeLists.txt` hardcodes `${CMAKE_SOURCE_DIR}/../libkalburator/tests/sync/fakecaldavserver.cpp` instead of resolving through `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`. Breaks non-flat-sibling clones. Trivial CMake fix.

---

## Open handoff/RFC docs worth tracking

These either need a libkalburator response or sit on the WP-edit pile:

| Doc | Direction | Status |
|---|---|---|
| `2026-06-10-plan8-consumer-wave-response-wildpalms.md` | WP → lib | **WP wave COMPLETE**; lib step 3 (runSyncFuture deletion) unblocked from WP's side |
| `2026-06-09-libkalburator-collectioninfo-contenttypes-handoff.md` | WP → lib | **CLOSED** — shipped lib v0.67 (`2026-06-10-v067-response.md`); WP pinned at v0.69 |
| `2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` | WP → lib | **CLOSED** — shipped lib v0.59 (`FilteredCollectionBackend`/`RecordFilter`); Resolution section added + committed 2026-06-11 |
| `2026-05-27-libkalburator-topology-authority-proposal.md` | WP → lib | Open RFC; the hub editability authority/demotion question. No response yet from lib AFAICT. |
| `2026-05-26-calendar-writer-palmwire-parse-handoff-libkalburator.md` | WP → lib | Labeled "Blocker for CalDAV→Palm calendar sync"; status not re-verified this session |

The DAV-config-integration handoff (`2026-05-26-dav-config-integration-handoff-from-libkalburator.md`) is closed — `AccountFormWidget` already bridges `IProviderConfigWidget` (verified `src/app/accounts/accountformwidget.cpp:117-119, 157-159`).

---

## Long-running unstaged file — RESOLVED 2026-06-11

The FilteredCollectionBackend proposal doc's long-running WIP edits were finished
(Resolution/CLOSED section added) and **committed** — there is no longer a
deliberately-uncommitted working-tree doc to step around.
