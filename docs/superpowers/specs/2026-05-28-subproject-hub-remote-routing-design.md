# Sub-project — Hub↔Remote Routing via Category-Filtered Views

**Date:** 2026-05-28
**Status:** Approved design (converged via brainstorming)
**Parent:** `docs/superpowers/specs/2026-05-27-three-tier-sync-architecture-design.md` (the umbrella; this is the sub-project sitting between C and D — it brings remotes back through the hub before D rewrites the views to read the hub).
**Depends on:** libkalburator `FilteredCollectionBackend` (proposal at
`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` — must land + PlanStan-green before this work consumes it).

---

## 1. Goal

Restore remote sync after sub-project C (where Palm↔hub `Star` mappings
deliberately superseded the persisted user-configured remote mappings), with
**Palm category names as the routing key** on the remote leg. End-state: a
record in canon `categories=[X]` on the hub flows to whichever remote calendar
the user has mapped category X to, in either direction. Recategorization (Palm
or remote moving a record from category X to category Y) **moves** the record
between the corresponding remote calendars automatically.

This is "Tier-3 spokes" of the umbrella architecture: each user-configured
remote calendar becomes a `Sync` binding on a per-route `LogicalCalendar` whose
`Primary` is a category-filtered view of the hub.

## 2. Why this design

The engine binds backends + collections strictly 1:1 (collection-to-collection
sync). To route by category without inventing a new engine concept, we:
- Keep the hub as the canonical store (per-domain collections holding canon
  records with the `categories` field that C established).
- For each user-configured category-route, present a **filtered view** of the
  hub as a separate (backend, collection) pair the engine can bind to via a
  per-route `LogicalCalendar`. `generateMappings(Star)` then emits one
  pairwise `SyncMapping` per route: `view ↔ remote calendar`.

The filtered view is a generic libkalburator primitive — `FilteredCollectionBackend`
— so the abstraction is shared with PlanStan and is persistable (typed
predicate), not a WP-specific hack.

## 3. Scope

### In scope
- A libkalburator handoff doc proposing `FilteredCollectionBackend` + the typed
  `RecordFilter` predicate (the doc lives in WP's repo per the standing
  handoff workflow; the implementation lands in libkalburator).
- WildPalms consumption:
  - Per-route `FilteredCollectionBackend` instances, registered with stable
    per-route ids.
  - Per-route `LogicalCalendar` construction (`Primary` = the filtered view;
    `Sync1` = the remote calendar). Fed into `generateMappings(Star)`
    alongside C's per-domain Palm↔hub LCs; the engine runs the union.
  - **Migration of persisted F.3 / wizard SyncMappings** into per-route LCs
    (closes the C interim regression). Translation rule in §6.
  - Tests proving: a category-X record flows hub→remote-Y; a remote-Y change
    flows back to hub canon (with `categories` containing X); recategorization
    moves a record between remotes via the two routes' delete/create diffs.

### Out of scope (deferred)
- **Wizard / F.3 graph UI updates** to express the model explicitly as
  "category-name → remote calendar" (today's per-slot UI keeps working via
  the slot→name translation in §6). Sub-project after this one or later.
- **Demotion** of the hub to user-read-only when a remote owns a category —
  belongs to sub-project E (editability gating) per the umbrella spec.
- **`discoveredWritable` integration** for read-only remotes — sub-project E.

### Out of scope of the libkalburator handoff
- Query languages, joins, persistence of filter sets, multi-property predicates
  (single property + op + value is enough; YAGNI).

## 4. Architecture

```
                  ┌──────────────────────┐
                  │  WildPalms hub (C)   │
   Palm ◄─Star──► │  GenericSqliteBackend │
                  │  collection "calendar"│
                  │  records carry        │
                  │  `categories` field   │
                  └─┬─────────┬───────────┘
                    │         │
   FilteredView "cal/Work"   FilteredView "cal/Home"
   wraps wp-hub:calendar      wraps wp-hub:calendar
   filter categories⊇{Work}   filter categories⊇{Home}
        │ Star (Primary)            │ Star (Primary)
        ▼                           ▼
   CalDAV "WorkCal" (Sync1)    CalDAV "HomeCal" (Sync1)
```

Three "layers" of `LogicalCalendar`, all consumed by one `generateMappings`
call at `finishConnect`:

| LC kind | Built when | Primary | Sync1 |
|---|---|---|---|
| Per-domain (from C) | Palm connect | `wp-hub` : `<domain>` | `<palm-id>` : `palm:<domain>` |
| Per-route (new) | Per persisted/configured route | `wp-route-<id>` : `<vcol>` | `<remote-id>` : `<remote-cal>` |

The per-route LC's `Primary` backend is a `FilteredCollectionBackend` instance
wrapping the hub. Each route is one backend instance, one virtual collection,
one filter. The engine's `Star` then emits one mapping per LC; the union of
mappings handles everything.

## 5. libkalburator addition (handoff)

Proposed in detail in
`docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md`. Summary:

```cpp
// libkalburator src/types/recordfilter.h
namespace Kalburator::Shape {
struct RecordFilter {
    enum class Op { Contains, Equals };
    PropertyId property;
    Op op = Op::Contains;
    QVariant value;
    bool matches(const QJsonDocument &canonRecord) const;  // pure
};
}

// libkalburator src/universal/filteredcollectionbackend.h
namespace Kalburator::Sinks {
class FilteredCollectionBackend : public Sync::SyncBackend {
public:
    FilteredCollectionBackend(Sync::SyncBackend *parent,
                              QString parentCollectionId,
                              QString virtualCollectionId,
                              Shape::RecordFilter filter,
                              QObject *parent_qobj = nullptr);
    // Exposes a single virtual collection. loadRecords parses canon
    // and filters; create/updateRecord stamp the filter value into the
    // record before delegating to parent. shapeFor returns parent's shape
    // for the parent collectionId.
};
}
```

- One filter / one virtual collection per instance — keeps the engine's
  1:1 binding contract clean.
- Stamping (write): `Contains` adds the value to the list if missing
  (preserves any existing entries); `Equals` sets the value, replacing.
- Read: each `loadRecords` parses canon JSON and filters; for typical Palm
  record counts this is fine (no indexing).
- Borrows the parent (no ownership), like the hub borrows things elsewhere.

WildPalms will write this proposal as an RFC and not consume it until it
lands + PlanStan ctest stays green.

## 6. WildPalms consumption

### 6.1 Per-route setup at `finishConnect`

After C's per-domain LCs are built but BEFORE `generateMappings` runs,
`PalmRuntime::finishConnect` also:
1. Loads the persisted user SyncMappings from the profile
   (`loadMappingsFromProfile()` — currently the documented seam).
2. Translates each persisted mapping (per §6.2) into:
   - A `FilteredCollectionBackend` instance owned by `PalmRuntime` (member
     `std::vector<std::unique_ptr<FilteredCollectionBackend>> m_routeViews`),
     registered with the local `BackendRegistry` under a stable id
     `wp-route-<lcId>`.
   - A per-route `LogicalCalendar` with `Primary` = the view + the virtual
     collection id, `Sync1` = the persisted target (the remote backend + its
     collection), `syncEnabled=true`, `domain` = the route's domain.
3. Appends the per-route LCs to the LC list passed to
   `Kalburator::Sync::generateMappings(allLcs, Star)`.
4. `m_engine->setSyncMappings(...)` consumes the union (C's per-domain
   mappings + per-route mappings) — engine runs them all in one queue.

### 6.2 Translation rule (persisted SyncMapping → per-route LC)

The persisted mappings in `Profile::syncMappingsJson` today come in two shapes
the wizard/F.3 graph writes:

| Persisted | Means | New per-route LC |
|---|---|---|
| `sourceBackend = <palm-plugin-id>`, `sourceCalendar = "palm:<domain>/<slot>"`, target = `<remote-id>` : `<remote-col>` | F.3 graph: this Palm category slot maps to this remote calendar. | Resolve `slotName` from `CategoryMappingStore`. LC: `domain = <domain>`; route id stable from `(palm-id, slot, remote-id, remote-col)`. Primary binding = `wp-route-<id>` : `route-<id>` ; the filtered view wraps `wp-hub:<domain>` with filter `categories Contains <slotName>`. Sync1 = `<remote-id>` : `<remote-col>`. |
| `sourceBackend = <palm-plugin-id>`, `sourceCalendar = ""` (wildcard), target = `<remote-id>` : `<remote-col>` | Wizard "send all of this domain to this remote calendar" wildcard. | Same as above but the filter is "match all" → use the parent collection directly without filtering. Equivalent to a per-route LC whose `Primary` IS the unfiltered hub collection (`wp-hub` : `<domain>`), `Sync1` = the remote. No `FilteredCollectionBackend` needed; the binding can target the hub directly. |

Two edge cases for the slot-mapped case:
- **Slot name unknown / empty.** `CategoryMappingStore` returns empty for the
  slot until AppInfo is parsed (post-connect — that's already settled).
  Translation runs AFTER `createPalmBackend` populates the store, so names are
  available. If a name is still empty (unused slot the user mapped anyway),
  skip the route with a warning.
- **Two persisted mappings for the same `(domain, slotName)` going to
  different remotes.** Each becomes its own per-route LC; both run via Star.
  The engine syncs both directions; if a record matches both filters
  (categories=[Work]), it lands on both remotes — which is the documented
  semantic of having two routes for one category.

### 6.3 Lifetime

`m_routeViews` lives on `PalmRuntime` and is destroyed before `m_hub` (which
is already declared before `m_engine`) — keeps the engine→view→hub borrow
chain valid throughout the runtime's life. Cleared and rebuilt on
disconnect/reconnect (the persisted mappings can change between sessions).

## 7. Data flow

- **Palm → remote (first sync, simple case):**
  Palm record with category byte 3 ("Work") → Palm↔hub Star mapping pushes it
  to the hub via C's stages, landing canon with `categories=["Work"]`. Then
  the Work-route mapping (filtered view ↔ WorkCal) runs: the view's
  `loadRecords` returns this record; the engine pushes it to CalDAV "WorkCal".
- **Remote → Palm:**
  CalDAV "WorkCal" gains a record. Work-route's view side `createRecord`
  stamps `categories Contains "Work"` (adds if missing) and delegates to the
  hub. The hub now holds the canon record with `categories=["Work"]`. C's
  Palm↔hub mapping runs next, pushes to Palm with category byte resolved via
  `slotForName("Work")`. Lands in Palm slot 3.
- **Recategorization (Work → Home):**
  Hub canon record's `categories` changes from `["Work"]` to `["Home"]`.
  - On Work-route's next sync, its filtered view's `loadRecords` no longer
    returns the record. Compared to baseline ⇒ the engine sees "deleted on
    the view side" ⇒ deletes on WorkCal.
  - On Home-route's next sync, the filtered view's `loadRecords` returns it
    for the first time ⇒ creates on HomeCal.
  This is recategorization → record-moves-between-remotes, the desired
  routing behavior, achieved with no special-casing — just the engine's
  ordinary delete/create diff over filtered views.

## 8. Open / validation items

1. **`createRecord` virtual collection id.** Verify that `FilteredCollectionBackend::createRecord(parentColId, …)` — i.e. when the engine writes to the view, it uses the engine-known target collection (which for a `Star` mapping is `binding.calendarId`, the virtual id) — is correctly interpreted as "the view's only collection." The class' single-collection-per-instance invariant makes this trivial; the test in 9.3 pins it.
2. **Cost of canon-parsing on every `loadRecords`.** Typical Palm record counts (hundreds) are fine. If profiling shows it matters, the lib class can later add a tiny LRU; not in scope now.
3. **`categories` field path in canon JSON.** Confirm libkalburator's canon JSON key is exactly `"categories"` (it is for cal/contacts/todo — `icalcanonstages.cpp:322`, `vcardcanonstages.cpp:336`, `vtodocanonstages.cpp:191`). For memo the categories live in `providerExtras["frontmatter"]` (see C's memo design) — out of scope here (memo has no remote routing today; memo remote sync is its own future question).

## 9. Success criteria

- A persisted F.3 mapping `(palm:calendar/3 → CalDAV:WorkCal)` after profile
  load + Palm connect produces: one Work-route LC, one
  `FilteredCollectionBackend` registered as `wp-route-<id>`, one engine
  SyncMapping `wp-route-<id>:route-<id> ↔ caldav-…:WorkCal`. The engine runs
  it after Palm↔hub.
- A Palm record in slot 3 ends up on CalDAV WorkCal after a sync.
- A CalDAV WorkCal record ends up in Palm slot 3 after a sync, with hub canon
  carrying `categories=["Work"]`.
- Recategorizing a hub record from Work to Home moves it from WorkCal to
  HomeCal across two consecutive syncs (delete on Work-route's first sync;
  create on Home-route's next sync) without manual intervention.
- The FetchContent build (Codeberg) stays green; PlanStan's ctest stays green
  on the libkalburator change.

## 10. Testing

- **libkalburator unit tests** (on the proposal side, owned by that team):
  `FilteredCollectionBackend` round-trip + stamping + filter semantics; no
  dependency on Palm or WP.
- **WildPalms integration tests:**
  - `tst_palm_runtime_routes`: persist a mapping in the profile, set device,
    assert the engine receives the expected `wp-route-…` mapping in addition
    to the C per-domain mappings.
  - `tst_palm_runtime_route_first_sync`: with a mock remote, prove Palm slot 3
    → CalDAV WorkCal end-to-end, and back.
  - `tst_palm_runtime_route_recategorization`: simulate hub-side canon change
    from `["Work"]` to `["Home"]`; assert delete on Work-route and create on
    Home-route across two sync calls.
