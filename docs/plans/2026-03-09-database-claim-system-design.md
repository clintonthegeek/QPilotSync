# Database Claim System Design

**Date:** 2026-03-09
**Status:** Approved

## Problem

Multiple conduit plugins may want to handle the same Palm database. For example, a simple Calendar conduit and a more comprehensive Akonadi PIM conduit both want to sync DatebookDB. The current architecture assumes a 1:1 mapping between conduit and database, with no mechanism for the user to choose between competing handlers.

Additionally, some Palm applications (e.g., ShadowPlan, Documents To Go) use many databases — one per user-created document, prefixed by a pattern like `ShadP-*`. A conduit for such an app cannot enumerate all its databases in advance.

## Design

### Database Claims in Plugin Metadata

Each sync conduit declares which Palm databases it handles via a new `X-WildPalms-PalmDatabases` array in its JSON metadata, replacing the old singular `X-WildPalms-PalmDatabase` field.

Entries can be exact database names or glob patterns:

```json
"X-WildPalms-PalmDatabases": ["DatebookDB"]
```

```json
"X-WildPalms-PalmDatabases": ["ShadP-*", "ShadTags", "ShadViews", "ShadCat", "ShadFilters"]
```

Each entry is an exclusive claim. The format is extensible — in the future, entries could become objects with an `access` field (e.g., `{"name": "DatebookDB", "access": "observer"}`) but only string entries are implemented now.

Conduits that don't claim any databases (Install, Plucker, WebCalendar) omit this field entirely and remain governed by the existing enable/disable toggle.

### Per-Database Claim Descriptions

Plugins can advertise what makes them special for each database they claim via `X-WildPalms-ClaimDescriptions`:

```json
"X-WildPalms-ClaimDescriptions": {
    "DatebookDB": "Syncs into Akonadi calendar. Integrates with KOrganizer, Merkuro, and Thunderbird.",
    "AddressDB": "Syncs into Akonadi contacts. Integrates with KAddressBook and KMail."
}
```

Per-database descriptions rather than one global blurb, because a multi-database plugin may have different selling points for each claim. Falls back to `KPlugin.Description` if omitted.

### Contention Resolution and User Preferences

When `ConduitManager::discoverConduits()` runs, it builds a database-to-conduits map — for each claimed database name or pattern, which plugins can handle it. Multiple plugins claiming the same database is expected.

Profile storage adds a per-database active conduit selection:

```ini
[databases]
DatebookDB/activeConduit=akonadi-pim
AddressDB/activeConduit=contacts
ToDoDB/activeConduit=akonadi-pim
```

Selection rules:

- If only one discovered plugin claims a database, it is the active handler automatically.
- If multiple plugins claim the same database, the user must choose. Until they do, no conduit handles that database.
- A multi-database plugin can be active for some of its claims and not others. Selection is per-database, not per-plugin.

Glob pattern contention: if plugin A claims `"DatebookDB"` and plugin B claims `"Date*"`, both match DatebookDB. The contention map normalizes this — exact names and patterns that overlap are treated as competing claims.

### Runtime Activation Rules

- **Sync conduits (with database claims):** only run if at least one of their claimed databases has them selected as the active handler. If none of their claims are selected, the conduit is skipped entirely.
- **Standalone conduits (no database claims):** governed by the existing enable/disable toggle, run independently.

Unclaimed databases on the Palm device are ignored. Backup is a separate concern.

### Dependency Ordering

`RunBefore` and `RunAfter` arrays support two reference types:

- **Conduit ID** (plain string): `"webcalendar"` — references a specific plugin.
- **Database reference** (`@` sigil): `"@DatebookDB"` — resolves to whichever conduit is currently the active handler for that database.

Example — WebCalendar must run before whatever handles DatebookDB:

```json
"X-WildPalms-RunBefore": ["@DatebookDB"]
```

During `resolveExecutionOrder()`, the ConduitManager expands `@DatabaseName` references to the active conduit ID for that database. If no conduit is active for that database, the reference is silently dropped.

The `@` sigil only works with exact database names, not glob patterns. To order relative to a conduit that uses glob claims, reference it by conduit ID directly.

### Interface Changes

**`ISyncConduit`:**
- Remove `palmDatabaseName()` (returns `QString`)
- Add `palmDatabaseNames()` returning `QStringList`

**`SyncContext` additions:**
- `QStringList activeDatabases` — the subset of this conduit's claims that the Profile has selected. A conduit claiming DatebookDB and ToDoDB but active only for ToDoDB sees `["ToDoDB"]`.

**`ConduitManager` additions:**
- `QMap<QString, QStringList> databaseClaimMap() const` — database name to list of conduit IDs that claim it
- `QString activeConduitForDatabase(const QString &dbName) const` — resolves active handler, consulting Profile
- `QStringList activeDatabasesForConduit(const QString &conduitId) const` — inverse lookup
- Glob pattern matching during claim resolution

**`PluginInfo` struct changes:**
- Add `QStringList databaseClaims` — raw claim strings from metadata, including globs

**`Profile` additions:**
- `QString activeDatabaseHandler(const QString &dbName) const`
- `void setActiveDatabaseHandler(const QString &dbName, const QString &conduitId)`
- Per-conduit enable/disable removed for sync conduits (replaced by database selection). Retained for standalone conduits only.

### Settings UI

The conduit settings page splits into two sections:

**Database Handlers** — Lists every database name discovered across all plugin claims (union of all `X-WildPalms-PalmDatabases` entries, with glob patterns shown as-is). Each entry shows a dropdown of plugins that claim it. When only one plugin claims a database, the dropdown is pre-selected. When multiple compete, the dropdown defaults to unselected until the user picks. The claim description is shown below or as a tooltip when a handler is selected in the dropdown.

**Standalone Conduits** — Lists conduits with no database claims (Install, Plucker, WebCalendar). Simple enable/disable toggles.

### Migration of Existing Plugins

All seven in-tree plugins updated. No out-of-tree plugins exist, so no backward compatibility needed.

| Plugin | Old `PalmDatabase` | New `PalmDatabases` |
|---|---|---|
| Calendar | `"DatebookDB"` | `["DatebookDB"]` |
| Contacts | `"AddressDB"` | `["AddressDB"]` |
| Memo | `"MemoDB"` | `["MemoDB"]` |
| Todo | `"ToDoDB"` | `["ToDoDB"]` |
| WebCalendar | *(none)* | *(none — standalone)* |
| Install | *(none)* | *(none — standalone)* |
| Plucker | *(none)* | *(none — standalone)* |

Each sync conduit:
- Implements `palmDatabaseNames()` instead of `palmDatabaseName()`
- Adds `X-WildPalms-ClaimDescriptions` (optional but recommended)
- Updates `RunBefore`/`RunAfter` to use `@` sigil references where appropriate
