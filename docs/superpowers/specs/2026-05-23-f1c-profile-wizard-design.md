# F.1c — Multi-page Profile-Creation Wizard — design

**Date:** 2026-05-23
**Status:** Design approved through brainstorming. Spec ready for plans.
**Phase:** F.1c (third of three Phase F.1 sub-projects; follows F.1a ✅ 2026-05-22 and F.1b ✅ 2026-05-22).
**Predecessors:**
- F.1a (profile persistence + app-level `ProfileRegistry`) ✅
- F.1b (new File menu: Switch / Import / Forget) ✅
- F.2 (palm-sync honesty + real `IConflictPresenter` + LWW default policy) ✅
**Supersedes (in part):** `docs/superpowers/specs/2026-05-21-f1-new-profile-wizard-design.md` — the earlier "F.1" spec banner declared itself superseded and pointed forward to this one. Page-structure and `MappingSpec` ideas from that document are reused with adjustments for the post-F.1a/F.1b model.

---

## 1. Why this exists

After F.1a + F.1b, `KF6MainWindow::onNewProfile()` is a one-field
`QInputDialog::getText` stopgap: prompt for a profile name,
`ProfileRegistry::registerNew(name)` allocates the next `profileN`
directory under `~/.wildpalms/`, then `loadProfile` runs. The user
learns about accounts, sync mappings, and conflict policies only
after the profile is loaded, by hunting through menus.

The result: the path from "I want to sync my Palm with Fastmail" to
a working sync is still undiscoverable. F.1c replaces that one-field
stopgap with a multi-page wizard that:

- captures the profile name,
- lets the user pick a target backend per Palm domain
  (Calendar / Contacts / Memo / ToDo),
- creates new CalDAV / CardDAV / Akonadi accounts inline if the user
  asks for one,
- discovers each remote account's collections and lets the user pick,
- writes the resulting profile + accounts + mapping intent to disk
  on Finish (and only on Finish — transient model throughout).

F.1c is the discoverability deliverable that Phase F.1 has been
building toward. Phase E delivered the engine; F.1a + F.1b delivered
the registry and menu plumbing; F.1c delivers the on-ramp.

## 2. Scope

### 2.1 In scope (two ordered sub-projects)

F.1c naturally splits into two sub-projects shipping in dependency
order. Each ships as its own spec → plan → PR cycle.

**F.1c.0 — `AccountFormWidget` refactor.** Lifts the credential UI
out of `AddAccountDialog` (`src/app/accounts/addaccountdialog.{h,cpp}`)
into a `QWidget` subclass with its own validation and signals. The
existing `AddAccountDialog` becomes a thin dialog wrapper around the
widget. Settings → Accounts UI unchanged from the user's perspective.
Tests: a new unit test for `AccountFormWidget` in isolation; existing
`AddAccountDialog` tests stay green. Mechanical refactor; ~½ day.

**F.1c.1 — Profile-creation wizard.** Replaces
`KF6MainWindow::onNewProfile()` (the F.1a one-field stopgap) with a
`NewProfileWizard : QWizard` over five pages. Inline account creation
reuses `AccountFormWidget` from F.1c.0. Wildcard mapping output plus
a small `PalmRuntime::finishConnect` change to honor empty
`sourceCalendar`. Tests: per-page units, e2e wizard, integration
test against `KF6MainWindow`, narrow runtime test for the wildcard
change.

### 2.2 Out of scope

- **KWallet credential hardening.** Per brainstorming Q5, credential
  storage stays in `profile.conf [accounts]` (matching today). KWallet
  becomes its own sub-project after F.1c.
- **First-run / empty-registry auto-launch.** Per brainstorming Q2,
  the wizard fires from `File → Profile → New Profile` only. First-run
  keeps the F.1a `showProfilePickerStopgap` one-field prompt.
- **Per-Palm-category routing UX** (F.3). The wizard generates *one*
  mapping per domain; richer per-category routing is still
  `MappingEditorDialog` territory.
- **Radicale E2E + user docs** (F.4).
- **"Create new collection" sub-flow on the discovery page.** Per
  brainstorming follow-up #1, v1 ships without it; users with no
  existing collections create one via the provider's web UI then
  re-run discovery. Revisit if needed in F.3.
- **Settings → Accounts using the wizard's credential pages.**
  Out of scope; AccountFormWidget refactor (F.1c.0) is the only
  shared piece.

## 3. Confirmed design choices

Established during 2026-05-23 brainstorming (decision log below):

1. **Inline account creation in scope.** Wizard captures CalDAV /
   CardDAV / Akonadi credentials directly; no detour through
   Settings → Accounts.
2. **Launches from `File → Profile → New Profile` only.** First-run
   path keeps the F.1a stopgap.
3. **Two-phase page structure.** Five pages: Name → Pick targets →
   Add accounts (conditional) → Discovery (conditional) → Review.
   Pages 3 and 4 skip dynamically based on prior choices.
4. **Transient model.** All wizard state lives in memory until
   Finish. Cancel = nothing on disk.
5. **KWallet deferred.** Credentials write into `profile.conf
   [accounts]` (matching existing `AccountController` behavior).
6. **Block-finish on discovery failure.** Discovery page Next stays
   disabled until every row has a chosen collection. No defer path
   in v1 — stronger guarantee that Finish = working profile.
7. **Approach A — refactor `AddAccountDialog`.** F.1c.0 lifts the
   credential UI into a reusable `AccountFormWidget`. F.1c.1's
   AddAccountsPage stacks one per pending account.
8. **No "Create new collection" button on the discovery page** for
   v1 (per follow-up).
9. **Akonadi accounts get a kind-aware AccountFormWidget body**
   (per follow-up) — Akonadi's account model differs from CalDAV /
   CardDAV (it's a wrapped resource, not server+user+pass). The
   refactor preserves this kind-awareness.

## 4. Architecture

### 4.1 Sub-project decomposition

```
F.1c.0  (refactor)
  ├─ src/app/accounts/accountformwidget.{h,cpp}    NEW
  ├─ src/app/accounts/addaccountdialog.{h,cpp}     MODIFIED (thin wrapper)
  └─ tests/app/accounts/tst_accountformwidget.cpp  NEW

F.1c.1  (wizard)
  ├─ src/app/wizard/                       NEW directory
  │   ├─ newprofilewizard.{h,cpp}          NewProfileWizard : QWizard
  │   ├─ wizardstate.h                     struct WizardState + helpers
  │   ├─ namepage.{h,cpp}                  Page 1
  │   ├─ targetpickerpage.{h,cpp}          Page 2
  │   ├─ targetpickerrow.{h,cpp}           Used by Page 2
  │   ├─ addaccountspage.{h,cpp}           Page 3 (conditional)
  │   ├─ discoverypage.{h,cpp}             Page 4 (conditional)
  │   ├─ discoveryrow.{h,cpp}              Used by Page 4
  │   └─ reviewpage.{h,cpp}                Page 5
  ├─ src/kf6/kf6mainwindow.{h,cpp}         MODIFIED — onNewProfile + writeWizardResultToProfile + app-level BackendRegistry member
  ├─ src/runtime/palmruntime.cpp           MODIFIED — finishConnect honors empty sourceCalendar
  └─ tests/app/wizard/                     NEW directory (see §7)
```

F.1c.1 depends on F.1c.0. Ship F.1c.0 first; verify Settings →
Accounts still works; then ship F.1c.1.

### 4.2 Data types (F.1c.1)

`WizardState` is a plain struct owned by `NewProfileWizard`; pages
get a non-owning pointer at construction. `QWizard::field()` is used
only for simple string fields (e.g. profile name) where the standard
idiom is natural; complex list types pass through `WizardState`
directly to avoid `Q_DECLARE_METATYPE` boxing.

```cpp
namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,         // local files (Markdown / .ics / .vcf)
    RemoteExisting,   // user picked an account that already existed (none in v1
                      // since Settings→Accounts isn't reachable from the wizard,
                      // but reserved for future expansion)
    RemoteNew,        // user picked "Add new …" — credentials captured on Page 3
};

struct PendingAccount {
    QString id;          // wizard-local UUID (QUuid::createUuid().toString());
                         // also used as the on-disk account id on persistence.
                         // (See §10 — reversible choice.)
    QString kind;        // "caldav" | "carddav" | "akonadi"
    Kalburator::Sync::BackendConfiguration config;
};

struct MappingSpec {
    QString    pluginId;     // wildpalms.calendar | .contacts | .memo | .todo
    TargetKind kind;
    QString    accountRef;   // empty for RawFiles;
                             // PendingAccount.id for RemoteNew
    QString    collectionId; // resolved by DiscoveryPage; empty for RawFiles
};

struct WizardState {
    QString               profileName;
    QList<PendingAccount> pendingAccounts;
    QList<MappingSpec>    mappings;  // exactly four entries, one per pluginId
};

struct Result {
    WizardState state;        // wizard returns by value on accept()
};

} // namespace WildPalms::Wizard
```

`NewProfileWizard::result()` returns `Result` by value.

### 4.3 Page flow

```
Page 1 NamePage
  ↓ Next
Page 2 TargetPickerPage  (always shown)
  ↓ Next
  ├─ has any "Add new…" pick?  yes → Page 3
  │                            no  → skip
Page 3 AddAccountsPage    (conditional)
  ↓ Next
  ├─ has any remote pick?      yes → Page 4
  │                            no  → skip
Page 4 DiscoveryPage      (conditional)
  ↓ Next  (blocked until every row has a chosen collection — Q5 block-finish)
Page 5 ReviewPage
  ↓ Finish → accept()
```

Skip logic via `QWizard::nextId()` overrides on Pages 2 and 3.

### 4.4 Wizard ctor and dependencies

```cpp
NewProfileWizard(
    WildPalms::Runtime::ProfileRegistry      *registry,    // borrowed
    Kalburator::Sync::BackendRegistry        *backendReg,  // borrowed
    QWidget                                  *parent = nullptr);
```

- `registry` — used by `NamePage` to check the typed name against
  existing profiles for uniqueness.
- `backendReg` — used by `DiscoveryPage` to construct transient
  `IProvider` instances from `PendingAccount.config` via
  `contributionFor(kind)->createProvider(config)`.

`BackendRegistry` doesn't currently exist as a permanent member of
`KF6MainWindow` — today it's owned by `PalmRuntime` per profile.
The wizard needs it before a profile exists. **Resolution:** add an
app-level `BackendRegistry` constructed in `KF6MainWindow`'s ctor,
populated with the same contributions as the per-profile one. See
§10 (reversible choice).

### 4.5 Integration with `KF6MainWindow`

The F.1a stopgap body of `onNewProfile()` is replaced:

```cpp
void KF6MainWindow::onNewProfile()
{
    WildPalms::Wizard::NewProfileWizard wiz(
        m_profileRegistry.get(),
        m_appBackendRegistry.get(),   // new permanent member; see §4.4
        this);
    if (wiz.exec() != QDialog::Accepted) return;

    const auto r = wiz.result();

    const auto entry = m_profileRegistry->registerNew(r.state.profileName);
    if (!entry.isValid()) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not create profile."));
        return;
    }

    if (!writeWizardResultToProfile(entry.path, r)) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not write profile files. Check the log."));
        m_profileRegistry->unregister(entry.id);
        QDir(entry.path).removeRecursively();   // best-effort rollback
        return;
    }

    loadProfile(entry.path);
}
```

`writeWizardResultToProfile(const QString &path, const Result &r)`
is a new private helper on `KF6MainWindow`:

1. Opens `<path>/profile.conf` (the freshly-created file from
   `registerNew`).
2. Writes the `[accounts]` rows by serializing each
   `PendingAccount.config` via the same path
   `AccountController::addProvider` uses today.
3. Builds the `[syncMappings]` JSON array from the wizard's
   `MappingSpec` list (one row per remote-target spec; RawFiles
   specs produce no rows — `finishConnect` auto-generates them).
4. Calls `Profile::save()`. Returns `false` on failure (caller
   rolls back).

The helper lives on `KF6MainWindow` rather than the wizard so the
wizard stays pure-UI (no I/O), in keeping with the transient-model
principle.

## 5. Per-page detail (F.1c.1)

### 5.1 Page 1 — `NamePage`

**Field:** profile name (`QLineEdit`, registered as `name*`, required).

**Validation (`isComplete()`):**
- non-empty after `trimmed()`,
- not already in use by `m_registry->entries()` (case-insensitive),
- inline warning label if duplicate.

**Writes to `WizardState`:** `profileName` on Next.

**No async work.**

### 5.2 Page 2 — `TargetPickerPage`

Four `TargetPickerRow`s stacked vertically (Calendar, Contacts,
Memo, ToDo). Each row shows:

- domain icon,
- domain label + Palm DB name (e.g. "Calendar / DatebookDB"),
- a `QComboBox` of compatible targets.

**Compatible kinds per domain:**

| Row      | Compatible target kinds                |
|----------|----------------------------------------|
| Calendar | RawFiles, CalDAV, Akonadi              |
| Contacts | RawFiles, CardDAV, Akonadi             |
| Memo     | RawFiles only (no remote memo backend) |
| ToDo     | RawFiles, CalDAV (as VTODO), Akonadi   |

**Dropdown options for each row** (populated in `initializePage`):
1. `"Local files (default)"` — always present. Selects
   `TargetKind::RawFiles`.
2. One entry per existing `PendingAccount` whose `kind` matches a
   compatible kind for this row, label format:
   `"<displayName> (<kind uppercase>)"` e.g. `"Fastmail (CALDAV)"`.
3. One `"Add new <kind> account…"` per compatible kind. Selecting
   one appends a fresh `PendingAccount` to `WizardState` (id from
   `QUuid::createUuid()`, empty `BackendConfiguration`), then
   re-selects the dropdown to point at that new account by id.

**Memo row:** dropdown disabled, single entry "Local Markdown files".

**Validation:** always complete (every row has a default
selection). Next is enabled immediately.

**Writes to `WizardState`:** `mappings[*]` (exactly four entries,
keyed by `pluginId`), updated as dropdowns change.

### 5.3 Page 3 — `AddAccountsPage` (conditional)

**Shown only if** `WizardState::pendingAccounts` is non-empty.

One `AccountFormWidget` per `PendingAccount`, stacked vertically.
Each widget:
- shows a header like `"New CalDAV account"`,
- has the kind **locked** (the user committed on Page 2; changing
  kind here would require re-doing the dropdown selection),
- exposes the same fields as `AddAccountDialog` does today for that
  kind (server URL / username / password / display name for
  CalDAV+CardDAV; the Akonadi-specific subset for akonadi),
- emits `validityChanged(bool)` whenever its `isValid()` flips.

**Validation (`isComplete()`):** every embedded
`AccountFormWidget::isValid()` returns true.

**Writes to `WizardState`:** each `PendingAccount.config` populated
from its widget's `configuration()` on Next.

### 5.4 Page 4 — `DiscoveryPage` (conditional)

**Shown only if** any `WizardState::mappings` row has
`kind == RemoteNew` (in v1 — RemoteExisting reserved but unreachable).

**Per row, on `initializePage`:**
- Resolve the account by `accountRef` against
  `WizardState::pendingAccounts`.
- Construct a transient `IProvider` via
  `m_backendRegistry->contributionFor(account.kind)->createProvider(account.config)`.
- Kick off `provider->discoverCollections()` into a
  `QFutureWatcher<QList<CollectionInfo>>` owned by the `DiscoveryRow`.
- Spinner shows "Discovering on `<account.displayName>`…"

**Per-row states and transitions:**

| State        | UI                                                    | Next blocked? |
|--------------|-------------------------------------------------------|---------------|
| Loading      | Spinner + status text + Cancel button (per-row)       | Yes           |
| Loaded(n>0)  | Single-select list (`QListWidget`, radio behavior)    | Until pick    |
| Loaded(n=0)  | Inline "No calendars found on this account" + Retry  | Yes           |
| Failed       | Inline "⚠ Couldn't reach: `<reason>`" + Retry        | Yes           |
| ChosenOK     | Bound `collectionId` written into `WizardState`       | No            |

**Page validation (`isComplete()`):** every row has reached `ChosenOK`.

**No "Create new collection" sub-flow in v1** (out of scope per §2.2).

**Cancel:** `DiscoveryRow` destructor calls `QFutureWatcher::cancel`;
`QWizard::reject()` cascades through child destruction. Provider
instances are short-lived (own the watcher); destruction tears them
down.

**Writes to `WizardState`:** `MappingSpec::collectionId` per row.

### 5.5 Page 5 — `ReviewPage`

Read-only rich-text summary, populated on `initializePage` from
`WizardState`:

```
You're about to create:

   Profile: "Palm m505"

Sync mappings:

   📅 Calendar  →  Fastmail / "Personal" (CalDAV)
   👤 Contacts  →  Local .vcf files
   📝 Memo      →  Local .md files
   ✓  To-do     →  Fastmail / "Tasks" (CalDAV)

New accounts to be created:

   • Fastmail (CalDAV) — server caldav.fastmail.com
```

**Finish:** calls `accept()`. Wizard's `result()` returns the
populated `WizardState`. Persistence happens in `KF6MainWindow`,
per §4.5.

## 6. Mapping output + `finishConnect` change

### 6.1 Wizard-side: wildcard mappings

For each non-RawFiles `MappingSpec`, the wizard writes **one**
`SyncMapping` row into `profile.conf [syncMappings]`:

```json
{
  "id":             "default-<pluginId>-<accountId>-<collectionId>",
  "sourceBackend":  "<pluginId>",
  "targetBackend":  "<accountId>",
  "sourceCalendar": "",
  "targetCalendar": "<collectionId>",
  "mode":           "TwoWay",
  "conflictPolicy": "LastWriteWins",
  "enabled":        true
}
```

(Empty `sourceCalendar` = wildcard: this target covers every Palm
slot for the given pluginId. `conflictPolicy` matches the F.2
sub-project C default.)

For RawFiles picks the wizard writes **no** mapping —
`finishConnect` already auto-generates per-slot RawFiles mappings
for every Palm collection not covered by a user mapping (current
behavior, no change). "User picked Local files" == "user accepted
defaults" == no row needed.

Maximum mapping rows the wizard writes: 4 (one per domain;
RawFiles rows produce 0).

### 6.2 Runtime-side: `PalmRuntime::finishConnect` change

`finishConnect` today iterates Palm collections and emits one
auto-mapping per slot for any not already covered by a user mapping.
The coverage check matches on exact `sourceCalendar` equality.

**Change:** when checking coverage, a user mapping with empty
`sourceCalendar` covers every slot for its `sourceBackend`
(pluginId).

Pseudocode:

```cpp
auto isCovered = [&](const QString &pluginId, const QString &slot) {
    for (const auto &m : userMappings) {
        if (m.sourceBackend != pluginId) continue;
        if (m.sourceCalendar.isEmpty()) return true;    // NEW: wildcard
        if (m.sourceCalendar == slot)   return true;    // existing
    }
    return false;
};
```

~5 lines in `palmruntime.cpp`, around `src/runtime/palmruntime.cpp:305-340`.

**Effect:** the wizard's one wildcard row for "Calendar → Fastmail"
makes `finishConnect` skip its per-slot RawFiles generation for
every Palm calendar category. The Palm calendar plugin's sync runs
against Fastmail for all categories. Per-category routing (different
categories → different targets) remains a manual `MappingEditorDialog`
task (F.3).

**Test:** `tst_palmruntime_wildcard_mapping.cpp` — drive
`finishConnect` with a wildcard mapping fixture, assert no
auto-RawFiles row is emitted for that pluginId.

## 7. Error handling

| Failure | Surfacing | Recovery |
|---|---|---|
| Page 1: duplicate profile name | Inline warning under field; Next disabled | User edits name |
| Page 2: no compatible accounts | Dropdown still has "Local files" + "Add new…"; never an error | n/a |
| Page 3: form field invalid | Per-widget inline marker (existing widget validation) | User fills in |
| Page 4: `discoverCollections()` future fails | Inline "⚠ Couldn't reach…" + Retry; Next blocked | Retry; or Back to Page 3 to fix credentials (re-runs discovery on Next); or Cancel |
| Page 4: success but empty list | Inline "No calendars found"; Retry; Next blocked | Same as above |
| Page 4: `createProvider` throws | Caught at call site; treated as discovery failure | Same |
| Finish: `registerNew` fails | Modal `QMessageBox::critical`; no profile created | User retries via menu |
| Finish: `writeWizardResultToProfile` fails after `registerNew` | Modal critical; **best-effort rollback** (`unregister` + `QDir::removeRecursively`) | User retries |
| Finish: `loadProfile` fails after persistence | Existing `loadProfile` error path; profile registered but not loaded | User picks via File → Switch Profile or unregisters and retries |
| Cancel any page | `QWizard::reject()`; transient state discarded; nothing on disk | n/a |

**No partial state.** The transient model + the rollback in
`onNewProfile` means Finish either succeeds end-to-end or leaves
no profile registered.

## 8. Testing

### 8.1 F.1c.0 (refactor)
- `tests/app/accounts/tst_accountformwidget.cpp` — unit test in
  isolation: kind switching, validation, `configuration()`
  round-trip per kind (including Akonadi's kind-specific subset).
- Existing `tst_addaccountdialog.cpp` stays green; it now exercises
  the dialog wrapper over the new widget.

### 8.2 F.1c.1 (wizard) — per-page units

`tests/app/wizard/` mirrors the file layout:

| Test file | Coverage |
|---|---|
| `tst_namepage.cpp` | Empty/whitespace name; duplicate vs registry (case-insensitive); field propagation on Next. |
| `tst_targetpickerpage.cpp` | Dropdown population from stub `WizardState` (zero accounts, one CalDAV, mixed); Memo row disabled; "Add new…" appends to `pendingAccounts`; selecting it re-selects by id. |
| `tst_addaccountspage.cpp` | Page skipped via `nextId()` when no pending accounts; stacked widgets render; Next disabled until every widget is valid; kind-locked header. |
| `tst_discoverypage.cpp` | Per-row state transitions (Loading → Loaded/Empty/Failed → ChosenOK); block-finish; Retry re-runs; Cancel cancels in-flight `QFutureWatcher`. Uses `StubProvider`. |
| `tst_reviewpage.cpp` | Summary rendering for four fixtures (all local, one remote, mixed, multiple remote). |

### 8.3 F.1c.1 — E2E and integration

| Test file | Coverage |
|---|---|
| `tst_newprofilewizard.cpp` | E2E: three flows — all-local happy path, one-remote with successful discovery, one-remote with "Add new…" account. Asserts `result()` returns expected `WizardState`; Cancel discards. Uses `WILDPALMS_QTEST_MAIN` (nested event loop). |
| `tst_kf6mainwindow_newprofile.cpp` | Integration. Test seam: virtual `runProfileWizard()` on `KF6MainWindow` returns a pre-built `Result`; production override runs `NewProfileWizard::exec()`. Verifies `registerNew` + `writeWizardResultToProfile` + `loadProfile` chain. Verifies rollback when write fails. |
| `tst_palmruntime_wildcard_mapping.cpp` | Narrow: wildcard `sourceCalendar` suppresses per-slot RawFiles auto-mapping for the matching pluginId; concrete `sourceCalendar` behaves as before. |

### 8.4 Stubs

`tests/app/wizard/stubs/`:
- `stubbackendregistry.{h,cpp}` — implements only what the wizard
  pages need (`contributionFor(kind)`).
- `stubcontribution.{h,cpp}` — `BackendContribution` returning a
  `StubProvider`.
- `stubprovider.{h,cpp}` — `IProvider` with controllable
  `discoverCollections()` outcomes (immediate success / empty /
  failure; deferred-resolve via signal for cancel-during-flight
  tests).

### 8.5 What's NOT tested here

- Network behavior against real CalDAV / CardDAV / Akonadi servers
  (belongs to F.4 / Radicale E2E).
- `QWizard` chrome (Qt6 ships it).
- POSE64-driven Palm device interactions (cancelled per E.18).

## 9. Success criteria

1. **Discoverability.** From a fresh app launch with no
   pre-configured accounts, a user can create a profile syncing
   Palm Calendar to a fresh Fastmail CalDAV account in under
   90 seconds without consulting documentation.
2. **Correctness.** The `profile.conf` written by the wizard loads
   cleanly via the existing `loadProfile` path; no manual edits
   needed before the first sync.
3. **Robustness.** Discovery failures don't strand the user behind
   a stale spinner — Retry always available, Cancel always
   discards cleanly.
4. **No regressions.** Existing File → Open / Close / Configure
   Mappings flows are untouched. Settings → Accounts behavior
   identical before and after F.1c.0.
5. **Wildcard semantics solid.** With one wildcard mapping
   present, `finishConnect` emits zero auto-RawFiles rows for that
   pluginId; with no wildcard, behavior is unchanged.
6. **Test coverage.** All five page-level units + the three e2e
   flows + the MainWindow integration + the runtime wildcard test
   land green. Existing WildPalms ctest suite stays green.

## 10. Reversible architectural choices

Two decisions made during brainstorming were called out for explicit
documentation so a future maintainer can revisit without archaeology.
Both are reversible without breaking the public API of the wizard.

### 10.1 App-level `BackendRegistry` permanent member

**Decision:** add a `m_appBackendRegistry` member to `KF6MainWindow`
constructed in its ctor, in parallel with the existing per-profile
`BackendRegistry` owned by `PalmRuntime`.

**Why:** the wizard needs a `BackendRegistry` to construct transient
`IProvider`s for discovery, before any profile is loaded (and
therefore before `PalmRuntime` exists). Two alternatives were
considered:
- *Wizard constructs its own short-lived registry.* Workable but
  duplicates contribution-registration logic.
- *Hoist the per-profile registry to app-level entirely.* Larger
  refactor; touches `PalmRuntime`, `BackendRegistry` lifetime, and
  every backend that today registers per-profile.

The chosen middle path is the least invasive and isolates the
wizard's needs from runtime-side architecture.

**When to revisit:**
- If KWallet integration (deferred sub-project) needs app-level
  backend lifecycle anyway, the second alternative (full app-level
  hoist) may become cheaper.
- If the per-profile registry diverges from app-level (different
  contributions enabled per profile), maintaining two registries
  becomes a contradiction.

### 10.2 `PendingAccount.id` is a wizard-local UUID, reused on persistence

**Decision:** `PendingAccount.id` is generated by
`QUuid::createUuid().toString()` at the moment the user picks "Add
new…" on Page 2. The same id becomes the on-disk account id in
`profile.conf [accounts]` when the wizard finishes.

**Why:** reusing the id avoids a rename step at persistence time
and keeps `MappingSpec::accountRef` (which references the id) valid
across the transient → persisted transition.

**When to revisit:**
- If `AccountController`'s on-disk id scheme grows constraints the
  wizard's UUID doesn't satisfy (e.g., human-readable ids derived
  from displayName), the wizard would need a rename step at Finish.
- If accounts become globally addressable across profiles, the id
  scheme will need rethinking anyway — at that point the wizard's
  UUID convention is one small adjustment among several.

## 11. Open implementation points (for the plans to resolve)

- **F.1c.0: where exactly does the kind-aware field set live?**
  Today `AddAccountDialog` switches its internal layout based on
  the selected contribution. The refactor needs to preserve this.
  Options: (a) `AccountFormWidget` owns the kind-switching internally
  (matches today's dialog behavior); (b) one `AccountFormWidget`
  per kind, with a parent dispatcher. Plan picks (a) unless it
  uncovers a reason for (b).
- **F.1c.1: test seam name on `KF6MainWindow`.** §8.3 proposes
  `runProfileWizard()` as a virtual stubbable in tests. Verify
  it doesn't clash with the existing virtual surface
  (`showProfilePickerStopgap`, `confirmForgetProfile`,
  `runLoadProfileForTest`, etc.). Pick a clash-free name.
- **F.1c.1: discovery row Cancel semantics.** Per-row Cancel
  (during Loading state) should put the row into Failed state
  with a "Cancelled by user" message and a Retry button —
  not back to a blank initial state. Plan confirms this matches
  what users expect.
- **F.1c.1: where does the `app-level BackendRegistry` get
  populated?** Today contributions are registered during
  `PalmRuntime` construction via plugin discovery. The app-level
  registry needs the same population path called from
  `KF6MainWindow`'s ctor. The plan extracts a free function or
  static method `registerStandardContributions(BackendRegistry*)`
  callable from both places.
- **F.1c.1: Akonadi resource lifecycle during the wizard.**
  Creating an Akonadi account today may spawn an Akonadi resource
  process. The transient model says no persistence until Finish —
  does that apply to the Akonadi resource as well? Plan
  investigates; worst case, the spec adds an "Akonadi resources
  created during wizard are torn down on Cancel" requirement.

## 12. References

- Phase F roadmap: `docs/plans/2026-04-20-libkalburator-integration.md` §Phase F.
- F.1a: `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md` ✅
- F.1b: `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md` ✅
- F.2: `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` ✅
- Superseded ancestor: `docs/superpowers/specs/2026-05-21-f1-new-profile-wizard-design.md` (page-structure ideas reused with adjustments)
- `AddAccountDialog`: `src/app/accounts/addaccountdialog.{h,cpp}` (F.1c.0 refactor target).
- `ProfileRegistry`: `src/runtime/profileregistry.h`.
- `AccountController`: `src/runtime/accountcontroller.h`.
- `PalmRuntime::finishConnect`: `src/runtime/palmruntime.cpp:298-345` (wildcard-honoring change site).
- libkalburator providers: `~/dev/libkalburator/src/plugin/{caldav,carddav,akonadi}providerplugin.{h,cpp}`.
- `IProvider::discoverCollections`: libkalburator `src/plugin/iprovider.h`.
