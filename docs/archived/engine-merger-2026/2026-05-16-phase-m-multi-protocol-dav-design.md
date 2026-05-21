# Phase M — Multi-protocol DAV provider + UI lift

**Date:** 2026-05-16
**Status:** design (plan to follow as
`2026-05-16-phase-m-multi-protocol-dav-plan.md`)
**Deferred-work catalog item:** B.5 (Combined multi-protocol provider,
Nextcloud-style) — partial overlap with D.1 (PlanStan CardDAV
add-account UI; closes as a side effect of the UI lift).
**Tag (proposed):** `v0.42-phase-m-multi-protocol-dav`

## Goal

Ship a single libkalburator `IProvider` that lets a user add one
"Nextcloud-style account" — one base URL, one set of credentials —
and use both CalDAV and CardDAV from that single configuration.

The phase also lifts account-management UI chrome (provider-config
dialog, accounts list widget, collection picker) from WildPalms /
PlanStan into libkalburator so both consumers host the same widgets.
The library has already committed to shipping per-provider config
widgets (`CalDavConfigWidget`, `CardDavConfigWidget`,
`AkonadiConfigWidget`); Phase M extends that commitment to the chrome
around them.

PlanStan picks up CardDAV + Akonadi + the new multi-protocol provider
as combo entries the moment it hosts the lifted dialog. That closes
deferred-work item D.1 ("PlanStan: CardDAV add-account UI") as a side
effect — without writing a CardDAV-specific PlanStan UI.

## Motivation

`IProvider`'s docstring at `iprovider.h:25` always advertised
"Nextcloud (one server speaking CalDAV+CardDAV)" as an example. The
H + Ib implementation shipped two providers (`CalDavProvider`,
`CardDavProvider`) that work fine pointed at the same server but
present as two unrelated accounts in the UI. For Nextcloud users
this is a UX cut: they think of their server as one account, not
two, and they re-enter the same URL and password twice.

The architectural pressure is real and forward-looking: every
future multi-domain provider (Google, Microsoft Graph, JMAP,
iCloud) is "one account, N domains, shared transport" by nature
of the underlying API. Phase M establishes the design pattern
once, against the cheapest example (Nextcloud is two
well-understood DAV protocols on one server), so later providers
follow precedent rather than reinventing.

## Architecture

### Provider categories (orientation)

The `IProvider` abstraction does not require credentials or even
the concept of an "account". It is a registration shape — a
configurable source of backends, exposed through `ProviderManager`.
Concrete providers fall into three categories that already coexist
in the codebase:

1. **Account-shaped providers** — credentials + remote endpoint
   (`CalDavProvider`, `CardDavProvider`, the new
   `MultiProtocolDavProvider`, future `GoogleProvider`/
   `MicrosoftProvider`). Config widget collects credentials.

2. **Accountless providers** — config is "where", not "who"
   (`AkonadiProvider` from Phase L; a hypothetical
   `LocalCalendarProvider` reading a flat `.ics` directory). Config
   widget collects a path, a display name, or nothing at all.

3. **Non-provider backends** — instantiated by runtime detection,
   not user-add-account flow (the HotSync USB/serial Palm backends).
   Registered as plain `BackendContribution` without a paired
   `ProviderContribution`. Live entirely outside `ProviderManager`.

Phase M operates exclusively in category 1. The phase does not
change `IProvider`, does not change how categories 2 or 3 are
modeled, and does not push toward unifying the three lanes.

### Chosen approach — concrete standalone class with shared DAV helpers

Three architectural shapes were considered in brainstorming
(2026-05-16):

- **A. Compositional aggregator** — `MultiProtocolDavProvider`
  holds owned `CalDavProvider` + `CardDavProvider` instances and
  federates `collections()` / `createBackend()`. Rejected:
  decomposes naturally for DAV (two real protocols) but breaks for
  Google/MS Graph (one API, multiple typed sub-resources — no
  natural single-domain leaves). Forces credential and OAuth-state
  duplication.
- **B. Concrete standalone class** with shared helpers extracted
  out of the existing single-protocol providers. **Chosen.**
  Generalises to every realistic future provider (one
  `IProvider` = one account; provider owns its auth/transport
  state; sub-API surfaces are internal). Bounded duplication via
  helpers.
- **C. Plugin-level composition** — one contribution registers two
  providers tagged with a shared account id, ProviderManager grows
  group-by-accountId. Rejected: grows the data model, papers over
  the "one account / N providers" fiction at every UI surface, and
  fails the Google/MS test for the same reason as A.

The decisive argument for B is the future provider list. Google
and Microsoft Graph are one API endpoint speaking many typed
sub-resources, sharing OAuth state, sharing rate limits. Either
they are one `IProvider` (Option B) or they are an artificial
decomposition into multi-IProvider groups that have to
re-coordinate everything the underlying SDK already coordinates
(Options A/C). The cheap example (Nextcloud) is the place to
establish the pattern with the least new infrastructure.

Option B's main cost — code duplication between
`CalDavProvider`'s discovery and `MultiProtocolDavProvider`'s
CalDAV half — is fully mitigated by extracting shared DAV
helpers in this phase. The existing single-protocol providers
become consumers of the helpers too; they get cleaner as a side
effect.

## Library file layout

```
libkalburator/src/sync/dav/                # NEW directory
  discovery.h / discovery.cpp              # NEW: well-known + PROPFIND helpers
  collectionenum.h / collectionenum.cpp    # NEW: principal → calendar/addressbook list

libkalburator/src/sync/
  caldavprovider.h / .cpp                  # REFACTORED to use src/sync/dav/
  carddavprovider.h / .cpp                 # REFACTORED to use src/sync/dav/
  multiprotocoldavprovider.h               # NEW
  multiprotocoldavprovider.cpp             # NEW
  multiprotocoldavconfigwidget.h           # NEW
  multiprotocoldavconfigwidget.cpp         # NEW

libkalburator/src/ui/                      # NEW directory
  providerconfigdialog.h / .cpp            # NEW: generic add/edit account dialog
  accountslistwidget.h / .cpp              # NEW: list of configured accounts
  collectionpickerwidget.h / .cpp          # NEW: discovered-collection selector

libkalburator/src/plugins/
  stock_plugins.cpp                        # MODIFIED: register new provider
  multiprotocoldavproviderplugin.h         # NEW
  multiprotocoldavproviderplugin.cpp       # NEW

libkalburator/tests/sync/dav/              # NEW
  tst_dav_discovery.cpp                    # NEW: helper unit tests
  tst_multiprotocol_dav_discovery.cpp      # NEW: provider integration test

libkalburator/tests/ui/                    # NEW
  tst_providerconfigdialog.cpp             # NEW: dialog with fake provider
  tst_accountslistwidget.cpp               # NEW
  tst_collectionpickerwidget.cpp           # NEW
```

### Shared DAV helpers

`src/sync/dav/discovery.{h,cpp}` exposes free functions (not a
class) with explicit parameters — no hidden state:

```cpp
namespace Kalburator::Dav {

struct PrincipalDiscoveryResult {
    QUrl principalUrl;
    QString errorString;
    bool ok() const { return errorString.isEmpty(); }
};

// Probes <baseUrl>/.well-known/{caldav,carddav} and PROPFINDs current-user-principal.
// Returns the principal URL or an explanatory error.
QFuture<PrincipalDiscoveryResult>
    discoverPrincipal(QNetworkAccessManager *nam,
                      const QUrl &baseUrl,
                      DavProtocol protocol,           // Caldav | Carddav
                      const QString &username,
                      const QString &password);

struct CollectionListResult {
    QList<CollectionInfo> collections;
    QString errorString;
    bool ok() const { return errorString.isEmpty(); }
};

// Given a principal URL, PROPFINDs the home-set (calendar-home-set or
// addressbook-home-set) and enumerates collections. Tags each with the
// appropriate CollectionInfo::type (calendar / contacts).
QFuture<CollectionListResult>
    enumerateCollections(QNetworkAccessManager *nam,
                         const QUrl &principalUrl,
                         DavProtocol protocol,
                         const QString &username,
                         const QString &password);

} // namespace
```

`CalDavProvider` and `CardDavProvider` collapse onto these helpers
verbatim; their existing tests (`tests/sync/tst_caldavprovider`,
`tests/sync/tst_carddavprovider`) pin the no-regression contract.

### `MultiProtocolDavProvider`

```cpp
class MultiProtocolDavProvider : public IProvider {
    Q_OBJECT
public:
    static constexpr const char *kProviderId = "multiproto-dav";

    explicit MultiProtocolDavProvider(QObject *parent = nullptr);

    // IProvider
    QString id() const override { return kProviderId; }
    QString displayName() const override;
    QFuture<bool> connect(const BackendConfiguration &config) override;
    QList<CollectionInfo> collections() const override;
    SyncBackend *createBackend(const QString &collectionId,
                               const BackendConfiguration &config,
                               QObject *parent) override;
    QWidget *createConfigWidget() override;

private:
    // Shared transport state — one QNAM, one credential set
    std::unique_ptr<QNetworkAccessManager> m_nam;
    QString m_baseUrl;
    QString m_username;
    QString m_password;

    // Discovered state after connect()
    QUrl m_caldavPrincipal;
    QUrl m_carddavPrincipal;
    QList<CollectionInfo> m_collections;  // mixed calendar + contacts
};
```

Collection id scheme: `multiproto-dav:cal:<server-collection-id>` for
calendars, `multiproto-dav:contacts:<server-collection-id>` for
addressbooks. `createBackend()` dispatches by prefix to construct
either a `RemoteCalendarBackend` or `RemoteContactsBackend`, reusing
the per-collection backend classes verbatim (no new backends needed
— the work is provider-side enumeration + dispatch).

### Discovery flow

```
User opens "Add Account" dialog → picks "Multi-protocol DAV"
  ↓
MultiProtocolDavConfigWidget collects:
  - Display name
  - Base URL (e.g., https://cloud.example.com)
  - Username
  - Password / app password
  - [Advanced] Manual CalDAV principal URL  ← override
  - [Advanced] Manual CardDAV principal URL ← override
  ↓
Provider.connect(config):
  if manual_caldav_principal set:
    m_caldavPrincipal = manual_caldav_principal
  else:
    m_caldavPrincipal = await discoverPrincipal(baseUrl, Caldav)
  if manual_carddav_principal set:
    m_carddavPrincipal = manual_carddav_principal
  else:
    m_carddavPrincipal = await discoverPrincipal(baseUrl, Carddav)
  m_collections = await enumerateCollections(m_caldavPrincipal,  Caldav)
                + await enumerateCollections(m_carddavPrincipal, Carddav)
  return ok && (m_collections non-empty)
```

Both halves of the discovery run in parallel via `QtConcurrent` /
`QFuture` composition. Partial success is surfaced: if CalDAV
discovery succeeds and CardDAV fails (or vice versa), the provider
returns the calendars it found *and* propagates a non-fatal warning
via a new `lastWarning()` accessor on the provider. The UI shows the
warning above the collection picker. The user can still proceed with
the half that works.

### Coexistence with existing providers

`CalDavProvider` and `CardDavProvider` remain. Users with only a
CalDAV server (no addressbooks at all) still want
`CalDavProvider` standalone. Users who manually configure two
unrelated DAV servers — one for calendar, one for contacts —
still want the two-provider model. `MultiProtocolDavProvider` is
the third option for the common "one server, both protocols" case;
it is not a replacement.

The add-account dialog combo will offer all three: "CalDAV",
"CardDAV", "Multi-protocol DAV (Nextcloud-style)".

## UI lift — three new library widgets

### `ProviderConfigDialog`

Replaces the chrome of WildPalms's `AddAccountDialog`. Constructor
takes a `ProviderManager *`. Shows a provider combo populated from
`ProviderManager::registeredProviderIds()`; selecting a provider
embeds `provider->createConfigWidget()` inline. "Test" button runs
`provider->connect(config)` and renders the resulting
`CollectionInfo` list via `CollectionPickerWidget`. "Save" returns
the populated `BackendConfiguration` to the caller; the caller
persists it (consumer-specific storage).

```cpp
class ProviderConfigDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { AddNew, EditExisting };
    ProviderConfigDialog(ProviderManager *manager,
                         Mode mode,
                         const BackendConfiguration &existing = {},
                         QWidget *parent = nullptr);

    BackendConfiguration result() const;  // valid after accept()
    QList<CollectionInfo> selectedCollections() const;
};
```

### `AccountsListWidget`

Embeddable `QWidget`. Constructor takes a `ProviderManager *` and a
configuration source (a callback or signal that emits the current
`QList<BackendConfiguration>`). Renders one row per account with:

- Provider icon (from `IProvider::icon()` — new optional accessor,
  defaults to a generic gear)
- Display name
- Provider type ("CalDAV", "Multi-protocol DAV", etc.)
- Enabled checkbox (writes through to `BackendConfiguration::enabled`)
- Edit button (opens `ProviderConfigDialog` in `EditExisting` mode)
- Remove button

Emits signals: `accountAddRequested()`, `accountEdited(id,
newConfig)`, `accountRemoved(id)`, `accountEnabledChanged(id, bool)`.
The consumer hosts the widget and connects to the signals to drive
its persistence.

### `CollectionPickerWidget`

Embeddable. Takes a `QList<CollectionInfo>` and a current
"selected" set. Renders one checkbox per collection, grouped by
type ("Calendars", "Address Books"). Emits `selectionChanged(set)`.
Used inside `ProviderConfigDialog` after a successful test/discover.

### What stays consumer-side

- **The settings dialog host** (where these widgets are embedded).
  WildPalms uses its existing `SettingsDialog` infrastructure;
  PlanStan integrates into its own UI surface. The library is
  agnostic about the host.
- **The mapping editor.** WildPalms's `MappingRowDialog` binds a
  collection → Palm category slot (4 fixed enums:
  DateBook/Address/Memo/ToDo); PlanStan binds a collection → a
  logical calendar (dynamic per-database list). Different targets,
  different shapes. The library has no opinion.
- **Persistence.** WildPalms persists to KF6Settings; PlanStan to
  SQLite. The library returns `BackendConfiguration` instances; the
  consumer decides where bytes land.

## Consumer migration

### WildPalms (small)

`AddAccountDialog` becomes a near-empty subclass of (or replaced
inline by) `ProviderConfigDialog`. `AccountsPage` becomes a thin
host of `AccountsListWidget` + button to launch
`ProviderConfigDialog` in `AddNew` mode. `MappingRowDialog` and
all calendar/contacts-to-slot mapping code unchanged.

Net change: ~150 LOC deleted from WildPalms's UI files; ~30 LOC
added (signal wiring). Tests stay green.

### PlanStan (small but higher-risk)

The existing CalDAV add-account flow in
`PlanStan/src/controllers/collectioncontroller.cpp:1691-1707` is
replaced with an instantiation of `ProviderConfigDialog`. The
provider combo offered to PlanStan users now includes CardDAV,
Akonadi, and Multi-protocol DAV — picking any of them runs through
the same flow as the existing CalDAV path. PlanStan's
logical-calendar mapping UI (which fires after the dialog
returns) is unchanged: it receives a `BackendConfiguration` +
`QList<CollectionInfo>` and binds them to logical calendars
exactly as today.

The CalDAV add-account flow is in production-shape and the
rewrite must not regress feature behavior. Mitigation:

- A discrete test gate ("PlanStan CalDAV: add → discover → bind →
  sync round-trip") added before the migration and rerun after
- If the rewrite proves larger than expected during implementation,
  PlanStan's migration carves out as Phase M.5 — Nextcloud still
  ships in Phase M, WildPalms still migrates, PlanStan stays on
  its existing CalDAV flow until M.5 lands

D.1 closure note: the deferred-work catalog item asks for a
"CardDAV add-account UI in PlanStan". With the lifted dialog,
PlanStan's account-add UI is provider-polymorphic — CardDAV
support is a combo entry, not a separate code path. D.1's
acceptance criteria collapse to "the existing dialog now offers
CardDAV" which is automatic once the migration lands.

### Net WildPalms + PlanStan code change

Expected ~300 LOC deleted across both consumers; ~50 LOC added
(library widget hosting + signal wiring). Library gains ~700 LOC
(helpers + provider + 3 widgets) + ~400 LOC tests.

## Persistence

No schema change. `BackendConfiguration` already has a
`connectionParams` `QVariantMap` that holds provider-specific
configuration (URL, username, password for DAV providers).
`MultiProtocolDavProvider` uses the same keys as `CalDavProvider`
plus optional `manualCaldavPrincipal` / `manualCarddavPrincipal`
override keys. Existing WildPalms and PlanStan persistence paths
serialize the map opaquely; no consumer-side changes needed.

Migration from two separate (CalDAV + CardDAV) accounts pointing
at the same server into one multi-protocol account is **not**
automated. The user-facing migration path is: add a new
multi-protocol account, verify it works, remove the two old ones.
The deferred-work catalog tracks this as "manual" for now; if
real users hit it often, B.5 close-out can add a migration prompt
in a follow-up phase.

## Tests

### Library tests

- `tst_dav_discovery` — unit tests against an in-process
  `QHttpServer` fake. Covers `.well-known` redirect handling,
  `current-user-principal` PROPFIND, error paths (401, 404,
  malformed XML, timeout).
- `tst_multiprotocol_dav_discovery` — integration test using a
  `FakeNextcloudServer` (subclass of the existing
  `FakeCalDavServer` + `FakeCardDavServer`, served from a single
  port with both protocols' principal URLs under one virtual
  host). Asserts: parallel discovery succeeds; partial-failure
  surface (kill the addressbook half mid-test) propagates a
  warning but does not fail discovery overall; manual-principal
  overrides bypass `.well-known` correctly.
- `tst_providerconfigdialog` / `tst_accountslistwidget` /
  `tst_collectionpickerwidget` — Qt Test against a
  `MockProvider` fixture (returns canned collections without
  network). Asserts: combo populates from
  `ProviderManager::registeredProviderIds()`; embedded config
  widget round-trips through `BackendConfiguration`; signals fire
  on user actions.

### Consumer tests

- WildPalms: existing `tst_addaccountdialog` and
  `tst_accountspage` adapt to the new widgets; new
  `tst_settingsdialog_with_lifted_widgets` smoke test.
- PlanStan: `tst_collectioncontroller_carddav_add` (new — exercises
  the D.1 close-out path) and `tst_collectioncontroller_caldav_add`
  (updated — pins the no-regression CalDAV behavior).

### Real-server gate

The phase tag does not block on a live Nextcloud round-trip — the
fake-server pair is the engine for test pass. A successful manual
test against a real Nextcloud instance is recorded in `FINDINGS.md`
as the closing-status entry (consistent with how Phase L recorded
its live-Akonadi verification).

## Provider categories — non-goals

The phase **does not**:

- Unify the three lanes (account-shaped / accountless /
  non-provider) into a single abstraction. They remain distinct.
- Touch `IProvider`'s contract except for two optional additions:
  `lastWarning()` (for partial-discovery-success surfacing) and
  `icon()` (returns a `QIcon` for the accounts-list row;
  default-implemented to a generic gear so existing providers
  don't need updates).
- Add a generic "account" object orthogonal to `IProvider`. The
  provider *is* the account.
- Introduce OAuth, token refresh, or any auth flow beyond HTTP
  basic / app-password. Future OAuth-shaped providers (Google,
  Microsoft) will add their own auth infrastructure in their own
  phases.

## Risks and mitigations

| Risk | Likelihood | Mitigation |
|---|---|---|
| PlanStan CalDAV rewrite regresses production behavior | medium | Discrete test gate + carve-out plan (Phase M.5) if scope grows |
| Shared-helpers extraction breaks existing CalDav/CardDav tests | low | Existing tests pin the contract; refactor lands as task-1, tests run before task-2 |
| Partial-discovery semantics confuse users ("why does my account show only calendars?") | medium | Inline warning in dialog above collection picker; FINDINGS entry documents the UX |
| QNetworkAccessManager lifetime issues across `connect()`/dialog teardown | low | NAM owned by provider; provider owns its own lifetime; widget never holds a NAM pointer |
| Discovery races (CalDAV + CardDAV parallel; one finishes first) | low | `QFuture::then()` composition handles ordering; tests pin |

## Acceptance criteria

A successful Phase M lands the following on
`refactor/engine-merger` in all three repos, with `verify-all.sh`
exit 0:

1. **Library:**
   - `src/sync/dav/{discovery,collectionenum}.{h,cpp}` exists and
     is unit-tested.
   - `CalDavProvider` and `CardDavProvider` use the helpers; their
     existing tests pass unmodified.
   - `MultiProtocolDavProvider` + `MultiProtocolDavConfigWidget`
     exist; integration test passes against fake-server pair.
   - `ProviderConfigDialog`, `AccountsListWidget`,
     `CollectionPickerWidget` exist and are unit-tested.
   - `MultiProtocolDavProviderPlugin` is registered via
     `stock_plugins.cpp`.
2. **WildPalms:**
   - `AddAccountDialog` and `AccountsPage` host the library
     widgets; their existing tests pass.
   - `MappingRowDialog` is untouched; mapping tests pass
     unmodified.
   - "Multi-protocol DAV" appears in the provider combo; manual
     test passes against a fake-server pair.
3. **PlanStan:**
   - `CollectionController` hosts `ProviderConfigDialog` for
     add-account.
   - CalDAV add-account flow round-trips exactly as before (no
     feature regression).
   - CardDAV add-account flow works (D.1 closed by the same
     dialog).
   - Akonadi appears in the combo when
     `KALBURATOR_HAVE_AKONADI=ON`; otherwise it is absent
     (the contribution is compile-gated out).
4. **Doc roundup:**
   - `04w-deferred-work.md` B.5 flipped to ✅ landed; D.1 flipped
     to ✅ landed.
   - `04ac-phase-m-status.md` written (next available 04* slot;
     04aa/04ab already taken by Phase K audits directory and
     prior usage — verify at plan time).
   - `FINDINGS.md` entries for any non-obvious discoveries.
   - `CURRENT-STATUS.md` and `ROADMAP.md` updated.

## Cross-references

- **Brainstorm transcript:** 2026-05-16 session (this design doc
  is its written-up output).
- **Deferred-work catalog:** B.5, D.1
  (`libkalburator/docs/phase0/04w-deferred-work.md`).
- **Provider abstraction history:** Phase H design
  (`2026-05-06-phase-h-providers-design.md`) introduced
  `IProvider`. Phase Ib design
  (`2026-05-08-phase-ib-carddav-transport-design.md`) added
  `CardDavProvider`. Phase L's status doc
  (`libkalburator/docs/phase0/04y-phase-l-status.md`) added
  `AkonadiProvider` and confirmed accountless providers.
- **K.7/K.8a plugin surface:** the `Kalburator::Plugin` /
  `BackendContribution` / `ProviderContribution` abstractions used
  to register the new provider live in
  `libkalburator/docs/phase0/2026-05-14-phase-k8a-plan.md`.

## Tag

`v0.42-phase-m-multi-protocol-dav` on libkalburator's
`refactor/engine-merger` HEAD once acceptance criteria are met.
