# Sub-project A — Configuration substrate

**Date:** 2026-06-11
**Status:** Approved design, awaiting implementation plan.
**Parent:** `2026-06-11-config-two-doors-umbrella-design.md`
**Depends on:** nothing new lib-side (all required libkalburator primitives —
BackendContribution registry, FilteredCollectionBackend, LogicalCollection
roles, Star generator — shipped at or before v0.69).
**Unblocks:** sub-project B (wizard) and C (graph editor).

The substrate is pure mechanism: no visible UX change except better failure
reporting. It has four parts: the conduit descriptor contract (A1), the
unified source model (A2), names-first categories with device reconciliation
and the AppInfo write path (A3), and persistence-schema additions (A4).

---

## A1. Conduit descriptor contract

### Problem

PalmRuntime, the wizard, the graph, and routemapping all hardcode the four
stock conduits. Inventory of sites to eliminate:

| Site | File | Today |
|---|---|---|
| Backend/conflict/category dispatch | `palmruntime.cpp` finishConnect | 4-way `dynamic_cast` chain ×3 blocks |
| Category-store collection | `palmruntime.cpp` buildRouteLogicalCalendars | 4-way cast chain |
| Mapping identity (label/icon) | `palmruntime.cpp` resolveMappingIdentity | 4-way cast chain |
| Tickle-pause palm-id collection | `palmruntime.cpp` | 4-way cast chain |
| Palm-direct mapping test | `palmruntime.cpp` `kPalmBackendIds` | literal 4-string array |
| Hub collections | `palmruntime.cpp` ensureHubCollections | literal 4-tuple wiring array |
| Palm↔hub default LCs | `palmruntime.cpp` finishConnect | same wiring array |
| Domain lookup | `routemapping.cpp` domainForPalmPluginId | 4-case if-chain |
| DB-name lookup | `routemapping.cpp` translateRouteSpec | 4-case inline ternary |
| Collection matching | `app/wizard/domainfilter.cpp` | 4-case if-chain |
| Wizard seeding / rows | `newprofilewizard.cpp`, `targetpickerpage.cpp` | literal 4-string loops |
| Main-view registration | `kf6mainwindow.cpp` | cast chain |

### Design

Promote the surface the four plugins already implement (by convention, on
their concrete classes) into `PimPlugin` as virtuals, making it the **conduit
descriptor**. No new class: `PimPlugin` *is* the descriptor; it gains the
methods and the runtime stops casting to concrete types.

```cpp
// src/plugins/pimplugin.h (extended; existing setHub/setRuntime unchanged)
class PimPlugin : public Kalburator::Plugin {
public:
    // ── identity / declaration ────────────────────────────────────
    virtual QString conduitId() const = 0;             // "calendar", "todo", …
    virtual Kalburator::Shape::DomainId domain() const = 0;  // hub-collection id
    virtual QString primaryDbName() const = 0;          // "DatebookDB", …
    virtual QStringList claimedDatabases() const;       // default {primaryDbName()}
    virtual QString conduitDisplayName() const = 0;     // "Datebook", …
    virtual QString conduitIconName() const;            // default generic

    // ── capabilities ──────────────────────────────────────────────
    virtual bool supportsCategories() const { return true; }
    /// Which provider collections can serve as a sync target for this
    /// conduit. Default implementation matches on CollectionInfo::type and
    /// contentTypes per domain (subsumes app/wizard/domainfilter.cpp).
    virtual bool matchesCollection(const Kalburator::Sync::CollectionInfo &c) const;

    // ── factories (normalized from the four concrete signatures) ─────
    virtual std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) = 0;
    /// Default nullptr — a conduit without conflict UI is legal.
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler() { return nullptr; }
    /// Contract: non-null iff supportsCategories(). Default nullptr.
    virtual WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const { return nullptr; }
    virtual QStringList categorySlotNames() const;      // default via categoryStore(); empty if none
    virtual bool hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent) { Q_UNUSED(parent); return nullptr; }
};
```

Notes:

- `createPalmBackend` returns `SyncBackendBase` (the post-P3 neutral base);
  the calendar plugin's `unique_ptr<SyncBackend>` converts implicitly.
  Signatures of the four concrete implementations are normalized during
  implementation; behavior is unchanged.
- `conduitId()` keeps today's bare ids ("calendar", "contacts", "memo",
  "todo") — they are persisted in mapping rows as `sourceBackend` and must
  not change meaning. (`pluginId()` on `Kalburator::Plugin` remains the
  namespaced manifest id, e.g. "wildpalms.calendar".)
- `domain()` is the hub collection id; note the memo conduit's domain is
  `note` — the descriptor makes that mapping explicit instead of buried in
  `routemapping.cpp`.
- `matchesCollection` default lives in one place (`pimplugin.cpp`),
  implementing the current domainfilter semantics: contentTypes are
  authoritative when reported, `type` is the fallback. A document conduit
  overrides it for its own source types.

### Consumers after A1

- **PalmRuntime** holds `QList<PimPlugin*>` (filtered once from the loaded
  batch) and iterates: backend registration under `conduitId()`, conflict
  handlers, category snapshots, hub-collection creation
  (`ensureHubCollections` iterates descriptors, creating one canon-shaped hub
  collection per `domain()`), palm↔hub default LCs, palm-direct mapping test
  (`isPalmDirectMapping` checks membership in the registered conduit-id set),
  mapping identity, tickle ids.
- **routemapping**: `translateRouteSpec` takes the descriptor list (or a
  small lookup built from it) instead of a `stores` hash + if-chains;
  domain and DB-name lookups go through the descriptor.
- **Wizard/graph**: enumerate descriptors for rows/columns and call
  `matchesCollection` for target filtering. (`domainfilter.{h,cpp}` is
  deleted; its tests move to the descriptor default.)
- **KF6MainWindow**: iterates descriptors for main-view registration.

### Extensibility proof

A test registers a fifth fake conduit (`FakeDocumentConduit`: domain
"document", DB "DocumentDB", `supportsCategories() == false`) into the batch
and asserts: a hub collection appears, palm↔hub LC generated, wizard-visible
descriptor enumeration includes it, mapping rows route to it, and nothing
about the stock four broke. This test is the contract a real third-party
conduit relies on.

---

## A2. Unified source model

### Design

Everything addable is a `BackendContribution` → `IProvider` (umbrella
decision 1). The substrate ships the **first credential-less contribution**
to prove the pattern and to populate the graph's future Sources column:

**`LocalFolderContribution`** (WP-side; upstreaming to libkalburator is a
later RFC once PlanStan wants it):

- `backendType() = "local-folder"`, display name "Local folder".
- Its provider is configured with a list of (folder path, domain) entries;
  each entry is one `CollectionInfo` (id = stable UUID, type from domain,
  `readOnly = false`). `connect()` validates paths and reports collections —
  no network, resolves immediately.
- `createBackend(collectionId)` dispatches per domain to existing lib sinks.
  v1 dispatch table (fixed, small, honest): note → `MarkdownFilesBackend`;
  every other domain → `RawFilesBackend`. Richer per-domain sinks (the lib's
  LocalBackend ICS family for calendar/todo) are a follow-up once a consumer
  needs round-trippable local files rather than an export/import folder.
- Config widget: folder picker + domain combo per entry (lib's
  `IProviderConfigWidget` bridge, same as every other contribution).

Because it is an ordinary contribution, AccountController, the Add Account
dialog, profile persistence, cascade-delete, and mapping rows
(`"<providerId>:<collectionId>"`) all work unchanged.

Out of scope for A: ICS-feed/webcal contribution (revival of the deleted
webcalendar plugin), ShadowPlan store. They follow the same pattern later.

### Hub-only state

A conduit with no mapping rows syncs palm↔hub only. This existing behavior
becomes the *documented* meaning of "no routes" (it replaces the wizard's
special-cased RawFiles target in sub-project B; the substrate itself does not
change wizard behavior).

---

## A3. Names-first categories: reconciliation + AppInfo write

### Model

- Category routes bind to **names**, not slot indices. Row schema:
  `sourceCalendar = "palm:<domain>/name:<categoryName>"` (replaces
  `"palm:<domain>/<slot>"`; no migration — pre-substrate profiles are
  recreated, per the no-back-compat decision).
- Each profile persists a **desired category table** per claimed database:
  the set of category names configuration wants to exist on the device
  (≤15 names; Unfiled implicit). Sources of desired names: wizard
  auto-naming (sub-project B) and graph edits (sub-project C); the substrate
  provides storage + reconciliation.

### Reconciler

Runs inside `finishConnect()` after AppInfo snapshots, per claimed database:

1. Parse device categories (existing `CategoryInfo::parse`).
2. For each desired name, case-insensitively match a device slot
   (`categoryIndex`). Matched → bound.
3. Unmatched names claim free slots (`getOrCreateCategory`) and are written
   back to the device: new `CategoryInfo::serialize()` (inverse of `parse`,
   same struct layout, 15-char + NUL names, bumped `lastUniqID`s) +
   the existing `IPalmDatabaseAccess::writeAppBlock` (already on the
   interface at `kpilotlink.h:71`) via open-read-write → write → close.
4. No free slot → the route's status becomes `NoFreeSlot`; nothing written.
5. Device categories not in the desired set are **left alone** (the
   wholesale table replacement is the wizard-clobber path in B, which uses
   the same serializer).
6. The reconciled name→slot binding updates `CategoryMappingStore` and the
   `Profile::categorySlotNames` snapshot exactly as today.

### Route resolution + status

`translateRouteSpec` resolves `name:` references through the reconciled
store. Resolution outcomes become a visible per-route status instead of
today's silent `nullopt` drop:

```cpp
enum class RouteStatus { Active, WaitingForDevice, NoFreeSlot, NameUnresolved };
```

PalmRuntime exposes `routeStatuses()` (mapping id → status) and emits a
change signal; the substrate logs them, B/C render them. The silent-vanish
bug dies here.

### Hardware caveat

`writeAppBlock` against a real device needs hardware verification (POSE64 is
unstable for DLP timing — E.18 cancellation stands). Like clobber-sync Task
12, A lands with fake-device tests; the first live AppInfo write joins the
existing hardware-verification queue.

---

## A4. Persistence additions

In `profile.conf` (alongside the existing `[categories/<dbName>]` snapshot):

```ini
[desiredCategories/DatebookDB]
names=ACSW,Next Actions,TBS          ; ≤15, comma-escaped via QSettings list

[initialSync]
pending=true                          ; set by wizard (B); cleared after the
                                      ; armed clobber completes
```

Mapping rows: unchanged schema except the `sourceCalendar` name form (A3).
`syncMappingsJson` remains the single source of truth both doors write.

---

## Testing

- **Descriptor**: the FakeDocumentConduit extensibility test (A1 above);
  regression tests that the four stock conduits still register, snapshot, and
  generate identical LCs (compare generated mapping lists pre/post refactor).
- **matchesCollection default**: absorb `tst_domainfilter` cases unchanged.
- **LocalFolderContribution**: provider lifecycle (connect/collections/
  createBackend per domain), AccountController add/remove/cascade, mapping
  row round-trip.
- **Reconciler**: fake `IPalmDatabaseAccess` asserting serialized AppInfo
  bytes (round-trip through `CategoryInfo::parse`), slot claim, exhaustion
  status, case-insensitive matching, leave-unknown-categories-alone.
- **Route status**: name-resolved, waiting, exhausted; assert no silent
  drops (every enabled row yields a status).
- **Full suite** stays green (120 binaries; counts grow).

## Risks / notes

- The five cast-chain removals touch the hottest file in the repo
  (`palmruntime.cpp`); the refactor is behavior-preserving and gated on the
  pre/post LC-list comparison tests.
- Palm category names: Latin-1 on-device, 15 chars. Auto-naming (B) must
  truncate/transliterate; the substrate's serializer enforces the hard limit
  and reports collisions after truncation.
- `conduitId()` values are persisted in rows; treat as frozen identifiers.
