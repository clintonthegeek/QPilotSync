# WildPalms — agent handoff

Cross-repo Qt6/KF6 desktop app for syncing Palm OS PIM data with an Akonadi/cloud-backed hub. Sync engine is libkalburator (sibling repo); per-conduit logic lives in four GitHub-hosted submodules under `src/plugins/{calendar,contacts,memo,todos}/`.

For deeper history check `~/dev/CLAUDE.md` (the global dev-root instructions) and `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` (auto-memory).

---

## Current branch and state (as of 2026-06-14)

**Branch:** local `main` at `007f4a7` — **pushed to `origin/main`** (GitHub) 2026-06-14.
`main` is the ONLY working branch; both the multi-hop feature and the POSE64 e2e harness were
fast-forward merged from short-lived branches and deleted, per the project's linear-main
convention. (Push happens at user request; everything through `007f4a7` is pushed.)
**libkalburator pin:** tag **`v0.77`** (`CMakeLists.txt`; commit `5d225d8` on main). Verified
superset of the prior `493bd80` Akonadi-fix SHA: Akonadi scoped-backend read fix ✅, contacts
id-prefix fix ✅ (shared `akonadiCollectionId` scheme), hub `ChangeDetection` ✅ (the piece
that activates the multi-hop skip path). Re-pin only forward (newer tags).
**Build dir convention:** legacy `build/` (no `CMakePresets.json`). Stray dirs `build-dev/`, `build-c/`, `build-fetchcontent/`, `build-appimage/` may exist on disk from prior experiments; ignore unless cleaning house.
**ctest:** **130/130 pass** (126 prior baseline + 3 always-on `tests/device-e2e/` unit tests +
1 skip-gated integration test). The device-e2e integration test runs **GREEN against a POSE64
emulator** via `ctest -L device-e2e` with `WILDPALMS_POSE64_BIN` + `WILDPALMS_PALM_BASELINE_PSF` set.
**Stray branches** (pre-existing, not ours): `task8-three-tier-sync`, two `worktree-agent-*`.

### POSE64 e2e HotSync harness (Phase 1) — LANDED 2026-06-14 (merged to `main`, `007f4a7`, pushed)

`tests/device-e2e/` drives a real HotSync against a headless POSE64 emulator over
its pty/DLP link and asserts calendar fidelity via an independent pilot-link decoder
— the first test surface to exercise WP's real DLP wire (all other tests mock the
device). Opt-in: `ctest -L device-e2e` with `WILDPALMS_POSE64_BIN` +
`WILDPALMS_PALM_BASELINE_PSF`; skips otherwise (plain ctest unchanged, 130/130 + 3
new always-on unit tests). First scenario (GREEN on hardware-emulator): hub→Palm
calendar, clean first HotSync — a canon event seeded into the hub lands on the Palm's
DatebookDB with description/note/start/end/category verified by export+decode. No
src/ change (single-element device-path list skips WP's probe → pi_bind direct).
Runbook: `docs/device-e2e-harness.md`. Spec/plan:
`docs/superpowers/{specs,plans}/2026-06-14-pose64-e2e-hotsync-harness*`.
**Two real issues surfaced (follow-ups, unfixed):** (1) contacts conduit write-back
fails against a baseline with pre-seeded AddressDB ("Write to contacts failed");
(2) canon→Palm calendar alarm transcode is lossy (`warnings: QList("alarms")`).
Next phases: the full fidelity matrix (Phase 2) and three-tier remote leg (Phase 3) — see
Roadmap → "POSE64 e2e harness — Phase 2 (fidelity matrix) + Phase 3 (three-tier)" below.

### Transparent multi-hop bidirectional sync — LANDED on main 2026-06-14

One HotSync/FullSync now propagates changes across BOTH hops of the `Palm — Hub — Remote`
star in a single user action (fixes the symptom where Akonadi events reached `hub.db` but not
the Palm without a second HotSync). 13-commit feature (Tasks 1–13 of the plan), subagent-
driven with two-stage review per task. Spec/plan:
`docs/superpowers/{specs,plans}/2026-06-14-multi-hop-bidirectional-sync*`.

- **How:** Palm device exposes `databaseRevision` (DBInfo modnum via `dlp_FindDBInfo`); the
  four Palm conduit backends implement `Sync::ChangeDetection` via a shared
  `WildPalms::PalmSync::PalmChangeDetection` mixin over a per-profile `PalmRevisionStore`
  (`<profile>/.state/palm-revisions.ini`, injected in `finishConnect`);
  `PalmRuntime::runAllMappings(maxPasses, skipUnchanged)` runs a **fixpoint loop**
  (HotSync 3 passes + skip ON; FullSync 2 passes + skip OFF), holding the device across
  passes, terminating via the pure helper `shouldContinueSync`. Clobber/Mirror unchanged.
- **Goal 1 (propagation) WORKS;** loop terminates via `SyncStats::hasChanges()`, independent
  of skip.
- **Goal 2 (cheap repeat passes) is now LIVE** as of the **v0.77** pin (2026-06-14). The
  engine skips only when BOTH mapping sides implement `ChangeDetection`; v0.77 added it to the
  hub (`GenericSqliteBackend` SHA-256 content digest + `_collection_revisions` table) and
  `FilteredCollectionBackend`, so WP's already-shipped `setSkipUnchangedMappings(true)`
  activated on the pin bump (no WP source change). RFC **CLOSED**:
  `docs/2026-06-14-libkalburator-hub-changedetection-for-skip-handoff.md`. Lib uses a content
  digest (not a write counter — a counter never settles under the engine's TwoWay re-writes);
  a settled hub skips on pass 1 of the next session, a real change costs one confirming pass
  (within cap-3). On-device verify: settled HotSync passes log `skipping unchanged mapping …`.
- **Submodule commits — PUSHED + gitlinks bumped** (`e6f0e0d`): the four conduit backends
  gained the mixin on their `feature/canon-adoption-phase1` branches — calendar `ad92ce5`,
  contacts `c681fcc`, memo `3c2ca00`, todos `3893223` (all on GitHub).
- **ONLY PENDING = on-device smoke test** (gated on a real Palm; everything code-side is done
  and merged): fresh profile, populated Akonadi calendar → datebook, **one** HotSync should
  land events on the Palm (previously needed two) AND later fixpoint passes should log
  `SyncEngine: skipping unchanged mapping …` instead of full `DatebookDB` reads (Goal-2 / RFC
  criterion 3). Also worth re-binding contacts to the real address book (Akonadi coll 184) to
  confirm the v0.77 contacts id-prefix fix. Then this feature is fully verified.

### Akonadi scoped-backend read fix — consumed 2026-06-12 (pin bump v0.69 → SHA `493bd80`)

The 2026-06-12 handoff (Akonadi sync transfers 0 records) was verified against the v0.69
tree, amended (Fix B rescoped: the engine must discriminate the base-class "fetchItems not
implemented" default — which loadRecords-only backends like the hub's
`FilteredCollectionBackend` and WP's palm plugins rely on — from a genuine fetch failure;
a blanket "Failed ⇒ fail mapping" would have broken every palm↔hub leg), and **fixed
same-day by libkalburator** at `493bd80` (lib suite 149/149): lazy
`ensureScopedCollection` seeding for both Akonadi backends + a new
`SyncOperation::NotSupported` terminal state driving the engine's fetch gate. WP pinned
to the SHA; builds clean, ctest 125/125. Full trail: the handoff doc's §Resolution and
the lib's `docs/2026-06-12-akonadi-scoped-backend-fix-response.md`.

**Watch item (lib-flagged known gap):** the first-sync fast path (`dispatchFirstSync`,
OneWayUpload + quick-path) is NOT fetch-gated — a cache-backed source like Akonadi reads
0 records there and genuine fetch failures stay silent. WP's Akonadi routes are TwoWay
today, so unaffected; if WP ever runs an Akonadi route as OneWayUpload, the lib follow-up
(gate `dispatchFirstSync`) becomes load-bearing first.

**On-device test 2026-06-14 — calendar read CONFIRMED working; two follow-ups found.**
New profile, populated Akonadi calendar → datebook, real address book (coll 184) →
contacts; Clobber then HotSync. Result: `hub.db` got **83 calendar events** (akonadi-54
read works ✓) but **0 contacts**, and **nothing reached the Palm**. Two distinct issues,
both diagnosed (systematic-debugging, evidence in hand):

1. **Mapping execution order (WP-side, = roadmap item #2 "hub↔remote-only sync gap").**
   `PalmRuntime::runAllMappings` dispatches mappings in `m_mappings` order — palm↔hub legs
   FIRST, then hub↔remote routes. So in one HotSync the hub→palm calendar leg runs while
   the hub is still empty (no-op), THEN the akonadi→hub route fills the hub (83 events) too
   late. Proof: 83 events sit in `hub.db`, 0 on Palm. **A second HotSync pushes them to the
   Palm** (the hub→palm leg then sees the 83). Reordering routes-first only flips the
   failure to the outbound (palm→remote) direction — the real fix is an inbound-then-
   outbound sweep / fixpoint, which needs the brainstorm→spec→plan flow (NOT a quick
   reorder). Engine honors request order (no internal topo-sort); WP owns the ordering.
2. **Contacts id-prefix mismatch (lib bug in the just-landed fix).** `AkonadiProvider`
   emits `"akonadi-<id>"` for ALL domains (`akonadiprovider.cpp:137`), but
   `AkonadiContactsBackend` parses `"akonadi-contacts-<id>"`
   (`akonadicontactsbackend.cpp:29,133-139`) → `ensureScopedCollection` can't resolve
   `"akonadi-184"` → contacts `fetchItems` fast-fails → 0 records. Calendar is immune (its
   prefix matches). The lib's regression test passed only because it used the backend's
   self-invented `"akonadi-contacts-1"` instead of the provider's real scheme. Fix B works
   correctly here (caught the genuine failure → no "completed" log line). **Handoff written:
   `docs/2026-06-14-libkalburator-akonadi-contacts-id-prefix-mismatch-handoff.md`** (prefer
   aligning the contacts backend prefix to `"akonadi-"`, no WP migration).

**Immediate user action:** run HotSync once more — the 83 calendar events already in the
hub should appear on the Palm (confirms the ordering diagnosis). Contacts stays empty until
the lib prefix fix lands.

### Sync Patchbay — Part 1 (Phases 0–1) — LANDED 2026-06-11

Executed `docs/superpowers/plans/2026-06-11-sync-patchbay-part1.md` in full (16 tasks).
Spec: `docs/superpowers/specs/2026-06-11-sync-patchbay-design.md`. The three-tier
**Palm | Hub | Remotes** mapping editor/monitor is now a central KPageWidget page
("Patchbay", default-visible after profile load).

- **New dependency: Graffodil** (`~/dev/Graffodil`, sibling-override + FetchContent,
  pinned **`v0.2.0`**; `WILDPALMS_GRAFFODIL_SOURCE_DIR` / `WILDPALMS_GRAFFODIL_GIT_TAG`).
  Phase 0 landed two Graffodil features upstream (WP drove them, Graffodil's own suite
  16/16 gates): edge midpoint labels (`EdgeLabelStyle` + `GraphEdgeItem::setLabel`,
  Phase 6c) and a consumer-steppable dash offset (`setDashOffset`), plus
  `PROJECT_IS_TOP_LEVEL` demo/test guards. Tag `v0.2.0` pushed to Codeberg.
- **`src/app/patchbay/`** (new static lib `WildPalmsAppPatchbay`, same synctypes.h
  isolation as AppMapping): `PatchbayModel` (pure data — Profile rows + `routeStatuses()`
  + provider state → nodes/ports/wires/strands; 17 unit tests, no graphics),
  `PatchNodeItem` (one generic `IGraphNode` renderer for palm/hub/remote/ghost),
  `SignalPathWire` (domain-colored wires + read-only strands + chevrons + ✗ beads),
  `SyncPatchbayView` (Graffodil scene, three-column manual layout, drag-to-connect both
  directions, Delete-key removal, inline "+ category…" editor, context-menu category
  removal; 7 view tests), `PatchbayInspector` + `PatchbayPage` (write-through to Profile,
  rebuild on account/runtime signals, `RouteStatus` status story).
- **Edit parity with F.3:** drag port→port creates a validated row; selecting a wire
  edits mode/policy/enabled; category lifecycle via the hub band. **Persisted rows
  unchanged — no migration; F.3 Settings graph page still works in parallel** (retired in
  Part 2).
- **Teardown:** `KF6MainWindow::destroyPatchbayPage()` runs at the top of
  `loadProfile()`/`closeProfile()` and in `~KF6MainWindow` so the page (which borrows
  profile/runtime/accounts) dies before them (the `1be66a3` lesson).

**Pending (Part 2, do NOT do yet):** live run animation (dash ticker), run-result beads,
read-only-during-sync guard, retire `src/app/mapping/` + Settings page +
`tst_syncmappingsgraphview`, dashboard → summary strip, hub record-count/baseline footers,
"Add account…" ghost node, wire context-menu delete, patchbay-first page ordering.
**User smoke test pending:** launch the app, load a wizard profile, confirm the Patchbay
page renders 4 hub bands + palm↔hub strands + account wires, and that dragging a hub port
to a collection creates a row.

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
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= \
      -DWILDPALMS_LIBKALBURATOR_GIT_TAG=493bd804a549e161718986065848f0af301b5667
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

### NEXT (most prepared, highest-leverage) — POSE64 e2e harness: Phase 2 (fidelity matrix) + Phase 3 (three-tier)

Phase 1 (LANDED, see above) is the walking skeleton: one scenario (hub→Palm calendar, clean
first HotSync), real pty/DLP wire, independent pilot-link oracle. It established the
**seed → run → export → decode → assert** pattern and the two-process orchestration. Phases 2–3
scale that pattern. This is the vehicle that finally retires the long "hardware-pending /
user-smoke-test-pending" backlog scattered through this file (clobber Task 12, category-reconciler
first live `writeAppBlock`, multi-hop skip-unchanged on-device log, contacts id-prefix re-test).

**Phase 2 — the fidelity matrix.** Parametrize the Phase-1 oracle over a scenario table and assert
record-level fidelity in every cell. Grow `tests/device-e2e/` incrementally (one scenario family
per commit); spin a full spec→plan only for a family that's genuinely large.

- **Conduits (4):** calendar (`DatebookDB`), contacts (`AddressDB`), memo (`MemoDB`), todos (`ToDoDB`).
  Each needs a `buildCanon<Domain>Event`-style seed helper (sibling to `canonseed`) and a
  pilot-link decoder (`unpack_Address` / `unpack_ToDo` / `unpack_Memo`, sibling to the
  appointment decoder — all independent of WP's encoder).
- **Sync modes (5):** `hotSync`, `fullSync`, `copyPalmToPC` (mirror PC←Palm), `copyPCToPalm`,
  `clobberSync`.
- **Edit / direction patterns (per conduit × mode):**
  1. **seed-on-hub → assert on Palm** (Phase-1 shape; inbound leg).
  2. **seed-on-Palm** (ReControl `install` a populated DB) **→ assert in `hub.db`** (outbound read leg).
  3. **both-edited same record →** conflict policy (AskUser / last-writer) behaves; assert resolution.
  4. **delete-on-one-side →** tombstone propagates; the mass-delete guard fires above threshold.
  5. **recategorize →** named-category routing survives (exercises the AppInfo reconciler +
     first live `writeAppBlock` — folds in that hardware gap).
  6. **unchanged second pass →** assert the skip-unchanged log (`skipping unchanged mapping …`),
     i.e. multi-hop Goal-2 / v0.77 `ChangeDetection` verified on real hardware.
  7. **field coverage per domain →** every field that should survive the Palm wire round-trips
     (calendar recurrence/exceptions/alarm; contacts phone/address/IM fields; todo
     priority/due/completed; memo body + category).
- **Oracle depth:** extend `DecodedAppointment` (and the new sibling structs) to the fields each
  pattern asserts. Keep decoders pilot-link-based.
- **Harness ergonomics:** make the integration test data-driven (`QTest::addColumn`/`addRow`) so
  each matrix cell is a named row. Extend `tests/device-e2e/scripts/make-baseline.sh` +
  `mkdatebook.c` to also bake empty `AddressDB`/`MemoDB`/`ToDoDB` (and, for the recategorize
  pattern, a category AppInfo block) into the baseline `.psf`.

**Phase 3 — three-tier (Remote↔Hub↔Palm).** Add a remote tier to the harness
(`FakeCalDavServer`/`FakeCardDavServer` from libkalburator's tests, or a `LocalFolderContribution`)
and assert ONE HotSync propagates across BOTH hops with fidelity at each tier — the on-hardware
verification of the multi-hop feature. Also the natural home for verifying the device-less
hub↔remote path once Roadmap item "Hub↔remote-only sync gap" lands.

**Two real bugs the Phase-1 harness already surfaced** (fix candidates; reproduce via the matrix):
- **Contacts conduit write-back fails** ("Write to contacts failed") when syncing against a
  baseline whose `AddressDB` has pre-seeded records → reproduce with Phase-2 *contacts ×
  seed-on-Palm*, then debug (systematic-debugging); likely the contacts update/merge path or the
  `AddressDB` record codec.
- **canon→Palm calendar alarm transcode is lossy** — a seeded alarm doesn't reach the device
  (engine logs `onWorkerTranscodingWarning warnings: QList("alarms")`). libkalburator-side
  (canon→ical→palm alarm stage); write a handoff doc once the matrix pins the exact loss.

References: spec/plan `docs/superpowers/{specs,plans}/2026-06-14-pose64-e2e-hotsync-harness*`;
runbook + baseline tooling `docs/device-e2e-harness.md`, `tests/device-e2e/scripts/`.

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

### 2. Hub↔remote-only sync gap (item D in prior sessions) — PARTIALLY closed

**The multi-hop fixpoint feature (landed 2026-06-14) closed the FIRST facet:** when a Palm
*is* connected, one HotSync/FullSync now propagates across BOTH hops in either direction
(Remote→Hub→Palm and Palm→Hub→Remote), instead of needing a second sync. The mapping-ordering
limitation is gone.

**STILL OPEN — the literal original concern:** propagating a hub/UI edit to a cloud spoke
**without** a connected Palm. `hotSync`/`fullSync` still require a connected device, so a
purely hub↔remote sync (no Palm present) is not yet possible. The three-tier architecture
promises "hub buffers edits and propagates to remotes when reachable"; that no-device path is
still unwired.

**Next move:** brainstorm → spec → plan for a device-less hub↔remote sync entry point
(reuse `runAllMappings`'s fixpoint loop, but gate the palm↔hub legs off when no device is
connected and run only the hub↔remote routes).

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
| `2026-06-14-libkalburator-hub-changedetection-for-skip-handoff.md` | WP → lib | **CLOSED** — shipped lib **v0.77** (`GenericSqliteBackend` content digest + `FilteredCollectionBackend` parent-derived revision). WP re-pinned v0.77; skip-unchanged now live, ctest 126/126. On-device skip-log verify pending. See doc §Resolution. |
| `2026-06-14-libkalburator-akonadi-contacts-id-prefix-mismatch-handoff.md` | WP → lib | **CLOSED** — fixed in **v0.77** via a shared `akonadiCollectionId` scheme (provider `…ToString` + backend `…FromString` agree); cleaner than the proposed one-liner. Contacts Akonadi reads should now resolve (on-device re-test pending). |
| `2026-06-12-libkalburator-akonadi-scoped-backend-read-handoff.md` | WP → lib | **CLOSED** — `493bd80` Akonadi fix merged to main and is in the **v0.77** tag; WP pinned v0.77. Calendar read on-device CONFIRMED earlier (83 events → hub). See doc §Resolution. |
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
