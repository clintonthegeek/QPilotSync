# Sub-project C — Canonical Local Hub + Domain-Level Star + Category-as-Field

**Date:** 2026-05-27
**Status:** Approved design (converged via brainstorming)
**Parent:** `docs/superpowers/specs/2026-05-27-three-tier-sync-architecture-design.md` (sub-project C)
**Depends on:** libkalburator v0.57 (already adopted — see O7 port)

---

## 1. Goal

Stand up WildPalms' **canonical local hub** as the Tier-2 store of the three-tier
architecture, and route the Palm device into it with a **dead-simple,
domain-level topology**. Encode the Palm category as a **first-class canonical
field** (`categories`) so that category-based filtering/routing later becomes a
matter of reading a record field — never a web of per-category sync mappings.

This is the foundation sub-project D builds on (views read the hub).

## 2. Why domain-level (not per-category) topology

`SyncEngine` runs `SyncMapping`s strictly collection-to-collection (1:1), in a
sequential queue. The Palm backend already serves its 16 per-category
collections from a **per-database cache** (`palmbackend.cpp:283` —
`loadPalmRecords` reads the device once, caches), **but** that cache is
invalidated on writes back to the device. So a per-category `TwoWay` topology
re-reads the entire `DatebookDB` over the serial link after each category that
pushes a Palm-bound change — a higher-order device cost on slow hardware.

Therefore the **expensive Palm leg is touched once per domain**. Category is not
a topology concern; it is a **field on the canonical record**. Routing/filtering
reads that field at the cheap local/hub level (a later sub-project), never the
device leg.

## 3. Scope

### In scope
- A per-profile `GenericSqliteBackend` **hub** holding one canon collection per
  domain (calendar, contacts, todo, note).
- A **domain-level collection** on each of the four Palm plugin backends (whole
  DB, unfiltered).
- `PalmRuntime::finishConnect` builds one `LogicalCalendar` per domain
  (Primary = hub, Sync1 = Palm) and drives sync via
  `Kalburator::Sync::generateMappings(lcs, SyncTopology::Star)` →
  `engine->setSyncMappings(...)`. The per-slot RawFiles default mappings are
  **removed** (the hub replaces them as the local store).
- **Category-as-field encoding** in the four WP plugin transcoding stages:
  - calendar/contacts/todo: map the Palm category byte ⇄ the intermediate
    encoding's `CATEGORIES` property (the **name**, via `CategoryMappingStore`);
    libkalburator's `ical/vcard/vtodo ↔ canon` stages then carry it into canon
    `categories` for free.
  - memo/note: carry the category **name** in the markdown **frontmatter**
    (note canon has no `categories` field).

### Out of scope (later sub-projects)
- **hub ↔ remote** re-routing + the category-routing decorator (next sub-project).
- Views reading the hub (sub-project D) — incl. the contacts V2 view.
- Editability gating (E); legacy config/state cleanup (F).

### Accepted interim states (explicit)
- **Remote sync is temporarily Palm-only-to-hub.** Existing wizard/F.3
  `Palm↔remote` mappings are superseded by `Palm↔hub`; direct remote sync
  returns in the next sub-project as `hub↔remote`. Acceptable: single developer
  user, device-unverified, remotes restored immediately next.
- **All four views are empty after C** (memo included — its RawFiles markdown
  default is removed). D restores them by reading the hub. This is not a
  regression of a working feature for calendar/contacts/todo (already empty);
  memo's view goes temporarily empty until D.

## 4. Architecture

```
   Palm device                     WP hub (GenericSqliteBackend, one .db)
   ┌──────────────┐  domain-level   ┌─────────────────────────────┐
   │ DatebookDB   │  Star mapping   │ collection "calendar" (canon)│
   │ (palm shape) │ ◄────TwoWay────►│  records carry `categories`  │
   └──────────────┘                 │ collection "contacts" (canon)│
   (read once/                      │ collection "todo"     (canon)│
    cached; one                     │ collection "note"     (canon)│
    collection per                  └─────────────────────────────┘
    domain)                          Primary role; feeds views (D)
```

- **Hub backend:** one `GenericSqliteBackend("<profile>/.state/hub.db")`
  registered once (id `"wp-hub"`), with four collections created via
  `createCollection({id=<domain>}, Shape{<domain>, "canon"})`. One instance
  serves all domains; `shapeFor("calendar")` → `(calendar, canon)`, etc.
- **Palm backends:** each gains a domain-level collection (e.g. id
  `"palm:<domain>"`) whose `loadRecords` returns **all** records (no slot
  filter) from the cache, `shapeFor` → `(<domain>, palm)`. The 16 per-slot
  collections remain for now (used by existing UI); the domain-level collection
  is what the hub mapping binds to.
- **Role bindings (per domain):** `LogicalCalendar{ domain=<domain>,
  syncEnabled=true, bindings=[ {wp-hub, <domain>, Primary},
  {<palm-backend-id>, palm:<domain>, Sync1} ] }`.
- **Mappings:** `generateMappings(lcs, Star)` emits one `wp-hub:<domain> ↔
  palm:<domain>` `TwoWay` mapping per domain. `engine->setSyncMappings(...)`.

## 5. Category as a canonical field

Canon already defines `categories` (`PropertyKind::StringList`) for calendar
(`calendarcanonproperties.cpp:48`), contacts (`contactscanonproperties.cpp:29`),
and todo (`todocanonproperties.cpp:29`), and libkalburator's peer↔canon stages
round-trip it (`icalcanonstages.cpp:322`, `vcardcanonstages.cpp:336`,
`vtodocanonstages.cpp:191`). The differs track it. **No libkalburator change is
needed** for these three domains.

WP's palm↔peer stages (which sit *below* the peer↔canon stages) change as
follows:

- **Outbound (palm → peer):** read the Palm record's category byte; resolve the
  **name** via `CategoryMappingStore::slotName(db, slot)`; write it as the sole
  entry of the encoding's `CATEGORIES` property (KCalendarCore
  `Incidence::setCategories`, KContacts `Addressee::setCategories`). Slot 0
  (Unfiled) → no category (or omit).
- **Inbound (peer → palm):** read `CATEGORIES`; take the first entry that
  resolves to a Palm slot via `CategoryMappingStore` (reverse name→slot;
  Unfiled/0 fallback when absent or unknown); set the Palm record's category
  byte. Extra categories beyond the first are dropped — Palm has a single slot;
  this loss is recorded in the existing loss profile and **degrades inside the
  Palm backend**, per the "backend peculiarities degrade in the backend" rule.

**Memo/note exception:** `note` canon is markdown; no `categories` field. WP's
`PalmToMarkdown`/`MarkdownToPalm` (memomarkdown) stages carry the category name
as a frontmatter key (e.g. `category: Work`). Outbound: slot→name into
frontmatter. Inbound: frontmatter `category`→slot via `CategoryMappingStore`.

**Rename behavior:** category identity is the **name**, resolved fresh from the
live AppInfo (`CategoryMappingStore`) each sync. A Palm-side rename re-emits the
new name (a tag rename downstream); a remote-side rename inbound resolves to the
matching slot or Unfiled. No stable-ID tracking — this matches how
`CATEGORIES`/tags behave everywhere.

## 6. Data flow

- **First sync (empty hub):** `Palm↔hub` TwoWay with empty baseline copies Palm
  records → hub as canon (categories populated). Nothing writes back to Palm, so
  the device cache is not invalidated. One device read for the whole session.
- **Steady state:** Palm-side edits promote to hub; hub-side edits (from D's
  editing UI, or later from remotes) demote to Palm. Per-mapping baselines in
  the existing `Kalburator::Storage::BaselineStore` (already wired).

## 7. File / component structure

- **Modify (main repo):** `src/runtime/palmruntime.{h,cpp}` — own + register the
  hub `GenericSqliteBackend`; create its four canon collections; build the
  per-domain `LogicalCalendar`s; call `generateMappings` + `setSyncMappings`;
  delete the per-slot RawFiles default-mapping loop in `finishConnect`.
- **Modify (4 submodules):** each Palm backend (`palmcalendarbackend`,
  `palmcontactsbackend`, `todoblobbackend`, `memoblobbackend`) — add the
  domain-level collection (`availableCollections`, `loadRecords`,
  `createRecord`/`updateRecord`/`deleteRecord`, `shapeFor`).
- **Modify (4 submodules):** the palm↔peer transcoding stages
  (`palmtoicstransformation`, `palmtovcardtransformation`,
  `palmtovtodotransformation`, `palmnotetransformation`) — slot↔name category
  mapping into `CATEGORIES` (cal/contacts/todo) or frontmatter (memo). These
  stages need access to the `CategoryMappingStore`; thread it in (the stages
  currently run without it — see open item 8.1).

## 8. Open / validation items (resolve during planning/implementation)

1. **`CategoryMappingStore` access from stages.** The transcoding stages are
   pure byte→byte (`TransformationStage::transform`) and don't currently hold a
   `CategoryMappingStore`. Determine how to give the slot↔name table to the
   stage: constructor injection when the plugin builds its `ShapeContribution`
   edges, vs a per-record carried hint. This is the main implementation unknown.
2. **`GenericSqliteBackend` linkage.** Confirm WP links the target exposing
   `GenericSqliteBackend` and that the `Qt6::Sql` dependency (the configure-time
   warning seen during the O7 port) resolves for the WP executable.
3. **Existing remote mappings.** Confirm how to cleanly supersede the
   wizard/F.3 `Palm↔remote` wildcard mappings (drop from the generated set;
   leave the persisted profile config untouched so the next sub-project can
   migrate them to `hub↔remote`).
4. **Memo frontmatter key.** Confirm the memomarkdown stage's frontmatter
   format and pick the category key name; ensure round-trip.

## 9. Success criteria

- On connecting a Palm with no remote configured, after one sync the hub
  (`.state/hub.db`) contains canon records for each populated domain, each
  carrying the correct `categories` (name) — verifiable by reading the hub.
- A Palm record in category slot N round-trips: Palm → hub canon
  (`categories=[<slotName>]`) → Palm (back to slot N), with no spurious diffs on
  a second sync (baseline stable).
- The Palm device is read **once per session** for a first sync (no per-category
  re-reads).
- The full ctest suite is green; runtime/e2e tests updated to assert the hub
  receives records (replacing the old per-slot RawFiles assertions).
- No libkalburator change required (calendar/contacts/todo use existing canon
  `categories`; memo uses frontmatter).

## 10. Testing

- Unit: each plugin's stage maps slot↔`CATEGORIES`/frontmatter both directions
  (incl. Unfiled, unknown-name→Unfiled, multi-category→first-slot loss).
- Integration: `Palm↔hub` first sync populates the hub with categories;
  second sync is a no-op (baseline stable); a hub-side category change demotes to
  the correct Palm slot.
- Reuse the existing `MockBlobBackend`/`MockPalmDatabaseAccess` harnesses; assert
  against the hub `GenericSqliteBackend` instead of RawFiles dirs.
