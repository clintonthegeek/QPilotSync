# Proposal to libkalburator: `FilteredCollectionBackend` + `RecordFilter`

**Date:** 2026-05-28
**From:** WildPalms (a downstream `SyncEngine` / `SyncBackend` consumer)
**To:** libkalburator maintainer (+ PlanStan as co-consumer)
**Status:** Proposal / RFC — requesting a small, focused addition
**Why now:** WildPalms is restoring its remote sync via the hub-and-spoke topology you adopted in v0.57. The remote leg needs to route records by the canonical `categories` field. We could build the slicing wholly inside WP, but it's a generic primitive PlanStan can reuse (any property-based tag filtering), so it belongs in libkalburator — small, narrow, persistable.

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
    FilteredCollectionBackend(Sync::SyncBackend *parent,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Shape::RecordFilter filter,
                              QObject *parent_qobj = nullptr);
    // ...standard SyncBackend overrides...
};
}
```

One filter per instance, one virtual collection. Each instance presents a
sliced view of a parent backend's collection: reads filter; writes stamp the
filter value into the record (`Contains`: add to list if missing; `Equals`:
set). Borrowing parent backend pointer (no ownership).

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
- The set of ops is deliberately tiny (YAGNI). Extensions can be added when
  a real need arises; we'd rather not ship a query language.

### 2.2 `FilteredCollectionBackend`

A `SyncBackend` (so the engine can sync to it directly). One instance =
one virtual collection.

```cpp
class FilteredCollectionBackend : public Sync::SyncBackend {
public:
    FilteredCollectionBackend(Sync::SyncBackend *parent,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Shape::RecordFilter filter,
                              QObject *parent_qobj = nullptr);

    QString backendType()  const override { return QStringLiteral("filtered-view"); }
    QString displayName()  const override;
    QString resourceId()   const override;
    bool    isAvailable()  const override { return m_parent && m_parent->isAvailable(); }

    QList<Shape::Shape> nativeShapes() const override; // delegates: parent.shapeFor(parentColId)
    Shape::Shape shapeFor(const QString &collectionId) const override;

    QList<Sync::CollectionInfo> availableCollections() override; // returns one entry
    Sync::CollectionInfo        collectionInfo(const QString &collectionId) override;

    QList<Sync::BackendRecord>            loadRecords(const QString &collectionId) override;
    std::optional<Sync::BackendRecord>    loadRecord(const QString &recordId)      override;
    QString createRecord(const QString &collectionId,
                         const Sync::BackendRecord &record) override;
    bool    updateRecord(const Sync::BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId)            override;

private:
    Sync::SyncBackend *m_parent = nullptr;  // borrowed
    QString             m_parentColId;
    QString             m_virtualColId;
    Shape::RecordFilter m_filter;
};
```

Behavior:

- **Reads.** `loadRecords(virtualColId)` calls `m_parent->loadRecords(m_parentColId)`,
  parses each record's payload as canon JSON (`QJsonDocument::fromJson`), and
  filters by `m_filter.matches(...)`. Records not parseable as canon JSON
  fail-closed (omitted, no exception). `loadRecord(recordId)` delegates and
  applies the filter (returns `nullopt` if it doesn't pass).
- **Writes.** `createRecord(virtualColId, record)` mutates `record.data` so
  the filter would now pass — for `Contains`, ensure the array contains the
  value (no-op if it already does); for `Equals`, set the property to the
  value. Then `m_parent->createRecord(m_parentColId, modifiedRecord)`. The
  returned id is the parent's. `updateRecord(record)` does the same stamping
  and delegates. `deleteRecord(id)` delegates unchanged.
- **`shapeFor` / `nativeShapes`** delegate: a filtered view has the same
  shape as the parent collection (the records are canon, just sliced).
- **Identity.** `resourceId()` returns something like
  `"filtered-view:<parent.resourceId>:<parentColId>?<filter>"` for the
  baseline-store keying contract.
- **Discovered-writable.** `discoveredWritable()` delegates to the parent
  (consistent with v0.57's `discoveredWritable` enforcement in the write path).

### 2.3 Stamping notes

- `Contains` is the WildPalms category-routing case. Stamping is **additive**:
  preserves any other category names the record already carries, so a record
  legitimately tagged `["Work","Important"]` doesn't lose `Important` when it
  comes in via the Work route.
- `Equals` resets the property. Useful for "every record in this view has
  status=Done" type slicing.

### 2.4 Loss profile

A filtered view does not change the shape — no loss profile to introduce.
The class is shape-transparent.

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
  records.
- Filtering: only records whose canon `categories` contains "Work" come
  through `loadRecords`.
- Equals semantics: replaces the property value (and the post-write
  predicate trivially passes).
- Delegation: `shapeFor` returns the parent's shape for the parent collection.
- Negative: a record with bad / non-JSON payload is silently filtered out
  (no throw).

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

## 6. Cost & risk

- **Code**: one class (~200-300 lines incl. canon-JSON parsing), one struct
  (~40 lines), one test file.
- **Cross-repo**: this proposal + PlanStan-green is the standing gate.
- **Forward compatibility**: the typed predicate gives room to grow ops
  without breaking ABI.

WildPalms does not consume this until it lands; we'll re-pin from v0.57 to
whatever release carries it.
