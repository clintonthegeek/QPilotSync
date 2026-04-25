# Phase E.11 — ToDo Plugin Design

**Status:** Draft, 2026-04-25
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.11 (line 589)
**Predecessor:** Phase E.10 (Calendar plugin landed 2026-04-24)
**Successor:** Phase E.12 (Contacts plugin)

## Goal

Rewrite the ToDo conduit as the third new-ABI `IBackendPlugin`, after Memo
(E.9) and Calendar (E.10). Become the second consumer of `PalmBackend` plus
the new shared AppInfo reader, exposing Palm ToDoDB category slots as virtual
sub-collections that route end-to-end through `BlobSyncEngine::twoWayWithBaseline`,
with a ToDo-aware conflict handler that protects completion-status from being
silently reverted by an unrelated edit on the other side.

Land behind a new `WILDPALMS_TODO_PLUGIN_V2` toggle (default ON) following
Memo's and Calendar's pattern; the legacy `TodoConduit` stays buildable in tree
until E.16 deletes the old surface.

## Decisions

The five questions answered during brainstorming, plus one architectural
detail surfaced while drafting.

### Decision #1 — Blob-face format is iCalendar VTODO

`TodoBlobBackend` exposes records as VCALENDAR bytes carrying a single VTODO,
mirroring Calendar's E.10 shape. `TodoIcsTranscoder` composes
`WildPalms::PalmCodecs::decodeTodo` (E.7, Palm bytes → `Todo` POD) with a
`Todo` → `KCalendarCore::Todo::Ptr` mapper, then serialises via
`KCalendarCore::ICalFormat`. Reverse direction inverts.

**Why:** ToDo is calendar-shaped — `KCalendarCore::Todo` is an `Incidence`,
the E.7 codec already produces the POD that maps onto it cleanly, and a future
CalDAV/local-task-list target can sync via libkalburator without an extra
transcoding hop. Calendar + ToDo share a transcoder family (one mental model
for both plugins).

**Alternative considered:** Markdown with YAML frontmatter, mirroring memo's
shape. Rejected because the `Todo` POD carries enough structured fields
(due-date, priority, completion, isPrivate) that flat Markdown either loses
fidelity or becomes a YAML grab-bag — and there's no disk-friendliness
upside today (no `LocalBlobBackend` consumer exists yet).

### Decision #2 — Virtual sub-collections per category

`TodoBlobBackend` exposes `palm:todo/<N>` for each populated category slot:
slot 0 ("Unfiled", always present) plus slots 1..15 named in the ToDoDB
AppInfo block. Records route by `PalmRecord::category`.

**Why:** mirrors E.10's calendar shape and the parent spec's "category
routing verified" exit-gate phrase; CalDAV task-list mapping per Palm category
is the obvious near-term target. The reader plumbing is mechanical reuse from
E.10 (Decision #6 below promotes it into a shared lib).

**Alternative considered:** flat single `palm:todo` collection with category
as VTODO `CATEGORIES:` metadata, mirroring memo. Rejected: pushes routing into
target-side code that doesn't exist yet, and silently demotes the parent
spec's category-routing claim from "tested" to "trust me."

### Decision #3 — `TodoConflictHandler` with one overlay

Land a `TodoConflictHandler` that delegates to `PalmConflictHandler` for the
shared archive/secret/category overlays, with one ToDo-aware overlay applied
first:

1. **Completion-asymmetric merge** → if exactly one side flipped
   `isComplete` false→true and the other side touched non-completion fields,
   return `Merge` with `{isComplete=true; text/priority/due fields from the
   editing side}`. Falls through to `PalmConflictHandler::resolve` for every
   other diff shape.

**Why:** under pure `lastModified` tie-breaking (the path memo uses), a Palm
that marks a task complete at `t1` will silently lose that flag if the target
was edited at `t2 > t1` for any reason, even a cosmetic note tweak. That's
the one historical failure mode I'd bet money is in the legacy `TodoConduit`'s
bug history. The other speculative overlays (priority tie-break, due-date
"one cleared / one moved") are not on the table without evidence.

**Alternative considered:** ship without a handler, like memo did. Rejected
per the rationale above. A "stub the overlay for later" middle ground would
just be code without tests; the overlay either earns its keep from day one or
shouldn't ship.

### Decision #4 — Reuse `TaskView` untouched

`TaskView` is already a standalone `QWidget` with no legacy-conduit
dependency in its header (`taskview.h:1-30`). The new plugin returns a fresh
`TaskView` from `IBackendPlugin::createMainView`, exactly mirroring E.9's
`MemoView` and E.10's `CalendarView` reuse pattern.

**Why:** any rewiring of TaskView's data path to the new adapter is a
separate concern that doesn't belong in E.11. Pulling UI work into a backend
sub-phase is the kind of scope creep that breaks the buildable-at-each-phase
invariant from the parent spec's row-3 decision.

### Decision #5 — `tst_todo_v2` shape: ~25 tests, MockBlobBackend e2e

Component tests per artefact (transcoder round-trip, blob backend collection
routing, conflict handler completion-overlay, plugin metadata) plus 3–4 e2e
scenarios via `BackendPluginManager` against `MockBlobBackend` (matching
`tst_calendar_v2`'s id-space deferral). Total ~25 tests.

**Why:** Calendar set the standard for calendar-format-shaped plugins; ToDo
is the second member of that family. Lighter coverage hands regression risk
forward to E.18 unnecessarily, especially around the completion overlay
(subtle merge logic where one mishandled state transition silently breaks
task workflow).

**Alternative considered:** memo-shaped (~15 tests, single focused e2e) or
parent-spec-literal "smoke passes" (one round-trip). Both rejected per above.

### Decision #6 — Promote `CategoryAppInfoReader` into a shared lib

The E.10 reader (`src/plugins/calendar/categoryappinforeader.{h,cpp}`) wraps
pisock's `unpack_CategoryAppInfo`, which is generic across Palm's standard
AppInfo layout (Datebook, ToDoDB, AddressDB, MemoDB all share it). To reuse it
from the ToDo plugin without a runtime-plugin → runtime-plugin dependency:

1. Move both files from `src/plugins/calendar/` to `src/palm/calendar/` and
   add them to the existing `WildPalmsPalmCalendar` static lib.
2. Rename `parseDatebookAppInfo` → `parseCategoryAppInfo`. The function body
   doesn't change; the new name reflects what it actually parses.
3. Update Calendar plugin's CMakeLists, its `calendarbackendplugin.cpp` call
   site, and `tests/plugins/calendar/tst_categoryappinforeader.cpp` to the
   new name + new include path.

**Why:** keeps E.11 honest — the alternative is duplicating identical pisock
glue across plugins, which is the classic "premature interface, eventual
divergence" trap. `WildPalmsPalmCalendar` is the right home today (it already
houses the `CategoryMappingStore` the reader populates); a future
generalisation (e.g. a dedicated `WildPalmsPalmCategories` lib) can wait
until a third consumer needs it.

**Alternative considered:** keep the reader in calendar plugin and introduce
an awkward indirection (e.g. expose the parser through `PalmDeviceConnection`).
Rejected as architecturally worse than a file move.

---

## Scope

**In scope (E.11):**

- New plugin under `src/plugins/todos/` alongside the legacy:
  - `TodoBackendPlugin` (`IBackendPlugin` implementation)
  - `TodoBlobBackend` (`IBlobBackend` over `PalmBackend`'s ToDoDB)
  - `TodoIcsTranscoder` (Palm bytes ↔ VTODO bytes)
  - `TodoConflictHandler` (completion-asymmetric overlay)
  - `todo-backend-plugin.json` (plugin metadata)
- AppInfo-reader promotion (Decision #6): file move + symbol rename + 3
  call-site updates.
- CMake toggle `WILDPALMS_TODO_PLUGIN_V2` in `src/plugins/todos/CMakeLists.txt`
  (default ON), mirroring memo + calendar.
- `tst_todo_v2` test executable at `tests/plugins/todos/` covering
  transcoder, blob backend, conflict handler, plugin metadata, and 3–4 e2e
  scenarios.

**Out of scope (deferred, called out in plan header):**

- `LocalBlobBackend` real-target tests for ToDo (deferred to E.15+, the same
  id-space cutover memo + calendar deferred).
- TaskView ↔ adapter rewiring. UI works against today's data path.
- Live-device integration test. Deferred to E.18 (POSE64 sandbox).
- Legacy plugin removal. Deferred to E.16.
- Speculative conflict overlays (priority tie-break, due-date asymmetric
  edits). Land in a follow-up if real syncs surface them.

**Pre-existing pieces reused unchanged:**

- `src/palm/codecs/todocodec.{h,cpp}` (E.7) — Palm bytes ↔ `Todo` POD.
- `src/palm/adapters/palmtodosadapter.{h,cpp}` (E.7) — typed adapter,
  available if any consumer wants it (TaskView does not, per Decision #4).
- `src/plugins/todos/taskview.{cpp,h}` — reused via `createMainView`.
- `src/palm/conflict/PalmConflictHandler` — base resolution path; the new
  handler composes one for delegation.
- `src/palm/calendar/categorymappingstore.{h,cpp}` — keyed by Palm dbName;
  ToDo plugin uses key `"ToDoDB"`.
- `src/runtime/backendpluginmanager.{h,cpp}` (E.8) — loads the new plugin.

---

## Architecture

### End-state shape

```
                    TodoBackendPlugin (IBackendPlugin)
                              │
                ┌─────────────┼─────────────┐
                │             │             │
                ▼             ▼             ▼
       TodoBlobBackend  PalmTodosAdapter*  TaskView
       (IBlobBackend)   (E.7, available;   (createMainView)
              │          unused by view)
              │ wraps
              ▼
        PalmBackend (shared, ToDoDB collection)
              │
              ▼
        IPalmDatabaseAccess (PilotLink or Mock)

       TodoConflictHandler (registered "palm-todo")
              │ delegates to
              ▼
       PalmConflictHandler (shared, registered "palm")

       CategoryAppInfoReader (now in WildPalmsPalmCalendar)
              │ populates
              ▼
       CategoryMappingStore (key "ToDoDB", slots 0..15)
```

`* PalmTodosAdapter` ships from E.7 and is wired through the plugin only if a
future UI consumer asks for it. TaskView in E.11 does not consume it.

### Class layout

- **`TodoBackendPlugin : QObject, IBackendPlugin`** at
  `src/plugins/todos/todobackendplugin.{h,cpp}`. Mirrors
  `CalendarBackendPlugin`'s shape: `pluginId()/displayName()/icon()/...`
  metadata, `claimedDatabases() == {"ToDoDB"}`, `createBackends(host, device)`
  builds the chain and populates the mapping store via the reader,
  `createConflictHandler()` returns a fresh `TodoConflictHandler`,
  `createMainView()` returns a `TaskView`. Owns the per-session
  `CategoryMappingStore` and the `PalmBackendConfig`. Caches the
  `PalmDeviceConnection*` for `createConflictHandler` (same trick
  `CalendarBackendPlugin` uses).

- **`TodoBlobBackend : Kalburator::Sync::IBlobBackend`** at
  `src/plugins/todos/todoblobbackend.{h,cpp}`. Wraps `device->palmBackend()`'s
  `ToDoDB` collection. Owns the `TodoIcsTranscoder` (transcoder is a free
  namespace, the backend just calls into it). `collections()` derives from
  the mapping store: emit `palm:todo/0` always; `palm:todo/<N>` for each
  named slot 1..15. `loadRecords(collection)` filters PalmBackend records
  whose `category == N`, transcodes each. `createRecord/updateRecord` parse
  the slot back out of the collection id and stamp it on `PalmRecord`.
  `backendId() == "palm-todo"`.

- **`TodoIcsTranscoder`** at `src/plugins/todos/todoicstranscoder.{h,cpp}`.
  Two free functions in `WildPalms::TodoPlugin`:

  - `QByteArray encodePalmToIcs(const PalmRecord &)` — decodeTodo →
    `KCalendarCore::Todo::Ptr` mapper → `ICalFormat::toString` over a fresh
    `MemoryCalendar`. Stamps `X-WP-PALM-CATEGORY-SLOT` (string) and
    `X-WP-PALM-RECORDID` (string) for round-trip.
  - `std::optional<PalmRecord> decodeIcsToPalm(const QByteArray &, int slotHint)`
    — `ICalFormat::fromString` → first VTODO → mapper → `encodeTodo` (E.7).
    Reads `X-WP-PALM-RECORDID` if present; otherwise `recordId = 0`.

- **`TodoConflictHandler : Kalburator::Sync::QSyncCore::ConflictHandler`** at
  `src/plugins/todos/todoconflicthandler.{h,cpp}`. Composes (owns) a
  `PalmConflictHandler` for delegation. `resolve(snapshot)`:

  1. Decode both sides' bytes via `TodoIcsTranscoder::decodeIcsToPalm` →
     decode the inner Palm bytes via `decodeTodo` to get `Todo` PODs.
  2. If either decode fails → straight to
     `PalmConflictHandler::resolve(snapshot)`. (Decode failures are not the
     overlay's problem.)
  3. Compute the diff. If exactly-one-side `false→true` on `isComplete`
     **and** the other side has any non-completion field changed (description,
     note, due, priority, isPrivate) **and** the completion-flipping side did
     not change those fields → return `Merge` with the engineered VTODO bytes
     for `{isComplete=true, other-side's text/priority/due/private}`.
  4. Otherwise → delegate to `PalmConflictHandler::resolve(snapshot)`.

- **`todo-backend-plugin.json`** — `KPlugin.Id == "todo"`,
  `X-WildPalms-PluginType == "backend"`, `X-WildPalms-PalmDatabases ==
  ["ToDoDB"]`, `X-WildPalms-DefaultEnabled: true`,
  `X-WildPalms-SortOrder: 30` (after memo=10 and calendar=20).

### AppInfo-reader promotion

The file move from Decision #6 lands first in E.11's plan (it's a
pre-condition of the plugin compiling). Concretely:

- Move `categoryappinforeader.{h,cpp}` into `src/palm/calendar/`.
- Rename `parseDatebookAppInfo` → `parseCategoryAppInfo` in both files.
- Update `src/palm/calendar/CMakeLists.txt` to add the two files to the
  `WildPalmsPalmCalendar` STATIC library.
- Update `src/plugins/calendar/CMakeLists.txt` to drop the two files from
  the plugin's source list (they are reachable via `WildPalmsPalmCalendar`,
  which the plugin already links).
- Update `src/plugins/calendar/calendarbackendplugin.cpp:76` to call
  `parseCategoryAppInfo` (the only production call site).
- Update `tests/plugins/calendar/tst_categoryappinforeader.cpp` to call
  `parseCategoryAppInfo` and `#include <palm/calendar/categoryappinforeader.h>`.
- Update `tests/plugins/calendar/CMakeLists.txt` if its include path needed
  the plugin source dir (it already links `WildPalmsPalmCalendar`).

After the move, both Calendar and ToDo plugins use
`WildPalms::PalmCalendar::parseCategoryAppInfo` plus
`populateFromAppInfo(store, "ToDoDB"|"DatebookDB", bytes)`.

### Surface additions on `IPalmDatabaseAccess` / `PalmBackend`

None. E.10 already added `readAppBlock(const QString &dbName)` to both,
which is keyed by dbName and works for `"ToDoDB"` without change. Verified
during plan execution.

### CMake toggle plumbing

`src/plugins/todos/CMakeLists.txt` becomes:

```cmake
option(WILDPALMS_TODO_PLUGIN_V2 "Build the new IBackendPlugin-based ToDo plugin" ON)

if (WILDPALMS_TODO_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_todos_v2
        SOURCES
            todobackendplugin.cpp     todobackendplugin.h
            todoblobbackend.cpp       todoblobbackend.h
            todoconflicthandler.cpp   todoconflicthandler.h
            todoicstranscoder.cpp     todoicstranscoder.h
            taskview.cpp              taskview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_todos_v2 PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(wildpalms_todos_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync
            WildPalmsPalmCalendar          # AppInfo reader + CategoryMappingStore
            WildPalmsPalmCodecs            # decodeTodo
            WildPalmsPalmConflict          # PalmConflictHandler base
            KF6::CoreAddons KF6::CalendarCore KF6::I18n KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    # Legacy: kcoreaddons_add_plugin(wildpalms_todos … todoconduit/todomapper/taskview)
endif ()
```

The legacy branch is the file's existing top-level command, just demoted into
the `else ()`. Both targets never co-installed (one writes
`wildpalms/plugins/`, the other `wildpalms/conduits/`).

---

## Components

### `TodoIcsTranscoder`

```cpp
namespace WildPalms::TodoPlugin {
QByteArray encodePalmToIcs(const PalmRecord &record);
std::optional<PalmRecord> decodeIcsToPalm(const QByteArray &icsBytes, int slotHint);
}
```

Pure functions. The Todo↔KCalendarCore::Todo mapper (private to the .cpp)
covers:

| `Todo` field          | `KCalendarCore::Todo` field             | Notes                                                        |
| --------------------- | --------------------------------------- | ------------------------------------------------------------ |
| `description`         | `summary`                               | Palm "description" is the Todo's one-line title              |
| `note`                | `description`                           | Palm "note" is freeform body                                 |
| `hasIndefiniteDue`    | (drives `dtDue` validity)               | When true, omit DTSTART/DTDUE                                |
| `due`                 | `dtDue` (and `dtStart = dtDue`)         | Palm carries date-only; emit as date-only DTDUE              |
| `priority` (1..5)     | `priority` (1..9 mapping below)         | Palm 1=highest, 5=lowest. KCal: 1 = highest, 9 = lowest      |
| `isComplete`          | `isCompleted()` + `completed` timestamp | When flipping false→true, set `completed = QDateTime::currentDateTimeUtc()` |
| `isPrivate`           | `secrecy = SecrecyPrivate`              | Otherwise `SecrecyPublic`                                    |

Priority mapping: Palm 1→KCal 1, 2→3, 3→5, 4→7, 5→9. Reverse rounds: KCal
1→Palm 1, 2→1, 3→2, 4→2, 5→3, 6→4, 7→4, 8→5, 9→5. Lossy on the round trip
when KCal sources arrive with even priority, but lossless for any value the
Palm could have authored (the only case that needs to be exact).

X-properties on the VTODO:

- `X-WP-PALM-CATEGORY-SLOT` — int as string, written and read back by
  `decodeIcsToPalm` (used to tolerate slot mismatches between collection-id
  hint and embedded slot; `slotHint` wins).
- `X-WP-PALM-RECORDID` — string repr of `PalmRecord::recordId`. Round-trips
  through libkalburator's blob store.

### `TodoBlobBackend`

```cpp
class TodoBlobBackend : public Kalburator::Sync::IBlobBackend {
public:
    TodoBlobBackend(PalmBackend *palm, CategoryMappingStore *store);
    QString backendId() const override { return "palm-todo"; }
    QList<CollectionInfo> collections() const override;
    QList<BackendRecord>  loadRecords(const QString &collectionId) override;
    BackendRecord         createRecord(const QString &collectionId,
                                       const QByteArray &data) override;
    bool                  updateRecord(const QString &collectionId,
                                       const QString &recordId,
                                       const QByteArray &data) override;
    bool                  deleteRecord(const QString &collectionId,
                                       const QString &recordId) override;
};
```

`collections()` always emits `palm:todo/0` (display name "Unfiled"). For
slots 1..15, emits `palm:todo/<N>` if `store->slotName("ToDoDB", N)` is
non-empty. Display name = the slot name from the store. Both calls are O(16).

Routing on read: a single `palm->loadRecords("ToDoDB")` per session; cache
in a per-collection partition keyed by `category`. Routing on write: parse
slot from collection-id suffix; stamp into `PalmRecord::category`; pass to
`palm->createRecord`/`updateRecord`. Cross-collection moves (slot change) are
deletes + creates from the engine's view.

### `TodoConflictHandler`

```cpp
namespace WildPalms::TodoPlugin {
class TodoConflictHandler : public Kalburator::Sync::QSyncCore::ConflictHandler {
public:
    TodoConflictHandler(PalmDeviceConnection *device,
                        CategoryMappingStore *store);
    Resolution resolve(const ConflictSnapshot &snapshot) override;
private:
    PalmConflictHandler m_palm;  // composed; takes (device, store)
    // overlay implementation lives in the .cpp
};
}
```

The overlay logic, in pseudo-code:

```
let lhs = decode(snapshot.left.bytes); let rhs = decode(snapshot.right.bytes);
if !lhs or !rhs: return m_palm.resolve(snapshot)

let diff = computeDiff(lhs, rhs)
if diff.completionFlipped == OneSideOnly &&
   diff.completionFlipper.otherFieldsUnchanged &&
   diff.peer.hasNonCompletionFieldChanges:
    let merged = peer  // take peer's text/priority/due/private
    merged.isComplete = true
    return Merge(encode(merged))

return m_palm.resolve(snapshot)
```

The diff uses the `Todo` POD's `operator==` (already generated, line
codec.h:24) plus per-field equality checks for the merge construction. Decode
failures hand off to the base path so the overlay never blocks resolution.

### `TodoBackendPlugin`

Mirrors `CalendarBackendPlugin` almost line-for-line:

- `claimedDatabases() == {"ToDoDB"}`.
- `createBackends(host, device)` — instantiates `m_categoryStore`,
  reads the AppInfo block via `device->palmBackend()->readAppBlock("ToDoDB")`,
  calls `populateFromAppInfo(*m_categoryStore, "ToDoDB", bytes)`, builds a
  `TodoBlobBackend`, returns it as the `blobBackend` field. Typed `SyncBackend`
  field stays null (no typed-todo upstream layer; deferred per parent spec).
- `createConflictHandler()` → `new TodoConflictHandler(m_device, m_categoryStore.get())`.
- `hasMainView() == true`, `createMainView(parent) == new TaskView(parent)`,
  `mainViewName() == i18n("Tasks")`, `mainViewIcon()` = appropriate KF icon
  (e.g. `view-pim-tasks`).
- `enrichConflictSnapshot` / `formatConflictRecordHtml` — minimal: render the
  Todo summary + completion state as HTML for the conflict dialog; full
  fidelity can wait for a second iteration (memo's plugin is similarly
  minimal here).

---

## Data flow

**Sync (read side):**

1. `BackendPluginManager` loads `wildpalms_todos_v2.so`.
2. `TodoBackendPlugin::createBackends(host, device)` builds the chain.
3. `BlobSyncEngine` calls `TodoBlobBackend::collections()`, sees
   `palm:todo/0` plus any populated slots, then `loadRecords` on each.
4. For each `palm:todo/<N>`: pull all records from `PalmBackend("ToDoDB")`,
   filter `category == N`, transcode bytes via `encodePalmToIcs`.
5. Engine hashes each blob and runs `twoWayWithBaseline` against the
   counterparty. Conflicts hit `TodoConflictHandler` registered as
   `"palm-todo"`.

**Sync (write side):**

1. Engine calls `createRecord(collectionId, vtodoBytes)` /
   `updateRecord(...)`.
2. `decodeIcsToPalm(vtodoBytes, slotFromCollectionId)` → `PalmRecord`.
3. Forward to `PalmBackend::createRecord/updateRecord`. The Palm device
   write path is unchanged from E.4.

**Plugin lifecycle:**

1. `BackendPluginManager::loadAll()` discovers the `.so` via KCoreAddons.
2. Plugin metadata (`X-WildPalms-PluginType: backend`, claims `ToDoDB`)
   accepted by manager.
3. On profile build: `createBackends(host, device)` invoked once per
   session.
4. On profile teardown: plugin destructor releases `m_categoryStore`,
   `m_palmConfig`, the borrowed `m_device` pointer is forgotten.

---

## Error handling

- AppInfo block missing/short: `populateFromAppInfo` returns false, store
  stays empty → `collections()` emits only `palm:todo/0`. Sync still works for
  Unfiled. Logged at info level.
- `decodeIcsToPalm` failure inside the conflict handler: hand off to
  `PalmConflictHandler::resolve` — the base path always wins. Logged at
  warning level.
- `decodeIcsToPalm` failure inside `createRecord`/`updateRecord`: return
  `BackendRecord{}` for create or `false` for update; engine surfaces as a
  per-record error without aborting the sync. Same pattern E.10 chose.
- `PalmBackend::readAppBlock` failure: same as missing AppInfo — store empty,
  Unfiled-only. Logged.
- TaskView construction failure: not currently possible (no I/O in ctor); a
  future failure would propagate via the `IBackendPlugin::createMainView`
  contract (return null is allowed).

---

## Testing

`tests/plugins/todos/CMakeLists.txt` lands a single `tst_todo_v2` executable
plus per-component test sources. Aim ~25 tests broken down approximately:

- `tst_todoicstranscoder.cpp` — 8 tests:
  - encodePalmToIcs: empty input, all-fields-set, indefinite-due,
    isComplete=true, isPrivate=true, X-properties present.
  - decodeIcsToPalm: round-trip, slotHint precedence over embedded slot,
    invalid bytes → nullopt.
- `tst_todoblobbackend.cpp` — 8 tests:
  - collections() with empty store (Unfiled only), with 3 named slots, with
    slot 0 explicitly named (verify it stays "Unfiled").
  - loadRecords routes by category, mixed-category dataset.
  - createRecord/updateRecord stamp slot from collection id; cross-slot move
    is delete+create.
  - deleteRecord forwards to PalmBackend.
- `tst_todoconflicthandler.cpp` — 5 tests:
  - completion-asymmetric-merge happy path (Palm flips complete; target
    edits note → Merge with both).
  - completion change on both sides → falls through to PalmConflictHandler.
  - completion change with same-side text edit → falls through.
  - decode failure on either side → falls through.
  - registration: handler returns "palm-todo" id.
- `tst_todobackendplugin.cpp` — 4 tests:
  - metadata: pluginId, displayName, claimedDatabases, sort order.
  - createBackends populates the mapping store from AppInfo bytes.
  - createConflictHandler returns a `TodoConflictHandler`.
  - createMainView returns a non-null QWidget that is a TaskView.

End-to-end via `tst_todo_v2_e2e.cpp` (integrated into the same target via
QTEST_MAIN switch or as a separate executable; pick whichever matches
`tst_calendar_v2`'s convention) — 4 scenarios:

1. Empty Palm + 2 target VTODO records → Palm gains both, in correct slots.
2. Palm has 3 records across 2 slots, target empty → all 3 land at target,
   in correct sub-collections.
3. Conflict: Palm marks complete, target edits note → merged record on both
   sides.
4. Cross-slot move: target re-categorises a VTODO from slot 1 → slot 2 →
   Palm record's `category` updates accordingly.

End-to-end fixtures use `MockBlobBackend` as the target (per
`tst_calendar_v2`'s id-space deferral) and `MockPalmDatabaseAccess` as the
device. `LocalBlobBackend` integration is deferred to E.15+.

---

## Exit gate

Per parent spec row E.11 ("Smoke passes"), plus the Calendar-shaped bar from
Decision #5:

- WP `ctest` passes; ~25 new tests added, no pre-existing test regressions.
- `tst_todo_v2` covers the 4 e2e scenarios above against `MockBlobBackend`.
- `WILDPALMS_TODO_PLUGIN_V2=ON` (default) builds + installs
  `wildpalms_todos_v2.so` to `wildpalms/plugins/`. `WILDPALMS_TODO_PLUGIN_V2=OFF`
  builds the legacy `wildpalms_todos.so` to `wildpalms/conduits/` exactly as
  before.
- The AppInfo-reader move (Decision #6) leaves Calendar's `ctest` green.
- Plan header documents the deferrals (LocalBlobBackend, live-device,
  TaskView rewiring, legacy removal).

---

## Out of scope (deferred to later phases)

| Item                                          | Lands in                            |
| --------------------------------------------- | ----------------------------------- |
| LocalBlobBackend e2e for ToDo                 | E.15+ (id-space cutover)            |
| TaskView ↔ PalmTodosAdapter rewiring          | post-E.16 UI follow-up              |
| Live-device integration test in POSE64        | E.18                                |
| Legacy `TodoConduit`/`todomapper` removal     | E.16                                |
| Priority/due-date conflict overlays           | post-E.18 if real syncs surface bug |
| ToDoDB AppInfo reader live-device test        | E.18                                |
| Typed `TodoSyncBackend` upstream extraction   | When a second consumer needs it     |

---

## Open questions

None blocking implementation. One stylistic call left to plan execution: do
we factor a shared `WildPalms::PalmIcs` helper for the X-property
read/write across calendar and todo transcoders, or keep both transcoders
self-contained for now? Lean toward self-contained — second member of a
family doesn't usually justify the third's abstraction.
