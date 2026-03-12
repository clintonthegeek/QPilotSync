# ShadowStan Conduit Adaptation Design

Advisory specification for adapting [ShadowStan](../../) — a standalone Qt6 ShadowPlan desktop editor — into a WildPalms sync conduit plugin. This is the first test of adapting an existing application into a complex, multi-database conduit.

## Context

ShadowStan is a Qt6 application that reads and writes Palm OS ShadowPlan databases. It handles five database types:

| Database | Purpose | Record Structure |
|---|---|---|
| `ShadP-*` | Task/checklist lists (one per list) | Hierarchical tree nodes, level-encoded |
| `ShadTags` | Tag definitions | Tag name + Palm OS category assignment |
| `ShadViews` | Named view configurations | Column bitmask + filter reference |
| `ShadFilters` | Custom filter definitions | Filter rules with AND/OR grouping |
| `ShadCat` | List-to-category mappings | Filename → category map |

ShadowStan's architecture is already modular:

- `libs/pdb` — Palm Database format I/O (PdbReader, PdbWriter, PdbDatabase)
- `libs/shadow` — ShadowPlan data model and codecs (ShadowNode, ShadowList, ShadowCodec, TagStore, FilterStore, WorkspaceIO, plus JSON serialization)
- `src/` — GUI application (MainWindow, ShadowTreeWidget, PropertiesPanel, filter/view/tag dialogs, undo/redo)

No dependencies beyond Qt6::Core and Qt6::Widgets.

## Integration Model

**Approach chosen:** Plugin lives in ShadowStan's repository. A CMake option (`-DBUILD_WILDPALMS_PLUGIN=ON`) builds the conduit plugin `.so` alongside the standalone app. The conduit code, mapper, and JSON metadata all live in ShadowStan's repo.

**Key design decisions:**

1. **The conduit IS ShadowStan.** When the user opens the ShadowPlan conduit view in WildPalms, they get the full tree editor. Not a simplified browser — the real editor.
2. **PC-side data lives as `.pdb` files** in WildPalms's sync folder, managed by a custom sync backend. ShadowStan already speaks PDB natively.
3. **All five database types are claimed by one conduit** with full bidirectional sync. Companion databases (tags, views, filters, categories) are semantically coupled to the task lists.
4. **Sync status awareness is a WildPalms platform concern**, not a ShadowStan concern. WildPalms will provide infrastructure that all conduits benefit from (tracking unsynced objects, modified-since-sync indicators, etc.). This will be designed and implemented separately.

## Section 1: Component Extraction Architecture

The central refactoring is extracting a reusable `ShadowEditorWidget` from `MainWindow`. Currently MainWindow mixes three concerns: application shell (menus, recent files, window title, QSettings), list editor (tree widget, properties panel, filter/view/tag management, undo stack, toolbars), and data lifecycle (WorkspaceIO loading/saving, dirty tracking).

### Prescribed directory structure

```
libs/pdb/              (unchanged — Palm DB format I/O)
libs/shadow/           (unchanged — data model & codecs)
libs/editor/     [NEW] — ShadowEditorWidget and all GUI components
    ShadowEditorWidget     — QWidget composing tree + properties + toolbars
    ShadowTreeWidget       — moved from src/
    ShadowTreeModel        — moved from src/
    ShadowSortFilterModel  — moved from src/
    PropertiesPanel        — moved from src/
    Commands               — moved from src/ (undo/redo)
    FilterManagerDialog    — moved from src/
    ViewManagerDialog      — moved from src/ (includes ViewDetailDialog, ColumnPickerDialog)
    TagManagerDialog       — moved from src/
    ListPropertiesDialog   — moved from src/
    ColumnsDialog          — moved from src/
    colorpalette.h         — moved from src/ (inline color lookup functions)
    ViewConfig             — moved from src/
src/                   — standalone app (MainWindow wraps ShadowEditorWidget in MDI)
src/wildpalms-conduit/ [NEW] — WildPalms conduit plugin
```

### ShadowEditorWidget API surface

```cpp
class ShadowEditorWidget : public QWidget {
    Q_OBJECT
public:
    // Load a pre-parsed list (caller owns the ShadowList lifetime)
    void loadList(ShadowList *list);
    void unloadList();

    // Data access
    ShadowList *currentList() const;
    bool isDirty() const;

    // Companion data injection (set before loadList)
    void setTagStore(TagStore *store);
    void setFilterStore(FilterStore *store);

    // Companion data access
    TagStore *tagStore() const;
    FilterStore *filterStore() const;

signals:
    void dirtyChanged(bool dirty);
    void listModified();
};
```

The editor widget receives an already-parsed `ShadowList*` rather than file paths. This keeps I/O responsibility with the shell (standalone MainWindow uses `WorkspaceIO::load()`, conduit view uses the sync backend). The editor is purely a view/edit component.

### How the two shells use it

**Standalone MDI app:** `MainWindow` becomes a thin shell that holds N `ShadowEditorWidget` instances in a tab bar or other MDI arrangement. MainWindow manages file open/close, recent files, QSettings, and the overall application lifecycle. Each tab is one list.

**WildPalms conduit view:** `createView()` returns a wrapper widget containing a list selector bar and one `ShadowEditorWidget` instance. The selector is populated by scanning the sync folder for `ShadP-*.pdb` files. Switching the selector swaps which list the editor displays.

The `ShadowEditorWidget` knows nothing about files, sync, or multi-list navigation. It receives a loaded list and emits signals when things change.

## Section 2: The Conduit Class — Multi-Database Sync

ShadowPlan is unlike existing WildPalms conduits. It handles five database types with different record structures. Existing conduits each handle one flat-record database.

### One conduit, not five

The companion databases are semantically dependent on the ShadP-* lists. Tag IDs referenced by nodes must be consistent with the tag definitions. Syncing them independently is meaningless. The user sees "ShadowPlan" as one thing.

### How multi-database sync works (implemented)

WildPalms now supports multi-database conduits natively. The sync engine drives per-database iteration — the conduit does not need to loop over databases itself. Here's how it works for ShadowPlan:

1. The conduit declares its databases in the JSON metadata array: `["ShadTags", "ShadViews", "ShadFilters", "ShadCat", "ShadP-*"]`
2. **Array order is sync order.** Companion databases are listed first to ensure referential integrity — tags and views exist before the lists that reference them.
3. At sync time, the engine expands `ShadP-*` against the Palm's actual database list (e.g., `ShadP-Personal`, `ShadP-Work`).
4. The engine calls `sync(context)` once per resolved database, with:
   - `context->palmDatabase` — the specific database name (e.g., `"ShadTags"`, `"ShadP-Personal"`)
   - `context->state` — per-database SyncState (ID mappings, baselines), keyed by `shadowplan/ShadTags`, `shadowplan/ShadP-Personal`, etc.
   - `context->collectionId` — `"shadowplan/ShadTags"`, `"shadowplan/ShadP-Personal"`, etc.
   - `context->activeDatabases` — the full list of resolved databases for this sync run

### Record conversion dispatch

The record conversion methods receive a `const SyncContext*` parameter. ShadowPlan uses `context->palmDatabase` to dispatch to the correct codec:

```cpp
bool ShadowPlanConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend,
                                      const SyncContext *context) const
{
    if (context->palmDatabase == "ShadTags") {
        return compareTagRecords(palm, backend);
    } else if (context->palmDatabase == "ShadViews") {
        return compareViewRecords(palm, backend);
    } else if (context->palmDatabase == "ShadFilters") {
        return compareFilterRecords(palm, backend);
    } else if (context->palmDatabase == "ShadCat") {
        return compareCategoryRecords(palm, backend);
    } else {
        // ShadP-* list databases
        return compareNodeRecords(palm, backend);
    }
}
```

The same dispatch pattern applies to `palmRecordDescription()`, `palmToBackend()`, and `backendToPalm()`.

### Error isolation

If syncing one database fails (e.g., `ShadTags` corrupted), the engine logs the error and continues with the remaining databases. A failure syncing metadata shouldn't prevent task lists from syncing.

### What remains to be determined

- Whether `SyncConduitBase`'s default sync algorithms work well for all five database types, or whether ShadowPlan overrides `sync()` entirely for some databases (e.g., companion databases with simple structure vs. hierarchical task lists)
- How the custom PDB-based backend integrates — the per-database state isolation and collection IDs are now in place, but the backend implementation itself is still needed (see Section 3)

### Preparation

ShadowStan developers should ensure codecs in `libs/shadow` have clean per-record encode/decode entry points. The existing JSON serialization layer covers this — records can be individually serialized and compared.

## Section 3: Custom Sync Backend — PDB Storage

Existing conduits use `LocalFileBackend` (one file per record). ShadowPlan's natural storage is `.pdb` container files — one file holds many records.

### Direction

A custom `SyncBackend` implementation (e.g., `PdbSyncBackend`) that:

- Stores PC-side data as `.pdb` files in the sync folder
- Exposes individual records within containers to WildPalms's sync engine for diffing/pairing
- Uses `PdbReader`/`PdbWriter` from `libs/pdb` for I/O
- Maps WildPalms record IDs to Palm record unique IDs (already embedded in `.pdb` records)

### Why this works

`libs/pdb` preserves raw record data faithfully through round-trips, and `libs/shadow`'s codec layer preserves unknown per-node bytes (`trailingEntries`, `tailPadding`). Together, the backend can read a `.pdb`, let the sync engine modify individual records, and write it back without corruption. `PdbDatabase` already exposes records by index and unique ID, mapping naturally to backend record enumeration.

### Per-database state and collection IDs

The sync engine now provides per-database `SyncState` and `collectionId` automatically. For ShadowPlan this means:

- `ShadTags` gets its own SyncState keyed as `shadowplan/ShadTags`, with `collectionId = "shadowplan/ShadTags"`
- Each `ShadP-*` list gets its own SyncState (e.g., `shadowplan/ShadP-Personal`), with its own ID mappings and baselines
- The backend can use `context->collectionId` to scope its storage — one `.pdb` file per collection ID maps naturally

### Uncertain

Whether the custom backend lives in `libs/shadow` (portable, no WildPalms dependency) as an adapter class, or in `src/wildpalms-conduit/` as conduit-specific code. Depends on the `SyncBackend` interface shape. ShadowStan developers should be aware they'll need to implement one but shouldn't build it until the WildPalms SDK is available (see Dependencies).

## Section 4: The Conduit View — List Selection and Editor

When WildPalms calls `createView()`, the conduit returns a composite widget:

```
┌─────────────────────────────────────────────────┐
│ [ShadP-Personal ▼]  [ShadP-Work]  [ShadP-Shop] │  ← list selector
├─────────────────────────────────────────────────┤
│                                                 │
│              ShadowEditorWidget                 │
│         (tree + properties + toolbars)          │
│                                                 │
└─────────────────────────────────────────────────┘
```

### Responsibilities

**The conduit wrapper** (lives in `src/wildpalms-conduit/`) owns:

- List selector bar (tabs, combo, or other UX — developer's choice)
- Scanning the sync folder for `ShadP-*.pdb` files
- Loading companion databases from the sync folder
- Injecting companion data into the editor widget
- Swapping the editor's loaded list when the selector changes

**WildPalms** owns:

- Providing the sync folder path via `SyncContext::syncFolderPath`
- Sync status indicators (synced, local-only, modified-since-sync) — to be designed as platform infrastructure in the next project
- The tab/page in WildPalms's main UI that contains this view

**ShadowEditorWidget** owns:

- Editing a single loaded list
- Filter, view, and tag management within that list
- Undo/redo for that list
- Emitting change signals

### Workspace path

The conduit receives the sync folder path from WildPalms. The view wrapper scans it directly — no intermediary list from WildPalms needed. This keeps the component self-contained: the same directory-scanning logic works in the standalone MDI shell too.

## Section 5: Conflict Display

When WildPalms detects a conflict (same record modified on both Palm and PC), it calls `enrichConflictSnapshot()` and `formatConflictRecordHtml()` to show both sides.

### ShadowPlan node conflicts

A ShadowPlan node carries: title, memo, priority, progress, dates (created, target, start, finish), color, bold flag, auto-number, tag IDs, checked state, and links. The conflict HTML should focus on what humans care about:

```html
<b>☐ Buy groceries</b>
Priority: 3 · Progress: 40%
Target: 3/15 · Tags: Shopping, Errands
Memo: Don't forget the milk
```

Fields that differ between sides should be highlighted. Display resolved names (tag names, not tag IDs; category names, not indices) — this requires the codec layer to provide lookup utilities.

### Companion database conflicts

Tags, views, filters, and categories are structural data. Conflicts should be described plainly — "Tag 'Work' category assignment changed" rather than raw binary diffs.

### Preparation

ShadowStan developers should add summary renderer utilities in `libs/shadow`:

- Human-readable summary of a `ShadowNode` (HTML or plain text)
- Summary renderers for tag, view, filter, and category records

These are useful beyond conflict display — tooltips, search result previews, status bar information in the standalone app.

## Section 6: Build System

### CMake configuration

ShadowStan's root `CMakeLists.txt` adds one option:

```cmake
option(BUILD_WILDPALMS_PLUGIN "Build WildPalms conduit plugin" OFF)

add_subdirectory(libs/pdb)
add_subdirectory(libs/shadow)
add_subdirectory(libs/editor)
add_subdirectory(src)

if(BUILD_WILDPALMS_PLUGIN)
    find_package(KF6CoreAddons REQUIRED)
    find_package(WildPalms REQUIRED)
    add_subdirectory(src/wildpalms-conduit)
endif()
```

The standalone app always builds. The plugin is opt-in.

### Plugin build target

```cmake
kcoreaddons_add_plugin(wildpalms_shadowplan
    SOURCES
        shadowplanconduit.cpp
        shadowplanconduit.h
        shadowplanview.cpp
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_shadowplan
    ShadowStan::Editor
    ShadowStan::Shadow
    ShadowStan::Pdb
    WildPalmsCore
    KF6::CoreAddons
    Qt::Widgets
)
```

### libs/editor build target

New library following the existing pattern:

```cmake
add_library(shadowstan-editor STATIC
    ShadowEditorWidget.h
    ShadowEditorWidget.cpp
    # ... all GUI components moved from src/
)

set_target_properties(shadowstan-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(ShadowStan::Editor ALIAS shadowstan-editor)

target_link_libraries(shadowstan-editor
    PUBLIC
        Qt6::Widgets
        ShadowStan::Shadow
        ShadowStan::Pdb
)
```

### WildPalms prerequisite

WildPalms must provide an installable CMake config (`WildPalmsConfig.cmake`) that exports the `WildPalmsCore` target with its public headers. This doesn't exist yet — it will be needed for any 3rd-party plugin development.

## Section 7: JSON Plugin Metadata

**Array order is sync order.** The engine iterates `X-WildPalms-PalmDatabases` left-to-right, expanding globs against the device's database list. Companion databases are listed before `ShadP-*` to ensure referential integrity — tag and filter definitions exist before the task lists that reference them.

```json
{
    "KPlugin": {
        "Name": "ShadowPlan Sync",
        "Description": "Syncs ShadowPlan task lists, tags, views, filters, and categories",
        "Icon": "view-list-tree",
        "Authors": [{ "Name": "ShadowStan developers" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Sync"
    },
    "X-WildPalms-ConduitId": "shadowplan",
    "X-WildPalms-ConduitType": "sync",
    "X-WildPalms-PalmCreatorId": "Shad",
    "X-WildPalms-PalmDatabases": ["ShadTags", "ShadViews", "ShadFilters", "ShadCat", "ShadP-*"],
    "X-WildPalms-ClaimDescriptions": {
        "ShadP-*": "Full ShadowPlan tree editor with bidirectional sync",
        "ShadTags": "Tag definitions and category assignments",
        "ShadViews": "Named view configurations",
        "ShadFilters": "Custom filter definitions",
        "ShadCat": "List-to-category mappings"
    },
    "X-WildPalms-RequiresDevice": true,
    "X-WildPalms-RunBefore": [],
    "X-WildPalms-RunAfter": [],
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 10
}
```

## Section 8: Conduit Settings

Settings stored per-profile in WildPalms, exposed via `hasSettings()`/`createSettingsWidget()`:

**Include:**

- **Default list type for new lists** — Checklist, Tasklist, Note, etc. Useful standalone too.
- **Conflict policy for companion databases** — structural data (tags, views, filters) may warrant a default of "Palm wins" or "PC wins" rather than prompting for each.

**Exclude:**

- View/filter/tag preferences — these are per-list data in the `.pdb` files, not conduit config
- Icon theme — GUI preference, belongs in the editor or standalone app settings
- Auto-save behavior — irrelevant in conduit mode; WildPalms owns the save lifecycle

## Section 9: Standalone Improvements That Benefit Both Modes

Recommendations that reduce integration friction while improving the standalone app:

### Per-list undo stacks

Currently `MainWindow` owns one `QUndoStack`. When the editor becomes multi-list (standalone MDI or conduit view), each list needs its own stack. Move the undo stack into `ShadowEditorWidget` — each instance owns its undo history.

### Injectable companion data

Currently `AppDatabases` loads tags/views/filters/categories from `QStandardPaths::AppDataLocation` (resolves to `~/.local/share/ShadowStan/` on Linux). In conduit mode, these come from `.pdb` files in the sync folder. In standalone MDI mode, they come from the workspace directory. `ShadowEditorWidget` receives companion data through setters or constructor injection — it doesn't care where the data came from.

### Clean load/unload cycle

`ShadowEditorWidget::loadList()` must fully reset state — no leftover selection, filter, scroll position, or proxy model state from a previous list. Matters for both conduit view (switching lists) and standalone MDI (closing and opening tabs).

### Summary renderers in libs/shadow

Human-readable summaries of nodes, tags, views, and filters. Useful for conflict display (Section 5), and also standalone for tooltips, search result previews, or an enhanced status bar.

### Decouple WorkspaceIO utilities

Currently `WorkspaceIO::load()` opens a `ShadP-*.pdb` and auto-discovers companions in the same directory. The logic of "given a directory, find all ShadP-* files" and "given a ShadP-* file, find its companions" should be cleanly callable as separate utility functions. Both the conduit and the standalone MDI shell need them independently of a full load cycle.

## Dependencies on Future WildPalms Work

This design assumes three WildPalms improvements, designed and implemented separately:

1. **Sync status infrastructure** — Per-object tracking of sync state (synced, local-only, modified-since-sync) with UI indicators or a dashboard. All conduits benefit, not just ShadowPlan. *Status: not yet started.*

2. ~~**Multi-database conduit support**~~ — **Done.** The sync engine now supports per-database iteration, glob expansion, `const SyncContext*` on record conversion methods, per-database SyncState isolation, and per-database collection IDs. See Section 2 for details.

3. **Installable WildPalms SDK** — A `WildPalmsConfig.cmake` that exports `WildPalmsCore` with public headers, enabling 3rd-party plugin compilation. *Status: not yet started.*

## Summary of Prescribed Changes for ShadowStan

| Change | Effort | Benefits Both Modes |
|---|---|---|
| Extract `ShadowEditorWidget` into `libs/editor/` | High | Yes |
| Move GUI components from `src/` to `libs/editor/` | Medium | Yes |
| Per-list undo stacks | Medium | Yes |
| Injectable companion data | Medium | Yes |
| Clean load/unload cycle | Low | Yes |
| Summary renderers in `libs/shadow` | Low | Yes |
| Decouple WorkspaceIO utility functions | Low | Yes |
| Standalone MDI mode | Medium | Standalone only |
| Add `BUILD_WILDPALMS_PLUGIN` CMake option | Low | Plugin only |
| Create `src/wildpalms-conduit/` with conduit class | Medium | Plugin only |
| Implement conduit view wrapper (list selector + editor) | Medium | Plugin only |
