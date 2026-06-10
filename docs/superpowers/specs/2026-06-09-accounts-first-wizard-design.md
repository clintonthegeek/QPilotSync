# Accounts-first profile wizard — design

**Date:** 2026-06-09
**Status:** Approved
**Branch:** feature/three-tier-sync

## Problem

The New Profile wizard's account/backend linking flow is garbled. After naming a
profile, the user faces four per-conduit dropdowns whose items include
"Add new CalDAV account…" / "Add new Akonadi account…". Selecting one of these
*mutates the dropdown's own item list* (inserting a UUID-labeled row) instead of
opening anything; the user must reopen the dropdown and pick the new row. Worse,
configuring an "Akonadi" account presents a CalDAV credential form.

### The literal bug chain (Akonadi → CalDAV form)

1. `CMakeLists.txt:59` — `KALBURATOR_HAVE_AKONADI` defaults **OFF** (and is OFF
   in the build cache), so `registerStandardContributions()`
   (`src/runtime/standardcontributions.cpp`) registers only CalDAV and CardDAV;
   `AkonadiBackendContribution` is `#ifdef`'d away.
2. `TargetPickerPage::buildRows()` (`src/app/wizard/targetpickerpage.cpp:37-52`)
   **hardcodes** `"akonadi"` as a compatible kind for calendar/contacts/todo,
   offering a provider that does not exist in the binary.
3. `AccountFormWidget::buildUi()` with `lockedKind = "akonadi"` finds no matching
   contribution (`lockedIndex` stays -1) and falls through to `onKindChanged(0)`
   (`src/app/accounts/accountformwidget.cpp:95-97`) — silently showing the
   **first** contribution's config form (CalDAV), kind combo left visible.

### Paradigm mistakes (the structural problem)

1. **The wizard duplicates knowledge the library exposes.** libkalburator's
   `BackendContribution::nativeShapes()` declares which domains a provider kind
   serves; `CollectionInfo::type`/`contentTypes` classify discovered collections
   by domain. The wizard ignores both and ships a hand-rolled `{domain → kinds}`
   table that can (and does) drift from the build.
2. **Accounts are made subordinate to conduits; libkalburator's model is the
   inverse.** An account (`IProvider`) is a connection to a *store*: one account →
   many collections → one backend per collection, across domains. The wizard
   spawns accounts from inside a single domain's dropdown, so sharing one
   Nextcloud account across three conduits requires creating it in one row and
   re-selecting a raw-UUID-labeled entry in the others.
3. **Dropdown-as-action-menu.** "Add new X account…" as a combo item conflates
   choosing a kind, creating an account, and binding the mapping, and defers
   actual configuration to a later page (`AddAccountsPage`), with a separate
   per-mapping `DiscoveryPage` after that.
4. **Akonadi mischaracterized as a credentialed remote.** Akonadi is a local
   access point: no URL, no credentials, no Test Connection; at most one per
   machine. The library models this (minimal `AkonadiConfigWidget`,
   `NeutralProvider` for account-less backends); the wizard forces it through
   the remote-credential pipeline.
5. **Hand-rolled lifecycle.** PlanStan (reference consumer) rides
   `ProviderLifecycle`/`ProviderManager`; WP's `AccountController` partially
   hand-rolls this. Out of scope here (see Non-goals), but it explains why
   discovery was bolted on per-mapping.

## Decisions

| Decision | Choice |
|---|---|
| Account scope | **Per-profile** — accounts stay in `profile.conf` (`Profile::accounts()`); persistence shape unchanged |
| Akonadi | **Enable now** — `KALBURATOR_HAVE_AKONADI=ON`, verify KPim6 deps |
| Scope | **Wizard + Settings** — account dialog is one reusable unit; Settings `AccountsPage` inherits fixes |
| Approach | **Accounts-first wizard** (vs. minimal repair, vs. ProviderLifecycle adoption first) |

## Design

### 1. Page flow

```
Page 1  Name        (unchanged)
Page 2  Accounts    NEW — replaces TargetPicker's add-new staging
Page 3  Bindings    REWORKED TargetPickerPage — picks (account, collection) per conduit
Page 4  Review      (role unchanged; summary text updated)
```

`AddAccountsPage`, `DiscoveryPage`, and `DiscoveryRow` are **deleted**. Page 2 is
skippable: Next with zero accounts = local-files-only profile (today's default
path). The `PendingAccount` staging struct and `__add_new__:` sentinel-item
machinery in `WizardState`/`TargetPickerRow` are deleted with them.

### 2. Page 2 — Accounts

A list of accounts created for this profile plus **Add Account…** / **Edit** /
**Remove** buttons. Add opens the existing `AddAccountDialog`
(`src/app/accounts/addaccountdialog.cpp`); its kind combo is populated from
`registry->contributions()`, so a build without Akonadi never offers Akonadi.

On dialog accept, the wizard constructs the provider via its contribution,
calls `provider->load(cfg)` then `provider->connect()` async. Each list row
shows live status: *Connecting… / Connected, N collections / Failed: <error>*.
This replaces `DiscoveryRow` one-for-one but runs **once per account**, not once
per mapping. The wizard owns these transient providers (same app-level
`BackendRegistry` seam as today, F.1c §10.1); they are discarded on finish.
`AccountController` reconstructs real providers at profile load — unchanged.

### 3. Page 3 — Bindings

Four conduit rows (calendar, contacts, memo, todo). Each dropdown contains only
real, immediately-valid choices:

- *Local files (default)*
- One entry per matching collection across all connected accounts:
  `"<account displayName> ▸ <collection name>"`, filtered per domain via
  `CollectionInfo::type`/`contentTypes`:
  calendar → type "calendar" / VEVENT; todo → "todos" / VTODO;
  contacts → "contacts" / VCARD; memo → "memos".
- `readOnly` collections are shown but marked and non-selectable (Palm→remote
  requires writability).

No sentinel items; no dropdown self-mutation. Going Back to add more accounts is
the escape hatch when the wanted collection isn't listed. One pick = one
complete mapping (`accountRef` + `collectionId` resolved at selection time).
Rows whose domain has no compatible collections while accounts exist show a
hint line ("No calendar collections found on your accounts").

Memo today ends up local-files-only automatically (no registered contribution
serves a "memos" collection type) — derived, not hardcoded.

### 4. Finish — persistence (downstream contract unchanged)

`writeWizardResultToProfile()` (`src/kf6/kf6mainwindow.cpp`) keeps writing the
same two artifacts:

- `Profile::setAccounts(QList<BackendConfiguration>)` — one entry per account
  (id = account UUID, type = provider kind, connectionParams).
- Sync-mappings JSON — `targetBackend` = account UUID, `targetCalendar` =
  collection id.

`PalmRuntime::loadMappingsFromProfile()`, `buildRouteLogicalCalendars()`, and
`translateRouteSpec()` are untouched. This is a front-of-house fix only.

### 5. AccountFormWidget hardening + Settings convergence

- The locked-kind constructor and its silent first-contribution fallback
  (`accountformwidget.cpp:30-37, 91-97`) are **removed**; nothing needs
  kind-locking once `AddAccountsPage` is gone. An unavailable requested kind is
  a hard error, never an impersonation.
- Akonadi uses the lib's minimal `AkonadiConfigWidget`. The *Test Connection*
  button stays for **all** kinds: `IProvider::connect()` is the universal verb,
  and for Akonadi it legitimately verifies the daemon is reachable and
  enumerates collections. (No `isLocal()`/`requiresCredentials()` API exists on
  `IProvider`/`BackendContribution`, and none is needed; if the lib ever grows
  one, the label can specialize — not a dependency of this work.)
- Settings → Accounts (`AccountsPage` + `AccountController`) already uses
  `AddAccountDialog` and inherits every fix. Post-wizard conduit rebinding stays
  in the existing mapping graph (`SyncMappingsPage`) — out of scope beyond
  verifying it still round-trips.

### 6. Akonadi enablement

Flip `KALBURATOR_HAVE_AKONADI=ON` (`CMakeLists.txt:59`); verify KPim6 Akonadi
dev packages installed; rebuild. With registry-driven UI, no further wiring:
the contribution registers, its `nativeShapes()` declare calendar+contacts
coverage, and "Akonadi" appears in the Add Account dialog credential-free.

### 7. Error handling

- Account `connect()` fails on Page 2 → row shows the provider's `error()`
  text; account can be edited/removed; wizard can proceed (account contributes
  no collections).
- Connected account with zero domain-matching collections → contributes nothing
  to that row; hint line as above.
- Akonadi compiled in but daemon not running → ordinary connect failure, same
  path.

### 8. Testing

Existing wizard tests adapt via the `runProfileWizard()` virtual seam. Tests
covering dropdown staging / `PendingAccount` / Discovery are deleted with their
classes. New coverage:

- (a) Registry-driven offering: no Akonadi contribution ⇒ no Akonadi in the
  Add Account dialog.
- (b) Collection filtering per domain, driven by a fake `IProvider` with canned
  `collections()` (no network).
- (c) Persistence parity: a finished wizard writes the same `profile.conf`
  shape as before (accounts + mappings JSON).
- (d) Locked-kind removal is compile-time.

## Non-goals

- `ProviderLifecycle` adoption in `AccountController` (Approach 3) — follow-up.
- App-wide/shared account store — rejected for now; per-profile persists.
- "Create new collection on server" sub-flow — still out of scope (F.1c §2.2).
- Mapping-graph (SettingsDialog) redesign — untouched except round-trip checks.
