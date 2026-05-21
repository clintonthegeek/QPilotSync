# Phase E — libkalburator adoption + WP plugin ABI rewrite

**Date:** 2026-04-21
**Status:** Design approved 2026-04-21. **E.1 + E.2 landed upstream 2026-04-21**
(tags `v0.7-phase-b3-baseline`, `v0.8-phase-b4-engine-conflicts`). WP-side
sub-phases E.3+ are next.
**Pointers:**

- `docs/LIBKALBURATOR.md` — short status/coordination pointer.
- `docs/plans/2026-04-20-libkalburator-integration.md` — phased plan (Phase-E
  row will be superseded by this spec's sub-phases when implementation starts).
- `docs/plans/2026-04-20-libkalburator-integration-design.md` — original
  high-level design; this spec concretises Phase E.
- `docs/superpowers/plans/2026-04-21-phase-e1-blob-baseline-store.md` —
  executed E.1 plan (upstream landed as libkalburator Phase B3).
- `docs/superpowers/plans/2026-04-21-phase-e2-engine-conflict-wiring.md` —
  executed E.2 plan (upstream landed as libkalburator Phase B4).
- `~/dev/libkalburator/docs/phase0/README.md` — living upstream index.
- `~/dev/libkalburator/docs/phase0/04h-blob-layer-design.md` — Phase B2 blob
  layer; "Explicitly deferred" list feeds this spec's upstream scope.
- `~/dev/libkalburator/docs/phase0/04i-blob-baseline-store-design.md` — E.1
  landed as Phase B3.
- `~/dev/libkalburator/docs/phase0/04j-engine-conflict-wiring-design.md` —
  E.2 landed as Phase B4.
- `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md` —
  cross-repo proposal.

---

## Summary

Phase E completes Wild Palms' adoption of libkalburator. It is intentionally
wider than the bullet list in `docs/plans/2026-04-20-libkalburator-integration.md`
§"Phase E": in addition to `PalmBackend` + `PalmCalendarBackend` +
`PalmConflictHandler`, Phase E also **rewrites WP's plugin ABI** so that
every WP plugin is a libkalburator backend (or a small one-shot action).
Orchestration becomes 100% library-driven via `Kalburator::Sync::SyncCoordinator`,
and Client Mode / Full Sync Mode collapse into a single runtime with different
default `SyncMapping` sets.

Two upstream deliverables land first, in libkalburator itself:
`BlobBaselineStore` (hash-per-record persistent store) and
`BlobSyncEngine` ↔ `ConflictStore` wiring (so blob-level conflicts surface
through the existing `ConflictHandlerRegistry`). Both are catalogued as
deferred Phase-B2 followups in `04h-blob-layer-design.md`. Each upstream
commit is pre-validated against PlanStan's ctest baseline before landing.

Non-calendar Palm data (contacts, memos, todos) stays WP-internal. libkalburator
is **not** generalised into a multi-domain PIM library; its `SyncBackend`
(calendar-typed) upper layer and `IBlobBackend` (format-agnostic) lower layer
are already sufficient to carry any record format. Typed upper layers for
contacts/memos wait for a second consumer to concretely need them.

Palm per-record category routing is handled inside `PalmCalendarBackend`
by exposing **virtual sub-calendars** — one `CalendarBackendBinding` per Palm
category slot (0..15). The existing upstream routing engine
(`LogicalCalendar` + `SyncMapping`) handles target-calendar binding at
calendar granularity; no upstream changes for categories.

---

## Decisions recorded during brainstorm (2026-04-21)

| # | Decision | Rationale |
|---|---|---|
| 1 | Phase E splits into sub-phases (E.0 … E.N). | Touches ~260 consumer files; a monolithic branch sits broken too long. Per-sub-phase commits keep the tree buildable. |
| 2 | Palm category routing via virtual sub-calendars in `PalmCalendarBackend`. No upstream filter predicate on `SyncMapping`. | libkalburator is an iCalendar sync engine; backend handles graceful degradation. Principle preserved. |
| 3 | Full WP plugin ABI rewrite. `ISyncConduit`/`IConduit`/`IToolConduit`/`SyncConduitBase` all deleted. | No third-party plugins exist; the ABI is negotiable. "Thoroughly and sanely" argues for a clean rebuild. |
| 4 | libkalburator is **not** generalised further for contacts/memos. | Upstream explicitly deferred typed layers for those domains; extract-on-second-consumer pattern. |
| 5 | `BlobBaselineStore` + `BlobSyncEngine`↔`ConflictStore` land upstream **first**, before WP builds `PalmBackend`. | "Thoroughly and sanely" argues against shipping a known regression (stateless sync drops deletions). |
| 6 | CMake toggles disable old plugins during migration; app need not be runtime-complete through Phase E. | No release pressure; simpler than a dual-loader shim. |
| 7 | Reference plugin order: Memo → Calendar → ToDo → Contact → WebCalendar → Plucker → Install. | Memo is simplest; proves the new ABI against a boring record type before the calendar-typed adapter layers in. |
| 8 | Upstream-first sequencing. Every upstream commit is pre-validated against PlanStan ctest baseline (86/26/112) before landing. | Clean separation; WP doesn't split-brain between pre- and post-baseline engine behaviour. |
| 9 | Directory layout: `src/palm/` grows; `src/plugins/` stays; `src/core/` gains new interfaces; `src/sync/` deleted; `src/fullsync/` merged into a new `src/runtime/`; `src/conflict/` new. | Existing conventions respected. Palm device code concentrates in `src/palm/`. |

See also `memory/feedback_library_vs_backend_responsibility.md` for the
"backends handle degradation" principle this spec honours, and
`memory/feedback_planstan_pretest_for_upstream.md` for the upstream
commit gate.

---

## Architecture

### End-state picture

```
                       ┌───────────────────────────────────┐
                       │  Application (UI, profile mgmt)   │
                       └───────────────────────────────────┘
                                      │
                   ┌──────────────────┼──────────────────┐
                   │                  │                  │
         ┌─────────▼──────────┐  ┌────▼─────┐  ┌─────────▼────────┐
         │ BackendPluginMgr   │  │Plugin    │  │ SyncCoordinator  │
         │ (loads plugins;    │  │ActionMgr │  │ (libkalburator)  │
         │  registers         │  │ (loads   │  │                  │
         │  backends with     │  │ actions) │  │ runs SyncMappings│
         │  coordinator)      │  │          │  │ via BlobSyncEng. │
         └─────────┬──────────┘  └────┬─────┘  └─────────┬────────┘
                   │                  │                  │
          ┌────────┴────────┐         │        ┌─────────┴──────┐
          ▼                 ▼         ▼        ▼                ▼
    ┌──────────┐      ┌──────────┐ ┌─────┐┌────────────┐┌────────────┐
    │IBackend  │ ...  │IBackend  │ │IPlug││IBlobBack-  ││SyncBackend │
    │Plugin    │      │Plugin    │ │in   ││end (lib)   ││(lib, calen-│
    │(Memo)    │      │(Calendar)│ │Action│           ││dar-typed)  │
    └────┬─────┘      └────┬─────┘ │(Inst)│           └─────┬──────┘
         │                 │       └─────┘                  │
         │provides         │provides                         │adapter
         ▼                 ▼                                 ▼
    ┌──────────┐      ┌──────────┐                    ┌──────────────┐
    │PalmBack- │      │PalmBack- │                    │PalmCalendar- │
    │end       │◄─────┤end (same │                    │Backend       │
    │(IBlob)   │      │instance) │                    │(SyncBackend) │
    └──────────┘      └──────────┘                    └──────┬───────┘
                                                             │wraps
                                                             ▼
                                                     ┌──────────────┐
                                                     │PalmBackend   │
                                                     │'datebook'    │
                                                     │collection    │
                                                     └──────────────┘
```

Plugins register their backends with `SyncCoordinator`. The coordinator owns
`ConflictHandlerRegistry` (per-backend handlers) and consults `SyncMapping`s
from `ISyncConfigStore` (already implemented in Phase D as `SyncConfigStore_WP`).
The runtime is unified — same code path whether the user has configured
"just Palm ↔ local files" (today's Client Mode) or "Palm + CalDAV + local
with three-way sync."

### WP-side class layout

- **`PalmBackend : Kalburator::Sync::IBlobBackend`** — one instance, owned by
  the app. Exposes one `CollectionInfo` per Palm database (DatebookDB,
  AddressDB, MemoDB, ToDoDB, DateBk6's DatebookDB, etc. as determined by the
  plugin claim system). Opaque `BackendRecord.data` carries the raw Palm
  record bytes. Transcodes in `loadRecords()` / `createRecord()` by delegating
  to WP's existing codec code (relocated from `src/plugins/*/mapper.cpp` into
  `src/palm/codecs/`).

- **`PalmCalendarBackend : Kalburator::Sync::SyncBackend`** — calendar-typed
  adapter that wraps `PalmBackend`'s `datebook` collection. Decodes Datebook
  bytes → `Incidence::Ptr` by wrapping the existing `CalendarMapper` codec.
  Exposes virtual sub-calendars per Palm category slot (see §"Category
  routing"). Owned by the Calendar plugin.

- **`PalmConflictHandler : Kalburator::Sync::QSyncCore::ConflictHandler`** —
  Palm-aware conflict resolution: archive-bit preservation, secret-flag
  preservation, category-ID remap across resolution, HotSync keep-alive
  tickle during interactive prompts. Holds a `PalmDeviceConnection*` and a
  `CategoryMappingStore*`. Registered with the coordinator under backend ID
  `"palm"` via `coordinator->conflictRegistry()->registerHandler("palm",
  handler)`.

- **`PalmBackendConfig`** — plain struct carrying the Palm-specific fields
  that were stripped from upstream `ConflictPolicy` in Phase B:
  `ConnectionBehavior` enum (KeepAlive / DisconnectAndDefer / TimeoutThenDefer),
  `connectionTimeoutSeconds`, HotSync tickle interval, user-name. Stored on
  the `PalmBackend` instance; `PalmConflictHandler` reads it via a borrowed
  pointer.

- **`PalmContactsAdapter`, `PalmMemosAdapter`, `PalmTodosAdapter`** —
  WP-internal typed wrappers over `PalmBackend` for UI consumption. These are
  NOT libkalburator types; they exist in `src/palm/adapters/` and expose
  typed views for WP's contact/memo/todo UI widgets. Each plugin constructs
  them as needed. libkalburator never sees them.

### Plugin ABI

**`IPlugin`** (base; metadata-only):

```cpp
namespace WildPalms {

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual QString pluginId() const = 0;           // e.g. "memo", "calendar"
    virtual QString displayName() const = 0;
    virtual QIcon   icon() const = 0;
    virtual QString description() const = 0;
    virtual QString version() const = 0;

    virtual bool           hasSettings() const { return false; }
    virtual QWidget*       createSettingsWidget(QWidget *parent) { return nullptr; }
    virtual void           loadSettings(const QJsonObject &settings) {}
    virtual QJsonObject    saveSettings() const { return {}; }
};

} // namespace WildPalms
```

**`IBackendPlugin : IPlugin`** — provides one or more libkalburator backends:

```cpp
class IBackendPlugin : public IPlugin {
public:
    // Palm database claim system (same JSON manifest keys as today:
    // X-WildPalms-PalmDatabases, X-WildPalms-ClaimDescriptions).
    virtual QStringList claimedDatabases() const = 0;

    // Invoked by BackendPluginManager once per session. The plugin constructs
    // and returns its backend(s). Ownership is transferred to the caller
    // (usually SyncCoordinator via a QObject parent).
    //
    // A plugin may provide:
    //   - a blob-only backend (IBlobBackend*), or
    //   - a blob + calendar-typed pair (IBlobBackend* + SyncBackend*).
    //
    // The manager registers the blob backend for transport and (if present)
    // the calendar-typed backend for typed ICalendarSync consumers.
    struct ProvidedBackends {
        Kalburator::Sync::IBlobBackend*  blob = nullptr;   // required
        Kalburator::Sync::SyncBackend*   calendar = nullptr; // optional
    };

    virtual ProvidedBackends createBackends(
        Kalburator::Sync::ISyncHost *host,
        PalmDeviceConnection *device) = 0;

    // Optional: a plugin may register a per-backend ConflictHandler.
    // Returns nullptr if the plugin wants default handler behaviour.
    virtual Kalburator::Sync::QSyncCore::ConflictHandler*
        createConflictHandler() { return nullptr; }

    // Execution ordering hints (replaces SyncConduitBase::runBefore/runAfter).
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter()  const { return {}; }
};
```

**`IPluginAction : IPlugin`** — one-shot triggerable, no record-level sync:

```cpp
class IPluginAction : public IPlugin {
public:
    // Progress/status signals emitted via a QObject proxy (actions are
    // not QObject themselves to keep them cheap to load).
    class ActionContext : public QObject {
        Q_OBJECT
    public:
        virtual void setTotal(int total) = 0;
        virtual void setCurrent(int current) = 0;
        virtual void log(const QString &msg) = 0;
        virtual bool isCancelled() const = 0;
    signals:
        void progress(int current, int total);
        void message(const QString &msg);
    };

    // Triggered from UI. Synchronous; caller typically runs on a worker thread.
    // Returns true on success.
    virtual bool execute(ActionContext *ctx,
                         PalmDeviceConnection *device,
                         const QJsonObject &parameters) = 0;

    // Describe what the action requires to run (e.g. Install needs an open
    // device connection; Restore may need a profile with backup files).
    struct Preconditions {
        bool requiresDeviceConnection = true;
        QStringList requiresFiles;
    };
    virtual Preconditions preconditions() const = 0;
};
```

**Metadata (JSON manifest):** existing WP keys survive, one new key
discriminates the interface type.

```json
{
  "KPlugin": {
    "Id": "memo",
    "Name": "Memo Sync",
    "Description": "Syncs Palm Memo DB to local files.",
    "Icon": "internet-mail",
    "Version": "2.0"
  },
  "X-WildPalms-PluginType": "backend",
  "X-WildPalms-PalmDatabases": ["MemoDB"],
  "X-WildPalms-ClaimDescriptions": {
    "MemoDB": "Syncs MemoDB to Markdown files."
  }
}
```

`X-WildPalms-PluginType` is `"backend"` or `"action"`. `"tool"` (the old
value) is retired. `X-WildPalms-ConduitType` key is dropped.

**`BackendPluginManager`** (WP; replaces `ConduitManager`):

```cpp
class BackendPluginManager : public QObject {
    Q_OBJECT
public:
    explicit BackendPluginManager(
        Kalburator::Sync::ISyncHost *host,
        PalmDeviceConnection *device,
        Kalburator::Sync::SyncCoordinator *coordinator,
        QObject *parent = nullptr);

    void loadPlugins();                 // scans plugin dirs, filters by PluginType=backend
    QList<IBackendPlugin*> plugins() const;
    IBackendPlugin *pluginForDatabase(const QString &palmDbName) const;
};
```

**`PluginActionManager`** — parallel manager for `IPluginAction` plugins.
Simple registry; the UI asks it "give me all actions matching these
preconditions" and surfaces them in menus/buttons.

### Unified runtime

Today's two orchestration paths (`SyncConduitBase`-driven Client Mode,
library-driven Full Sync Mode) collapse into one:

1. App loads profile.
2. App constructs `PalmDeviceConnection` (when device is present).
3. App constructs `SyncHost_WP` (Phase D's `ISyncHost` impl), relocated from
   `src/fullsync/` to `src/runtime/`.
4. App constructs `SyncCoordinator` with the host + a `BackendRegistry`.
5. `BackendPluginManager` loads all backend plugins, asks each for its
   `ProvidedBackends`, registers them with the registry and (if the plugin
   supplies one) registers its `ConflictHandler` with the coordinator's
   registry.
6. `SyncConfigStore_WP` (Phase D's `ISyncConfigStore` impl) supplies the
   list of `SyncMapping`s for the profile.
7. `coordinator->runSync()` iterates mappings, runs each via
   `BlobSyncEngine::twoWayWithBaseline(sourceBackend, targetBackend,
   collectionId)` (or the equivalent; depends on the upstream API landed
   in E.2). Conflicts route to the registered handlers.

**Client Mode** becomes a profile default: mappings from each Palm
collection to a `LocalBlobBackend`-backed collection at the profile's sync
folder. **Full Sync Mode** becomes a profile that additionally configures
CalDAV/Akonadi/etc. targets and maps those in. The wizard UI (Phase F) is
orthogonal; Phase E just makes them the same machinery.

---

## Upstream libkalburator deliverables

### E.1 — `BlobBaselineStore`

**Purpose:** Persistent record-hash baseline that lets `BlobSyncEngine`
distinguish "record X was deleted on source since last sync" from
"record X never existed on source." Without a baseline, `twoWayNaive`
cannot propagate deletions correctly.

**Surface:**

```cpp
namespace Kalburator::Sync {

class BlobBaselineStore {
public:
    // Opens/creates the baseline table in .planstan-sync.db (same DB as
    // SyncStore and IDMappingStore; coordinated schema via ADD COLUMN).
    explicit BlobBaselineStore(const QString &dbPath);
    ~BlobBaselineStore();

    // Per-mapping hash snapshot. mappingId uniquely identifies a source/
    // target pair within a profile.
    bool setBaseline(const QString &mappingId,
                     const QString &recordId,
                     const QString &contentHash);
    QString baselineHash(const QString &mappingId,
                         const QString &recordId) const;

    // Bulk write at end of successful sync.
    bool commitBaselines(const QString &mappingId,
                         const QMap<QString, QString> &recordIdToHash);

    // Bulk read: all known record IDs for a mapping. Used by the engine to
    // compute "deleted since baseline" = (baseline ∩ !current).
    QStringList baselineRecordIds(const QString &mappingId) const;

    // Clear on unbind.
    bool clearMapping(const QString &mappingId);
};

} // namespace Kalburator::Sync
```

WP's existing `qsynccore/baselinestore.{h,cpp}` is the reference
implementation; `BlobBaselineStore` is a SQLite rewrite matching the pattern
of `IDMappingStore` (same `.planstan-sync.db`, idempotent `ALTER TABLE ADD
COLUMN` migrations, per-mapping queries).

Tag: `v0.7-phase-b3-baseline`.

### E.2 — `BlobSyncEngine::twoWayWithBaseline` + `ConflictStore` wiring

**Purpose:** Make `BlobSyncEngine` correctness-complete. Integrate the
baseline store from E.1. Surface blob-level conflicts through the existing
`ConflictHandlerRegistry` so `PalmConflictHandler` can actually receive them.

**Design decision (pinned during E.2 execution):** The registry is passed
into the engine as a borrowed pointer. The engine does **not** own a
registry and does **not** expose `registerConflictHandler(...)`. Callers
(`SyncCoordinator` in shared code; the WP runtime constructed in E.15)
own a registry and hand it to each `twoWayWithBaseline` call. This keeps
the engine stateless and matches the existing `SyncCoordinator::conflictRegistry()`
pattern.

**Engine operation (as landed):**

```cpp
class BlobSyncEngine : public QObject {
public:
    // Replaces twoWayNaive for production use. Consults baseline store;
    // consults per-backend handler registry on conflict.
    BlobSyncResult twoWayWithBaseline(
        IBlobBackend *a,
        IBlobBackend *b,
        const QString &collectionId,
        const QString &mappingId,
        BlobBaselineStore *baseline,
        QSyncCore::ConflictHandlerRegistry *handlers,
        QSyncCore::ConflictStore *conflicts,
        const QSyncCore::ConflictPolicy &policy);

    // twoWayNaive + mirror retained for MockBlobBackend tests and for
    // backends that don't need baselining (rare).
};
```

`BlobSyncStats` gained a `conflicts` counter. `BlobSyncResult` is unchanged
in shape; the counter lives on both `sourceStats` and `targetStats`.

Conflict detection logic (3-way diff, as landed — nine cases):

- For each record ID seen on either side or in the baseline:
  - `baseline=B, a=B, b=B` → no change → skip.
  - `baseline=B, a=A', b=B` → modified on A only → propagate to B.
  - `baseline=B, a=B, b=B'` → modified on B only → propagate to A.
  - `baseline=B, a=A', b=B'`, A'≠B' → **conflict**. Call
    `handlers->handlerFor(a->backendId())->handleConflict(...)`. Apply
    decision (UseSource / UseTarget immediately; Skip / Pending / unsupported
    are persisted via `conflicts->addConflict(...)` and the baseline is
    left at its previous value so the conflict recurs next sync). `Merge`,
    `Duplicate`, `UseBoth`, `DeleteBoth` decisions are treated as Skip in
    B4; they'll be re-examined in a follow-up upstream phase when a
    concrete consumer needs them.
  - `baseline=B, a=missing, b=B` → deleted on A since baseline → delete on B.
  - `baseline=B, a=A, b=missing` → deleted on B since baseline → delete on A.
  - `baseline=missing, a=A, b=missing` → new on A → create on B.
  - `baseline=missing, a=missing, b=B` → new on B → create on A.
  - `baseline=missing, a=A, b=B` (both sides present, no baseline) → no
    action. The landed implementation deliberately falls through this
    combo per the in-code comment "Other edge cases (both missing, or
    impossible combos) fall through." Record IDs are normally scoped to
    their backend so this case is rare in practice, but it can surface
    when WP's `PalmBackend` and a target backend both hold a pre-sync
    record with a coincidentally-identical ID. Handling is an upstream
    micro-phase, tracked under risks as R8; the first WP integration
    test that exercises this will force its resolution.

`BlobSyncStats` gained a `conflicts` counter.

Tag landed: `v0.8-phase-b4-engine-conflicts` (2026-04-21).

### Upstream commit gate

Every upstream commit lands only after PlanStan's ctest suite (86 pass /
26 fail / 112 total baseline) is re-run and holds. See
`memory/feedback_planstan_pretest_for_upstream.md`. This is hard-gated,
not advisory.

---

## Category routing (virtual sub-calendars)

Palm Datebook records carry a 4-bit category (0..15) where slot 0 is
"Unfiled" and slots 1..15 are user-defined. Stock Palm DateBook ignores this
field on events; DateBk6+ and other third-party Palm calendars use it
heavily.

**Approach:** `PalmCalendarBackend` exposes each populated category slot as a
distinct virtual calendar identifier. Slot 0 ("Unfiled") is always present;
slots 1..15 appear when the Palm's `AppInfo` block has a non-empty name in
that slot.

**Virtual calendar naming:** `palm:calendar/<slot>` where `<slot>` is the
integer 0..15. The calendar's display name is the Palm category name
(`CategoryAppInfo_t.name[slot]`). Example:

- `palm:calendar/0` — "Unfiled"
- `palm:calendar/1` — "Work"
- `palm:calendar/2` — "Personal"
- ... etc.

**On read (Palm → target):** `PalmCalendarBackend::loadItems()` iterates
Datebook records, unpacks each, and emits it to whichever virtual calendar
matches the record's category slot. `SyncMapping` for each virtual calendar
routes the event to the user's chosen target calendar.

**On write (target → Palm):** `PalmCalendarBackend` receives an
`Incidence::Ptr` bound to a specific virtual calendar ID, extracts the slot
from the calendar ID, and packs the Datebook record with that slot in its
attributes byte. If the target calendar isn't bound to any virtual calendar
on the Palm side (e.g., a brand-new CalDAV calendar with no Palm mapping
yet), the event is stored under `palm:calendar/0` ("Unfiled") as a safe
default.

**On category rename/reorder:** The category name lives in AppInfo, not in
the record. If a Palm-side rename happens, the virtual calendar's display
name updates but the slot ID stays the same — mappings survive. If a slot is
reassigned (rare; user-initiated via DateBk6's category editor), the mapping
needs re-validation. Out-of-scope for Phase E — logged as a known limitation
and surfaced in the Phase-F config UI.

**On category-ID collision during conflict resolution:** `PalmConflictHandler`
owns the remap logic. If the source and target of a conflict disagree on
category (Palm event in slot 3 vs target event in a calendar mapped to slot
5), the handler picks a winner per the Palm record's `lastModified` —
preserving the intent of the most-recent Palm edit.

**No upstream changes.** `Kalburator::Sync::SyncMapping` continues to route
at calendar granularity; the backend exposes the granularity it wants. This
honours the "backends handle degradation" principle.

---

## Directory layout

**Existing directories (grow or stay):**

- `src/palm/` — pilot-link device access (DeviceSession, DeviceWorker, DLP
  wrappers). **Grows** to hold:
  - `src/palm/palmbackend.{h,cpp}` — the `IBlobBackend`.
  - `src/palm/palmcalendarbackend.{h,cpp}` — the `SyncBackend` adapter.
  - `src/palm/palmbackendconfig.h` — the config struct.
  - `src/palm/codecs/` — Datebook, Address, Memo, ToDo record codecs
    (relocated from `src/plugins/*/mapper.cpp`).
  - `src/palm/adapters/` — WP-internal typed wrappers (PalmContactsAdapter,
    PalmMemosAdapter, PalmTodosAdapter).
- `src/plugins/` — each subdir becomes a new-ABI plugin.
- `src/core/` — **gains**:
  - `src/core/iplugin.h`
  - `src/core/ibackendplugin.h`
  - `src/core/ipluginaction.h`
  - **Deletes** at end of Phase E: `iconduit.h`, `isyncconduit.h`,
    `itoolconduit.h`.

**New directories:**

- `src/runtime/` — absorbs `src/fullsync/`'s contents. Houses the unified
  runtime: `SyncHost_WP`, `CalendarCollection_WP`, `SyncConfigStore_WP`,
  `ConflictResolver_WP`, `ConflictPresenter_WP`, `BackendPluginManager`,
  `PluginActionManager`. `src/fullsync/` is deleted.
- `src/conflict/` — `PalmConflictHandler.{h,cpp}` and any future
  WP-specific conflict shims.

**Deleted entirely:**

- `src/sync/` — all of it. `qsynccore/` (merged upstream), `syncbackend.h`,
  `syncengine.*`, `syncstate.*`, `synctypes.h`, `conduit.{h,cpp}`,
  `localfilebackend.{h,cpp}` (superseded by `LocalBlobBackend`).

---

## Sub-phases

Each sub-phase is one commit (or a small handful of commits when the diff
warrants). Tree stays buildable; runtime may be incomplete mid-phase per
decision #6 (CMake toggles disable in-flight plugins).

Legend: ✅ = landed. Others have no marker yet. Tracking state lives here
rather than in a separate doc so this spec stays the single source of truth
for Phase-E sub-phase state.

| # | Scope | Repo | Dep | Exit gate |
|---|---|---|---|---|
| ✅ **E.0** | Spec lands. This doc. Committed `2a484ca` 2026-04-21. | WP | — | Committed to `docs/superpowers/specs/`. |
| ✅ **E.1** | `BlobBaselineStore`. SQLite store sharing `.planstan-sync.db`. Library-side tests. Landed 2026-04-21 as upstream Phase B3, tag `v0.7-phase-b3-baseline`. Plan: `docs/superpowers/plans/2026-04-21-phase-e1-blob-baseline-store.md`. | libkalburator | E.0 | libkalburator ctest + PlanStan ctest baseline held. Tag `v0.7-phase-b3-baseline`. |
| ✅ **E.2** | `BlobSyncEngine::twoWayWithBaseline` + `ConflictStore` wiring via externally-owned `ConflictHandlerRegistry`. Landed 2026-04-21 as upstream Phase B4, tag `v0.8-phase-b4-engine-conflicts`. Plan: `docs/superpowers/plans/2026-04-21-phase-e2-engine-conflict-wiring.md`. | libkalburator | E.1 | libkalburator ctest + PlanStan ctest hold. Tag `v0.8-phase-b4-engine-conflicts`. |
| ✅ **E.3** | `PalmBackend : IBlobBackend` scaffold. Implements all `IBlobBackend` methods against a mock device (no real pilot-link yet). Unit tests against `BlobSyncEngine` with `MockBlobBackend` as counterparty. New static lib `WildPalmsPalmSync` houses PalmBackend + IPalmDatabaseAccess + MockPalmDatabaseAccess + PalmRecord value type. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e3-palm-backend-scaffold.md`. | WP | E.2 | `ctest` passes; `PalmBackend` round-trips synthetic `BackendRecord`s through the engine (tst_palmbackend_roundtrip). |
| ✅ **E.4** | `PalmBackend` wired to real pilot-link via new `PilotLinkPalmDatabaseAccess` adapter over the existing abstract `KPilotLink` interface. New static lib `WildPalmsPalmDevice` at `src/palm/device/` houses adapter + `PilotRecord <-> PalmRecord` bridge. Tests use a new `MockKPilotLink` test double (KPilotLink is already abstract). Codec relocation deferred to E.6/E.7 when concrete consumers land. Live-device integration test deferred to E.18 (POSE64 sandbox). Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e4-palm-backend-pilot-link.md`. | WP | E.3 | WP ctest passes; adapter round-trips records through `BlobSyncEngine::twoWayWithBaseline` (tst_palmdevice_roundtrip). |
| ✅ **E.5** | `PalmConflictHandler` + `PalmBackendConfig` + `ConnectionBehavior` landed at `src/palm/conflict/` in new static lib `WildPalmsPalmConflict`. Handler delegates to upstream `ConflictPolicy` for base resolution and applies three Palm overlays: archive-bit safety, secret-flag protection, category tie-break. Live device queries via borrowed `IPalmDatabaseAccess*`. Registers with `ConflictHandlerRegistry` under backend id `"palm"`. `CategoryMappingStore` defer to E.6; bit-preservation on apply-path defer to E.7. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e5-palm-conflict-handler.md`. | WP | E.4 | WP ctest passes; 17 handler tests + 3 registration tests cover each overlay and registry integration. |
| ✅ **E.6** | `PalmCalendarBackend : SyncBackend` landed at `src/palm/calendar/` in new static lib `WildPalmsPalmCalendar`. Fresh `DatebookCodec` (Palm record bytes ↔ `KCalendarCore::Event::Ptr` via pisock's `pack_Appointment`/`unpack_Appointment`) with full content round-trip: times/summary/description/alarm/repeat/EXDATE/private flag. `CategoryMappingStore` stores slot → display-name map, injected into backend non-owning. Virtual calendar IDs `palm:calendar/<slot>` for 0..15. Records route to/from virtual calendars by `PalmRecord::category`. Real fetchItems/pushItems/deleteItems against `IPalmDatabaseAccess`. Legacy `loadItems`/`storeItems`/`updateItem`/`removeItem`/`startSync` scaffolded (forward to operation API). AppInfo-block parsing + plugin wiring defer to E.10/E.17. Landed 2026-04-21. Plan: `docs/superpowers/plans/2026-04-21-phase-e6-palm-calendar-backend.md`. | WP | E.5 | WP ctest passes; 7 store + 17 codec + 18 backend tests cover round-trip and routing. |
| ✅ **E.7** | Typed codecs + stateless adapters for Contacts/Memos/Todos at `src/palm/codecs/` and `src/palm/adapters/`. New static libs `WildPalmsPalmCodecs` + `WildPalmsPalmAdapters`. POD + pisock-driven encode/decode (`pack_*`/`unpack_*`), optional KDE PIM converters (Contact↔Addressee, Todo↔KCalendarCore::Todo). Legacy mappers at `src/plugins/{contacts,memo,todos}/` untouched. Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e7-typed-adapters.md`. | WP | E.4 (parallel to E.5/E.6) | WP ctest passes; ~55 tests across codecs + adapters; legacy mapper tests still pass. |
| ✅ **E.8** | New plugin ABI interfaces at `src/core/` (`iplugin.h`, `ibackendplugin.h`, `ipluginaction.h`). `BackendPluginManager` + `PluginActionManager` at `src/runtime/` in new static lib `WildPalmsRuntime`. Shared metadata readers (`metaString`/`metaBool`/`metaInt`/`metaStringList`) factored out of `ConduitManager` for reuse. `SimpleActionContext`: concrete `IPluginAction::ActionContext` with atomic cancellation flag. `IPluginAction::ActionContext` hoisted to namespace-level `IActionContext` with `using` alias (Qt MOC does not allow `Q_OBJECT` in nested classes). Dummy backend + action plugins at `tests/plugins/`. `src/fullsync/` relocation to `src/runtime/` **deferred to E.15** per E.8 plan decision (not correctness-blocking). Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e8-plugin-abi.md`. | WP | E.6 | WP ctest passes (41/41); `tst_plugin_factory_roundtrip` exercises both managers against real `.so` fixtures under `tests/plugins/`. |
| ✅ **E.9** | Memo rewritten as `IBackendPlugin` (`MemoBackendPlugin` + `MemoBlobBackend` + `MemoMarkdown`). `PalmDeviceConnection` concrete. `IBackendPlugin` gained view + conflict hooks. CMake toggle `WILDPALMS_MEMO_PLUGIN_V2=ON` swaps the new plugin in at `wildpalms/plugins/`; legacy `MemoConduit` remains at `wildpalms/conduits/` until E.16. Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e9-memo-plugin.md`. | WP | E.8 | WP ctest passes (47/47); `tst_memo_v2` covers the full round-trip via `BlobSyncEngine::twoWayWithBaseline` with a `MockBlobBackend` target (LocalBlobBackend + cross-id-space mapping deferred to E.15+); coordinator-level coverage deferred to E.18. |
| ✅ **E.10** | Calendar rewritten as `IBackendPlugin` (`CalendarBackendPlugin` + `CalendarBlobBackend` + `CalendarConflictHandler` + `CategoryAppInfoReader` + `IcsTranscoder`). First real consumer of `PalmBackend` and `PalmCalendarBackend`. AppInfo-block parsing landed (`CategoryAppInfoReader`); `IPalmDatabaseAccess::readAppBlock` + `PalmBackend::readAppBlock` + `PilotLinkPalmDatabaseAccess::readAppBlock` plumbing. Virtual sub-collections `palm:calendar/<N>` (one per populated category slot, slot 0 always present). `CalendarConflictHandler` adds three calendar-aware overlays (alarm-only, EXDATE-only, DTSTART-tz-only) before delegating to `PalmConflictHandler`. CalendarView reused untouched. CMake toggle `WILDPALMS_CALENDAR_PLUGIN_V2=ON`; legacy `CalendarConduit` remains buildable. `tst_calendar_v2` runs end-to-end via `BackendPluginManager` against `MockBlobBackend` (per `tst_memo_v2`'s id-space deferral). Landed 2026-04-24. Plan: `docs/superpowers/plans/2026-04-24-phase-e10-calendar-plugin.md`. | WP | E.9 | WP ctest passes; ~30 calendar tests cover reader/transcoder/blob-backend/conflict-handler/plugin metadata + 4 e2e scenarios. |
| ✅ **E.11** | ToDo rewritten as `IBackendPlugin` (`TodoBackendPlugin` + `TodoBlobBackend` + `TodoConflictHandler` + `TodoIcsTranscoder`). Second consumer of the shared `CategoryAppInfoReader` (promoted from calendar plugin into `WildPalmsPalmCalendar` static lib in Task 1; `parseDatebookAppInfo` renamed `parseCategoryAppInfo` for generality). Virtual sub-collections `palm:todo/<N>` (one per populated category slot, slot 0 always present). `TodoConflictHandler` adds one ToDo overlay (completion-asymmetric merge) before delegating to `PalmConflictHandler`. TaskView reused untouched. CMake toggle `WILDPALMS_TODO_PLUGIN_V2=ON`; legacy `TodoConduit` remains buildable. `tst_todo_v2` runs end-to-end via `BackendPluginManager` against `MockBlobBackend`. Landed 2026-04-25. Plan: `docs/superpowers/plans/2026-04-25-phase-e11-todo-plugin.md`. | WP | E.10 | WP ctest passes; ~25 todo tests cover transcoder/blob-backend/conflict-handler/plugin metadata + 4 e2e scenarios. |
| ✅ **E.12** | Rewritten **Contacts** as `IBackendPlugin`. `ContactsBackendPlugin` + `ContactsBlobBackend` + `ContactsConflictHandler` + `ContactsVcardTranscoder` in `src/plugins/contacts/` (submodule). vCard 4.0 on the wire, virtual sub-collections `palm:contact/<slot>`. One conflict overlay: per-slot field-union for phone[]/custom[] when single-valued fields agree. AddressDB AppInfo parsed via the shared `parseCategoryAppInfo` (third consumer after Calendar + ToDo). No main view in this phase (legacy ContactView stays with legacy ContactConduit until E.16). CMake toggle `WILDPALMS_CONTACTS_PLUGIN_V2=ON`. Landed 2026-04-25. Plan: `docs/superpowers/plans/2026-04-25-phase-e12-contacts-plugin.md`. | WP | E.11 | WP ctest passes; `tst_contacts_v2` covers full round-trip via `BlobSyncEngine::twoWayWithBaseline` with a `MockBlobBackend` target. |
| ✅↩ **E.13** | **Landed 2026-04-26, removed 2026-05-21.** WebCalendar was rewritten as `IBackendPlugin` (`WebcalBackendPlugin` + `WebcalBlobBackend` + `WebcalFeed`) on top of new upstream `Kalburator::Sync::IcsFeedFetcher`. The implementation carried a cross-thread parenting bug (QNetworkAccessManager owned on the GUI thread, fetcher called from the sync worker thread) that was flagged as an E.16 deferral. Rather than fix it, the plugin was deleted on 2026-05-21 (submodule + tests + PalmRuntime registration + Plucker runAfter entry). `Kalburator::Sync::IcsFeedFetcher` stays upstream — it's still useful for PlanStan and for a future redesign of Web feed subscriptions in WP. Plan: `docs/superpowers/plans/2026-04-26-phase-e13-webcalendar-plugin.md` (historical). | WP | E.12 | n/a — code removed. |
| ✅ **E.14** | Plucker rewritten as `IBackendPlugin` (`PluckerBackendPlugin` + `PluckerBlobBackend` + `PluckerFetcher`). Two collections: `plucker:channels` (one record per due channel, blob = .pdb bytes from PyPlucker `Spider.py` subprocess) and `plucker:bootstrap` (SysZLib + viewer PRC bytes, gated on `IPalmDatabaseAccess::hasDatabase("Plucker")`). Source-only — runtime install drain via E.15's `IPluginAction`. Settings as JSON `channels[]` with all 25 fields + `last_fetched` ISO string per channel; per-channel scheduling persisted across runs (Plucker cadences are days/weeks, unlike WebCal). PluckerChannel struct + helpers lifted into shared `pluckerchannel.h` so V1 conduit and V2 plugin share scheduling logic. Settings widget is the channel-management UI (re-skin of legacy `PluckerView`); legacy `PluckerView`/`PluckerChannelDialog` stay with the legacy conduit. CMake toggle `WILDPALMS_PLUCKER_PLUGIN_V2=ON` (default ON); legacy `PluckerConduit` remains buildable. No libkalburator changes — Plucker DB is Palm-only. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e14-plucker-plugin.md`. | WP | E.13 | WP ctest passes (72/72); libkalburator ctest unchanged; 5 test executables (channel/fetcher/blob-backend/plugin/e2e) covering ~22 test cases. |
| ✅ **E.15a** | Install rewritten as `IPluginAction` (`InstallActionPlugin`). New `IPalmFileInstaller` abstraction (`PilotLinkPalmFileInstaller` real + `MockPalmFileInstaller`); `PalmDeviceConnection` gains `fileInstaller()`. New `InstallSourceCollector` at `src/runtime/` aggregates folder + cross-plugin blob backends (Plucker channels + bootstrap, deferred from E.14). Plucker `availableCollections()` order swapped to {bootstrap, channels}. CMake toggle `WILDPALMS_INSTALL_PLUGIN_V2=ON` (default ON); legacy `InstallConduit` remains buildable. UI trigger (Tools → Actions submenu) deferred to E.17 which already owns app-layer call-site migration. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e15a-install-action.md`. | WP | E.14 | WP ctest passes (76/76); 4 new test executables (palmfileinstaller, installsourcecollector, installactionplugin, install_v2_e2e) plus 2 new cases on tst_palmdeviceconnection. |
| ✅ **E.15b** | `git mv src/fullsync/* src/runtime/` complete; `WildPalmsFullSync` static lib folded into `WildPalmsRuntime`; Kalburator::Sync promoted to PUBLIC link on Runtime. Header guards renamed WILDPALMS_FULLSYNC_* → WILDPALMS_RUNTIME_*. `_wp` filename suffix retained (avoids name clash with libkalburator types). QSettings group keys `"fullsync/..."` in `syncconfigstore_wp.cpp` retained verbatim (config-compat). Test helper renamed `add_fullsync_test` → `add_runtime_test`. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e15b-fullsync-relocation.md`. | WP | E.15a | WP ctest passes (76/76); library graph one node smaller; no behavior change. |
| 🟡 **E.16** | **Landed (partial) 2026-04-28.** Collapse Client Mode / Full Sync Mode into unified runtime. New `WildPalms::Runtime::SyncRunner` (`src/runtime/syncrunner_wp.{h,cpp}`) drives `BlobSyncEngine::twoWayWithBaseline` per loaded `IBackendPlugin` for the six Tools-menu sync modes (HotSync/FullSync/CopyPalmToPC/CopyPCToPalm/Backup/Restore); modes implemented as `ConflictPolicy` + baseline-clear variants on a single code path. `synctypes.h` relocated `src/sync/` → `src/core/synctypes.h` (header guard `WILDPALMS_CORE_SYNCTYPES_H`); `Sync::SyncMode` enum int ordering preserved for QSettings compat. `DeviceSession::requestSync` + `DeviceWorker::doSync` overloaded with new SyncRunner-taking variants; legacy SyncEngine overloads retained. `KF6MainWindow` constructs `m_syncRunner` and routes all six menu handlers through it; `setKPilotLink()` builds a real `PalmDeviceConnection` via new `pilotlinkconnectionfactory` (compiled into WildPalmsCore; PalmDevice's link to Core demoted PUBLIC→include-path-only via `INTERFACE_INCLUDE_DIRECTORIES` query + AUTOMOC-off to break the Core↔PalmDevice cycle). All seven plugins' legacy `*Conduit` halves + matching `*-conduit.json` manifests + legacy mappers deleted (six plugins are submodules; submodule pointer bumps included). 4 mapper tests removed; 11 new SyncRunner test methods added (`tests/runtime/tst_syncrunner.cpp`) covering all six SyncMode policies + cancellation. Real-device smoke test on a Palm m505: 621 records flowed Palm→PC across calendar/contacts/memo/todos plugins on first HotSync. **Deferred to follow-up:** (a) `InteractiveConflictHandler` rebind from WP-internal `QSyncCore` to `Kalburator::Sync::QSyncCore::ConflictHandler` — required before `src/sync/` can actually be deleted; (b) WebCalendar plugin cross-thread parenting bug in `createBackends`; (c) multi-collection plugins re-read Palm DB N times (perf); (d) `LocalBlobBackend` cross-id-space mapping (likely duplicates on second sync, untested); (e) `WildPalms::FullSync` → `WildPalms::Runtime` namespace rename. `src/sync/`, `ConduitManager`, and the `IConduit` family **still exist on disk and still build** — they are unreachable from the Tools menu and from any plugin manifest, but kf6mainwindow still constructs `m_syncEngine` + `m_conduitManager` at startup (dead code). Plan: `docs/superpowers/plans/2026-04-28-phase-e16-unified-runtime.md`. | WP | E.15 | WP builds; SyncRunner-driven sync verified on a real Palm m505 (621-record first HotSync); WP ctest 71/71 passing. Final delete of `src/sync/` family blocked on conflict-handler rebind. |
| **E.17** | Migrate app-layer call sites: `kf6mainwindow`, `devicesession`, `deviceworker`, `conflictdialog`, `conflictreviewwidget`, `interactiveconflicthandler`, `profile`. Most already adjusted by earlier sub-phases; this cleans up the last stragglers. | WP | E.16 | WP builds + runs end-to-end in development configuration; manual smoke test passes. |
| ❌ **E.18** | **Cancelled 2026-05-21.** Original scope: integration tests driven by a POSE64 emulator sandbox. POSE64's DLP timing is unstable enough that building a reliable harness on top of it is not feasible at this time; the effort to stabilise it is out of proportion with the value. Coverage is provided by the per-plugin e2e tests already in the WP ctest suite (`tst_calendar_v2`, `tst_todo_v2`, `tst_contacts_v2`, `tst_memo_v2`, `tst_webcalendar_v2`, `tst_plucker_v2`, `tst_install_v2_e2e`) plus periodic manual smoke runs against real Palm hardware. Revisit if a viable emulator harness appears later. | WP | — | n/a — exit condition voided. |
| 🟡 **E.19** | **Partial 2026-05-21.** Done: legacy conduit / SDK docs (`CONDUIT_PLUGIN_DESIGN.md`, `plugin-developer-guide.md`, `sdk-plugin-guide.md`, `sdk-shadowstan-advisory.md`) + three pre-Phase-E TODO files moved to `docs/archived/` with an inventory `README.md`. Integration plan's Phase E row + this spec's status rows reconciled with reality. Pending: write new `docs/PLUGIN_ABI.md` describing `IPlugin`/`IBackendPlugin`/`IPluginAction`; refresh `docs/ARCHITECTURE_2026.md` + `docs/SYNC_ENGINE_ARCHITECTURE.md` for the unified runtime; flip integration plan's Phase E row to ✅ once the E.16 conflict-handler rebind also lands. | WP | E.16 (was E.18, now voided) | New docs committed; integration plan's Phase E row flips to ✅. |

**Total:** 19 sub-phases plus E.0 (this spec). Two upstream (E.1, E.2).
Thirteen WP code (E.3–E.15). Three WP integration/delete (E.16–E.18);
E.18 cancelled 2026-05-21 (POSE64 not viable). One docs (E.19).

Sub-phase dependencies form a mostly-linear chain with E.7 parallel to
E.5/E.6. Every sub-phase's exit gate includes a green `ctest` run.

---

## Testing strategy

### Library-side (libkalburator)

- **E.1:** new `tests/journal/tst_blobbaselinestore.cpp` covering CRUD,
  schema migration, concurrent reader/writer, mapping clear.
- **E.2:** extend `tests/blob/tst_blobsyncengine.cpp` with 3-way-merge
  scenarios (each of the nine diff cases in §"Upstream deliverables"),
  per-backend handler dispatch, conflict-store persistence.
- **Gate:** PlanStan's ctest (86/26/112) holds across both.

### WP-side

- **E.3–E.6:** unit tests per class. `PalmBackend` round-trips synthetic
  records; `PalmCalendarBackend` round-trips `Incidence::Ptr`;
  `PalmConflictHandler` resolves each conflict shape.
- **E.7:** typed-adapter unit tests (read/write through the adapter, confirm
  bytes match expected Palm-record encoding).
- **E.8:** plugin manager round-trip (load, register, query, unload).
- **E.9–E.15:** per-plugin smoke: build the plugin, register with a
  coordinator configured with a `LocalBlobBackend`, run `coordinator.runSync()`,
  assert expected record motion.
- **E.18 integration:** ❌ Cancelled 2026-05-21. The originally-scoped
  POSE64-driven automated integration suite is not feasible because the
  emulator's DLP timing is too unstable. The scenarios below are still
  valuable; they are exercised today as a mix of per-plugin e2e ctests
  (against `MockBlobBackend` / `LocalBlobBackend`) plus periodic manual
  smoke against real Palm hardware:
  - DateBk6-style multi-category DatebookDB → one `.ics` file per
    category in the expected subdirectory. *(Per-plugin e2e tests cover
    category routing logic; the multi-category device-side fixture is
    a manual exercise.)*
  - Modify-on-device → re-sync → blob reflects the change. *(Manual.)*
  - Delete-on-device → re-sync → blob removed (validates
    `BlobBaselineStore` deletion-propagation). *(Manual; `tst_calendar_v2`
    and siblings cover the same flow against `MockBlobBackend`.)*
  - Conflict (modify both sides between syncs) →
    `PalmConflictHandler` resolves per policy. *(Per-plugin e2e tests
    cover this against `MockBlobBackend`.)*

### Regression guards

- Every sub-phase runs `ctest` and must hold green.
- Upstream sub-phases (E.1, E.2) additionally run PlanStan ctest and must
  hold its baseline.
- End-to-end smoke test runs in E.17 (manual). E.18's automated harness
  was cancelled; the e2e ctests carry the regression load.

---

## Explicitly deferred / non-goals

- **Typed contacts/memos/todos upper layers in libkalburator.** WP builds
  these as WP-internal adapters; upstream extraction waits for a second
  consumer.
- **`SyncMapping` per-record filter predicates.** Category routing via
  virtual sub-calendars avoids needing them. If a future use case wants
  generic record-level filters, that's a later upstream phase.
- **Full Sync Mode UI polish.** Wizard, profile-creation UX, visual
  conflict-review dashboard — Phase F. Phase E delivers the machinery;
  Phase F delivers the UX on top.
- **Publishing the plugin ABI as an external SDK.** No external plugin
  authors exist; the ABI is internal-first. External SDK can wait.
- **`KalburatorConfig.cmake` install target.** Still deferred upstream.
- **Phase-E-driven refactor of PlanStan.** libkalburator's upstream commits
  are validated against PlanStan but do not refactor it. PlanStan continues
  to consume the library as-is.
- **Category slot rebinding on rename.** If the user rebinds a Palm
  category via DateBk6 (rare), mappings need re-validation — out of scope;
  logged as known limitation.
- **Migration of existing user profiles.** Profile format may change
  slightly to accommodate the unified runtime's `SyncMapping` list; a
  migration step lands in E.17. No user-visible data loss; WP's Client Mode
  defaults carry over.

---

## Risks / open items

**R1 — upstream API shape for `twoWayWithBaseline`** (resolved 2026-04-21).
The signature landed matches the sketch exactly. `ConflictPolicy` is passed
per-call. `ConflictHandlerRegistry` is a borrowed pointer, not engine-owned
(see §E.2 "Design decision"). No residual risk.

**R2 — `PalmDeviceConnection` ownership across plugins.** Today
`PalmDeviceConnection` (or equivalent) is owned by the device session. Each
plugin's `PalmBackend` needs access to it. Approach: plugins receive a
non-owning pointer via `createBackends(host, device)`. Device lifetime is
guaranteed to exceed plugin lifetime by the runtime. No shared_ptr gymnastics.

**R3 — Existing Phase-D host impls depend on deleted types.** Phase-D's
`SyncHost_WP` etc. reference `src/sync/` types indirectly (via e.g.
`SyncMode`). E.17 explicitly covers this cleanup; likely needs the Phase-D
files to compile under new types at each sub-phase transition, which means
some forward-declaration or type-alias shims briefly. Should be mechanical.

**R4 — `RecurrenceCapabilities` for Palm.** `Kalburator::Sync::SyncBackend`
has a `capabilities()` method reporting supported recurrence patterns.
Palm Datebook supports only simple repeats (daily/weekly/monthly/yearly,
no BYDAY/BYMONTHDAY/etc.). `PalmCalendarBackend::capabilities()` must
report this accurately so `RecurrenceLossInfo` can warn users on sync. No
external risk; a known piece of work inside E.6.

**R5 — DateBk6 DatebookDB variant name.** DateBk6+ replaces stock
DatebookDB with its own DatebookDB (same name, different schema — extended
fields in the `note` area). The Calendar plugin's claim for `DatebookDB`
may need claim-description variants so users with both DateBk6 installed
and the default Calendar plugin see a clear choice. Tracked but not
Phase-E-blocking; the existing claim-description mechanism handles it.

**R6 — Client-Mode profile migration.** Existing users with Client-Mode
profiles (directories of `.ics` / `.vcf` / `.md` files under
`~/.wildpalms/<profile>/`) need their profile to pick up auto-generated
`SyncMapping`s pointing at those directories on first launch with the new
runtime. E.17 includes this migration; spec is straightforward.

**R7 — Test harness for `PalmDeviceConnection`.** Unit tests for
`PalmBackend` need a mockable device. WP already has a test infrastructure
(per `src/plugins/*/tests/`); we'll extend or mirror it. Low risk.

**R8 — `twoWayWithBaseline` unhandled edge case: both-sides-present,
no-baseline.** The landed Phase B4 implementation has explicit branches
for seven of the nine logically possible (hasA, hasB, hasBase) combos.
The `hasA && hasB && !hasBase` case falls through silently per the
in-code comment "Other edge cases (both missing, or impossible combos)
fall through." In practice this is rare because record IDs are backend-
scoped — two independent backends producing the same ID collide only
under unusual conditions. Risk materialises if `PalmBackend` exposes
stable record IDs (`UniqueID`s from DLP) and the target backend happens
to reuse the same ID space. E.18 integration tests will exercise this;
if it shows up, the fix is a small upstream micro-phase adding an
explicit branch (likely "both identical → treat as convergent, no-op +
baseline" / "both different → treat as conflict"). Not blocking E.3+.

---

## Cross-references

- **Upstream status:** `~/dev/libkalburator/docs/phase0/README.md` — update
  when E.1 and E.2 land.
- **Upstream design docs:** `04h-blob-layer-design.md` "Explicitly deferred"
  list gets items struck as E.1 and E.2 close them.
- **WP integration plan:** `docs/plans/2026-04-20-libkalburator-integration.md`
  — Phase E row expands to reference this spec's sub-phases; Phase F row
  should acknowledge that Mode collapse happened in E rather than F.
- **Cross-repo proposal:** `~/dev/PlanStan/docs/proposals/2026-04-20-sync-library-extraction.md`
  — Status line updated when each upstream sub-phase lands.
- **WP docs superseded in E.19:** `CONDUIT_PLUGIN_DESIGN.md`,
  `plugin-developer-guide.md`, `sdk-plugin-guide.md`, `sdk-shadowstan-advisory.md`,
  `ARCHITECTURE_2026.md`, `SYNC_ENGINE_ARCHITECTURE.md`.
- **Memory references:** `feedback_library_vs_backend_responsibility.md`,
  `feedback_planstan_pretest_for_upstream.md`,
  `project_palm_category_routing.md`.
