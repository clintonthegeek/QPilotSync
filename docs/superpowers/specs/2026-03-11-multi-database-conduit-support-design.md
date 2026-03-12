# Multi-Database Conduit Support

Revisions to the WildPalms sync engine and conduit interfaces to support conduits that claim and sync multiple databases with different record structures. Motivated by the ShadowPlan conduit (5 database types, heterogeneous records) but designed as general infrastructure.

## Context

The current sync interface assumes one conduit handles one database with homogeneous flat records. The database claim system (recently implemented) already allows a conduit to claim multiple databases via `X-WildPalms-PalmDatabases`, but the sync engine treats `palmDatabaseNames()` as a flat list and only uses the first entry. Record conversion methods lack the context to distinguish which database a record belongs to.

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Who drives the per-database loop? | Hybrid (Option C) | Sync engine drives, conduit declares ordering. Conduit gets one `sync()` call per database but can rely on companions having synced first. |
| Sync state scoping | Per-database | Each database gets its own SyncState (ID mappings, baselines). Clean isolation, engine manages it transparently. |
| Context in record methods | Add `const SyncContext*` | Breaking change to `recordsEqual`, `palmRecordDescription`, `findMatch`. All existing conduits updated. |
| Intra-database ordering | Implicit (array order) | `X-WildPalms-PalmDatabases` array order = sync order. No new metadata field. |
| Glob expansion order | Unspecified | Individual databases within a glob match are independent; order doesn't matter. |

## Section 1: Interface Changes

### ISyncConduit (`src/core/isyncconduit.h`)

Three methods gain a `const SyncContext*` parameter:

```cpp
// Before:
virtual bool recordsEqual(PilotRecord *palmRecord,
                           BackendRecord *backendRecord) const = 0;
virtual QString palmRecordDescription(PilotRecord *record) const = 0;
virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                  const QList<BackendRecord*> &candidates) = 0;

// After:
virtual bool recordsEqual(PilotRecord *palmRecord,
                           BackendRecord *backendRecord,
                           const SyncContext *context) const = 0;
virtual QString palmRecordDescription(PilotRecord *record,
                                       const SyncContext *context) const = 0;
virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                  const QList<BackendRecord*> &candidates,
                                  const SyncContext *context) = 0;
```

Multi-database conduits use `context->palmDatabase` to dispatch to the correct codec. Single-database conduits ignore the parameter.

### SyncConduitBase (`src/sync/conduit.h`)

Same signature changes to the overrides.

**`palmRecordDescription`** is called from many locations throughout `conduit.cpp` — `hotSync()` (category change logging), `syncRecord()` (new record logging), `resolveConflict()` (conflict signal), `resolveConflictWithHandler()` (conflict record building), `restore()`, and the default `findMatch()`. All call sites must have `context` threaded through.

**`recordsEqual`** is not currently called from `conduit.cpp` — the base class uses baseline hash comparison and dirty flags for change detection. Adding `SyncContext*` to this method is forward-looking: multi-database conduits will need it when their override dispatches to different equality logic based on `context->palmDatabase`. Existing single-database conduits ignore the parameter.

**`findMatch`** default implementation calls `palmRecordDescription()`, so it receives context transitively.

## Section 2: Sync Engine — Per-Database Iteration

### Change to `syncConduit()`

Currently `syncConduit()` calls `sync(context)` once per conduit. The new behavior:

1. Read the conduit's `palmDatabaseNames()` — array order is sync order
2. For each entry, expand globs against the Palm's cached database list
3. For each resolved database name, call `sync(context)` with:
   - `context->palmDatabase` set to the specific database name
   - `context->state` set to a per-database `SyncState` instance

```
Current flow:
  syncConduit("shadowplan")
    → sync(context)  // context.palmDatabase = first claimed DB

New flow:
  syncConduit("shadowplan")
    → sync(context)  // context.palmDatabase = "ShadTags"
    → sync(context)  // context.palmDatabase = "ShadViews"
    → sync(context)  // context.palmDatabase = "ShadFilters"
    → sync(context)  // context.palmDatabase = "ShadCat"
    → sync(context)  // context.palmDatabase = "ShadP-Personal"
    → sync(context)  // context.palmDatabase = "ShadP-Work"
```

### Per-database SyncState

`stateForConduit()` currently takes one argument (conduit ID) and is declared in `syncengine.h`. It gains a second parameter:

```cpp
// syncengine.h — updated declaration
SyncState* stateForConduit(const QString &conduitId,
                            const QString &databaseName);
```

```cpp
// syncengine.cpp — updated implementation
SyncState* SyncEngine::stateForConduit(const QString &conduitId,
                                        const QString &databaseName)
{
    QString key = conduitId + "/" + databaseName;
    if (!m_states.contains(key)) {
        SyncState *state = new SyncState(m_palmUserName, key, this);
        if (!m_stateDirectory.isEmpty()) {
            state->setStateDirectory(m_stateDirectory);
        }
        state->load();
        m_states[key] = state;
    }
    return m_states[key];
}
```

The current single call to `stateForConduit(conduitId)` in `syncConduit()` moves inside the per-database loop and becomes `stateForConduit(conduitId, databaseName)`.

Note: Since `SyncState` constructs its file path as `baseDir/userName/key/`, a composite key like `"memos/MemoDB"` creates a nested directory `baseDir/userName/memos/MemoDB/`. This is intentional — it groups all databases for a conduit under one parent directory.

For single-database conduits this is functionally identical — `"memos/MemoDB"` instead of `"memos"`. The state file path changes, so existing state files will need migration or the first sync after this change will be treated as a first sync.

### Per-database collectionId

Currently `syncConduit()` sets `context.collectionId = conduitId`. For the per-database loop, this changes to `context.collectionId = conduitId + "/" + databaseName`, matching the state key. This ensures each database's backend records live in a separate collection — a ShadowPlan conduit syncing 5+ databases into one collection would be unworkable.

### Per-database context fields

In the per-database loop:
- `context.palmDatabase` — set to the specific database name for this iteration
- `context.collectionId` — set to `conduitId + "/" + databaseName`
- `context.state` — set to the per-database SyncState
- `context.activeDatabases` — retains the full list of all active databases for this conduit (not just the current one), so the conduit can see the broader context if needed

### Result accumulation

Results from each per-database `sync()` call accumulate into the conduit's overall result using the same addition pattern already used for per-conduit accumulation in `syncAll()`.

### Error handling within per-database iteration

If one database's `sync()` fails, the engine logs the error, marks the conduit's overall result as failed, and **continues** with the remaining databases. Rationale: a failure syncing `ShadTags` shouldn't prevent `ShadP-*` lists from syncing — partial sync is better than no sync. The conduit's accumulated result reflects all failures. Cancellation (user-initiated) stops immediately.

### Single-database conduits are unaffected

A conduit claiming `{"MemoDB"}` gets exactly one `sync()` call with `context->palmDatabase = "MemoDB"`. The only observable difference is the state file path gaining a `/MemoDB` suffix.

## Section 3: Database List Discovery

### Existing method on KPilotDeviceLink

`KPilotDeviceLink::listDatabases()` already exists and is fully implemented (calls `dlp_FindDBInfo` in a loop, returns all database names). It is already used by the UI layer. No new method needed — the sync engine just needs to call it.

### When called

Once at the top of `syncAll()` / `syncAllOrdered()`, before the conduit loop. The result is cached on `SyncEngine` as `m_palmDatabaseList` for the duration of the sync session.

### Glob expansion

Uses `QRegularExpression::wildcardToRegularExpression()` — the same pattern already used in `ConduitManager::activeConduitForDatabase()`. Applied in `syncConduit()` when iterating a conduit's claimed databases.

### Edge case: database not on Palm

If a conduit claims `ShadTags` but the Palm has no such database, that entry is skipped with a log message. No error. This allows conduits to claim databases that may not yet exist on the device (e.g., first sync from PC to Palm).

## Section 4: Changes to Existing Conduits

### Calendar, Contacts, Memo, Todos

Each conduit's header and implementation gain the `const SyncContext*` parameter on `recordsEqual()`, `palmRecordDescription()`, and `findMatch()`. The implementations don't change — they add the parameter and ignore it. Mapper classes are untouched.

Example (Memo):

```cpp
// Before:
bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override;
QString palmRecordDescription(PilotRecord *record) const override;

// After:
bool recordsEqual(PilotRecord *palm, BackendRecord *backend,
                   const SyncContext *context) const override;
QString palmRecordDescription(PilotRecord *record,
                               const SyncContext *context) const override;
```

### WebCalendar

`WebCalendarConduit` extends `SyncConduitBase` and overrides both `recordsEqual` and `palmRecordDescription`. It needs the same signature update as the other sync conduits — add `const SyncContext*`, ignore it.

### Plucker

Unaffected. Plucker is a tool conduit (`IToolConduit`) and does not implement `ISyncConduit`.

### State file migration

Existing state files are keyed by conduit ID (e.g., `memos`). After this change they're keyed by `conduitId/databaseName` (e.g., `memos/MemoDB`). Options:

- **Simple:** Treat the first sync after upgrade as a first sync. State rebuilds automatically. Acceptable for a pre-1.0 application.
- **Migration:** Rename existing state files. More work, low value at this stage.

Recommend the simple approach — accept a one-time first-sync on upgrade.

## Section 5: Documentation Updates

### `docs/plugin-developer-guide.md`

- Add that `X-WildPalms-PalmDatabases` array order determines intra-conduit sync order, with explanation (companion databases before data databases)
- Updated method signatures showing `const SyncContext*` parameter
- New section on multi-database conduits: using `context->palmDatabase` to dispatch, per-database state isolation, glob expansion at sync time
- Note that `recordsEqual` and `palmRecordDescription` must handle all claimed database types in a multi-database conduit
- Example:
  ```json
  "X-WildPalms-PalmDatabases": ["ShadTags", "ShadViews", "ShadFilters", "ShadCat", "ShadP-*"]
  ```
  "Tags, views, filters, and categories sync before task lists, ensuring referential integrity."

### `docs/SYNC_ENGINE_ARCHITECTURE.md`

- Per-database sync loop within a conduit
- Per-database SyncState scoping
- Database list discovery from Palm device
- Glob expansion mechanism

## Section 6: Test Coverage

### New sync engine tests

Using mock conduits (a test conduit claiming multiple databases that records invocation order):

- Multi-database conduit gets `sync()` called once per database in declared array order
- Glob expansion matches correct databases from a mock device database list
- Per-database SyncState isolation — state for one database doesn't affect another
- Single-database conduits behave identically to before (regression)
- Database not found on Palm is skipped with log message, no error
- Cancellation during per-database iteration stops remaining databases

### Interface tests

- `recordsEqual` and `palmRecordDescription` receive correct `context->palmDatabase` for each call
- `findMatch` receives context with correct database name

## Summary of Changes

| File | Change | Scope |
|---|---|---|
| `src/core/isyncconduit.h` | Add `SyncContext*` to 3 methods | Interface |
| `src/sync/conduit.h` | Same signature changes | Base class |
| `src/sync/conduit.cpp` | Thread `context` through all call sites | Base class |
| `src/sync/syncengine.h` | Add `m_palmDatabaseList`, update `stateForConduit` signature | Engine |
| `src/sync/syncengine.cpp` | Per-database iteration in `syncConduit()`, glob expansion, database list caching | Engine |
| `src/plugins/calendar/*` | Add `SyncContext*` parameter (unused) | Plugin |
| `src/plugins/contacts/*` | Add `SyncContext*` parameter (unused) | Plugin |
| `src/plugins/memo/*` | Add `SyncContext*` parameter (unused) | Plugin |
| `src/plugins/todos/*` | Add `SyncContext*` parameter (unused) | Plugin |
| `src/plugins/webcalendar/*` | Add `SyncContext*` parameter (unused) | Plugin |
| `docs/plugin-developer-guide.md` | Multi-database docs, ordering semantics, updated signatures | Docs |
| `docs/SYNC_ENGINE_ARCHITECTURE.md` | Architecture updates | Docs |
| `tests/` | New multi-database and regression tests | Tests |
