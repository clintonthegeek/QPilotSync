# Phase Ic — WildPalms accounts UX — design

**Date:** 2026-05-09
**Status:** Approved (post-brainstorm; user authorized autonomous execution per memory `feedback_autonomous_phase_execution.md`).
**Scope:** WildPalms-side `AccountController` + SettingsDialog Accounts page + post-connect mapping prompt; closes deferred-work items §D.2, §D.3, §D.4.
**Predecessor:** Phase Ib — CardDAV transport (tag `v0.28-phase-ib-carddav-transport`, landed 2026-05-08); Phase Ib.5 — engine generalization (tag `v0.28.5-phase-ib.5-engine-generalization`, landed 2026-05-08).
**Successor:** Phase J — WildPalms migrates other domains (calendar/memo/todo) to providers.
**Tag:** `v0.29-phase-ic-wildpalms-contacts-ux`.

---

## 1. Why this exists

After Phase Ia (vCard 4.0 canonical) + Phase Ia.5 (engine unification) + Phase Ib (CardDAV transport) + Phase Ib.5 (engine generalization), WildPalms has every engine and transport piece it needs to sync Palm contacts to a CardDAV server end-to-end. What's missing is the UI plumbing that lets a user actually configure such a flow without editing config files.

Four concrete consequences of the missing UX:

1. **Deferred-work §D.2, §D.3, §D.4 remain open.** The deferred-work catalog at `libkalburator/docs/phase0/04w-deferred-work.md` lists three Phase-Ic items: a SettingsDialog Accounts page, ProviderManager wiring through `PalmRuntime`, and default-mapping logic. None of these is delivered today.
2. **Phase J cannot start.** Phase J ("WildPalms migrates calendar/memo/todo to providers") needs `AccountController` + provider lifecycle in place; it can't migrate domains to a provider that has no UX surface.
3. **The IProvider abstraction has only one consumer.** Phase H.5 validated `IProvider` against PlanStan, but WildPalms — the second consumer — still bypasses providers entirely. Pressure-testing the abstraction against a Palm-shaped consumer (device-coupled syncs, profile-scoped runtime, KF6MainWindow ownership) is overdue.
4. **The existing MappingEditor cannot express provider-bound mappings.** `mappingrowdialog.cpp:171` hardcodes `targetBackend = "rawfiles-cal"`; opening any non-rawfiles row and clicking OK silently rewrites the target — a latent data-loss bug that any Phase Ic output would immediately trigger. Phase Ic must extend the row dialog before its own outputs are safe.

Phase Ic closes all four with a one-tag slice that mirrors Phase H.5's shape: a single tag, a single design doc, a single status doc, plumbing + UI + tests landed together.

## 2. Scope

**WildPalms only.** The companion item §D.1 (PlanStan CardDAV add-account UI) stays a separate slice tracked in `PlanStan/docs/todo/carddav-account-ui.md`. Bundling D.1 with this phase would double the UI surface and break the "one consumer per phase" rhythm that has held since Phase H.5.

**Both provider kinds in the Accounts UX surface.** Users can add CalDAV and CardDAV accounts. The Accounts page is symmetric across kinds; provider-supplied `createConfigWidget()` calls do the kind-specific UI. This means the Accounts UX ships complete on first cut.

**Only contacts wired through to Palm sync.** Phase Ic stops at the Accounts page + mapping prompt. CalDAV-bound mappings are persisted but the Palm-side `BlobBackendAdapter` for calendar isn't extended in this phase — that work is Phase J. CardDAV-bound mappings drive Palm contacts sync end-to-end (the existing `ContactsBlobBackend` already speaks Palm contacts; Phase Ib's `RemoteContactsBackend` already speaks CardDAV; the engine's unified dispatchSync routes between them).

**Always-prompt mapping policy.** When a provider's `connect()` succeeds and surfaces N collections, a `MappingPromptDialog` opens listing each collection × Palm slot. The user binds explicitly. No auto-binding heuristic this phase. Rationale (brainstorm Q4): the alternative — auto-bind first to Unfiled — produces user-invisible bindings that are then surprising when the user opens MappingEditor later. Always-prompt is more friction up-front but eliminates surprise.

**Confirm + cascade delete on account removal.** When the user removes a provider, a confirm dialog states the exact count of mappings that will be deleted alongside the account, with a sample. On confirm, AccountController atomically deletes both the provider config and the affected mappings.

**Accounts and Mappings are orthogonal.** AccountsPage manages credentials; MappingEditor manages bindings. Provider-supplied backends and direct backends both sit in `BackendRegistry` and are interchangeable in MappingEditor's combos. `MappingPromptDialog` (after add-account) is a convenience accelerator that writes the same `SyncMapping`s MappingEditor would write. Detail in §6.0.

**MappingEditor target combo extension is in scope.** `mappingrowdialog.cpp:171` hardcodes `targetBackend = "rawfiles-cal"` — Phase Ic replaces this with a real backend picker so provider-bound and direct-bound mappings are both editable losslessly. Detail in §4.5a, §7.2.

## 3. Out of scope

- **PlanStan CardDAV add-account UI** (§D.1) — separate slice.
- **Phase J domains** — calendar, memo, todo wiring through providers. Phase Ic provisions CalDAV accounts in the Accounts page but doesn't wire CalDAV-bound mappings to Palm Datebook.
- **KWallet credential storage** (§B.4) — passwords stay plaintext in the sidecar KConfig file, matching PlanStan's Phase H.5 baseline.
- **RFC 6764 email-based auto-discovery** (§B.3) — users enter URLs manually.
- **Multi-protocol Nextcloud-style provider** (§B.5) — one provider per kind per server is the today shape.
- **Akonadi provider** (§C.1) — separate phase.
- **Real-device verification gate** (§E.1) — independent gate, runs once before merge to main.
- **Mid-sync mutation flows** — Accounts page mutations are blocked while `PalmRuntime::isRunning() == true`. The spec doesn't try to make Accounts page edits safe during an active sync; it makes them unavailable instead.
- **Undo for cascade delete** — confirm-with-sample dialog is the only safety net.
- **vCard version negotiation hardening** (§B.6) — surfaces only if real-server testing reveals dialect issues.

## 4. Audit findings (pre-design)

### 4.1 Profile / Device / PalmRuntime relationships

`Profile.h` is explicit: each profile corresponds to **one Palm device** (identified by `DeviceFingerprint`), a sync folder, and device-specific connection settings. Profile ↔ device is 1:1.

`KF6MainWindow` holds **one active profile at a time** in `m_currentProfile`. Multiple profiles exist on disk, tracked in `KF6Settings::recentProfiles`; the user switches between them via Open/New Profile menu actions.

**`PalmRuntime` is profile-scoped, recreated per profile switch.** `KF6MainWindow::loadProfile()` (`kf6mainwindow.cpp:713`):

1. Deletes `m_currentProfile`, constructs `new Profile(path)`.
2. **Reconstructs** `m_palmRuntime = std::make_unique<PalmRuntime>(profile->stateDirectoryPath(), this)` (`:754`).
3. Reinstalls conflict handler bridge.

`closeProfile()` (`:871`) tears down `m_currentProfile`. PalmRuntime's `unique_ptr` is replaced (or implicitly cleared) on the next `loadProfile()` call.

**Implication for AccountController: it is also profile-scoped.** Constructed in `loadProfile()` immediately after `m_palmRuntime`; destroyed/replaced in `closeProfile()` and on subsequent `loadProfile()` calls. Same lifetime as `m_palmRuntime` and `m_currentProfile`.

### 4.2 Profile portability and persistence shape

`Profile` settings live in `<syncFolderPath>/.wildpalms.conf` (KConfig file inside the sync folder) — explicitly so the user can move the sync folder to another machine and the settings travel. Phase Ic preserves this principle: providers persist in a sidecar at `<syncFolderPath>/.wildpalms.providers`, also inside the sync folder.

This means a user with two profiles (two Palms) syncing to the same CardDAV server enters credentials twice — once per profile. Acceptable cost for portability; matches PlanStan's H.5 sidecar pattern (`<kalb>.providers`).

### 4.3 PalmRuntime BackendRegistry accessor

`PalmRuntime` (`src/runtime/palmruntime.{h,cpp}`, 157+699 LOC) owns `m_registry` (a `BackendRegistry` `unique_ptr` at `palmruntime.h:144`). Plugin backends register into `m_registry` during `connectDevice` → `finishConnect()`. There's no current accessor exposing the registry — Phase Ic adds one.

### 4.4 WildPalms's existing settings surface

`SettingsDialog` (`src/settingsdialog.{h,cpp}`, 85+529 LOC) is a `KPageDialog` with four pages: Profiles, Devices, Sync (per-profile, only when a Profile is supplied), Advanced. The pattern for adding a new page is clear from `createSyncPage()`. The ctor takes `(QWidget *parent, Profile *profile)` — Phase Ic adds an `AccountController *` ctor arg.

Note: SettingsDialog's "Sync" page is already conditional on whether a Profile was passed (the dialog can be opened without a profile to edit app-level settings only). Phase Ic's Accounts page follows the same pattern: only shown when `m_accounts != nullptr`, which is the case when SettingsDialog is opened from the profile-loaded main window.

### 4.5 Profile mapping accessors

`Profile::syncMappingsJson()` / `setSyncMappingsJson()` (`profile.h:258-259`) round-trip the SyncMapping list as JSON. Confirmed shape — `WildPalmsSyncMappingHelper::parseMappings()` is the parser (referenced in profile.h's docstring). Cascade-delete in AccountController will read, filter, and write back through these accessors.

### 4.5a MappingEditor target-combo limitation (must fix in Phase Ic)

`mappingrowdialog.cpp:171` hardcodes the target backend:

```cpp
// RawFiles target locked per design spec §5.1; not exposed in MVP UI.
m.targetBackend = QStringLiteral("rawfiles-cal");
```

The source combo accepts any string seeded by `setSourceBackends()`, but the target combo is locked to `"rawfiles-cal"`. MappingEditor today expresses exactly one shape: **Palm → local rawfiles**. It cannot express "Palm contacts → CardDAV addressbook," and crucially, **opening a provider-bound mapping row in MappingEditor and clicking OK silently rewrites the target to `"rawfiles-cal"`** — a data-loss bug that would be triggered by Phase Ic's own outputs.

Phase Ic extends `MappingRowDialog` so both source and target are real backend pickers, both seeded from `BackendRegistry::backendIds()`. Approximately +50 LOC across `mappingrowdialog.{h,cpp}` + caller wiring in `MappingEditorDialog`. Detail in §7.2.

### 4.6 PalmRuntime::reloadMappings interlock

`palmruntime.h:79` documents `reloadMappings(const QJsonArray &)` with the contract: caller must ensure `isRunning() == false`. AccountController honors this by gating Add/Remove buttons in the Accounts page off `palmRuntime->isRunning()`, and by short-circuiting addProvider/removeProvider with a clear error if isRunning is true at call time.

### 4.7 Profile-switch teardown order

When `loadProfile()` is called while a profile is already open, the existing flow tears down the old PalmRuntime and replaces it. Phase Ic's AccountController must be torn down **before** the old PalmRuntime, so its borrowed `BackendRegistry*` doesn't dangle even momentarily during the swap. Audit Task A1.5 confirms the right teardown sequence in `loadProfile()` and `closeProfile()`.

### 4.8 Composite-id convention from Phase H §4.7

Provider-supplied backends register under composite ids `<provider-uuid>:<collectionId>`. Phase H.5 confirmed this works in PlanStan against `BackendRegistry`. Phase Ic uses the same shape — `SyncMapping`'s palm-side stays `palm:contact/<slot>`, the non-palm side is `<provider-uuid>:<collectionId>`.

Note: provider UUIDs are assigned per-profile. Two profiles syncing to the same CardDAV server hold two distinct provider UUIDs (each profile's AccountController generates its own). This keeps mappings unambiguous and removal-cascades scoped to the current profile.

### 4.9 Static-link WHOLE_ARCHIVE requirement

Per Phase Ia.5 FINDINGS, every test binary that links `libkalburator.a` must wrap with `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>` or static-init plugin registrars never reach the binary's address space. Phase Ic adds two new test binaries; both must include the wrapper. Existing examples in `tests/CMakeLists.txt` show the pattern.

## 5. End-state architecture

**Lifetime model: AccountController is profile-scoped, sibling to PalmRuntime.** Both are owned by `KF6MainWindow`, both recreated on every profile switch (`loadProfile()`), both torn down on `closeProfile()`.

```
KF6MainWindow (existing, owner — single instance per app)
    │
    ├─ m_currentProfile : Profile*                    (one active at a time)
    │
    ├─ m_palmRuntime : PalmRuntime  (recreated per loadProfile)
    │     ├─ m_registry : BackendRegistry             (unchanged)
    │     ├─ m_engine, m_syncHost, m_baselineStore    (unchanged)
    │     └─ backendRegistry() accessor               ← NEW (3 LOC)
    │
    ├─ m_accountController : AccountController  (NEW; recreated per loadProfile)
    │     ├─ m_providerManager : ProviderManager
    │     ├─ m_registry : BackendRegistry*            ← borrowed from m_palmRuntime
    │     ├─ m_profile : Profile*                     ← borrowed (cascade-delete)
    │     ├─ m_palmRuntime : PalmRuntime*             ← borrowed (isRunning check)
    │     │
    │     ├─ ctor → ProviderManager::loadFromProfile(
    │     │           <syncFolderPath>/.wildpalms.providers)
    │     │       → connectAll() (async)
    │     │       → on each provider connect, backends register into m_registry
    │     │
    │     ├─ addProvider(kind, BackendConfiguration) → providerUuid
    │     ├─ removeProvider(uuid)         (cascade: drops mappings via m_profile)
    │     ├─ providers() / collectionsFor(uuid) / stateFor(uuid)
    │     └─ signals: providersChanged(),
    │                 connectFailed(uuid, error),
    │                 connectStateChanged(uuid, state)
    │
    └─ SettingsDialog (existing, transient, opened from menu)
          │
          ├─ ctor: (QWidget*, Profile*, AccountController*)   ← +1 arg
          │
          └─ AccountsPage (NEW KPageWidget item — only shown when AC != nullptr)
                ├─ Left: list of providers (one row per provider, with state badge)
                ├─ Right: provider->createConfigWidget(this) when row selected
                ├─ Add… → AddAccountDialog (kind picker → provider config widget
                │                            → Test Connection → Save)
                ├─ Remove → confirm with mapping count + sample → cascade delete
                ├─ Per-row Retry on Disconnected/Error
                │
                └─ on AddAccountDialog success →
                      MappingPromptDialog (NEW, modal)
                        ├─ lists discovered collections for the new provider
                        ├─ for CardDAV: each row has a Palm-slot picker
                        │   (Unfiled / categories 1-15, optional)
                        ├─ for CalDAV: row shows "Bound (Phase J wires this)"
                        │   (visible-but-no-op state — accounts UX symmetry)
                        └─ Save writes new SyncMappings to Profile
```

**Construction sequence inside `KF6MainWindow::loadProfile(path)`:**

```cpp
// (existing) 1. Tear down previous profile
if (m_currentProfile) { m_currentProfile->save(); delete m_currentProfile; }

// (existing) 2. Construct new Profile
m_currentProfile = new Profile(path);
// ... validity checks ...

// (existing) 3. Construct new PalmRuntime
m_palmRuntime = std::make_unique<PalmRuntime>(
    m_currentProfile->stateDirectoryPath(), this);

// (NEW) 4. Construct new AccountController borrowing PalmRuntime's registry
m_accountController = std::make_unique<AccountController>(
    m_currentProfile->syncFolderPath(),
    &m_palmRuntime->backendRegistry(),
    m_currentProfile,
    m_palmRuntime.get(),
    this);
// AccountController's ctor invokes loadFromProfile + connectAll asynchronously.
```

**Teardown sequence in `closeProfile()` and at the start of every `loadProfile()`:**

```cpp
// (NEW, ordered first) AccountController teardown — releases borrowed BackendRegistry*
m_accountController.reset();

// (existing) PalmRuntime teardown
m_palmRuntime.reset();   // (or replaced on next ctor in loadProfile)

// (existing) Profile teardown
delete m_currentProfile; m_currentProfile = nullptr;
```

The teardown order (AC → PR → Profile) is the reverse of construction. Documented in AC's header.

**Per-profile sidecar persistence file:**

```
<syncFolderPath>/.wildpalms.providers     ← KConfig sidecar to .wildpalms.conf

[Providers]
uuids=8b2e0a1c-4f3c-..., 4f3c0a9d-...

[Providers/8b2e0a1c-4f3c-...]
kind=carddav
displayName=Personal CardDAV
url=https://dav.example.com/contacts/
username=alice
password=...                              ← plaintext; KWallet deferred (§B.4)

[Providers/4f3c0a9d-...]
kind=caldav
...
```

This sits next to `.wildpalms.conf` inside the sync folder, so the entire profile (settings + provider configs + sync data) is portable: copying the sync folder to another machine carries everything. Matches PlanStan's H.5 sidecar pattern (`<kalb>.providers`); same ProviderManager round-trip code.

**Provider UUID scoping note.** Provider UUIDs are assigned per-profile. Two profiles syncing to the same CardDAV server hold two distinct provider UUIDs. This means a user with multiple Palms enters credentials once per profile (not deduplicated), which is the trade-off chosen for portability.

## 6. Key architectural choices

### 6.0 Accounts and Mappings are orthogonal (the core mental model)

The Phase Ic UX is built around a clear separation of concerns:

```
Accounts page  =  CREDENTIALS / lifecycle  (DAV server config; connect state)
                  ↓ providers produce backends ↓
            BackendRegistry  (palm:slot, <provider-uuid>:<col>, rawfiles:*, ...)
                  ↑ picked by ↑
MappingEditor  =  BINDINGS  (which Palm slot ↔ which backend, both sides freely)
```

- **Accounts page does not bind anything to anything.** It manages credentials, connection state, and shows what collections each provider exposes (read-only). Adding/removing/testing accounts is the only thing it does.
- **MappingEditor does not know about providers.** It picks from `BackendRegistry::backendIds()`. Provider-supplied backends (`<uuid>:<col>`) and direct backends (`palm:contact/0`, `rawfiles:cal`, plugin-direct) are interchangeable in its combos.
- **The two write to the same `Profile::syncMappingsJson()`.** No "account-bound mappings" vs "direct mappings" data partition. A row is just a row.

This answers the user-flexibility scenario directly: "contacts from provider A, calendar from provider B, memo from a local file" is three independent rows in MappingEditor, each freely choosing source and target from the registry. There is no "this account binds all my domains" coupling — accounts only contribute backends; mappings are per-slot.

**MappingPromptDialog (after add-account) is a convenience accelerator, not a separate flow.** It writes the same `SyncMapping`s MappingEditor would write. Skipping it is harmless: the user can bind collections later in MappingEditor and see the new account's collections in the dropdown. Keeping it is helpful for onboarding: "I just signed up for CardDAV; let me bind one addressbook to Palm Contacts and start syncing."

### 6.1 AccountController is standalone, not part of PalmRuntime (brainstorm Q2)

Phase H.5 chose to put PlanStan's ProviderManager *inside* CollectionController because that's where `m_backendRegistry` already lived. Phase Ic chooses differently: `AccountController` is a sibling of `PalmRuntime`, not embedded in it.

Rationale:
- **SettingsDialog needs provider-list access without depending on PalmRuntime's runtime state.** The user can open Settings while the device is disconnected; AccountsPage shouldn't require PalmRuntime's full lifecycle to be active.
- **PalmRuntime is already a 156-line class with deep responsibility for device coupling.** Embedding ProviderManager would push it past the point where the next refactor naturally splits it.
- **Phase J may add a sibling that needs AccountController too** (e.g., a future MappingEditor rewrite that consults `provider->collections()` directly). A free-standing AccountController is the right factoring for that.

The trade-off is one extra borrowed pointer (`AccountController` borrows `&palmRuntime->backendRegistry()`); the construction-order discipline is documented in AC's header.

### 6.2 Accounts UX accepts both kinds; Palm-side wires only contacts (brainstorm Q3)

Tag name (`v0.29-phase-ic-wildpalms-contacts-ux`) and TODO doc opener are contacts-focused. Deferred-work §D.2 says "CalDAV + CardDAV accounts." This design picks the middle path: the AddAccountDialog kind picker shows both, the AccountsPage hosts both `CalDavConfigWidget` and `CardDavConfigWidget`, but the MappingPromptDialog gives CalDAV-bound rows a "Bound (Phase J wires this)" affordance instead of the slot picker.

Rationale: Accounts UX symmetry ships once. Phase J's only Accounts-page-side delta is making the CalDAV row's slot picker active.

### 6.3 Always-prompt mapping policy (brainstorm Q4)

When `connect()` succeeds and surfaces N collections, MappingPromptDialog opens with no pre-selected bindings. User binds explicitly.

Rationale: the alternative ("auto-bind first to Unfiled, prompt for rest") creates surprise when the user later opens MappingEditor and finds bindings they don't remember making. Always-prompt is one more click in the common case but eliminates the entire class of "where did this mapping come from" questions.

The MappingPromptDialog can be re-opened from the AccountsPage via "Bind collections..." per-account button, so users who skip binding at add-time can revisit later.

### 6.4 Confirm + cascade delete on account removal (brainstorm Q5)

Removing an account also deletes every mapping referencing `<uuid>:*` on either side. The confirm dialog states the count and shows a sample of the first few mapping descriptions. No undo this phase.

Rationale: orphan mappings (the alternative) would clutter MappingEditor with unresolvable backend ids and surface as cryptic "backend not found" errors at sync time. Cascade delete keeps the model clean. The confirm-with-sample dialog gives enough information to avoid surprise.

### 6.5 Per-profile providers, sidecar inside the sync folder (brainstorm Q6)

Provider configs live in `<syncFolderPath>/.wildpalms.providers`, a KConfig sidecar to `.wildpalms.conf` (the existing per-profile config file inside the sync folder). AccountController is profile-scoped, recreated per `loadProfile()` alongside PalmRuntime.

Rationale:
- **Profile portability is preserved.** Profile.h's stated principle is "settings travel with the sync folder." App-level provider storage would break that — moving the sync folder to a new machine would leave provider configs behind. Per-profile keeps everything in one directory.
- **Matches PlanStan's H.5 pattern.** Same ProviderManager round-trip code (`loadFromProfile` / `saveToProfile`); same composite-id mapping shape; same cascade-delete-on-remove logic. The two consumers' provider persistence is structurally identical, just with different file paths.
- **Trade-off accepted.** A user with two Palms (two profiles) syncing to the same CardDAV server enters credentials twice. App-level storage would deduplicate but at the cost of: provider configs not portable; cross-profile mapping lookups needed on remove; KWallet integration scoped globally rather than per-profile (which complicates the future B.4 work).

Provider UUIDs are scoped per-profile; two profiles' `<provider-uuid>:<colId>` ids never collide across the BackendRegistry because only one profile is active at a time.

### 6.6 m_running interlock blocks Accounts mutations during sync

`AccountsPage` consults `palmRuntime->isRunning()` and grays out Add/Remove with a tooltip ("Sync in progress."). `AccountController::addProvider` / `removeProvider` short-circuit with an error if called while running, as a belt-and-braces guard.

Rationale: `PalmRuntime::reloadMappings` (the only mechanism that picks up cascade-deleted mappings) requires `isRunning() == false`. Surfacing this as a UI affordance is honest; queuing the mutations would be a bigger feature than this phase warrants.

## 7. Phase scope (concrete)

### 7.1 libkalburator changes

**None.** All machinery exists post-Phase-Ib (`IProvider`, `ProviderManager`, `CalDavProvider`, `CardDavProvider`, `BackendRegistry`, `BackendConfiguration`, `provider->createConfigWidget()`).

### 7.2 WildPalms changes

| File | Change | Approx. delta |
|---|---|---|
| `src/runtime/palmruntime.{h,cpp}` | Add `BackendRegistry &backendRegistry()` accessor returning a reference to `*m_registry`. | +3 LOC |
| `src/runtime/accountcontroller.{h,cpp}` (NEW) | Owns `ProviderManager`. Borrows `BackendRegistry*`, `Profile*`, `PalmRuntime*`. addProvider, removeProvider, providers, collectionsFor, stateFor, loadFromProfile, saveToProfile, connectAll, signals. Cascade-delete via Profile. | +200 LOC |
| `src/kf6/kf6mainwindow.{h,cpp}` | In `loadProfile()`: construct `m_accountController` after `m_palmRuntime` (~5 LOC). In `closeProfile()` and the top of `loadProfile()`: tear down AC before PalmRuntime (~3 LOC). Pass AC to SettingsDialog (~2 LOC). | +10 LOC |
| `src/settingsdialog.{h,cpp}` | Add `AccountController *m_accounts` ctor arg + member. Add `createAccountsPage()`; new `KPageWidgetItem` between Sync and Advanced. | +20 LOC |
| `src/app/accounts/accountspage.{h,cpp}` (NEW) | KPageWidget content. Provider list (left); selected provider's `createConfigWidget()` (right); Add/Remove buttons; per-row state badge + Retry. Off-state interlock from `palmRuntime->isRunning()`. | +250 LOC |
| `src/app/accounts/addaccountdialog.{h,cpp}` (NEW) | Modal. Kind picker (CalDAV / CardDAV), provider's `createConfigWidget()`, Test Connection button, Save. | +120 LOC |
| `src/app/accounts/mappingpromptdialog.{h,cpp}` (NEW) | Modal convenience accelerator after add-account. Lists discovered collections × Palm slots. CardDAV rows have full slot picker; CalDAV rows show "Bound (Phase J wires this)". Save writes the same SyncMappings MappingEditor would write. Skip is harmless — user can bind later in MappingEditor. | +180 LOC |
| `src/app/mapping/mappingrowdialog.{h,cpp}` | Replace hardcoded `targetBackend = "rawfiles-cal"` with a real target combo. Add `setTargetBackends(QStringList)` mirroring `setSourceBackends`. Default-add skeleton stays sensible (Palm-source by default; target picks first non-Palm registry id, falling back to `"rawfiles-cal"` if registry only has Palm backends). Edit-mode preserves existing target via `findText`/`addItem` symmetric to source. | +50 LOC |
| `src/app/mapping/mappingeditordialog.{h,cpp}` | Pass full `BackendRegistry::backendIds()` to BOTH `setSourceBackends` and `setTargetBackends` on row dialog open. (Today only source is seeded.) | +10 LOC |
| `tests/runtime/tst_account_controller.cpp` (NEW) | 12 sub-tests; integration against FakeCardDavServer + fake-CalDAV-server. WHOLE_ARCHIVE wrapper. | +250 LOC |
| `tests/widgets/tst_accounts_page.cpp` (NEW) | 6 sub-tests; widget-level against stub AccountController. WHOLE_ARCHIVE wrapper. | +150 LOC |
| `tests/runtime/tst_mapping_row_dialog.cpp` (EXTENDED — already exists) | Existing test (55 LOC, 1+ sub-test) seeds `targetBackend = "rawfiles-cal"` but doesn't assert target round-trip — it'll keep passing as a regression. ADD 4 sub-tests for the target-combo extension: (a) default-add picks a sensible target from a populated registry; (b) edit-mode round-trips a non-rawfiles target without rewrite; (c) provider-bound mapping (`target=<uuid>:<col>`) round-trips losslessly; (d) target combo population matches `BackendRegistry::backendIds()`. Plus add a `QCOMPARE(out.targetBackend, in.targetBackend)` line to the existing sub-test. WHOLE_ARCHIVE already wired. | +120 LOC |
| `tests/CMakeLists.txt` | Register two new binaries with `LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync` wrapper. | +20 LOC |

**Net: ~+1200 LOC WildPalms-side, ~+3 libkalburator-side.** Comparable to Phase H.5 (~+80 LOC PlanStan-side, but H.5 extended an existing controller; Ic adds three new dialog widgets).

### 7.3 Pre-flight audit (Task A1)

One read-only audit task opens the implementation plan, confirming five assumptions before any code changes:

1. **`SettingsDialog` ctor signature.** Current is `SettingsDialog(QWidget *parent, Profile *profile)`. Audit identifies all callers (expected: just `KF6MainWindow`); plan adds `AccountController *` as a third optional ctor arg (default `nullptr` for backward-compat with any other callers; AccountsPage hidden when `m_accounts == nullptr`, mirroring how the Sync page is hidden when `m_profile == nullptr`).
2. **Profile mapping accessor shape.** §4.5 already cites `Profile::syncMappingsJson()` / `setSyncMappingsJson()` at `profile.h:258-259`. Audit confirms the JSON shape matches what `WildPalmsSyncMappingHelper::parseMappings()` expects when round-tripping with composite-id `backendId` strings.
3. **`PalmRuntime::reloadMappings` semantics.** Confirm it picks up freshly-saved mappings from Profile (not a stale cached copy) when called after `Profile::save()` and `palmRuntime->isRunning() == false`.
4. **`m_running` interlock surface.** Confirm `palmRuntime->isRunning()` is the right signal for Accounts page gating; identify whether `runStarted`/`runFinished` signals (already wired to `KF6MainWindow::onPalmRunStarted`/`onPalmRunFinished` at `kf6mainwindow.cpp:756-759`) are the right hook for live UI updates in AccountsPage.
5. **Profile-switch teardown order.** Confirm the right place in `loadProfile()` and `closeProfile()` to tear down AccountController. Specifically: `loadProfile()` currently calls `delete m_currentProfile` at the top before constructing the new one; AccountController must be reset before that line (not after), since AC borrows the old Profile and would dangle. Audit identifies the correct insertion point.
6. **Existing `rawfiles-cal` test fixtures.** Confirm that `tst_mapping_editor_dialog`, `tst_palm_runtime_reload_mappings`, and `tst_palm_runtime_default_mappings_only_when_empty` use `rawfiles-cal` only as seeded input (not as an assertion that the row dialog produces it). Existing `tst_mapping_row_dialog` does not assert `out.targetBackend`, confirmed by `grep` (its only `QCOMPARE` calls are id, source, source/target calendar, enabled, mode, conflictPolicy). If the audit surfaces a fixture that asserts the rawfiles-cal output, the plan grows a fixture-update task.

If the audit surfaces a seventh structural surprise (e.g., a non-`KF6MainWindow` caller of `SettingsDialog`, or a Profile field that breaks the cascade-delete shape), the plan adapts before code lands.

## 8. Test plan

### 8.1 libkalburator (regressions only)

- Phase H + Ib's existing test set stays green: 75/75 (post-Ib.5 baseline).
- No library tests added or modified.

### 8.2 WildPalms — `tst_account_controller` (12 sub-tests)

Drives `AccountController` against `FakeCardDavServer` (Phase Ib) + fake-CalDAV server (Phase H). Stub `Profile`, real `BackendRegistry`.

| Sub-test | Asserts |
|---|---|
| `addCardDavProvider_persists` | After `addProvider("carddav", config)`, sidecar has `Providers/<uuid>` section; uuid returned non-empty. |
| `addCardDavProvider_registersBackends` | After add+connect, `BackendRegistry::backendIds()` contains `<uuid>:<colId>` entries equal in count to fake-server addressbooks. |
| `addCalDavProvider_succeeds` | Same shape, CalDAV side. Confirms Accounts UX symmetry. |
| `removeProvider_unregistersBackends` | After remove, registry no longer contains `<uuid>:*`. |
| `removeProvider_cascadesMappings` | Seeded mappings referencing `<uuid>:*` are gone from Profile post-remove. |
| `removeProvider_preservesUnrelatedMappings` | `palm:contact/0`-only mappings and other-provider mappings survive cascade. |
| `loadFromProfile_reconnects` | Fresh AC on existing sidecar; backends re-register; collectionsFor returns expected list. |
| `loadFromProfile_handlesUnreachableServer` | Provider whose URL no longer resolves: `connectFailed` signal fires; provider stays in list with state=Disconnected. |
| `connectAll_concurrent` | Two providers in sidecar; both connect in parallel; both end registered. |
| `addProvider_duplicateUrl_allowed` | Two providers same URL, different uuid, both register. |
| `mappingsCascadeDelete_persists` | Cascade-delete persists to `Profile::save()`; reload shows mappings actually gone. |
| `bothPalmAndProviderBackendsResolve` | Mixed mapping list (`palm:contact/0` + `<uuid>:abc`); both resolve through `m_registry`. |

### 8.3 WildPalms — `tst_accounts_page` (6 sub-tests)

Widget-level against a stub `AccountController`.

| Sub-test | Asserts |
|---|---|
| `emptyState_showsAddButton` | No providers → Add enabled, list empty. |
| `addClick_opensDialog` | Click Add → AddAccountDialog opens; kind picker shows CalDAV + CardDAV. |
| `removeClick_showsConfirm` | Select provider, click Remove → confirm dialog mentions mapping count from stub AC. |
| `removeConfirm_callsAccountController` | Confirm → `AccountController::removeProvider(uuid)` called exactly once. |
| `selectProvider_showsConfigWidget` | Select provider → right pane hosts `provider->createConfigWidget()` instance. |
| `mappingPromptDialog_writesToProfile` | After fake-successful connect, MappingPromptDialog opens; user binds 1 collection → `Profile::setSyncMappingsJson` called with new mapping. |

### 8.4 Existing tests

WildPalms's existing 78/78 stays green. SettingsDialog's new ctor arg defaults to nullptr. The existing `tst_mapping_row_dialog.cpp` doesn't assert `out.targetBackend`, so it remains green through the row-dialog extension; Phase Ic adds the missing assertion alongside the new sub-tests. Other tests baking `rawfiles-cal` as input data (`tst_mapping_editor_dialog`, `tst_palm_runtime_reload_mappings`, `tst_palm_runtime_default_mappings_only_when_empty`) seed it as input — they're unaffected by the row-dialog change. Audit Task A1 confirms this.

### 8.5 verify-all.sh

Post-Phase-Ic baseline:
- libkalburator: **75/75** (unchanged from post-Ib.5).
- PlanStan: **82/106** (unchanged; pre-existing env failures).
- WildPalms: **78 + 18 + 4 (existing test extended) = 100 sub-tests across 78+2 binaries** (new baseline; 12 from new `tst_account_controller`, 6 from new `tst_accounts_page`, 4 added to existing `tst_mapping_row_dialog`).

`scripts/verify-all.sh` exits 0; baselines refreshed in the final task.

### 8.6 What we deliberately don't test

- CalDAV-bound mappings driving Palm Datebook sync — Phase J.
- Real CardDAV / CalDAV servers (Nextcloud, Sabre/DAV, real Radicale) — deferred real-device gate (§E.1).
- KWallet — deferred (§B.4).
- Mid-sync UI mutations — interlocked off; not an exercised path.
- RFC 6764 auto-discovery — deferred (§B.3).

## 9. Risks & mitigations

### 9.1 Audit surprise

Pre-flight Task A1 may surface a structural assumption that breaks (e.g., WildPalms's `Profile::syncMappingsJson()` has a different shape than PlanStan's, or doesn't exist). **Mitigation:** audit runs first; if surprise, plan adapts before any code lands.

### 9.2 BackendRegistry lifetime cycle across profile switches

`AccountController` borrows `&palmRuntime->backendRegistry()` and a `Profile*`. **Profile switches are the dangerous case** — `KF6MainWindow::loadProfile()` is called repeatedly in a single app session, and each call replaces `m_palmRuntime` and `m_currentProfile`. If AC isn't reset first, its borrowed pointers dangle for the duration of the swap.

**Mitigation:** explicit teardown order in both `loadProfile()` (at the top, before deleting the old profile) and `closeProfile()`:

```cpp
m_accountController.reset();   // AC dies first; releases borrowed pointers
m_palmRuntime.reset();         // (or replaced on next ctor in loadProfile)
delete m_currentProfile;       // existing
```

Audit Task A1.5 confirms the exact insertion points in the existing code. Belt-and-braces: AC's dtor logs an error if `m_registry` or `m_profile` were nulled out before AC was reset (defensive against future refactors).

### 9.3 m_running interlock UX

If sync is running, Accounts mutations are blocked. **Mitigation:** AccountsPage grays out Add/Remove with a tooltip "Sync in progress." `AccountController::addProvider`/`removeProvider` short-circuit with an error if called while running, as belt-and-braces. Audit Task A1.4 confirms the right signal to subscribe to for live UI updates.

### 9.4 Connect-time failures

A provider whose server is unreachable on app start sits in the list with state=Disconnected. **Mitigation:** AccountsPage shows per-provider state (Connected / Disconnected / Error) with a Retry action. Error string surfaces in a per-row tooltip. Tested by `loadFromProfile_handlesUnreachableServer`.

### 9.5 Static-link plugin registration

Both new test binaries must wrap their libkalburator link with `$<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>` per Phase Ia.5 FINDINGS, or `CalDavProvider`/`CardDavProvider` static registrars never reach the binary's address space. **Mitigation:** explicit task in the implementation plan; pattern documented in FINDINGS and used elsewhere in `tests/CMakeLists.txt`.

### 9.6 Cascade-delete data loss

User confirms removal accidentally → mappings gone with no undo. **Mitigation:** confirm dialog states the exact count and a sample (`"Remove account 'Personal CardDAV'? This will delete 3 sync mappings: Unfiled→Contacts, Family→Family, ..."`). No undo machinery this phase; matches the rest of WildPalms's deletion UX.

### 9.7 MappingPromptDialog skipped at add-time

User dismisses the prompt without binding anything → provider exists with no mappings, but the user has no mental model for "go bind it later." **Mitigation:** AccountsPage shows a per-row "No mappings — Bind collections..." action when collectionsFor returns non-empty but the Profile has no mappings referencing the provider. Re-opens MappingPromptDialog.

## 10. Estimated effort

| Task | Days |
|---|---|
| Pre-flight audit (A1) | 0.5 |
| AccountController + persistence | 1.5 |
| PalmRuntime accessor + KF6MainWindow wiring | 0.25 |
| AccountsPage + AddAccountDialog | 1.5 |
| MappingPromptDialog | 1.0 |
| MappingRowDialog target-combo extension | 0.5 |
| `tst_account_controller` | 1.0 |
| `tst_accounts_page` | 0.75 |
| `tst_mapping_row_dialog` | 0.5 |
| CMake WHOLE_ARCHIVE wiring + verify | 0.25 |
| Baseline refresh, tag, persistence-doc updates | 0.5 |
| Buffer | 0.75 |

**Total: ~9 days of focused subagent work.** Slightly larger than the previous estimate (8 days) due to the MappingRowDialog target-combo extension. Still comparable to Phase H.5 in shape.

## 11. Phase J implications

After Phase Ic:

- **Phase J — WildPalms migrates other domains to providers**: receives a worked example AND a working MappingEditor. Phase J's job becomes wiring CalDAV-bound mappings to Palm Datebook (and similar for memo/todo, if those grow DAV equivalents). Phase Ic delivers: (a) AccountsPage already accepts CalDAV accounts, (b) MappingEditor already lets users pick `<uuid>:<colId>` as a target — so a CalDAV-bound mapping created via MappingEditor or MappingPromptDialog is fully expressible. Phase J's deltas are minimal: make MappingPromptDialog's CalDAV row's slot picker active, plus the per-domain `BlobBackendAdapter` extensions for Palm Datebook/Memo/Todo.
- **Akonadi backend (§C.1)**: when it lands, registers as a sibling provider; AccountsPage picks it up automatically (kind picker grows by one entry).
- **KWallet (§B.4)**: provider configurations move from plaintext to wallet handles; `BackendConfiguration` gains an opaque-credential field. AccountsPage and AddAccountDialog change negligibly.
- **RFC 6764 auto-discovery (§B.3)**: AddAccountDialog grows a "Discover from email" affordance; provider-kind picker can offer an "auto-detect" flow.
- **Combined multi-protocol provider (§B.5)**: kind picker grows a "Nextcloud" entry that produces a single provider speaking both protocols.

## 12. References

- Phase H design: `~/dev/refactor-engine-merger/2026-05-06-phase-h-providers-design.md`
- Phase H plan: `~/dev/refactor-engine-merger/2026-05-06-phase-h-providers-plan.md`
- Phase H.5 design: `~/dev/refactor-engine-merger/2026-05-07-phase-h5-planstan-providers-design.md`
- Phase H.5 plan: `~/dev/refactor-engine-merger/2026-05-07-phase-h5-planstan-providers-plan.md`
- Phase Ib design: `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04x-phase-ib-status.md` (status doc; design absorbed)
- Deferred-work catalog: `~/dev/refactor-engine-merger/libkalburator/docs/phase0/04w-deferred-work.md` (§D.1–D.4)
- WildPalms TODO: `~/dev/refactor-engine-merger/WildPalms/docs/TODO-contacts-account-ux.md`
- Roadmap: `~/dev/refactor-engine-merger/ROADMAP.md`
- IProvider interface: `libkalburator/src/sync/iprovider.h`
- ProviderManager: `libkalburator/src/sync/providermanager.{h,cpp}`
- CalDavProvider: `libkalburator/src/sync/caldavprovider.{h,cpp}`
- CardDavProvider: `libkalburator/src/sync/carddavprovider.{h,cpp}`
- WildPalms PalmRuntime: `WildPalms/src/runtime/palmruntime.{h,cpp}`
- WildPalms SettingsDialog: `WildPalms/src/settingsdialog.{h,cpp}`
- FINDINGS (WHOLE_ARCHIVE pattern): `~/dev/refactor-engine-merger/FINDINGS.md`

## 13. Brainstorm decisions log

Recorded so future-you understands why the design looks the way it does:

| Q | Decision | Reason |
|---|---|---|
| 1 — phase scope | WildPalms only (D.2–D.4) | Match roadmap; D.1 stays separate slice; preserves "one consumer per phase" rhythm |
| 2 — PM ownership | Standalone `AccountController` borrowed by PalmRuntime | Cleaner separation; SettingsDialog needs a non-runtime-coupled dependency; PalmRuntime stays focused on device coupling |
| 3 — domain coverage | Accounts UX accepts both kinds; only contacts wired through Palm sync | Accounts UX symmetry ships once; Phase J finishes calendar/memo/todo wiring |
| 4 — default mapping | Always prompt | Most explicit; eliminates "where did this mapping come from" surprise |
| 5 — account removal | Confirm + cascade delete | Clean state; orphan mappings would clutter MappingEditor with unresolvable backend ids |
| 6 — provider scope | Per-profile, sidecar inside sync folder | Preserves Profile portability ("settings travel with the sync folder"); matches Phase H.5; trade-off: same CardDAV server entered once per profile |
| 7 — mapping UX | Accounts and Mappings are orthogonal; MappingEditor target-combo extended in Phase Ic | One mapping mental model. Provider-supplied and direct backends are interchangeable in MappingEditor. AccountsPage manages credentials only. MappingPromptDialog is a convenience accelerator that writes the same SyncMappings MappingEditor would write. Required because the existing `targetBackend = "rawfiles-cal"` hardcoding would silently corrupt provider-bound mappings. |
| seq — sequencing | One-phase | Matches recent rhythm (H, H.5, Ia, Ia.5, Ib, Ib.5 all single-tag); work is small enough |
