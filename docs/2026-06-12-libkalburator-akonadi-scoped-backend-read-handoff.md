# libkalburator handoff — Akonadi scoped-backend reads 0 records (+ engine swallows fetch failures)

**Date:** 2026-06-12
**Direction:** WildPalms → libkalburator
**Status:** OPEN — needs a libkalburator fix + regression tests
**Severity:** High. Akonadi↔hub sync transfers nothing in any mode (HotSync / FullSync /
Clobber). This is the first on-device exercise of the per-collection ("Phase L.5") scoped
Akonadi backend; the read path was never wired to the scoped collection, so it has never
actually worked. Defect B (engine) additionally turns a failed source fetch during a
**clobber** into a silent "success" after the Palm has already been wiped — a data-loss
footgun.

Pinned at the libkalburator version WP currently consumes: **v0.69**
(`WILDPALMS_LIBKALBURATOR_GIT_TAG=v0.69`). All file:line cites below are against that tree
(verified in WP's `build/_deps/libkalburator-src`).

---

## TL;DR

A HotSync against a real Akonadi account ran all 8 mappings (4 palm↔hub + 4 hub↔akonadi
routes) and moved **zero** records into the hub, even though the bound Akonadi calendars
hold real data (collection 54 = 99 events, collection 64 = 28 events). The persistent hub
store (`<profile>/.state/hub.db`) is empty across all domains.

Two independent defects:

- **A (primary, Akonadi backend).** The engine uses a *fresh, per-collection*
  `AkonadiBackend` (created by `AkonadiProvider::createBackend`). That backend stores the
  collection id in `m_scopedCollectionId` but **never populates `m_collections`** — and
  `m_collections` is only ever filled by `loadCalendars()`, **which has no caller anywhere
  in `src/`**. So `fetchItems(<id>)` does `m_collections.find(<id>)` → miss → fails
  immediately ("Unknown calendar"), the in-memory item cache (`m_itemsByCalendar`) stays
  empty, and `loadRecords()` returns an empty list. Every Akonadi read yields 0 records.
  `AkonadiContactsBackend` has the identical defect.

- **B (compounding, SyncEngine).** `SyncEngineWorker` only awaits/inspects the
  `FetchOperation` when its state is `Running`. A `fetchItems` op that **failed
  immediately** (state `Failed`, not `Running`) is skipped without ever checking
  `op->state()`/`op->errorString()`; the worker proceeds straight to `loadRecordsOrError`
  on the empty cache and reports the mapping as **successful with 0 records**. With
  `clobber=true` the target collection has already been wiped at that point, so a failed
  source fetch silently destroys the target and reports success.

---

## Symptom (user-facing)

- New WildPalms profile, "Local Akonadi" account, four hub↔akonadi mappings configured in
  the patchbay. Palm is empty (fresh).
- **Clobber Palm from PC** → wipes each Palm DB, pushes nothing (hub empty). Reported success.
- **HotSync** → runs all 8 mappings, every akonadi route logs `=== Starting sync ... ===`
  then immediately `unifiedContinueAfterConflicts completed` with no record I/O. Nothing
  reaches the hub or the Palm. Reported success.

---

## Evidence (reproducible)

**Bound Akonadi collections vs. what synced** (queried directly from
`~/.local/share/akonadi/akonadi.db`):

| Mapping target | Akonadi collection | Items in Akonadi | Rows in hub.db after sync |
|---|---|---|---|
| `<acct>:akonadi-54` (calendar) | …/calendars/…/personal/ | **99** | 0 |
| `<acct>:akonadi-64` (calendar, "TBS" filtered route) | …/calendars/…/tbs/ | **28** | 0 |
| `<acct>:akonadi-58` (calendar) | …/waiting-for/ | 0 (genuinely empty) | 0 |
| `<acct>:akonadi-156` (contacts) | contactGroups/all | 0 (wrong collection¹) | 0 |

`hub.db` tables `calendar` / `contacts` / `note` / `todo` are **all 0 rows**; the file's
mtime is the previous day — the sync never wrote it.

¹ Separate, WP/user-side: the user's contacts mapping points at the empty `contactGroups/all`
pseudo-collection; their real contacts are Akonadi collection 184 (502 items). Not a
libkalburator issue — noted only so it isn't mistaken for part of this bug. Even after the
fix below, that mapping must be re-bound to the real address book.

**HotSync log (abridged), per akonadi route:**
```
SyncEngineWorker: === Starting sync [wp-hub/calendar] -> [<acct>:akonadi-54/akonadi-54] ===
  mapping: "auto_wp-route-default-calendar-...-akonadi-54_sync1" mode: TwoWay (unmonitored)
SyncEngineWorker::unifiedContinueAfterConflicts completed for "auto_wp-route-...-akonadi-54_sync1"
```
No fetch/read/write lines, no error — instant "completion".

---

## Root cause A — per-collection AkonadiBackend never resolves its collection

Call chain:

1. `AkonadiProvider::createBackend(collectionId)` (`src/sync/akonadiprovider.cpp`) makes a
   **new** backend per route:
   ```cpp
   cfg.insert("akonadiCollectionId", collectionId);   // e.g. "akonadi-54"
   auto *b = static_cast<AkonadiBackend*>(AkonadiBackend::create(cfg, nullptr));
   ```
2. `AkonadiBackend::create()` (`src/calendar/akonadibackend.cpp:55-65`) stores the id but
   does **not** populate `m_collections`:
   ```cpp
   const QString collId = config.value("akonadiCollectionId").toString();
   if (!collId.isEmpty()) backend->m_scopedCollectionId = collId;   // only this
   ```
   The ctor (`:50`) calls `setupMonitor()` only.
3. `m_scopedCollectionId` is then used **only** for the session name (`:86-88`) and
   `backendId()` (`:851-852`). It is never consulted by `fetchItems`/`loadRecords`/
   `createRecord`.
4. `m_collections` (calId → real `Akonadi::Collection`) is inserted into in exactly three
   places: `loadCalendars()` (`:240`), `createCollection()` (`:674`), and the Monitor's
   `collectionAdded` handler (`:804`). **`loadCalendars()` has no caller anywhere in
   `src/`** (grep: only the definition + header overrides). The Monitor only reports
   *newly added* collections, never pre-existing ones (`setupMonitor`, `:84-121`). So for a
   freshly-created scoped backend, `m_collections` is permanently empty.
5. `fetchItems(calendarId)` (`:378-389`):
   ```cpp
   auto colIt = m_collections.find(calendarId);
   if (colIt == m_collections.end()) {
       op->fail("Unknown calendar: " + calendarId);   // <-- always taken
       Q_EMIT fetchFinished(calendarId, false, ...);
       return op;                                      // state == Failed
   }
   ```
6. `loadRecords(collectionId)` (`:918-922`) reads `m_itemsByCalendar` — empty (only
   `fetchItems`/Monitor fill it) — and returns `{}`.

Net: the scoped Akonadi backend can never fetch or read its collection's items.
**Writes are broken too** — `createRecord`/`updateRecord` also do `m_collections.find`
(`:492` etc.), so even hub→akonadi pushes would fail once reads are fixed.

`AkonadiContactsBackend` is the same shape: `create()` sets `m_scopedCollectionId`
(`src/contacts/akonadicontactsbackend.cpp:43-48`), `fetchItems` does
`m_collections.find(collectionId)` (`:162`), `loadRecords` reads `m_itemsByCollection`
(`:410-413`); `m_collections` is never seeded for the scoped collection.

---

## Root cause B — SyncEngineWorker swallows a failed fetchItems

`src/engine/syncengine.cpp` (source fetch `~2096-2140`; target fetch `~2192-2233`):

```cpp
SyncOperation *fetchOpRaw = nullptr;
QMetaObject::invokeMethod(srcBackend, [&]{ fetchOpRaw = srcBackend->fetchItems(srcColId); },
                          Qt::BlockingQueuedConnection);
QPointer<SyncOperation> fetchOp = fetchOpRaw;
if (fetchOp && fetchOp->state() == SyncOperation::Running) {   // <-- only Running is handled
    QEventLoop loop; ... loop.exec();
}
// no else: a Failed op is ignored; op->state()/errorString() are never checked
...
srcBlob->loadRecordsOrError(srcColId, sourceRecords, fetchErr);   // empty cache -> 0 records, no error
```

Only `fetchErr` from `loadRecordsOrError` can fail the mapping. A `fetchItems` op that
returns already-`Failed` is dropped on the floor → 0 records read, mapping reported
successful. For `clobber=true` the wipe happens *before* the target fetch
(`~2168-2190`), so a failed source/target fetch erases the target and still "succeeds".

---

## Proposed fixes (libkalburator owns the final shape)

### Fix A — seed the scoped collection so reads/writes resolve it

The scoped backend knows its collection id (`m_scopedCollectionId`, e.g. `"akonadi-54"`).
The numeric Akonadi id is the suffix after the `akonadi-` prefix. Two viable approaches:

- **Preferred (lazy, no discovery race):** add an `ensureScopedCollection()` helper that,
  when `m_collections` lacks the requested id but it equals `m_scopedCollectionId`,
  constructs an id-only `Akonadi::Collection(numericId)` and inserts it into
  `m_collections`. Call it at the top of `fetchItems`/`createRecord`/`updateRecord`/
  `deleteRecord`. `Akonadi::ItemFetchJob`/`ItemCreateJob` accept an id-only `Collection`
  (the server resolves it), so no `CollectionFetchJob`/`loadCalendars` round-trip or async
  ordering is needed. This keeps the per-collection scoping intent intact.

- **Alternative (eager):** have `AkonadiProvider::createBackend` (or the backend on first
  connect) resolve the single scoped `Akonadi::Collection` and seed `m_collections` before
  the backend is registered/used. Heavier and reintroduces an async-discovery ordering
  concern against the engine's `fetchItems` await.

Apply the same to `AkonadiContactsBackend` (`m_collections` / `m_itemsByCollection`).

If `loadCalendars()` is genuinely dead for the scoped path, either wire it or delete it —
right now it's a trap (looks like the population path but is never invoked).

**Regression test (A):** with `KALBURATOR_HAVE_AKONADI=ON`, a calendar-layer integration
test (per `docs/phase0/04l-...`) that creates a scoped `AkonadiBackend` for a collection
known to hold N>0 incidences and asserts `fetchItems(id)` succeeds and
`loadRecords(id).size() == N`. A unit-level guard: a scoped backend's `fetchItems` for its
own `m_scopedCollectionId` must NOT return an immediately-`Failed` op with "Unknown
calendar".

### Fix B — surface a failed fetchItems in the engine

In both fetch blocks, after obtaining `fetchOp`, treat a non-cancel `Failed` op as a
mapping failure (set `m_currentResult.success = false`, `errorMessage = op->errorString()`,
emit `syncCompleted`, return) instead of falling through to `loadRecordsOrError`. Critically
this must run for `clobber=true` *before* (and independently of) the wipe wherever possible,
and at minimum must convert "fetch failed after wipe" into a reported failure rather than a
success.

**Regression test (B):** a stub backend whose `fetchItems` returns an immediately-`Failed`
op must cause the mapping to fail (not succeed with 0 records); and a clobber whose source
fetch fails must report failure (existing `tests/calendar` stub harness + a fail-fetch stub).

---

## Acceptance criteria

1. A scoped `AkonadiBackend`/`AkonadiContactsBackend` for a non-empty collection reads its
   records (`loadRecords().size() > 0`); writes resolve the collection too.
2. On WP v0.69+fix: a HotSync of the profile above pulls collection 54 (99) and 64 (28)
   into `hub.db`'s `calendar` table, and the palm↔hub legs then push them to the Palm.
3. A `fetchItems` failure fails the mapping (and aborts/【reports failure for】 a clobber)
   rather than reporting success with 0 records.
4. libkalburator suite green; PlanStan ctest baseline green before tagging
   (`feedback_planstan_pretest_for_upstream`).

---

## WP-side follow-ups (tracked here, not libkalburator's)

- Re-bind the contacts mapping from `akonadi-156` (empty `contactGroups/all`) to the real
  address book (collection 184). The patchbay should arguably not have offered
  `contactGroups/all` as a contacts target — worth a separate WP look once the backend works.
- Once WP bumps past the lib fix, re-run the HotSync → Clobber flow on device to confirm
  Akonadi→hub→Palm end to end (closes the long-pending clobber-sync Task 12 hardware
  verification and the first live Akonadi-route transfer).
