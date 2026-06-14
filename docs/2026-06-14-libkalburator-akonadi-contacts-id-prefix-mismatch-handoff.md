# libkalburator handoff — scoped Akonadi *contacts* backend can't resolve provider-emitted collection ids (prefix mismatch)

**Date:** 2026-06-14
**Direction:** WildPalms → libkalburator
**Status:** OPEN — follow-up defect in the 2026-06-12 Akonadi scoped-backend fix (`493bd80`,
branch `fix/akonadi-scoped-backend-reads`). Calendar half works on device; contacts half
does not.
**Severity:** High for Akonadi contacts sync (transfers 0 records); zero impact on calendar.
**Pinned at:** the fix commit `493bd804a549e161718986065848f0af301b5667`
(WP `CMakeLists.txt`). All cites below are against that tree
(`build/_deps/libkalburator-src`).

---

## TL;DR

The 2026-06-12 fix (`ensureScopedCollection`) made the scoped Akonadi **calendar** read
work on device — verified: an Akonadi calendar (collection 54) synced 83 events into
WP's `hub.db`. But the scoped **contacts** read still transfers 0 records, because the
provider and the contacts backend disagree on the collection-id scheme:

- `AkonadiProvider::collectionFetchResult` emits **`"akonadi-<numericId>"` for every
  collection regardless of type** (`src/sync/akonadiprovider.cpp:137`). A contacts
  address book at Akonadi collection 184 is published — and persisted by WP into the
  route — as **`"akonadi-184"`**.
- `AkonadiContactsBackend::akonadiIdForCollection` only parses ids that start with
  **`"akonadi-contacts-"`** (`AKONADI_CONTACTS_PREFIX`,
  `src/contacts/akonadicontactsbackend.cpp:29,133-139`). `"akonadi-184"` doesn't, so it
  returns `-1`, `ensureScopedCollection` bails (`:126-128`), `m_collections` stays empty,
  and `fetchItems("akonadi-184")` fast-fails `"Unknown collection: akonadi-184"`
  (`:177-183`).

The calendar backend is immune because its prefix (`AKONADI_PREFIX = "akonadi-"`,
`src/calendar/akonadibackend.cpp:36`) **matches** what the provider emits.

Fix B is behaving correctly here: the `Failed` fetch op now fails the contacts mapping
instead of reporting a silent 0-record success (in the device log the contacts route logs
`=== Starting sync … akonadi-184 … ===` and then **never** logs
`unifiedContinueAfterConflicts completed`).

---

## Evidence (on device, 2026-06-14)

WP profile with a "Local Akonadi" account; calendar route → `akonadi-54`, contacts route
→ `akonadi-184` (the real address book, ~502 contacts). Clobber sync, then HotSync.

`hub.db` after the run:

| domain   | rows |
|----------|------|
| calendar | **83** |
| contacts | **0**  |
| note     | 0    |
| todo     | 0    |

Calendar populated (read fix works); contacts empty. Device log, contacts route:

```
SyncEngineWorker: === Starting sync [wp-hub/contacts] -> [<uuid>:akonadi-184/akonadi-184] ===
  mapping: "auto_wp-route-default-contacts-<uuid>-akonadi-184_sync1" mode: TwoWay (unmonitored)
[KPilotDeviceLink] endSync() called          <-- run wraps up; no "completed" for this mapping
```

(Calendar's `akonadi-54` route logs the same "Starting" line **and** a matching
`unifiedContinueAfterConflicts completed` immediately before it.)

---

## Root cause

The id scheme is inconsistent between the producer and the contacts consumer, and has
been since before the fix — the fix simply made it *reachable* (before, `fetchItems`
fast-failed on the empty `m_collections` regardless, so the prefix never got exercised).

1. **Producer** — `AkonadiProvider` (the only thing that constructs scoped Akonadi
   backends, via `createBackend`) labels collections generically:
   ```cpp
   // src/sync/akonadiprovider.cpp:136-139  (inside the per-collection loop,
   // AFTER type is decided as "calendar" or "contacts")
   CollectionInfo info;
   info.id   = QStringLiteral("akonadi-%1").arg(col.id());   // <-- no per-type prefix
   info.name = col.displayName();
   info.type = type;
   ```
   `createBackend(collectionId)` then passes that id verbatim into
   `cfg["akonadiCollectionId"]` for both the calendar and contacts branches
   (`:177-193`), so a contacts backend is scoped to `m_scopedCollectionId == "akonadi-184"`.

2. **Consumer (contacts)** — parses a *different* scheme:
   ```cpp
   // src/contacts/akonadicontactsbackend.cpp:29
   static const QString AKONADI_CONTACTS_PREFIX = QStringLiteral("akonadi-contacts-");
   // :133-139
   Akonadi::Collection::Id AkonadiContactsBackend::akonadiIdForCollection(const QString &collectionId) const {
       if (!collectionId.startsWith(AKONADI_CONTACTS_PREFIX))
           return -1;                                   // <-- "akonadi-184" lands here
       ...
   }
   // ensureScopedCollection (:118-131) early-returns on id < 0 → m_collections stays empty
   ```

3. **Consumer (calendar)** — parses the *matching* scheme, so it resolves fine:
   ```cpp
   // src/calendar/akonadibackend.cpp:36
   static const QString AKONADI_PREFIX = QStringLiteral("akonadi-");
   ```

The backend even documents the `"akonadi-contacts-<id>"` scheme in its header
(`src/contacts/akonadicontactsbackend.h:32-33,81`), but **nothing actually produces ids in
that scheme** — the provider has always used `"akonadi-<id>"`.

### Why the regression test went green while the device fails

`tests/calendar/tst_akonadi_scoped_collection.cpp:29` hard-codes
`kContactsScopedId = "akonadi-contacts-1"` — i.e. it feeds the contacts backend an id in
the backend's *self-documented* scheme, which `akonadiIdForCollection` happily parses. But
that is **not** the id `AkonadiProvider` ever emits. The test and the only real producer
disagree, so the test can't catch this. (`kCalendarScopedId = "akonadi-1"` happens to match
the provider's real scheme, which is why calendar is genuinely covered.)

---

## Proposed fix (libkalburator owns the final shape)

**Preferred — align the contacts backend to the scheme the provider actually emits**
(`"akonadi-<id>"`), so existing persisted routes targeting `"akonadi-184"` resolve with no
WP-side migration:

- Set `AKONADI_CONTACTS_PREFIX = QStringLiteral("akonadi-")` (matching `AKONADI_PREFIX`),
  or otherwise make `akonadiIdForCollection`/`collectionIdForAkonadiId` use the generic
  `"akonadi-"` prefix. Akonadi collection ids are globally unique across the whole
  collection tree, so the `"-contacts-"` discriminator buys nothing — calendar already
  proves `"akonadi-<id>"` is sufficient. Update the `:32-33,81` header docs accordingly.
- Keep `collectionIdForAkonadiId` (reverse map, used by the Monitor handlers that key
  `m_itemsByCollection`) consistent with the new prefix so the monitor and fetch paths
  agree on the key.

**Alternative — make the provider emit a per-type prefix** (`"akonadi-contacts-<id>"` for
contacts). Rejected unless you prefer it: it changes the ids WP has already persisted into
route specs, so every existing contacts route would need re-binding (migration), and it
adds a special case to a loop that's otherwise type-agnostic.

**Test correction (load-bearing):** change
`tst_akonadi_scoped_collection.cpp`'s contacts case to use the id scheme the **provider**
emits (`"akonadi-<id>"`, not `"akonadi-contacts-<id>"`). Better still, add a small test
that pins provider↔backend agreement directly: construct `AkonadiProvider`, take the id it
publishes for a contacts collection, feed *that* into `createBackend`, and assert the
resulting backend's `fetchItems(thatId)` does **not** return an immediately-`Failed`
"Unknown collection" op. That asserts the contract the unit test currently can't (because
it invents the id itself).

---

## Acceptance criteria

1. A scoped `AkonadiContactsBackend` constructed by `AkonadiProvider::createBackend` for a
   real contacts collection resolves it: `fetchItems(<provider-emitted id>)` does not
   fast-fail, and `loadRecords` returns N>0 for a non-empty address book.
2. On WP @ fix: a HotSync of the profile above pulls Akonadi contacts collection 184 into
   `hub.db`'s `contacts` table (then the palm↔hub leg can push them to the Palm — subject
   to the separate WP-side ordering issue, below).
3. A unit/integration test pins provider↔backend id-scheme agreement (not the backend's
   self-invented scheme).
4. lib suite green; PlanStan ctest baseline green before tagging.

---

## Not libkalburator's — tracked WP-side

Even with this fixed, contacts (and calendar) won't reach the **Palm** in a *single*
HotSync: WP dispatches `palm↔hub` legs before `hub↔remote` routes, so remote records land
in the hub *after* the hub→palm leg already ran (a second HotSync then pushes them). That
is WP's mapping-ordering / "hub↔remote-only sync gap" (roadmap item #2), not a libkalburator
defect — handled separately in WP. The calendar's 83 hub rows with 0 on the Palm are the
symptom; the contacts prefix bug above is independent and must be fixed first for contacts
to populate the hub at all.
