# libkalburator handoff — Akonadi scoped-backend reads 0 records (+ engine swallows fetch failures)

**Date:** 2026-06-12
**Direction:** WildPalms → libkalburator
**Status:** RESOLVED lib-side (same day) — see §Resolution at the bottom. Remaining:
lib PlanStan gate → merge → tag (WP then re-pins to the tag); WP on-device verification.
**Amended 2026-06-12:** Fix B rescoped — a blanket "Failed op ⇒ fail mapping" would break
the loadRecords-only backend contract (base-class `fetchItems` default); the fix must
discriminate "not implemented" from genuine failure. See §Root cause B / §Fix B.
**Severity:** High. Akonadi↔hub sync transfers nothing in any mode (HotSync / FullSync /
Clobber). This is the first on-device exercise of the per-collection ("Phase L.5") scoped
Akonadi backend; the read path was never wired to the scoped collection, so it has never
actually worked. Defect B (engine) additionally turns a failed source fetch during a
**clobber** into a silent "success" — the target is wiped even though the source was never
readable — a data-loss footgun.

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

- **B (compounding, SyncEngine).** `SyncEngineWorker` cannot distinguish a `fetchItems`
  op that **genuinely failed** from the base-class "not implemented" default op — both
  arrive already-`Failed`, and the engine's deliberate skip for the latter (the
  loadRecords-only backend contract, `syncengine.cpp:2090-2095`) swallows the former too.
  `op->state()`/`op->errorString()` are never checked; the worker proceeds to
  `loadRecordsOrError` on the empty cache (whose default also never reports an error) and
  reports the mapping as **successful with 0 records**. Under `clobber=true` the engine
  then wipes the target believing the source read succeeded, so a failed source fetch
  silently destroys the target and reports success.

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

## Root cause B — the engine can't tell a genuine fetchItems failure from "not implemented"

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

**The skip is partly deliberate** — the comment directly above the block
(`syncengine.cpp:2090-2095`) documents it: `SyncBackendBase::fetchItems` defaults to an
immediately-`Failed` op ("fetchItems() not implemented by this backend",
`src/sync/syncbackendbase.cpp:36-43`), and backends that read solely via `loadRecords`
rely on the engine ignoring that op and proceeding to `loadRecordsOrError`. Today that is
every backend that doesn't override `fetchItems` — including
`Sinks::FilteredCollectionBackend` (the hub side of every WP palm↔hub mapping) and WP's
palm plugin backends.

The defect is that a **genuine** failure from a backend that *does* implement
`fetchItems` (here: the scoped Akonadi backend's "Unknown calendar" fail-fast) arrives in
the identical `Failed` state and is swallowed by the same skip. The second guard,
`loadRecordsOrError`, can't catch it either: its default delegates to `loadRecords` and
never reports an error (`src/blob/iblobbackend.h:55-61`), and neither Akonadi backend
overrides it. Net: 0 records read, mapping reported successful.

For `clobber=true` the wipe ordering is actually already defensive — it is deliberately
placed *after* the source fetch (`syncengine.cpp:2168-2173`: "a target is never destroyed
when the source can't be read") and before the target fetch. But the protection is
defeated because the failed source fetch is indistinguishable from "not supported": the
engine reaches the wipe believing the source read succeeded with 0 records, erases the
target, and still "succeeds".

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

### Fix B — surface a *genuine* fetchItems failure without breaking loadRecords-only backends

A blanket "`Failed` op ⇒ fail the mapping" is **wrong**: it would break every backend
that relies on the base-class "not implemented" default (see Root cause B) — i.e. every
WP palm↔hub leg. The engine must discriminate two terminal cases:

- **"Fetch not supported"** (base-class default op): keep today's behavior — fall
  through to `loadRecordsOrError`. This is the loadRecords-only backend contract and must
  not change.
- **"Fetch genuinely failed"** (an overriding backend's op reached `Failed`): fail the
  mapping (set `m_currentResult.success = false`, `errorMessage = op->errorString()`,
  emit `syncCompleted`, return) instead of falling through to the stale/empty cache.

libkalburator owns the discriminator's shape; candidates, roughly in order of
preference:

1. A distinct terminal state (e.g. `SyncOperation::NotSupported`) returned by
   `SyncBackendBase::fetchItems`/`deleteItems` defaults — overriding backends are
   untouched, and the engine's check is a one-liner per fetch block.
2. A `supportsFetchItems()` capability probe on the backend, checked before calling.
3. (Least robust) matching the sentinel error string — works today, breaks silently on
   reword.

Apply to both fetch blocks (source `~2096`, target `~2196`). Worth considering the same
discrimination for `loadRecordsOrError` (its default never reports an error,
`src/blob/iblobbackend.h:55-61`; neither Akonadi backend overrides it) so a backend whose
fetch path is fixed but whose read path fails still surfaces.

No clobber reordering is needed: the wipe already sits after the source fetch by design
(`syncengine.cpp:2168-2173`). Fixing the discrimination restores that protection — a
genuinely failed source fetch then fails the mapping *before* the wipe runs.

**Regression tests (B):**

1. A stub backend that **overrides** `fetchItems` and returns an immediately-`Failed` op
   must cause the mapping to fail (not succeed with 0 records).
2. A backend using the **base-class default** ("not implemented") must continue to sync
   successfully via the `loadRecords` path — pins the loadRecords-only contract so the
   fix can't regress palm↔hub-style mappings.
3. A clobber whose source fetch genuinely fails must report failure **and must not wipe
   the target** (existing `tests/calendar` stub harness + a fail-fetch stub).

---

## Acceptance criteria

1. A scoped `AkonadiBackend`/`AkonadiContactsBackend` for a non-empty collection reads its
   records (`loadRecords().size() > 0`); writes resolve the collection too.
2. On WP v0.69+fix: a HotSync of the profile above pulls collection 54 (99) and 64 (28)
   into `hub.db`'s `calendar` table, and the palm↔hub legs then push them to the Palm.
3. A *genuine* `fetchItems` failure — from a backend that implements fetch — fails the
   mapping rather than reporting success with 0 records, and fails a clobber *before* the
   target wipe. Backends that don't implement `fetchItems` (base-class default op) keep
   syncing via the `loadRecords` path unchanged.
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

---

## Resolution (RESOLVED lib-side, 2026-06-12)

libkalburator fixed both defects same-day on branch `fix/akonadi-scoped-backend-reads`
(commit `493bd80`, off `main` @ v0.73). Response doc:
`~/dev/libkalburator/docs/2026-06-12-akonadi-scoped-backend-fix-response.md`.

- **Fix A** — the handoff's preferred lazy approach: `ensureScopedCollection(id)` on both
  backends seeds an id-only `Akonadi::Collection(numericId)` into `m_collections` when
  the requested id equals `m_scopedCollectionId`; wired into `fetchItems` and
  `createRecord` (`updateRecord`/`deleteRecord` resolve via the fetch-populated item
  cache). `loadCalendars()` stays (live test + non-scoped path). Regression guard:
  `tests/calendar/tst_akonadi_scoped_collection.cpp` (Akonadi profile), verified
  RED→GREEN for both backends.
- **Fix B** — the amendment's #1-preference discriminator: new
  `SyncOperation::NotSupported` terminal state; `SyncBackendBase::fetchItems` default now
  returns `notSupported(...)`; both engine fetch blocks fail the mapping on a genuine
  `Failed` op and fall through to `loadRecordsOrError` on `NotSupported`. All three
  amended regression tests shipped
  (`tests/engine/tst_engine_fetch_failure_discrimination.cpp`), including
  clobber-does-not-wipe-on-failed-source-fetch. Lib suite 149/149.
- **Known gap flagged by the lib (tracked, not blocking):** the first-sync fast path
  (`dispatchFirstSync`, `OneWayUpload` + same-shape + quick-path + non-clobber) is NOT
  fetch-gated — a cache-backed source (Akonadi) reads 0 records there, and a genuine
  fetch failure is still silent. WP's real Akonadi routes are TwoWay, so unaffected
  today; if WP ever runs an Akonadi route as OneWayUpload, the lib's preferred follow-up
  (gate `dispatchFirstSync` the same way) becomes load-bearing.

**WP consumption (2026-06-12):** pin bumped v0.69 → SHA
`493bd804a549e161718986065848f0af301b5667` (`CMakeLists.txt`); WP builds clean, ctest
**125/125**. Re-pin to the lib's next tag once the PlanStan gate passes and the branch
merges. Acceptance criterion 2 (HotSync pulls collections 54 → 99 / 64 → 28 events into
`hub.db`) remains the user's on-device step, along with re-binding the contacts mapping
to collection 184.
