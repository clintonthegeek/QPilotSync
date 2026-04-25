# Phase E.10 — Calendar Plugin Design

**Status:** Draft, 2026-04-24
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.10 (line 588)
**Predecessor:** Phase E.9 (Memo plugin landed 2026-04-23)
**Successor:** Phase E.11 (Todo plugin)

## Goal

Rewrite the Calendar conduit as the second new-ABI `IBackendPlugin`. Become the
first real consumer of `PalmBackend` and `PalmCalendarBackend`. Surface Palm
Datebook category slots as virtual sub-collections that route end-to-end through
`BlobSyncEngine::twoWayWithBaseline`, with a calendar-aware conflict handler
that understands recurrence/alarm/timezone semantics.

Land behind the same `WILDPALMS_<NAME>_PLUGIN_V2` toggle pattern memo
established; both legacy `CalendarConduit` and the new plugin coexist until E.16.

## Decisions

The five questions answered during brainstorming, in resolution order:

### Decision #1 — Sub-calendar shape on the blob face

The `IBlobBackend` returned by the Calendar plugin exposes **one collection per
populated category slot**: `palm:calendar/0` ("Unfiled", always present) plus
`palm:calendar/<N>` for any slot 1..15 the AppInfo block names. Records route to
the matching collection by `PalmRecord::category` on read and on write.

**Why:** the parent spec's "virtual sub-calendars" framing maps one-to-one onto
both layers (typed `SyncBackend` from E.6 and the new blob backend), and it
makes the parent spec's E.10 exit-gate phrase "category routing verified"
testable at the blob boundary today, without depending on PlanStan's routing
engine (which has not yet been imported upstream — see project memory
`project_palm_category_routing.md`).

**Alternative considered:** single flat `palm:datebook` collection with category
travelling as `BackendRecord` metadata. Rejected because category routing then
becomes a target-side concern that requires PlanStan, pushing the exit gate's
verification claim out to E.16+.

### Decision #2 — AppInfo parsing lands in E.10

Add a `CategoryAppInfoReader` helper (pisock `unpack_CategoryAppInfo` over the
Datebook AppInfo block) that populates `CategoryMappingStore` at plugin
construction. Slots 1..15 surface automatically when the device defines them.

**Why:** the reader is what E.17 would have to invent anyway. Landing it now
makes the E.10 smoke test category-aware against a DateBk6-shaped fixture
instead of asserting routing against a manually-seeded store. The remaining
E.17 work (`PilotLinkPalmDatabaseAccess::readAppBlock` integration, live-device
test in POSE64) is mechanical.

**Alternative considered:** keep AppInfo deferred per memo's project memory.
Rejected because the deferral was driven by memo not needing categories at all
(`MemoBlobBackend` runs fine with a null store), whereas Calendar's whole
sub-calendar story dies without populated slot names.

### Decision #3 — Calendar-specific `ConflictHandler` with three overlays

Land `CalendarConflictHandler` that delegates to `PalmConflictHandler` for the
shared archive/secret/category overlays, then applies three calendar-aware
overlays before falling through to the base resolution:

1. **Alarm-only diff** → `KeepLeft` paired with merged alarms. Alarms are
   independent metadata; reconciling them is a non-conflict.
2. **EXDATE-only diff** → `Merge` with EXDATE-list union. Preserves "user
   removed instance X on Palm AND removed instance Y on target" semantics.
3. **DTSTART tz-only diff** with one floating + one zoned → prefer floating.
   Palm semantics are floating; the zoned counterpart was almost certainly
   autotagged by a CalDAV server.

**Why:** calendar events have substantively different conflict semantics from
memos. `PalmConflictHandler`'s pure `lastModified` tie-break would, for example,
silently drop an EXDATE list every time the Palm is modified after a target-side
exception was added. These three rules cover the three concrete failure modes
that surfaced in the legacy `CalendarConduit`'s history.

**Alternative considered:** rely on `PalmConflictHandler` alone (memo's
approach). Rejected per the rationale above. A "stub the overlays for later"
midpoint was rejected because the rules are well-known today; deferral would
buy nothing.

### Decision #4 — Reuse `CalendarView` untouched

The plugin's `createMainView(parent)` returns `new CalendarView(parent)`,
identical to memo's pattern. No UI changes in E.10.

**Why:** `CalendarView` doesn't depend on `SyncConduitBase`/`CalendarConduit`
internals (verified during exploration); it consumes `KCalendarCore` types
directly. Rewriting the view is out of scope for any reading of E.10.

If the implementation phase discovers real coupling, fall back to a minimal
adapter (small new class, not a rewrite). Flagged so the writing-plans skill
can encode it as a contingent step.

### Decision #5 — `tst_calendar_v2` uses `MockBlobBackend`, not `LocalBlobBackend`

The exit-gate phrase "smoke-syncs against `LocalBlobBackend`" cannot be honored
literally without solving the cross-id-space problem
(`LocalBlobBackend::createRecord` returns the absolute file path as the new
record id, breaking baseline matching on the second sync — `BlobSyncEngine`
matches by literal id). `IDMappingStore` is an E.15+ deliverable per project
memory `project_phase_e9_memo.md`.

`tst_calendar_v2` therefore uses `MockBlobBackend` (preserves ids verbatim),
matching the exact deviation memo took for `tst_memo_v2`. The effective E.10
exit gate becomes "Calendar plugin smoke-syncs against `MockBlobBackend`;
category routing verified across virtual sub-collections."

**Alternative considered:** ship a `TestLocalBlobBackend` subclass that
preserves `record.id`. Rejected — proves nothing the mock doesn't already
prove, and the gate text already drifted for memo without controversy.

Pulling `IDMappingStore` forward into E.10 was also considered and rejected as
an unjustified scope expansion.

## Scope

**In scope:**

- New plugin: `CalendarBackendPlugin` + `CalendarBlobBackend` +
  `CalendarConflictHandler` under `src/plugins/calendar/` (the existing
  submodule `wildpalms-conduit-calendar`).
- New helper: `CategoryAppInfoReader` (pure-function pisock wrapper).
- `IPalmDatabaseAccess::readAppBlock` virtual + `PalmBackend::readAppBlock`
  pass-through + `MockPalmDatabaseAccess::readAppBlock` test override + a
  one-line `PilotLinkPalmDatabaseAccess::readAppBlock` implementation
  forwarding to the existing `KPilotLink::readAppBlock`.
- CMake toggle `WILDPALMS_CALENDAR_PLUGIN_V2=ON` (default on); legacy
  `CalendarConduit` remains buildable when off; both `.so`s coexist in CI.
- New manifest: `calendar-backend-plugin.json` with
  `X-WildPalms-PluginType: "backend"`.
- Tests under `tests/plugins/calendar/` (parent repo).
- Plugin returns both `ProvidedBackends` slots: `blob = CalendarBlobBackend`
  and `calendar = PalmCalendarBackend` (existing E.6 type, unchanged).
- Land row-flip in master spec; new project memory entry.

**Out of scope (deferred, called out in plan header):**

- `IDMappingStore` / `LocalBlobBackend` smoke (E.15+).
- Live-device `PilotLinkPalmDatabaseAccess::readAppBlock` integration test
  (E.18, POSE64 sandbox).
- `ConflictDialog` new-plugin lookup-path regression (open from E.9; tracked).
- `CategoryMappingStore` rename/move to `src/palm/` (E.11/E.12 when
  contacts/todos consume it).
- Deleting `CalendarConduit`, `calendarmapper.{h,cpp}`,
  `calendar-conduit.json` (E.16).
- Main-window typed-calendar-tab consumption of `PalmCalendarBackend` via
  `SyncHost_WP` (E.16/E.17 unified runtime).
- Registry-side `lookupHandler("palm")` API addition. The plugin instantiates
  its own `PalmConflictHandler` for delegation (same constructor E.5 uses).
- `CalendarView` rewrite or refactor.

## Architecture

The Calendar plugin is the second `IBackendPlugin`. It returns **both** slots
of `IBackendPlugin::ProvidedBackends`:

- `blob = CalendarBlobBackend` — the new transcoding `IBlobBackend`. One
  collection per populated category slot. `mediaType = "text/calendar"`.
- `calendar = PalmCalendarBackend` — the existing typed `SyncBackend` from E.6,
  returned unchanged. Future PlanStan routing engine (when imported upstream)
  and the E.16/E.17 unified-runtime calendar tab consume it.

Both backends share a single `CategoryMappingStore` instance owned by the
plugin and populated at `createBackends()` time from
`CategoryAppInfoReader::populateFromAppInfo(store, "DatebookDB",
device->palmBackend()->readAppBlock("DatebookDB"))`. Both backends route by
`PalmRecord::category`.

### Class layout

All under `src/plugins/calendar/` (submodule `wildpalms-conduit-calendar`):

- `CalendarBackendPlugin : public QObject, public IBackendPlugin` — plugin
  shell with `K_PLUGIN_FACTORY_WITH_JSON`. Owns the per-session
  `CategoryMappingStore`. Implements all `IBackendPlugin` virtuals; returns
  both backends from `createBackends`; returns the conflict handler from
  `createConflictHandler`; returns `CalendarView` from `createMainView`.
- `CalendarBlobBackend : public Kalburator::Sync::IBlobBackend` — wraps
  `PalmDeviceConnection::palmBackend()`'s `palm:datebook` collection. Surfaces
  N virtual collections; routes records by slot.
- `CalendarConflictHandler : public Kalburator::Sync::QSyncCore::ConflictHandler`
  — composes (owns) a `PalmConflictHandler` for delegation. Implements the
  three calendar-specific overlays before falling through.
- `CategoryAppInfoReader` — namespace-scope free functions, no class. Pure
  helper; unit-testable without a Palm device.

Thin transcoding helper, also under `src/plugins/calendar/`:

- `IcsTranscoder` — namespace-scope free functions:
  `QByteArray encodePalmToIcs(const PalmRecord &)` and
  `std::optional<PalmRecord> decodeIcsToPalm(const QByteArray &, int slotHint)`.
  Wraps the existing `DatebookCodec` (E.6) plus
  `KCalendarCore::ICalFormat::toString` / `fromString`.

### Surface additions on `IPalmDatabaseAccess` / `PalmBackend`

Required because `readAppBlock` exists today only at the
`KPilotLink`/`KPilotDeviceLink` layer:

```cpp
// src/palm/sync/ipalmdatabaseaccess.h
class IPalmDatabaseAccess {
public:
    // ... existing ...
    virtual QByteArray readAppBlock(const QString &dbName) = 0;
};
```

```cpp
// src/palm/sync/palmbackend.h
class PalmBackend : public Kalburator::Sync::IBlobBackend {
public:
    // ... existing ...
    QByteArray readAppBlock(const QString &dbName);  // forwards to m_device
};
```

`MockPalmDatabaseAccess` gains a `setAppBlock(const QString &, const QByteArray &)`
test setter so `tst_categoryappinforeader` and `tst_calendar_v2` can pre-canned
AppInfo bytes. `PilotLinkPalmDatabaseAccess::readAppBlock` is a one-line
forward to the existing `KPilotLink::readAppBlock`.

## Components

### `CategoryAppInfoReader`

Pure-function helper, ~80 LOC + unit test.

```cpp
namespace WildPalms::PalmCalendar {

struct CategoryNames {
    std::array<QString, 16> names;  // names[0] always "Unfiled" if blank
};

std::optional<CategoryNames>
parseDatebookAppInfo(const QByteArray &appInfoBytes);

void populateFromAppInfo(CategoryMappingStore &store,
                         const QString &dbName,
                         const QByteArray &appInfoBytes);

}
```

Wraps pisock's `unpack_CategoryAppInfo` over the AppInfo block layout —
`name[16][16]`, `id[16]`, `lastUniqueID`, `padding`. Returns 16 `QString`s
(slot 0 forced to "Unfiled" if blank). `populateFromAppInfo` calls the parser
and drives `store.setSlotName(dbName, slot, name)` for every non-empty slot
1..15. Slot 0 stays implicit per the existing store contract.

The plugin calls `populateFromAppInfo(*m_store, "DatebookDB",
device->palmBackend()->readAppBlock("DatebookDB"))` from `createBackends()`.

### `CalendarBlobBackend`

~250 LOC + unit tests.

- Constructor: `(PalmBackend *palmBackend, const CategoryMappingStore *store,
  QObject *parent = nullptr)` — both pointers non-owning, both must outlive.
- `backendId()` → `"calendar"`. `displayName()` → `"Palm Calendar"`.
- `availableCollections()` → slot 0 ("Unfiled") plus one entry per
  `store->slotName("DatebookDB", N)` that's non-empty. Each entry:
  - `id = "palm:calendar/<N>"`
  - `displayName = name`
  - `type = "calendar"`
  - `mediaType = "text/calendar"`
- `loadRecords(coll)` — parses `<N>` from collection id, fetches all records
  from `palmBackend`'s `palm:datebook`, filters by `record.category == N`,
  transcodes wire → iCal via `IcsTranscoder::encodePalmToIcs`. Each
  `BackendRecord.id` = original Palm record id (preserves cross-sync
  identity). Each `.contentType = "text/calendar"`.
- `createRecord(coll, rec)` — parses slot from coll id; decodes iCal →
  `PalmRecord` via `IcsTranscoder::decodeIcsToPalm(rec.data, slot)`; calls
  `palmBackend->createPalmRecord("palm:datebook", palmRec, slot)`. Returns the
  Palm-side id.
- `updateRecord(rec)` — finds the slot from the Palm-side record's existing
  category (round-trip via `palmBackend->loadRecord`); decodes iCal; calls
  `palmBackend->updatePalmRecord("palm:datebook", palmRec)`.
- `deleteRecord(id)` — forwards to `palmBackend->deleteRecord(id)`.
- `modifiedSince` / `deletedSince` / `supportsDeleteTracking` — forward to
  `palmBackend`, then filter by slot from collection id.

### `CalendarConflictHandler`

~180 LOC + unit tests.

- Constructor: `(PalmDeviceConnection *device, CategoryMappingStore *store,
  QObject *parent = nullptr)` — instantiates and owns its own
  `PalmConflictHandler(device, store)` for delegation.
- `resolve(snapshot pair, baseline)`:
  1. Pre-decode both sides' content as `KCalendarCore::Event::Ptr` via
     `KCalendarCore::ICalFormat::fromString`. If either decode fails, delegate
     straight to `PalmConflictHandler::resolve`.
  2. Compute structural diffs: alarm-only, EXDATE-only, DTSTART-tz-only.
  3. **Alarm-only** → return `KeepLeft` with merged alarms (alarms are
     independent metadata; both can apply).
  4. **EXDATE-only** → return `Merge` with EXDATE-list union; produce a
     synthesized merged-side iCal blob.
  5. **DTSTART tz-only** with one floating + one zoned → prefer floating
     (Palm semantics).
  6. Otherwise → delegate to `PalmConflictHandler::resolve`.
- The plugin's `createConflictHandler()` returns a fresh instance; the manager
  registers it under backend id `"calendar"` (matches the plugin id, the same
  pattern memo would have followed if memo had returned a non-null handler).

### `CalendarBackendPlugin`

Glue, ~120 LOC + unit tests.

- `pluginId()` → `"calendar"`.
- `displayName()` → `"Calendar"`.
- `description()` → `"Synchronizes Palm DatebookDB with iCalendar files"`.
- `version()` → `"2.0"`.
- `claimedDatabases()` → `{"DatebookDB"}`.
- `createBackends(host, device)`:
  1. Constructs and owns one `CategoryMappingStore` on first call (per
     session — re-entry with same plugin instance reuses).
  2. `populateFromAppInfo(*m_store, "DatebookDB",
     device->palmBackend()->readAppBlock("DatebookDB"))`.
  3. Returns `ProvidedBackends{
       .blob     = new CalendarBlobBackend(device->palmBackend(), m_store.get()),
       .calendar = new PalmCalendarBackend(device->device(), m_store.get()),
     }`.
- `createConflictHandler()` → `new CalendarConflictHandler(device, m_store.get())`.
  Note: this requires `PalmDeviceConnection` to be reachable from
  `createConflictHandler` — the plugin caches the `PalmDeviceConnection*`
  passed to `createBackends` for this purpose.
- `hasMainView()` → `true`. `createMainView(parent)` →
  `new CalendarView(parent)`. `mainViewName()` → `"Calendar"`.
  `mainViewIcon()` → `view-calendar` theme icon.
- `enrichConflictSnapshot` / `formatConflictRecordHtml` — calendar-aware:
  decode iCal, expose summary + DTSTART as metadata, render as HTML with the
  summary as `<h3>` and a brief description.

## Data flow

Per sync cycle (driven by the coordinator wiring landed in E.16; for the E.10
smoke test, the test harness drives `BlobSyncEngine::twoWayWithBaseline`
directly):

1. `BackendPluginManager` calls `CalendarBackendPlugin::createBackends`.
2. Plugin reads AppInfo → populates store → returns
   `{blob: CalendarBlobBackend, calendar: PalmCalendarBackend}`.
3. Coordinator calls `BlobSyncEngine::twoWayWithBaseline(palmBackend's
   palm:datebook, calendarBlobBackend's palm:calendar/<N>, baseline,
   registry)` for each populated slot. (Per-collection mappings.)
4. For each slot:
   - Source side: Palm Datebook records filtered to that slot, served as Palm
     wire bytes.
   - Target side: that slot's iCal records served as `text/calendar` bytes.
   - Engine three-way-merges via baseline; on conflict, dispatches to
     `CalendarConflictHandler` registered under `"calendar"`.
5. Resolved diffs apply through `createRecord` / `updateRecord` /
   `deleteRecord`, which transcode and forward to `palmBackend` (preserving
   slot via the category-aware helpers landed in E.9).

## Error handling

- `CategoryAppInfoReader::parseDatebookAppInfo` returns `std::nullopt` on bad
  input (length < expected, decode error). Plugin treats nullopt as "no named
  slots" — only `palm:calendar/0` surfaces. Logged at warning level once per
  session.
- `PalmBackend::readAppBlock` failure (no DatebookDB on device, transport
  error) → same path: empty store, "Unfiled" only, warning logged.
- `IcsTranscoder` decode failure inside `loadRecords`: skip that record,
  append to a per-call `QStringList m_skippedIds` exposed via a `lastErrors()`
  accessor for tests. Non-fatal — partial collection still returned.
- `IcsTranscoder` encode failure inside `createRecord`/`updateRecord`: return
  empty id / `false` respectively. Engine treats as a write failure; conflict
  store records the attempt for retry on next sync.
- `CalendarConflictHandler` decode failures fall through to
  `PalmConflictHandler` rather than failing the resolve — base path always
  works.
- All boundary errors use `qCWarning(WP_CALENDAR)` against a new
  `Q_LOGGING_CATEGORY` so users can grep one prefix.

## Testing

Test files live in `tests/plugins/calendar/` in the parent repo (matches memo's
split: source in submodule, tests in parent).

| Test | What it pins | Counterparty |
|------|--------------|--------------|
| `tst_categoryappinforeader.cpp` | Pisock-shape parsing: empty AppInfo → "Unfiled" only; named slots populate; truncated bytes → nullopt; slot 0 normalization | none (pure-function tests) |
| `tst_calendarblobbackend.cpp` | `availableCollections` reflects store contents; per-collection filtering; round-trip (load → modify → update → reload); category preservation through createRecord; modifiedSince/deletedSince forwarding | `MockPalmDatabaseAccess` + `PalmBackend` |
| `tst_calendarconflicthandler.cpp` | Each of the three overlays (alarm-only, EXDATE-only, tz-only) resolves correctly; non-matching shapes delegate to `PalmConflictHandler`; bad bytes delegate to base | constructed with own `PalmConflictHandler` |
| `tst_calendarbackendplugin.cpp` | Plugin metadata (`pluginId == "calendar"`, `claimedDatabases == ["DatebookDB"]`); `createBackends` returns both `blob` and `calendar` non-null; `hasMainView == true`; `createConflictHandler` non-null | dummy `PalmDeviceConnection` |
| `tst_calendar_v2.cpp` (the smoke test) | Full sync: build a `MockPalmDatabaseAccess` with a DateBk6-style fixture (10 events across 4 categories incl. one in slot 0), AppInfo block naming slots 1-3, run `twoWayWithBaseline` for each `palm:calendar/<N>` collection vs a `MockBlobBackend` target. Assert: 4 collections appear; events route to the right collection; modifying one event on each side then re-syncing produces no duplicates and no spurious conflicts; deleting one event on Palm side propagates to target | `MockBlobBackend` (per Decision #5) |

Test budget target: ~50 tests across the five files (matches memo's ~47 total
at E.9 land time).

## Risks

**R1 — `KCalendarCore::ICalFormat` round-trip stability for Palm-shaped
events.** The `DatebookCodec` lands in E.6 with passing tests, but those tests
go bytes → `Event::Ptr` → bytes. The new path is bytes → `Event::Ptr` → iCal
→ `Event::Ptr` → bytes. iCal serialization is permissive; round-trip stability
through it is not guaranteed for every field the codec preserves (e.g.
non-standard alarm types, Palm's repeat-no-end semantics). Mitigation: add
focused round-trip tests in `tst_calendarblobbackend` for each codec field
that's known to be sensitive; if any field round-trips lossily, hold the wire
bytes verbatim in a custom iCal X-property (`X-WildPalms-PalmRecord`) for the
field's restoration.

**R2 — `CalendarConflictHandler` overlay false positives.** "Alarm-only diff"
is well-defined only when alarm sets can be reliably normalized. If the codec
emits semantically-equivalent-but-textually-different alarm representations on
each side, the alarm-only check would never trigger. Mitigation: normalize
alarm sets via a canonical sort/comparison helper inside the handler;
unit-test against the cases the codec actually produces.

**R3 — Plugin caching `PalmDeviceConnection*`.** `createConflictHandler` needs
device access but receives no parameter. Caching the pointer from
`createBackends` works only because the manager guarantees a single session
has a single `PalmDeviceConnection` and `createConflictHandler` is called
after `createBackends`. Document the contract in `IBackendPlugin.h`'s
`createConflictHandler` doc; assert non-null in the plugin.

**R4 — `IDMappingStore` deferral persists.** `tst_calendar_v2`'s
`MockBlobBackend` deviation makes the test category-routing-correct but not
representative of a true Palm ↔ filesystem sync. E.18 (POSE64 integration)
is the eventual cover for that gap; E.15 is the eventual fix for the
underlying id-space mismatch. Recorded in the memory note for future-self
reference.

**R5 — `ConflictDialog` regression continues.** Carried forward from E.9 —
interactive conflicts on calendar records will not surface until the
ConflictDialog new-plugin lookup-path lands. Policy-driven resolution via
`CalendarConflictHandler` works fine.

## Documentation deltas

- Master spec row E.10 flips to `✅ **E.10**` with one-paragraph summary at
  land time.
- New project memory note `project_phase_e10_calendar.md`: AppInfo reader
  landed; CalendarConflictHandler with three overlays; cross-id-space
  deferral persists; toggle pattern.
- No changes to `docs/PLUGIN_ABI.md` — that doc lands in E.19.
- No changes to `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`'s
  category-routing section — current text remains accurate.
