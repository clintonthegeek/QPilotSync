# Proposal to libkalburator: `FilteredCollectionBackend` + `RecordFilter`

**Date:** 2026-05-28
**From:** WildPalms (a downstream `SyncEngine` / `SyncBackend` consumer)
**To:** libkalburator maintainer (+ PlanStan as co-consumer)
**Status:** **CLOSED — SHIPPED in libkalburator v0.59** (accepted as-proposed with
a minor ctor refinement). WildPalms consumes the library class directly; pinned
`v0.69`. See the Resolution section below. The original proposal text (§0–§6) is
preserved verbatim for the record.
**Why now:** WildPalms is restoring its remote sync via the hub-and-spoke topology you adopted in v0.57. The remote leg needs to route records by the canonical `categories` field. We could build the slicing wholly inside WP, but it's a generic primitive PlanStan can reuse (any property-based tag filtering), so it belongs in libkalburator — small, narrow, persistable.

---

## Resolution — SHIPPED (CLOSED 2026-06-11)

libkalburator **accepted this RFC and shipped it in v0.59** as
`Kalburator::Sinks::FilteredCollectionBackend` + `Kalburator::Shape::RecordFilter`
(both present in the v0.69 pin WildPalms now tracks). `RecordFilter` landed exactly
as proposed in §2.1: `enum class Op { Contains, Equals }; PropertyId property;
Op op = Op::Contains; QVariant value; bool matches(const QJsonDocument&)`. The
backend's contract — `backendType()=="filtered-view"`, `resourceId()`,
`isAvailable()`, shape/`discoveredWritable` delegation, and the additive-`Contains`
/ authoritative-`Equals` write semantics — all match §2.2.

**API delta from the proposal** (minor, accepted refinements):

- The base class is `Kalburator::Sync::SyncBackendBase`, not `Sync::SyncBackend`
  (post-P3 backend-interface neutralization renamed the base after this RFC was
  written; the FCB is shape-transparent either way).
- The shipped ctor adds two parameters over §2.2:

  ```cpp
  FilteredCollectionBackend(SyncBackendBase* parentBackend,
                            QString parentBackendId,             // NEW: id to match on unregister
                            QString parentCollectionId,
                            QString virtualCollectionId,
                            RecordFilter filter,
                            BackendRegistry* registry = nullptr, // NEW: explicit, for the §2.4 hook
                            QString displayNameOverride = QString(),
                            QObject* parent = nullptr);
  ```

  `parentBackendId` + `registry` make the §2.4 parent-lifetime hook explicit: the
  FCB connects to `BackendRegistry::backendInstanceUnregistered(QString)` and nulls
  its borrowed parent on teardown, rather than inferring the id/registry. The
  `displayNameOverride` + `QObject* parent` tail is preserved as proposed.

**WildPalms consumption — now live, not "planned" (§5).**
`PalmRuntime::buildRouteLogicalCalendars` builds one FCB per user category route,
registered under `wp-route-<mappingId>`, with
`RecordFilter{ property = categories, op = Contains, value = <categoryName> }`
wrapping `wp-hub:<domain>`. The route's `LogicalCalendar` binds that view as Primary
and the remote as Sync1, and the union of per-domain (Palm↔hub) and per-route
(view↔remote) mappings runs as one `Star` queue — exactly the §5 design.

**Refinement landed in sub-project A (2026-06-11 config substrate).** Route rows are
now **names-first**: `sourceCalendar == "palm:<domain>/name:<categoryName>"` instead
of the old slot-index form. `WildPalms::Runtime::translateRouteSpec` parses the
category name straight from the row and feeds it as the `Contains` filter value (no
device-slot lookup needed to materialize the view), and additionally reports a
per-route `RouteStatus` (Active / WaitingForDevice / NoFreeSlot / NotARoute)
describing whether the named category is bound to a device slot yet. **The FCB
itself is unchanged** — only WildPalms' computation of the filter value moved from
slot→name resolution to a direct name parse. There is **no WP-local
FilteredCollectionBackend**; the earlier roadmap note to that effect was stale — WP
consumes the library class directly.

**Pin state:** WildPalms tracks `v0.69` (`CMakeLists.txt:63`); the FCB has been
available since v0.59, so no further pin bump is needed for this primitive. Per §6,
PlanStan still does not consume it (the primitive remains opt-in; PlanStan stays
green without a code change).

---

## 0. TL;DR

A new generic primitive in libkalburator:

```cpp
// libkalburator src/types/recordfilter.h
namespace Kalburator::Shape {
struct RecordFilter {
    enum class Op { Contains, Equals };
    PropertyId property;
    Op        op    = Op::Contains;
    QVariant  value;
    bool matches(const QJsonDocument &canonRecord) const;
};
}

// libkalburator src/universal/filteredcollectionbackend.{h,cpp}
namespace Kalburator::Sinks {
class FilteredCollectionBackend : public Sync::SyncBackend {
public:
    FilteredCollectionBackend(Sync::SyncBackend *parentBackend,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Shape::RecordFilter filter,
                              QObject *parent = nullptr);
    // ...standard SyncBackend overrides...
};
}
```

One filter per instance, one virtual collection. Each instance presents a
sliced view of a parent backend's collection: reads filter; writes stamp the
filter value into the record (`Contains`: add to list if missing; `Equals`:
set). The parent backend pointer is **borrowed**: the FCB connects to
`BackendRegistry::backendInstanceUnregistered` and marks itself unavailable
if the parent is unregistered (see §2.4).

Each instance is registered with `BackendRegistry::registerBackendInstance`
exactly like any other backend instance — the engine doesn't need to know
it's filtered. It is **not** registered as a `BackendContribution`: there is
no `backendType()`-driven instantiation from user config and no add-account
UI surface; the consuming app composes FCBs at runtime.

WildPalms instantiates one of these per user-configured **category route**
(per `LogicalCalendar` whose primary is the filtered view + sync is a remote
calendar). PlanStan can reuse the primitive for any record-property
filtering (priorities, statuses, custom tags) it ever needs.

## 1. Why in libkalburator, not WildPalms

We considered putting this entirely in WildPalms. Two reasons it belongs in
libkalburator:

1. **Reusable shared abstraction.** The mechanism is *not* Palm- or
   category-specific. Filtering canon records by a typed property predicate
   is a generic shape-graph capability — PlanStan could use it the moment it
   wants to expose, say, "high-priority TODOs across all backends" as a
   separate sync target.
2. **Keeps WildPalms cleanly aligned with the engine contract.** The engine
   binds backends + collections 1:1; the cleanest expression of "this remote
   syncs to a *slice* of a hub collection" is "this slice IS a backend
   collection." Putting that in the library means downstream consumers don't
   reinvent the abstraction (and `FilteredCollectionBackend` naturally
   inherits the engine's diff/baseline/conflict machinery without
   special-casing).

The cost is one small class + one struct + tests. No `SyncEngine` change.

## 2. API in detail

### 2.1 `Kalburator::Shape::RecordFilter`

```cpp
struct RecordFilter {
    enum class Op { Contains, Equals };
    PropertyId property;
    Op        op    = Op::Contains;
    QVariant  value;

    /// Evaluate against a canon record (parsed JSON of the BackendRecord
    /// payload, conforming to the canonical encoding for the record's domain).
    /// `Contains`: property must be a JSON array containing `value`
    ///             (string compare for QString; structural for richer types).
    /// `Equals`:   property must equal `value`.
    /// Returns false on missing property or type mismatch (no exceptions).
    bool matches(const QJsonDocument &canonRecord) const;
};
```

- Property identifies the canon property by its `PropertyId` (the same
  identifier used in property catalogues / differs). For calendar/contacts/todo,
  `categories` is a `StringList`-kind property already defined in their canon
  catalogues.
- `PropertyId` is shape-scoped: the filter is implicitly tied to the shape
  the parent collection exposes (via the FCB's `shapeFor(parentColId)`).
  Constructing a `FilteredCollectionBackend` with a `PropertyId` that the
  parent's shape's property catalogue does not contain is a programmer
  error; the implementation should assert (debug) and treat it as
  always-non-matching (release).
- The set of ops is deliberately tiny (YAGNI). Extensions can be added when
  a real need arises; we'd rather not ship a query language.

### 2.1.1 Note on parsing

`matches` is shown here using `QJsonDocument::fromJson` against the
record's payload bytes. If there is a preferred canon-record accessor
in the shape layer (`Kalburator::Shape::RecordDiffer` already does this
work), the implementation should route through that instead — both for
consistency and so any future canon-envelope changes flow through one
place. The proposal is agnostic on which path; flagging here so we can
align with whatever the differs already do.

### 2.2 `FilteredCollectionBackend`

A `SyncBackend` (so the engine can sync to it directly). One instance =
one virtual collection.

```cpp
class FilteredCollectionBackend : public Sync::SyncBackend {
public:
    FilteredCollectionBackend(Sync::SyncBackend *parentBackend,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Shape::RecordFilter filter,
                              QString displayNameOverride = {},
                              QObject *parent = nullptr);

    QString backendType()  const override { return QStringLiteral("filtered-view"); }
    QString displayName()  const override;
    QString resourceId()   const override;
    bool    isAvailable()  const override; // false once parent unregistered

    QList<Shape::Shape> nativeShapes() const override; // delegates: parent.shapeFor(parentColId)
    Shape::Shape shapeFor(const QString &collectionId) const override;

    QList<Sync::CollectionInfo> availableCollections() override; // returns one entry
    Sync::CollectionInfo        collectionInfo(const QString &collectionId) override;

    bool    discoveredWritable(const QString &collectionId) const override; // forwards

    QList<Sync::BackendRecord>            loadRecords(const QString &collectionId) override;
    std::optional<Sync::BackendRecord>    loadRecord(const QString &recordId)      override;
    QString createRecord(const QString &collectionId,
                         const Sync::BackendRecord &record) override;
    bool    updateRecord(const Sync::BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId)            override;

private:
    Sync::SyncBackend *m_parent = nullptr;  // borrowed; nulled on parent unregister
    QString             m_parentColId;
    QString             m_virtualColId;
    Shape::RecordFilter m_filter;
    QString             m_displayNameOverride;
};
```

Behavior:

- **Reads.** `loadRecords(virtualColId)` calls `m_parent->loadRecords(m_parentColId)`,
  parses each record's payload as canon JSON, and filters by
  `m_filter.matches(...)`. Records not parseable as canon JSON fail-closed
  (omitted, no exception). `loadRecord(recordId)` delegates and applies the
  filter (returns `nullopt` if it doesn't pass).
- **Writes (`Contains`, additive).** `createRecord` / `updateRecord` ensure
  the filter property's array contains the filter value: **append if absent;
  preserve existing element order if present.** Other elements in the array
  are preserved untouched, so a record legitimately tagged
  `["Work","Important"]` keeps `Important` when written through a `Work`
  filter. String comparison is **case-sensitive** (canonical for category
  routes; consumers can normalise upstream if they need case-folding).
- **Writes (`Equals`, filter-authoritative).** `createRecord` / `updateRecord`
  **always overwrite** the filter property to the filter value, on both
  create and update. There is no escape hatch: a record that lives in an
  `Equals:status=Done` view *is* Done. To "move" a record out of an
  `Equals` view, delete it from this view and create it in another. This
  is the intentional design for sliced sync targets; if a consumer wants
  user-overridable values they should not use a filtered view.
- **`deleteRecord(id)`** delegates unchanged to the parent.
- **`shapeFor` / `nativeShapes`** delegate: a filtered view has the same
  shape as the parent collection (the records are canon, just sliced).
- **Identity / `resourceId`.** Returns a canonical, deterministic string:

  ```
  filtered-view:<parent.resourceId>/<parentColId>?p=<propertyId>&op=<contains|equals>&v=<urlencode(canonJson(value))>
  ```

  Keys are in the fixed order `p`, `op`, `v`. `canonJson(value)` is the
  canonical JSON serialisation of the filter value (sorted keys for
  objects, no whitespace). `urlencode` is RFC-3986 percent-encoding.
  The engine's baseline store keys on `resourceId()`, so this must be
  stable across runs and across equivalent filter constructions.
- **`discoveredWritable(collectionId)`** delegates to
  `m_parent->discoveredWritable(m_parentColId)`. v0.57's authority
  enforcement on the write path therefore applies transparently to the
  filtered view: a read-only parent yields a read-only view.
- **`isAvailable()`** returns `m_parent && m_parent->isAvailable()`. Once
  the parent has been unregistered (see §2.4), `m_parent` is null and
  `isAvailable()` returns false; subsequent reads return an empty list and
  subsequent writes return a failure value (false / empty record id) cleanly.

### 2.3 `CollectionInfo` composition

`availableCollections()` returns exactly one entry, for `m_virtualColId`.
The entry's fields compose from the parent's `CollectionInfo` for
`m_parentColId` as follows:

- `id` = `m_virtualColId`.
- `displayName` = `m_displayNameOverride` if non-empty, else
  `"<parent.collectionInfo(parentColId).displayName> [<filterDescription>]"`
  where `<filterDescription>` is human-friendly (e.g. `"categories ∋ Work"`
  for `Contains`, `"status = Done"` for `Equals`). The override exists
  precisely because consumers (WildPalms category routes) already know the
  user-meaningful name and shouldn't have to live with the composed default.
- `color` is inherited from the parent's `CollectionInfo`. Consumers that
  want a distinct per-route colour can mutate the returned `CollectionInfo`
  before exposing it; the FCB itself does not surface a colour-override
  constructor argument (YAGNI; not requested by either consumer).
- `readOnly` is inherited from the parent's `CollectionInfo`. (Authority
  enforcement on the write path goes through `discoveredWritable` —
  see §2.2 — but the `readOnly` flag on `CollectionInfo` also needs to be
  honest for UI consumers that key off it.)

`collectionInfo(collectionId)` returns the same composed `CollectionInfo`
when `collectionId == m_virtualColId`, and a default-constructed value
otherwise (matching existing backend convention).

### 2.4 Parent backend lifetime

The FCB borrows the parent backend pointer; it does not own it. To keep
this safe under registry teardown, the constructor connects to
`BackendRegistry::backendInstanceUnregistered(QString)`. On receiving
that signal with the parent's backend id, the FCB:

1. Nulls `m_parent` (so subsequent `isAvailable()` returns false and
   subsequent reads/writes return clean failure values).
2. Emits its own `availabilityChanged()` (if `SyncBackend` exposes one;
   otherwise nothing).
3. Optionally self-unregisters from `BackendRegistry` — recommended,
   since a filtered view of a now-gone parent has no useful future.

The FCB does **not** observe the parent backend's `availabilityChanged`
signal for transient unavailability — being temporarily offline (e.g.
network) is the parent's concern; `isAvailable()` delegates through, and
that's enough. The unregister hook is specifically for permanent
teardown.

### 2.5 Loss profile

A filtered view does not change the shape — no loss profile to introduce.
The class is shape-transparent for reads. Writes mutate exactly one
property (the filter property), with semantics fully documented in §2.2:
`Contains` is additive (no loss of existing values); `Equals` is
overwriting (intentional, by design of the view). This is filter
*stamping*, not transcoding loss.

## 3. What we are NOT asking for

- No query language. No multi-property predicates, no OR/AND, no
  comparisons beyond `Contains`/`Equals`. If/when someone needs more,
  add ops one at a time with concrete use cases.
- No indexing or caching. `loadRecords` parses on the fly. Indexed
  filtering can be a v2 optimization if profiling demands.
- No persistence format for filter sets — that's the consumer's job.

## 4. Tests we'd hope to see land with it

- Round-trip: stamping `Contains:"Work"` on a record with categories
  `["Personal"]` yields `["Personal","Work"]`; no duplicate on already-tagged
  records; **existing element order is preserved**.
- Filtering: only records whose canon `categories` contains "Work" come
  through `loadRecords`; **case-sensitive** match.
- Equals semantics: `createRecord` and `updateRecord` both overwrite the
  filter property to the filter value, including the case where the
  caller's `BackendRecord` carried a different value for that property
  (filter-authoritative).
- Delegation: `shapeFor` returns the parent's shape for the parent
  collection; `discoveredWritable(virtualColId)` returns
  `parent.discoveredWritable(parentColId)`.
- Filter property absent from the record (e.g. a VEVENT with no
  `categories` field at all): `matches` returns false, the record is
  excluded from `loadRecords`, and `loadRecord(recordId)` returns
  `nullopt`. No throw, no insertion of a default empty array.
- Bad / non-JSON payload: silently filtered out (no throw).
- Parent unregistered mid-session: after
  `BackendRegistry::backendInstanceUnregistered(parentBackendId)`, the
  FCB's `isAvailable()` returns false, `loadRecords` returns an empty
  list, `createRecord` returns an empty string, `updateRecord` /
  `deleteRecord` return false. No use-after-free, no crash.
- Resource id stability: two FCBs constructed with equivalent
  `(parent, parentColId, virtualColId, filter)` produce equal
  `resourceId()` values. Construction in either argument order, or
  with semantically-equivalent filter values that serialise to the
  same canonical form, must yield the same id.

## 5. WildPalms' planned consumption (informational)

Per `docs/superpowers/specs/2026-05-28-subproject-hub-remote-routing-design.md`,
WildPalms will:

1. At `PalmRuntime::finishConnect`, after the per-domain Palm↔hub LCs of
   sub-project C, iterate the persisted user remote SyncMappings and build
   one `FilteredCollectionBackend` per category-route, registered under a
   stable id `wp-route-<lcId>`.
2. Construct one `LogicalCalendar` per route with `Primary` = the view +
   `Sync1` = the remote. Append to the LC list passed to
   `Kalburator::Sync::generateMappings(...)`.
3. The engine runs the union of per-domain (Palm↔hub) and per-route
   (view↔remote) mappings as one `Star` queue.

Recategorization moves records between remotes automatically via the
ordinary engine diff against each route's baseline (the filter excludes
moved records on the old route ⇒ delete; includes them on the new route ⇒
create). No special routing logic in the engine.

**Implication for remotes.** To a remote sync target this looks like
`DELETE id=X` (on the old route) followed by `CREATE` of a record with a
**new** id on the new route — not as an `UPDATE`. Remotes that key by
record id (CalDAV with stable UIDs, Palm record-id-based databases) will
therefore see a record loss + a new record, not an in-place reassignment.
For the WildPalms-Palm case this is the intended behaviour. Consumers
whose remotes track per-record state (history, attachments, share
permissions) keyed by id should weigh this before choosing the filtered-
view routing approach over an in-engine recategorization mechanism.

## 6. Cost & risk

- **Code**: one class (~200-300 lines incl. canon-JSON parsing), one struct
  (~40 lines), one test file. Possibly less if `matches` can route through
  `Kalburator::Shape::RecordDiffer`'s existing canon-record accessor
  (see §2.1.1).
- **Cross-repo**: this proposal + the two consumers staying green is the
  standing gate.
- **Forward compatibility**: the typed predicate (`PropertyId` + `Op`)
  gives room to grow ops without breaking ABI; new ops are
  added one at a time with a concrete use case.

**Consumer pin state at time of writing:**
- PlanStan `master` is at `fe6e8a9c` (post-2026-05-28 PlanEngine
  severance), pinned to `libkalburator v0.57.1-phase2c-authority`.
  PlanStan does not consume `FilteredCollectionBackend` today; landing
  it requires only the pin bump (no PlanStan-side code change), and
  PlanStan remains green throughout.
- WildPalms re-pins from v0.57 to whatever release carries this. The
  WildPalms consumption is described in §5 and lands behind the pin.
