# WildPalms Plugin Developer Guide: Database Claim System

This guide covers the database claim system for 3rd-party conduit plugin
developers. It explains how to declare which Palm databases your plugin handles,
how contention between plugins is resolved, and how to declare execution
dependencies.

## Plugin Types

WildPalms has two plugin types:

### Sync Conduits (`ISyncConduit`)

Sync conduits handle bidirectional synchronization of Palm databases. They must
declare **database claims** -- the Palm databases they are responsible for. The
claim system determines which plugin handles each database when multiple plugins
compete.

Examples: Calendar (DatebookDB), Contacts (AddressDB), Memos (MemoDB).

### Standalone Conduits (`IConduit` / `IToolConduit`)

Standalone conduits are tools that don't claim Palm databases. They use simple
enable/disable toggles in the profile settings.

Examples: Install (pushes .prc/.pdb files), Plucker (fetches web content).

Set `"X-WildPalms-ConduitType": "tool"` in your JSON metadata to mark a
conduit as standalone. Tool conduits do not participate in the claim system.

## Database Claims

### Declaring Claims

Claims are declared in your plugin's JSON metadata file via the
`X-WildPalms-PalmDatabases` array:

```json
{
    "X-WildPalms-PalmDatabases": ["DatebookDB"]
}
```

Each entry is either an **exact database name** or a **glob pattern**.

### Exact Names vs Glob Patterns

Use exact names when your plugin handles a specific, well-known database:

```json
"X-WildPalms-PalmDatabases": ["MemoDB"]
```

Use glob patterns when an application stores data across a family of databases
with a common prefix:

```json
"X-WildPalms-PalmDatabases": ["DTG-*"]
```

This would match all Documents To Go databases (`DTG-WordDoc1`, `DTG-Sheet2`,
etc.). Standard glob wildcards `*` and `?` are supported and matched using
`QRegularExpression::wildcardToRegularExpression`.

### Exclusivity

Claims are exclusive. Only one plugin handles a given database at a time. If
your plugin is selected as the handler for `DatebookDB`, no other plugin will
sync that database.

### Claim Descriptions

Use `X-WildPalms-ClaimDescriptions` to provide per-database pitch text. This is
shown in the settings UI when the user chooses between competing plugins:

```json
"X-WildPalms-ClaimDescriptions": {
    "DatebookDB": "Syncs to Akonadi/KOrganizer. Full KDE PIM integration.",
    "AddressDB": "Syncs to Akonadi/KAddressBook. Full KDE PIM integration."
}
```

If no claim description is provided for a database, the plugin's general
`KPlugin.Description` is shown as a fallback.

### Future Format

The `X-WildPalms-PalmDatabases` array currently contains plain strings. A
future version may support objects with an `access` field to distinguish
read/write intent:

```json
"X-WildPalms-PalmDatabases": [
    { "name": "DatebookDB", "access": "readwrite" },
    { "name": "ContactsDB", "access": "readonly" }
]
```

This is not yet implemented. For now, use plain strings.

## Contention Rules

When multiple plugins claim the same database, contention is resolved as
follows:

1. **Single claimant**: If only one plugin claims a database, it is
   auto-selected. No user action required.

2. **Multiple claimants**: If two or more plugins claim the same database, the
   user must choose which one handles it via the profile settings UI.

3. **Per-database selection**: Selection is per-database, not per-plugin. If
   your plugin claims both `DatebookDB` and `AddressDB`, the user could select
   your plugin for `DatebookDB` but a different plugin for `AddressDB`.

4. **Sync eligibility**: A sync conduit only runs during sync if at least one of
   its claims is active (selected). If none of your claims are selected, your
   conduit is skipped entirely for that sync cycle.

5. **No selection**: If no conduit is selected for a contested database (the
   user hasn't chosen yet), that database is not synced.

## Dependency Ordering

Plugins can declare execution order constraints using `X-WildPalms-RunBefore`
and `X-WildPalms-RunAfter`.

### Conduit ID References

Reference a specific conduit by its `X-WildPalms-ConduitId`:

```json
"X-WildPalms-RunAfter": ["webcalendar"]
```

This means: "run my conduit after the `webcalendar` conduit." Use this when you
depend on a specific plugin's behavior.

### `@` Database References

Reference whichever conduit currently handles a database using the `@` sigil:

```json
"X-WildPalms-RunBefore": ["@DatebookDB"]
```

This means: "run my conduit before whatever conduit currently handles
`DatebookDB`." At sync time, the `@DatebookDB` reference is resolved to the
active conduit for that database (e.g., `calendar` or `akonadi-calendar`).

Use `@` references when:
- You depend on "whatever handles this data," not a specific plugin
- You want your ordering to work regardless of which competing plugin the user
  selected

Use conduit ID references when:
- You depend on a specific plugin's side effects
- The dependency is on a tool conduit that doesn't claim databases

### Constraints

- `@` references only work with **exact database names**, not glob patterns.
  `@DTG-*` is not valid.
- If no conduit is active for the referenced database, the dependency is
  **silently dropped**. Your conduit still runs; it just has no ordering
  constraint for that edge.
- Circular dependencies are detected and logged as a warning. The remaining
  conduits are appended in arbitrary order.

### How It Works

The execution order is resolved via topological sort (Kahn's algorithm). Among
conduits with no dependency edges between them, `X-WildPalms-SortOrder`
determines the order (lower values run first), with alphabetical conduit ID as
the tiebreaker.

## JSON Metadata Reference

Complete JSON metadata format for a sync conduit:

```json
{
    "KPlugin": {
        "Name": "My Calendar Sync",
        "Description": "Syncs Palm DatebookDB with Akonadi/KOrganizer",
        "Icon": "view-calendar",
        "Authors": [{ "Name": "Your Name" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-WildPalms-ConduitId": "akonadi-calendar",
    "X-WildPalms-ConduitType": "sync",
    "X-WildPalms-PalmCreatorId": "date",
    "X-WildPalms-PalmDatabases": ["DatebookDB"],
    "X-WildPalms-ClaimDescriptions": {
        "DatebookDB": "Syncs to Akonadi/KOrganizer. Full KDE PIM integration."
    },
    "X-WildPalms-RequiresDevice": true,
    "X-WildPalms-RunBefore": [],
    "X-WildPalms-RunAfter": ["webcalendar"],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 2
}
```

### Field Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `KPlugin` | object | yes | Standard KDE plugin metadata (Name, Description, Icon, etc.) |
| `X-WildPalms-ConduitId` | string | yes | Unique identifier. Falls back to KDE plugin ID if absent. |
| `X-WildPalms-ConduitType` | string | yes | `"sync"` or `"tool"` |
| `X-WildPalms-PalmCreatorId` | string | no | Palm OS 4-character creator ID (e.g., `"date"`, `"addr"`, `"memo"`) |
| `X-WildPalms-PalmDatabases` | string[] | sync only | Database names or glob patterns this conduit claims |
| `X-WildPalms-ClaimDescriptions` | object | no | Map of database name to pitch text for the settings UI |
| `X-WildPalms-RequiresDevice` | bool | no | Whether a Palm device connection is needed (default: `true`) |
| `X-WildPalms-RunBefore` | string[] | no | Conduit IDs or `@DatabaseName` refs this must run before |
| `X-WildPalms-RunAfter` | string[] | no | Conduit IDs or `@DatabaseName` refs this must run after |
| `X-WildPalms-DefaultEnabled` | bool | no | Whether enabled by default in new profiles (default: `true`) |
| `X-WildPalms-SortOrder` | int | no | UI and tiebreak ordering; lower runs first (default: `0`) |

## Best Practices for Multi-Database Plugins

**Claim only what you sync.** Don't speculatively claim databases you might
support in the future. Claims drive contention resolution -- false claims block
other plugins from handling those databases.

**Use glob patterns for app-specific database families.** If a Palm application
uses multiple databases with a shared prefix (e.g., ShadowPlan's `ShadP-*` or
Documents To Go's `DTG-*`), use a glob rather than listing each database
individually.

**Provide distinct claim descriptions.** When claiming multiple databases, write
a description for each one explaining why your plugin is the better choice.
Generic descriptions don't help users decide.

**Claims are about contention, not access control.** Your `sync()` method can
open any Palm database it needs via the device link. Claims only determine which
plugin is the designated handler for contention resolution. A web calendar
conduit, for example, doesn't claim `DatebookDB` -- it writes to it indirectly
by running before the calendar conduit.

**Check `activeDatabases` at runtime.** The `SyncContext` tells you which of
your claimed databases are active for this sync run:

```cpp
Sync::SyncResult MyConduit::sync(Sync::SyncContext *context)
{
    // Which of our claims are active?
    const QStringList &active = context->activeDatabases;

    // context->palmDatabase is set to the first active database
    // for backwards compatibility

    for (const QString &dbName : active) {
        // Sync this database...
    }
}
```

**Design for partial activation.** If you claim `DatebookDB` and `AddressDB`
but the user only selects you for `AddressDB`, your conduit must handle that
gracefully. Don't assume all your claims are active.

## Example: Simple Single-Database Plugin

A minimal conduit that syncs `MemoDB` to Markdown files.

### `mymemo-conduit.json`

```json
{
    "KPlugin": {
        "Name": "My Memo Sync",
        "Description": "Syncs Palm MemoDB to plain text files",
        "Icon": "view-pim-notes",
        "Authors": [{ "Name": "Jane Developer" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-WildPalms-ConduitId": "mymemo",
    "X-WildPalms-ConduitType": "sync",
    "X-WildPalms-PalmCreatorId": "memo",
    "X-WildPalms-PalmDatabases": ["MemoDB"],
    "X-WildPalms-ClaimDescriptions": {
        "MemoDB": "Syncs to plain .txt files. Simple and fast."
    },
    "X-WildPalms-RequiresDevice": true,
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 0
}
```

### `mymemoconduit.h`

```cpp
#pragma once

#include "sync/conduit.h"

namespace Sync {

class MyMemoConduit : public SyncConduitBase
{
    Q_OBJECT

public:
    explicit MyMemoConduit(QObject *parent = nullptr);

    // Identity
    QString conduitId() const override { return "mymemo"; }
    QString displayName() const override { return "My Memo Sync"; }
    QStringList palmDatabaseNames() const override { return {"MemoDB"}; }
    QString fileExtension() const override { return ".txt"; }

    // Record conversion
    BackendRecord *palmToBackend(PilotRecord *record, SyncContext *ctx) override;
    PilotRecord *backendToPalm(BackendRecord *record, SyncContext *ctx) override;
    bool recordsEqual(PilotRecord *palm, BackendRecord *backend) const override;
    QString palmRecordDescription(PilotRecord *record) const override;
};

} // namespace Sync
```

### `mymemoconduit.cpp` (bottom)

Register the plugin with the KDE plugin factory, referencing your JSON file:

```cpp
#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(MyMemoConduitFactory, "mymemo-conduit.json",
                           registerPlugin<Sync::MyMemoConduit>();)

#include "mymemoconduit.moc"
```

### `CMakeLists.txt`

```cmake
kcoreaddons_add_plugin(wildpalms_mymemo
    SOURCES
        mymemoconduit.cpp
        mymemoconduit.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_mymemo
    WildPalmsCore
    KF6::CoreAddons
    Qt::Widgets
)
```

The `INSTALL_NAMESPACE "wildpalms/conduits"` is how `ConduitManager` discovers
your plugin at runtime.

## Example: Multi-Database Plugin

A hypothetical Akonadi conduit that syncs both `DatebookDB` and `AddressDB` to
KDE PIM.

### `akonadi-conduit.json`

```json
{
    "KPlugin": {
        "Name": "Akonadi PIM Sync",
        "Description": "Syncs Palm PIM databases with KDE Akonadi",
        "Icon": "akonadi",
        "Authors": [{ "Name": "Jane Developer" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-WildPalms-ConduitId": "akonadi-pim",
    "X-WildPalms-ConduitType": "sync",
    "X-WildPalms-PalmDatabases": ["DatebookDB", "AddressDB"],
    "X-WildPalms-ClaimDescriptions": {
        "DatebookDB": "Syncs to KOrganizer via Akonadi. Two-way sync with KDE calendar.",
        "AddressDB": "Syncs to KAddressBook via Akonadi. Two-way sync with KDE contacts."
    },
    "X-WildPalms-RequiresDevice": true,
    "X-WildPalms-RunAfter": ["webcalendar"],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 5
}
```

### Handling Partial Activation

The user might select this plugin for `AddressDB` but keep the built-in
calendar conduit for `DatebookDB`. Your code must handle this:

```cpp
Sync::SyncResult AkonadiConduit::sync(Sync::SyncContext *context)
{
    SyncResult result;
    result.success = true;

    for (const QString &db : context->activeDatabases) {
        if (db == "DatebookDB") {
            auto r = syncCalendar(context);
            result.merge(r);
        } else if (db == "AddressDB") {
            auto r = syncContacts(context);
            result.merge(r);
        }
    }

    return result;
}
```

If `activeDatabases` is `["AddressDB"]`, the calendar sync is simply skipped.

### Contention Scenario

With this plugin installed alongside the built-in Calendar and Contact conduits,
the claim map looks like:

| Database | Claimants |
|----------|-----------|
| DatebookDB | `calendar`, `akonadi-pim` |
| AddressDB | `contacts`, `akonadi-pim` |
| MemoDB | `memos` |
| ToDoDB | `todos` |

The user sees a chooser for `DatebookDB` and `AddressDB` in the profile
settings. `MemoDB` and `ToDoDB` are auto-assigned since only one plugin claims
each.
