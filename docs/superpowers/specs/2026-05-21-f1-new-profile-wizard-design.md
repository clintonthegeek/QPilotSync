# F.1 — New Profile Wizard — design  ⚠ SUPERSEDED 2026-05-21

> **This spec is superseded.** During plan-writing, the design hit a
> blocker: it assumed remote accounts could exist *before* a profile
> was created, but in the current codebase accounts are per-profile.
> Rather than work around that, the user chose to redesign the whole
> profile-creation story:
>
> - Accounts stay per-profile (no app-global hoist).
> - Profile location becomes auto-generated (`~/.wildpalms/profileN`);
>   "open by directory" is removed entirely.
> - Profiles become app-registered (KConfig registry); the File menu
>   gains Switch / Import / Forget; first-run auto-launches the wizard.
> - Profile persistence gets restructured into multiple KConfig files.
> - The wizard handles inline account creation (Akonadi first, then
>   CalDAV/CardDAV with inline credentials), folder pickers move
>   down into the per-domain step.
>
> The new direction is split into three sub-projects:
>
> - **F.1a** — Profile persistence refactor + app-level
>   `ProfileRegistry`. No UI change. (See `2026-05-21-f1a-…-design.md`
>   once written.)
> - **F.1b** — New File menu (Switch / Import / Forget; removes
>   Open); first-run auto-launches the stopgap.
> - **F.1c** — The full multi-page wizard, replacing the stopgap.
>
> Everything below is preserved for reference: parts of it (page
> structure, testing approach, MappingSpec translation) will be reused
> in F.1c with adjustments for the new model.

**Date:** 2026-05-21 (superseded same day)
**Status:** ⚠ Superseded. See banner above.
**Original phase:** F.1 (first of four Phase F sub-projects: wizard / `IConflictPresenter` / calendar-binding UX / Radicale E2E + user docs).
**Predecessor:** Phase E ✅ closed 2026-05-21 (`docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`).

---

## 1. Why this exists

Today, "File → New Profile" calls
`QFileDialog::getExistingDirectory` and nothing else. The chosen
folder either has a `.wildpalms.conf` already (and gets opened) or
doesn't (and gets one written with hard-coded defaults). The user
learns about accounts, mappings, and conflict policies only after
the profile is loaded, by hunting through menus.

The result: the path from "I want to sync my Palm with CalDAV" to a
working sync is undiscoverable. Users either edit config files or
give up. Phase E's plugin/runtime work delivered the engine; Phase
F.1 delivers the on-ramp.

## 2. Scope

In scope:

- A modal `QWizard` that runs when the user picks **File → New
  Profile…**.
- Four pages: folder + name, per-domain target picker, optional
  collection-discovery, review + Finish.
- Persistence on Finish: writes `<folder>/.wildpalms.conf` and
  `<folder>/.kalb`, then routes through the normal `loadProfile`
  path so `PalmRuntime` instantiates as usual.
- Unit + e2e tests for the wizard and its integration with
  `KF6MainWindow`.

Out of scope (separate F sub-projects):

- The real `IConflictPresenter` wire-up (F.2).
- Calendar-binding UX polish for power users (F.3) — the wizard
  generates *one* mapping per domain; richer per-Palm-category routing
  is still done via `MappingEditorDialog`.
- An end-to-end test against a Radicale server (F.4).
- A user-facing `FULL_SYNC_MODE_GUIDE.md` (F.4).

Explicitly not addressed in this spec:

- First-run-of-the-app onboarding flow. The wizard fires from the
  explicit "New Profile" action, not from app launch.
- Multi-device-per-profile flows. `Profile↔DeviceFingerprint` is 1:1
  (Phase L Task 0); the wizard creates the profile without a
  fingerprint and binds to the first Palm that connects.
- Account creation. The wizard reads the list of *already-configured*
  accounts via `AccountController`; adding accounts is a separate
  trip through Settings → Accounts (the existing flow).

## 3. Confirmed design choices

Established during brainstorming:

1. **Scope:** profile + initial mappings. Wizard creates default
   mappings for all four sync plugins; user can tweak afterward in
   `MappingEditorDialog`.
2. **Target picking:** per-domain. Four rows (Calendar, Contacts,
   Memo, ToDo), each with a dropdown showing compatible targets.
3. **Discovery:** during the wizard. Picking a remote account
   triggers inline collection enumeration; the user picks one before
   Finish.
4. **Device binding:** bind on first connect. No requirement to plug
   in the Palm during the wizard.
5. **Toolkit:** `QWizard` + `QWizardPage` (not custom QDialog +
   QStackedWidget, not a library widget). Standard Qt6 idiom.
6. **Page 2 layout:** stacked cards (one card per domain) rather
   than a compact table.

## 4. Architecture

### 4.1 File layout

New directory `src/app/wizard/`:

| File | Class | Responsibility |
| --- | --- | --- |
| `newprofilewizard.{h,cpp}` | `NewProfileWizard : QWizard` | Wizard shell. Owns the four pages, exposes `result()` returning the populated `Result` struct, integrates with `KF6MainWindow::onNewProfile()`. |
| `folderpage.{h,cpp}` | `FolderPage : QWizardPage` | Step 1: folder picker + profile name. Validation. |
| `targetpickerpage.{h,cpp}` | `TargetPickerPage : QWizardPage` | Step 2: four `TargetPickerRow` widgets stacked vertically (one card per domain). |
| `targetpickerrow.{h,cpp}` | `TargetPickerRow : QWidget` | One card: domain icon + label + target dropdown. Reused inside `TargetPickerPage`. |
| `collectiondiscoverypage.{h,cpp}` | `CollectionDiscoveryPage : QWizardPage` | Step 3: for each remote target, async discover collections and let user pick. Skipped if no remote target was chosen. |
| `discoveryrow.{h,cpp}` | `DiscoveryRow : QWidget` | One block per remote (domain, account) pair. Owns the per-row `QFutureWatcher`, spinner→picker transition, retry/defer. Reuses libkalburator's `CollectionPickerWidget` once the future resolves. |
| `reviewpage.{h,cpp}` | `ReviewPage : QWizardPage` | Step 4: read-only summary, Finish button. |

### 4.2 Data types

```cpp
namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,   // local files (Markdown / .ics / .vcf)
    CalDAV,
    CardDAV,
    Akonadi,
};

struct MappingSpec {
    QString     pluginId;       // "wildpalms.calendar" | "wildpalms.contacts"
                                // | "wildpalms.memo" | "wildpalms.todo"
    TargetKind  targetKind;
    QString     accountId;      // empty for RawFiles
    QString     collectionId;   // resolved by discovery, or empty for
                                // RawFiles or "deferred"
    bool        deferred = false;
};

struct Result {
    QString             folder;       // chosen profile folder (absolute)
    QString             profileName;  // display name
    QList<MappingSpec>  mappings;     // exactly four entries (one per domain)
};

} // namespace WildPalms::Wizard
```

`Result` is a plain struct of value types. The wizard returns it by
value from `NewProfileWizard::result()`.

**`MappingSpec` + `QList<MappingSpec>` through `QWizard::field()`:**
`QWizard`'s field system passes values via `QVariant`. Custom types
require `Q_DECLARE_METATYPE(WildPalms::Wizard::MappingSpec)` in the
header and a matching `qRegisterMetaType<...>()` call at wizard
construction. Pages get/set values via `field("targetSpecs").value<...>()`
and `setField("targetSpecs", QVariant::fromValue(...))`.

An alternative: pages communicate directly through a pointer
back to the wizard (e.g. each page calls
`qobject_cast<NewProfileWizard*>(wizard())->setTargetSpecs(...)`).
That avoids QVariant boxing for the list-of-struct case and is
slightly cleaner. The implementation plan picks one approach; both
are workable.

### 4.3 Dependencies

`NewProfileWizard` ctor takes:

- `AccountController *accountController` — borrowed, non-owning.
  Used to enumerate configured accounts (`configuredAccounts()`)
  and to resolve `IProvider *` by `accountId` for discovery.
- `QWidget *parent` — standard Qt parent.

`AccountController` is already constructed and owned by
`KF6MainWindow` per Phase Ic.

### 4.4 Integration with `KF6MainWindow`

Existing `onNewProfile()` at `src/kf6/kf6mainwindow.cpp:1453`:

```cpp
void KF6MainWindow::onNewProfile() {
    QString path = QFileDialog::getExistingDirectory(this,
        i18n("Select Folder for New Profile"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);
    if (path.isEmpty()) return;
    loadProfile(path);
}
```

becomes:

```cpp
void KF6MainWindow::onNewProfile() {
    if (!m_accountController) {
        m_logWidget->logError(i18n(
            "Cannot create profile: account controller not initialised"));
        return;
    }
    WildPalms::Wizard::NewProfileWizard wiz(m_accountController, this);
    if (wiz.exec() != QDialog::Accepted) return;
    const auto result = wiz.result();
    if (!writeNewProfileFiles(result)) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not write profile files. Check the log."));
        return;
    }
    loadProfile(result.folder);
}
```

`writeNewProfileFiles(const Result &)` is a new private helper on
`KF6MainWindow` that materialises the `Result` to disk (see §5.5).

`m_accountController` exists per the existing `AccountController`
wiring (Phase Ic; constructed in `KF6MainWindow::loadProfile`).
**Open point:** today `m_accountController` is constructed *during*
`loadProfile`, after a profile is loaded. The wizard needs an
account controller *before* a profile exists. Resolution: either
(a) instantiate a profile-less `AccountController` in
`KF6MainWindow`'s ctor, populated from global app settings rather
than from a profile; or (b) the wizard owns a transient
`AccountController` constructed from app-global account state.

This is left for the implementation plan to resolve. The simpler
of the two is (a) — `AccountController` becomes a permanent
member of `KF6MainWindow` regardless of whether a profile is
loaded, populated from `KSharedConfig::openConfig()`. That matches
how PlanStan treats account state (app-global, not
per-collection). The wizard then borrows it as a non-owning pointer.

## 5. Per-page behaviour

### 5.1 Page 1 — Folder + name (`FolderPage`)

**Fields:**

- **Sync folder** — read-only line edit + "Browse…" button →
  `QFileDialog::getExistingDirectory(this, …, lastUsedParent)`.
  Pre-fill on first use: `~/PalmSync`. Subsequent invocations
  remember the last-used parent via `KSharedConfig::openConfig()`
  group `[wizard]` key `lastFolderParent`.
- **Profile name** — `QLineEdit`. Default value: folder basename
  (e.g. `~/PalmSync` → "PalmSync"). User-editable. The field is
  registered as `*` (required).

**Validation (`isComplete()` returns true only when all hold):**

- Folder path is non-empty.
- Folder exists on disk (`QDir(path).exists()`).
- Folder is writable (`QFileInfo(path).isWritable()`).
- Folder does **not** already contain a `.wildpalms.conf` — if it
  does, show an inline warning label "This folder is already a
  WildPalms profile. Use File → Open Profile instead." and Next
  stays disabled.
- Profile name (after `trimmed()`) is non-empty.

**On Next:** standard `QWizard` field propagation. `FolderPage`
registers `folder*` and `profileName*` fields read by later pages.

**No async work.**

### 5.2 Page 2 — Per-domain target picker (`TargetPickerPage`)

Four `TargetPickerRow` widgets stacked vertically. Each row shows:

- Domain icon (Calendar / Contacts / Memo / ToDo).
- Domain label + smaller Palm DB name (e.g. "Calendar /
  DatebookDB").
- `QComboBox` with the eligible targets.

**Target dropdown population** (per domain):

- Always present: "Local files (default)" — selects
  `TargetKind::RawFiles`.
- For each `AccountConfiguration` returned by
  `m_accountController->configuredAccounts()`:
  - Calendar row: include CalDAV accounts and Akonadi accounts.
  - Contacts row: include CardDAV accounts and Akonadi accounts.
  - Memo row: **dropdown disabled, always "Local Markdown
    files"** — no remote backend for memos exists.
  - ToDo row: include CalDAV accounts (as VTODO) and Akonadi
    accounts.

Dropdown label format: `"<Account display name> (<protocol>)"`
e.g. `"Fastmail (CalDAV)"`.

**No discovery on this page.** Selecting a remote account only
records the choice in the field.

**Field:** `TargetPickerPage` registers a single field
`targetSpecs` of type `QList<MappingSpec>` — exactly four entries
keyed by `pluginId`. Initialised to all-RawFiles. Updated as the
user changes dropdowns.

**Validation:** `isComplete()` is always true (every row has a
default selection). Next is enabled immediately.

### 5.3 Page 3 — Collection discovery (`CollectionDiscoveryPage`)

**Skip logic:** override `QWizardPage::isComplete()` AND
`QWizard::nextId()` on the previous page to skip directly to
Page 4 when no remote target was chosen on Page 2.

**On entry (`initializePage`):** for each `MappingSpec` in
`targetSpecs` where `targetKind != RawFiles`, instantiate a
`DiscoveryRow`. Each row owns a `QFutureWatcher<QList<CollectionInfo>>`
and triggers `provider->discoverCollections()` in its own
`initializePage` call. All discoveries run in parallel.

**Per-row states:**

1. **Loading.** Spinner + "Discovering calendars on this
   account…". Cancel button (per-row cancel via
   `QFutureWatcher::cancel`).
2. **Loaded.** Spinner replaced by libkalburator's
   `CollectionPickerWidget` populated with the result list, set to
   single-select mode (radio). Trailing "Create new collection…"
   button that delegates to provider-specific creation flow (see
   §5.3.1).
3. **Failed.** Inline error "⚠ Couldn't reach this account: <reason>"
   plus two buttons: "[Retry]" (re-runs the future) and "[Skip —
   resolve later in Configure Mappings]" (marks the row's
   `MappingSpec::deferred = true` and `collectionId = ""`).
4. **Deferred (after skip click).** Row collapses to a single line:
   "Calendar → Fastmail (will configure later)".

**Page validation (`isComplete()`):** every row is in state
**Loaded with a collection selected**, OR **Deferred**, OR
**Loaded after Create-new resolved successfully**.

**On Next:** the field `targetSpecs` is updated in place — each
remote entry now has a `collectionId` (resolved or empty +
`deferred=true`).

#### 5.3.1 "Create new collection" sub-flow

PlanStan's `NewCollectionDialog` has the same pattern. For F.1 we
delegate to a method on `IProvider`:

```cpp
QFuture<QString> createCollection(const QString &displayName,
                                  const QString &domain);
```

The implementation already exists for CalDAV / CardDAV / Akonadi
providers per Phase Ic + Phase M; if it returns `""` we report
failure and stay on the page.

If `IProvider::createCollection` does **not** exist for a particular
provider, the "Create new collection…" button is hidden for that
row. Discovery + pick is still available.

### 5.4 Page 4 — Review (`ReviewPage`)

Read-only summary, populated from `initializePage()` reading
`field("folder").toString()`, `field("profileName").toString()`,
`field("targetSpecs").value<QList<MappingSpec>>()`. Renders as a
single `QLabel` with rich text:

```
You're about to create a new profile:

   Folder:   ~/Sync/PalmM505
   Name:     Palm m505

Sync mappings:

   📅  Calendar  →  Fastmail / "Personal" (CalDAV)
   👤  Contacts  →  Local .vcf files
   📝  Memo      →  Local .md files
   ✓   To-do     →  Local .ics files (will configure collection later)
```

**On Finish:** wizard's `validateCurrentPage()` calls `accept()`.
`NewProfileWizard::result()` (a member function added on
`NewProfileWizard`) reads the fields and assembles a `Result`.

### 5.5 `KF6MainWindow::writeNewProfileFiles`

```cpp
bool KF6MainWindow::writeNewProfileFiles(
    const WildPalms::Wizard::Result &r)
{
    QDir().mkpath(r.folder);   // already validated on Page 1

    // .wildpalms.conf — written via QSettings (INI) by Profile::save().
    // Mappings are persisted inside the same file under
    // [syncMappings] json="..."; see Profile::save() at src/profile.cpp:548.
    // The .kalb file (libkalburator collection state) is written
    // separately by CollectionController during loadProfile and is not
    // a wizard responsibility.
    Profile p;
    p.setName(r.profileName);
    p.setSyncFolderPath(r.folder);
    p.setConflictPolicy(WildPalms::Sync::ConflictResolution::AskUser);
    p.setConnectionMode(Profile::ConnectionMode::KeepAlive);
    p.setAutoSyncOnConnect(false);
    p.setSyncMappingsJson(syncMappingsJsonFor(r.mappings));
    return p.save();
}
```

`syncMappingsJsonFor(const QList<MappingSpec> &)` is a free helper
in `newprofilewizard.cpp` that emits the JSON array `Profile`
already expects from `MappingEditorDialog`. Translation rules per
domain entry:

- `sourceBackend = spec.pluginId` (e.g. `wildpalms.calendar`).
- `conflictPolicy = AskUser`.
- `enabled = true` unless deferred (then `false`).

For RawFiles targets, the wizard writes **no mapping at all**.
`PalmRuntime::finishConnect()` already auto-generates a per-Palm-slot
RawFiles mapping for every Palm collection not covered by an
existing user mapping (`src/runtime/palmruntime.cpp:305-340`). So
"the user picked RawFiles" is equivalent to "the user accepted the
default behaviour" — which is what `finishConnect` does anyway. The
wizard's RawFiles dropdown selection records the user's intent
visually, but produces no persisted mapping.

For remote targets, the wizard writes per-(pluginId, collectionId)
mapping(s):

- `targetBackend = "<spec.collectionId>"` — the id resolved by
  Page 3. `AccountController::mirrorProviderBackends` ensures the
  backend instance is registered in `BackendRegistry` when the
  profile loads.
- `sourceCalendar` — see open point §9 below. The wizard either (a)
  writes one mapping with `sourceCalendar = ""` and relies on
  `finishConnect` to recognise this as a wildcard meaning "route
  every Palm slot for this plugin to this single target", or (b)
  writes one mapping per known Palm slot at first connect, lazily
  expanding the wildcard into N concrete mappings. The
  implementation plan picks one; if (a), `finishConnect` needs a
  small change to honour the wildcard.

For remote deferred targets (`deferred = true`): `targetBackend = ""`,
`enabled = false`. `MappingEditorDialog` already handles empty-target
rows.

## 6. Error handling

| Failure | Surfacing | Recovery |
| --- | --- | --- |
| Folder doesn't exist / not writable | Page 1 inline warning; Next disabled | User fixes selection |
| Folder is already a profile | Page 1 inline warning; Next disabled | User picks a different folder |
| `discoverCollections()` future fails | Per-row inline error on Page 3; Retry + Skip buttons | Retry re-runs; Skip marks deferred |
| `createCollection()` returns empty | Per-row inline error after the Create-new button | User picks an existing collection from the list, or retries |
| `Profile::save()` fails on Finish | Modal `QMessageBox::critical` from `KF6MainWindow`; wizard stays accepted but no profile loaded | User goes back to File → New Profile and retries |
| `loadProfile(r.folder)` fails after Finish | Existing `loadProfile` error surfacing (log widget + status bar) | Existing recovery: user closes profile or opens a different one |
| Cancel at any point | Wizard's `reject()`; nothing written to disk | None needed |

**No partial state.** Every Finish either writes both `.wildpalms.conf`
and `.kalb` and loads the profile, or writes neither.

## 7. Testing

### 7.1 Per-page unit tests (`tests/app/wizard/`)

| File | Coverage |
| --- | --- |
| `tst_folderpage.cpp` | Folder picker validation (empty, nonexistent, unwritable, already-a-profile, valid); name autofill from basename; name editability; field propagation on Next. |
| `tst_targetpickerpage.cpp` | Dropdown population from stub `AccountController` (zero accounts / one CalDAV / one CardDAV / mixed); Memo row disabled; field updates on selection change. |
| `tst_collectiondiscoverypage.cpp` | Discovery yields/fails/cancels via stub provider; spinner→picker transition; Retry re-runs; Skip marks deferred; Next disabled until all rows resolved; Create-new button visibility per provider. |
| `tst_reviewpage.cpp` | Summary rendering from a fixture `Result`; four standard cases (all local, one remote, mixed, deferred). |

Each per-page test follows the existing `WILDPALMS_QTEST_GUILESS_MAIN`
pattern (no MainWindow needed for individual `QWizardPage`s).

### 7.2 End-to-end wizard test

`tests/app/wizard/tst_newprofilewizard.cpp` — drives the full
wizard through three flows:

1. **All-local happy path.** Pick folder + name; accept defaults
   on Page 2 (all RawFiles); Page 3 is skipped; Finish.
2. **Remote target with successful discovery.** Pick folder; set
   Calendar row to a stub CalDAV account; on Page 3, stub provider
   yields two collections, test picks the first; Finish.
3. **Remote target with discovery failure + defer.** Same as (2)
   but stub provider's future fails; test clicks Skip; Finish.

For each: assert `.wildpalms.conf` + `.kalb` written with the
expected content; cancel discards everything.

Uses `WILDPALMS_QTEST_MAIN` (the macro that avoids the
`__cxa_finalize` exit crash) because `QWizard::exec()` spins a
nested event loop.

### 7.3 Integration with `KF6MainWindow`

`tests/app/wizard/tst_kf6mainwindow_newprofile.cpp` — replace
`onNewProfile()`'s wizard with a stub that returns a pre-built
`Result` (test-only seam: a virtual `runProfileWizard()` method on
`KF6MainWindow` that production overrides with the real
`NewProfileWizard::exec()`). Verifies the post-wizard wiring:
folder created, `Profile::save()` called with expected JSON,
`loadProfile()` invoked, `PalmRuntime` instantiated, mappings
reach `palmRuntime->reloadMappings()`.

### 7.4 Stubs

`tests/app/wizard/stubs/`:

| File | Class | Purpose |
| --- | --- | --- |
| `stubaccountcontroller.{h,cpp}` | `StubAccountController` | Implements the surface the wizard uses (`configuredAccounts()`, `providerFor()`). Configurable account list. |
| `stubprovider.{h,cpp}` | `StubProvider : IProvider` | Implements `discoverCollections()` + `createCollection()` with controllable outcomes (immediate success, immediate failure, deferred resolve via signal). |

### 7.5 What's NOT tested here

- Network behaviour against real CalDAV / CardDAV / Akonadi
  servers. Belongs to F.4 (Radicale E2E).
- POSE64-driven Palm-device interactions. Out of scope (cancelled
  per E.18).
- Multi-page `QWizard` chrome (back/next/finish buttons, progress
  dots) — Qt6 ships those; we don't test them.

## 8. Success criteria

1. **Discoverability.** From a fresh app launch on a system with at
   least one CalDAV account configured, a user can create a new
   profile syncing Palm Calendar to that CalDAV server in under
   60 seconds, without consulting documentation.
2. **Correctness.** The `.wildpalms.conf` + `.kalb` written by the
   wizard load cleanly via the existing `loadProfile` path; no
   manual edits required to start the first sync.
3. **Robustness.** Discovery failures don't strand the user; the
   defer path always lets the wizard reach Finish.
4. **No regressions.** Existing File → Open Profile / Close
   Profile / Configure Mappings flows are untouched.
5. **Test coverage.** All five page-level tests + the three e2e
   flows + the MainWindow integration test land green.

## 9. Open implementation points (for the plan to resolve)

- **`AccountController` lifetime.** Today it's constructed during
  `KF6MainWindow::loadProfile()`, i.e. *after* a profile is loaded.
  The wizard needs it *before* a profile exists. Preferred fix:
  hoist `AccountController` construction to `KF6MainWindow`'s ctor
  and populate from `KSharedConfig::openConfig()` (account state is
  app-global, not per-profile). The plan migrates existing call
  sites accordingly.
- **Wildcard mapping vs per-slot expansion for remote targets.** The
  wizard doesn't know Palm slot names without a connected device.
  Two viable resolutions: (a) wizard writes one mapping per
  pluginId with `sourceCalendar = ""`, and `finishConnect` learns to
  treat empty `sourceCalendar` as a wildcard meaning "this target
  covers every Palm slot for this plugin" (requires a small
  change to `palmruntime.cpp:298-345`); or (b) wizard writes the
  user's domain→target choice into a new "preferred targets" section
  of `.wildpalms.conf`, and `finishConnect` reads that section when
  generating per-slot mappings on first connect, routing matching
  slots to the user's chosen target instead of RawFiles. Option (b)
  is cleaner (the persisted SyncMapping list always reflects the
  actual concrete mappings, never wildcards), but adds a new config
  surface. Plan picks one.
- **Field-passing strategy.** Either `Q_DECLARE_METATYPE(MappingSpec)`
  + QVariant boxing through `QWizard::field()`, or direct pointer
  communication via `qobject_cast<NewProfileWizard*>(wizard())`.
  Plan picks one.
- **Discovery cancellation.** When the user clicks Cancel on
  Page 3, `QWizard::reject()` must propagate into in-flight per-row
  `QFutureWatcher`s. Plan specifies the cleanup pattern
  (`QObject::destroyed` connection or explicit
  `DiscoveryRow::~DiscoveryRow` cancel).
- **Provider error surfacing.** `IProvider::discoverCollections()`
  returns `QFuture<QList<CollectionInfo>>` with no explicit error
  channel. Today's empty-list-on-failure convention is workable
  but doesn't distinguish "no calendars on this account" from
  "auth failed". Plan checks whether libkalburator exposes a
  richer error mechanism (e.g. `QFuture::progressText` or a
  side-channel signal on `IProvider`); if not, the wizard treats
  empty-as-error with a generic message.
- **Test seam for `KF6MainWindow::onNewProfile()`.** §7.3 proposes
  a virtual `runProfileWizard()` for stubbing in tests. Plan
  verifies this doesn't conflict with existing virtual surface on
  `KF6MainWindow` and picks a name that doesn't clash.

## 10. References

- `docs/PLUGIN_ABI.md` — the plugin contract the wizard generates
  mappings for.
- `docs/ARCHITECTURE_2026.md` — surrounding `PalmRuntime` /
  `KF6MainWindow` / `AccountController` architecture.
- `docs/plans/2026-04-20-libkalburator-integration.md` §Phase F —
  the umbrella phase this spec is the first sub-project of.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
  — Phase E status (the predecessor).
- libkalburator `src/ui/collectionpickerwidget.{h,cpp}` — the
  collection picker reused on Page 3.
- libkalburator `src/plugin/*providerplugin.h` — the providers
  whose `discoverCollections()` Page 3 calls.
- PlanStan `NewCollectionDialog` — pattern reference for the
  collection-discovery sub-flow (not consumed; serves as visual
  precedent).
