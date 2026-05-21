# engine-merger 2026 — archived campaign artifacts

This folder preserves WildPalms-relevant artifacts from the
multi-repo `refactor/engine-merger` campaign (April–May 2026) that
unified WildPalms, PlanStan, and libkalburator on a common sync engine
and provider/backend data model.

## Provenance

- **Source:** `~/dev/refactor-engine-merger/` (campaign coordination
  worktree, three nested submodule worktrees).
- **Coordination worktree HEAD at extraction:** `dd6c06a` on `master`
  ("docs: Phase 3b complete — FetchContent cutover landed").
- **WildPalms refactor worktree HEAD at extraction:** `ed74132` on
  `refactor/engine-merger` ("lib: make pilot-link integration fall back
  to system package").
- **Extracted into WildPalms `main`:** 2026-05-21, post-merge cleanup.

The campaign coordination folder is no longer load-bearing; this
archive is the WildPalms-side record of what happened during the
campaign and why the current code looks the way it does. PlanStan and
libkalburator hold parallel records on their own sides.

## What's here

### Phase plans (WildPalms-focused subset)

The campaign produced ~50 phase design+plan documents across all three
repos. The subset preserved here is the one whose primary subject is
WildPalms code or whose changes landed in WildPalms.

- **Phase M5/M6 (Palm Runtime rewrite, 2026-05-01 → 2026-05-02):**
  `2026-05-01-palm-runtime-rewrite-design.md` and the seven
  `2026-05-01..02-palm-runtime-rewrite-plan-*` files. Plan-1 covers
  libkalburator-side surface required by the rewrite; plan-2 is the
  WildPalms MVP; plans 3a/3b/3c are the M5 conflict UI / mapping
  editor / views; plans 4/5 are M6a/M6b cleanup + runtime-owns-link.
- **Phase Ic — WildPalms accounts UX (2026-05-09):**
  `2026-05-09-phase-ic-task1-audit.md`,
  `2026-05-09-phase-ic-wildpalms-accounts-ux-design.md`,
  `2026-05-09-phase-ic-wildpalms-accounts-ux-plan.md`. Introduced
  `AccountController`, the Accounts settings page, `AddAccountDialog`,
  and the post-connect mapping prompt.
- **Phase J — WildPalms calendar+contacts via providers (2026-05-09):**
  `2026-05-09-phase-j-wildpalms-providers-{design,plan}.md`. First
  end-to-end runtime Palm↔DAV sync through the provider/backend stack.
- **Phase L Task 0 — multi-device cleanup (2026-05-15):**
  `2026-05-15-phase-l-multidevice-cleanup-design.md` and
  `2026-05-15-phase-l-task0-multidevice-cleanup-plan.md`. Folded into
  Phase L (Akonadi) as a pre-task block. Removed the
  multi-device-per-profile aspiration; Profile↔DeviceFingerprint is 1:1.
- **Phase M — multi-protocol DAV (2026-05-16):**
  `2026-05-16-phase-m-multi-protocol-dav-{design,plan}.md`. The
  WP-visible commits from this phase are `M.13` and `M.14` in
  `git log main` (AddAccountDialog friendly label; AccountsPage hosts
  library AccountsListWidget).
- **Merge plan (2026-05-21):** `2026-05-21-merge-plan.md`. Post-mortem
  of how the three repos came back together — the divergence audit, the
  per-file playbook, and the merge order that actually shipped.

### FINDINGS.md (full)

Verbatim copy of the campaign's running findings log — every gotcha,
race, build wart, and architectural surprise discovered across all
three repos over six weeks. The full file is preserved because many
entries cross-cut all three repos, and editing it would lose context.

WildPalms-specific entries are indexed below by topic. Line numbers
are in this archived copy; the original lived at
`~/dev/refactor-engine-merger/FINDINGS.md`.

## FINDINGS.md — WildPalms-relevant index

### Test-process / build infrastructure
- L3766 — `corrupted double-linked list` exit crash family; origin of
  the `WILDPALMS_QTEST_MAIN` / `WILDPALMS_QTEST_GUILESS_MAIN` macros.
- L3800 — `kSyncTimeoutMs` bumped 5s → 30s upstream (cross-cut).
- L1661 — `verify-all.sh` flags deleted tests as a regression.
- L4142 (F-M7) — WildPalms `.clangd` pointed at stale `build/`; updated
  to `build-dev/`.
- L4204 (F-M9) — adding QTest slots to an existing CTest binary does
  not increase the CTest target count.
- L4287 (F-M12) — stale baselines before M.5; `.last` file committed
  instead of the actual baseline.
- L4569 — refactor branch tracked broken self-symlinks for pilot-link.

### Plugin ABI / conduit family
- L1494 — WildPalms has two parallel `ConflictHandler` hierarchies (the
  direct precursor to the 2026-05-21 conflict-handler port refactor).
- L1578 — `WildPalmsAppConflict` required a separate static lib for
  AUTOMOC isolation.
- L1603 — V1 `BackendPluginManager` + V2 plugins: per-plugin page
  wiring is dead code post-M4.
- L1636 — Plucker plugin `.so` process-exit double-free (M6a fix).
- L3541 — stock-plugin `DomainDefinitions` not registered in WildPalms
  main app (hidden by a silent-error swallow).

### Palm runtime / pilot-link / DLP
- L1351 — 2026-05-01: `HotSyncCoordinator`'s Palm path latently broken
  (threading).
- L1378 — M3b: backup/restore semantics are raw `.pdb` device dump,
  NOT record-level.
- L1416 — `TickleWorker` races with every multi-second DLP operation.
- L1456 — M3 real-device: "Net Prefs" restore failure is expected.
- L1463 — M3/M4 real-device: V1 `BackendPluginManager` warnings are
  harmless post-M4.
- L1695 — `KPilotDeviceLink::connectionEstablished/connectionFailed`
  are inner-class signals (not on the link itself).
- L1725 — `Profile::ConnectionMode` is post-sync policy, not transport
  type.
- L3659 — engine treats backend read failures after disconnect as
  "source is empty" (same family as the 2026-05-21 `dlpErrNotFound` fix
  in `KPilotDeviceLink::readAllRecords`).
- L3811 — `loadRecordsOrError` infrastructure existed since Phase Ib.5.
- L3837 — `IPalmDatabaseAccess` does not expose `isConnected()`.

### Sync engine behavior
- L36 — `SyncEngine::runSync(mappingId)` is leaky (resolved).
- L88 — conflict signals require `ConflictResolution::AskUser` policy.
- L520 — `fetchRecordsViaBlob` must use `loadRecords`, not
  `modifiedSince`.
- L558 — `dispatchFirstSync` guard: only `BlobSyncEngine` when target
  is empty.
- L3614 — `AutoSyncOrchestrator` silently overrode user-opened profile.
- L3714 — Contacts/0 records written twice to mirror.
- L3739 — top-level profile dirs (`calendar/`, `contacts/`, `memos/`,
  `todos/`) are unused.

### Test flakes (WP-side)
- L425 — `tst_pluckerbackendplugin` order-dependent destructor flake.
- L585 — second order-dependent flake: `tst_calendar_v2`.
- L1473 — M4 `_v2` integration tests gated via inner guards.
- L3517 — `tst_accounts_page` flakes under verify-all parallel ctest.

### Accounts / providers UI (Phase Ic onward)
- L4087 (F-M4) — `ProviderConfigDialog::rebuildProviderWidget()` provider
  creation is stubbed.
- L4119 (F-M6) — `AccountsPage` was defined but not instantiated by any
  caller.
- L4227 (F-M10) — `FancyTabWidget::SetCurrentIndex` had `Q_ASSERT`
  preventing headless `MainWindow` construction.
- L4259 (F-M11) — KXmlGui `.rc` file lookup requires XDG data-path setup
  for File-menu tests.

### CalDAV/CardDAV transport (cross-cut; affects WP through Phase Ic/J)
- L1819 — pre-existing: CalDAV `davUrl` strings persist plaintext
  credentials in user config.
- L2088 — `CalDavProvider` does not pre-validate URL scheme before
  dispatching to QNAM.

### Environment / Akonadi (relevant to Phase L)
- L3857 — ECM module path required for Akonadi build on Arch/Manjaro.
- L3868 — Akonadi-gated code needs its own build directory for clangd.
- L3953 — `CompilationDatabase` must be a top-level key in `.clangd`,
  not under `CompileFlags`.

Other entries in FINDINGS.md cover PlanStan, libkalburator's internal
sync engine, the CalDAV/CardDAV transports, and Phase O's wizard /
topology canvas work in PlanStan. They are preserved verbatim in
`FINDINGS.md` for cross-reference but are not WildPalms-direct.

## What's NOT here

- PlanStan-focused phase plans (Phase H/H5, I/Ia/Ia.5/Ib/Ib.5, J-replan,
  K6, L-Akonadi, layer-b-silent-success, M5 runtime add account, N1
  per-record diff/merge, O1–O4 libkalburator-UI / provider-lifecycle /
  topology-canvas / legacy-cleanup, O7 wizard discovery, P merge
  readiness). These live with PlanStan's archive.
- libkalburator-internal documents (`docs/phase0/` deferred-work
  catalog, etc.). These live in libkalburator's repo.
- Campaign-wide coordination files (`CURRENT-STATUS.md`, `ROADMAP.md`,
  `OPERATIONS.md`, `SETUP.md`, `PREP-CHECKLIST.md`, `README.md`). These
  describe the three-repo workflow and don't belong to any single
  consumer.
- `2026-05-20-wizard-provider-architecture-journey.md` — PlanStan
  wizard architecture journey doc; lives with PlanStan.

## How to use this archive

- New contributor onboarding: read this README + the relevant phase
  plan for the area you're touching. Don't read everything.
- Investigating a specific gotcha: search `FINDINGS.md` directly, or
  use the topic index above.
- Tracing why a piece of WildPalms code looks the way it does: `git log
  --follow <file>` on `main` will point you at commits; the phase plans
  here explain the design intent behind those commits.
