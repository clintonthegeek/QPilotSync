# F.3 — Sync Mappings Graph View Design

**Date:** 2026-05-23
**Status:** Approved
**Depends on:** F.1a ✅, F.1b ✅, F.1c ✅, F.1d ✅

---

## 1. Goals

Replace the raw `MappingEditorDialog` + `MappingPromptDialog` with a visual,
embedded graph that makes the Palm ↔ provider sync topology comprehensible
without requiring knowledge of backend IDs or SyncMapping JSON internals.

The new UI is a fixed bipartite graph embedded as a tab in the profile settings
dialog. Palm DB nodes (left column) connect to provider collection nodes (right
column) via drag-to-connect edges. An inspector panel at the bottom exposes sync
mode, conflict policy, and enabled state for the selected edge.

---

## 2. Scope

### 2.1 In scope

- New "Sync Mappings" `KPageWidgetItem` in `SettingsDialog`
- `SyncMappingGraphView` — `QGraphicsView`-based fixed bipartite graph; no
  Graffodil dependency
- Four Palm DB nodes (left column): DatebookDB, AddressDB, MemoDB, ToDoDB; each
  shows populated category slots as port rows
- Provider nodes (right column): one per `AccountController` provider; each shows
  available collections as port rows
- Drag-to-connect from Palm port anchor to provider collection anchor creates a
  `SyncMapping`
- `MappingInspectorPanel` at bottom — sync mode, conflict policy, enabled toggle
  for selected edge
- Category slot name persistence: `Profile` gains a snapshot store for last-known
  slot names per DB; plugins write back after AppInfo is populated at session
  start; graph reads from snapshot
- Retirement of `MappingEditorDialog`, `MappingRowDialog`, `MappingPromptDialog`
- `onConfigureMappings()` in `KF6MainWindow` redirected to open `SettingsDialog`
  on the Sync Mappings page

### 2.2 Out of scope

- Category slot conflict handling (renames, delete+recreate, Palm-side category
  changes since last sync) — see §7
- Auto-suggest connections based on name matching (F.5 candidate)
- Graffodil integration (revisit if complexity warrants it)
- Any change to `SyncMapping` persistence format (stays as JSON in
  `Profile::syncMappingsJson`)
- Adding/removing provider accounts (stays on Accounts tab — see §3.4 note)

---

## 3. Architecture

### 3.1 Component map

```
SettingsDialog  (KPageDialog)
└── "Sync Mappings" KPageWidgetItem
    └── SyncMappingsPage  (QWidget)
        ├── SyncMappingGraphView  (QGraphicsView + custom QGraphicsScene)
        │   ├── PalmDbNode × 4          (left column, fixed vertical positions)
        │   │   └── PortRow × N         (one per populated slot + Unfiled)
        │   ├── ProviderNode × M        (right column, stacked by provider order)
        │   │   └── PortRow × K         (one per collection)
        │   └── MappingEdge × J         (one per SyncMapping)
        └── MappingInspectorPanel  (QWidget, pinned at bottom)
```

### 3.2 New files

| File | Responsibility |
|---|---|
| `src/app/mapping/syncmappingsgraphview.h/.cpp` | `QGraphicsView` subclass; owns scene, nodes, edges; emits `mappingsChanged(QJsonArray)` |
| `src/app/mapping/palmdbnode.h/.cpp` | `QGraphicsItem` for a Palm DB — title bar + port rows with right-side anchors |
| `src/app/mapping/providernode.h/.cpp` | `QGraphicsItem` for a provider — title bar + collection port rows with left-side anchors |
| `src/app/mapping/mappingedge.h/.cpp` | `QGraphicsItem` for a `SyncMapping` connection — bezier curve, selectable |
| `src/app/mapping/mappinginspectorpanel.h/.cpp` | `QWidget` showing selected edge properties; emits `edgeChanged` |
| `src/app/mapping/syncmappingspage.h/.cpp` | Container combining graph + inspector; added to `SettingsDialog` as a page |

### 3.3 Files retired

- `src/app/mapping/mappingeditordialog.h/.cpp`
- `src/app/mapping/mappingrowdialog.h/.cpp`
- `src/app/accounts/mappingpromptdialog.h/.cpp`

All three are deleted from the source tree and from CMakeLists.txt. The
`WildPalmsAppMapping` static lib is restructured to contain the new files
instead.

### 3.4 Files modified

| File | Change |
|---|---|
| `src/settingsdialog.h/.cpp` | Gains `setAccountController(AC*)` + `setPalmRuntime(PalmRuntime*)` setters; adds an **Accounts** page (`AccountsPage` widget, already exists at `src/app/accounts/accountspage.h`) when `accountController` is non-null — inserted between Sync and Advanced; constructs `SyncMappingsPage` when both are non-null — inserted after Accounts. **Note:** `AccountsPage` is currently wired only in the wizard; adding it here is the prerequisite for the "Accounts tab stays as-is" assumption in §2.2. |
| `src/kf6/kf6mainwindow.cpp` | `onSettings()` passes `m_accountController.get()` + `m_palmRuntime.get()` to `SettingsDialog`; `onConfigureMappings()` opens `SettingsDialog` navigated to Sync Mappings page |
| `src/profile.h/.cpp` | Gains `categorySlotNames(const QString &dbName) const → QStringList` (16-entry, index = slot, empty = unnamed) and `setCategorySlotNames(const QString &dbName, const QStringList &names)` |
| `src/plugins/calendar/calendarbackendplugin.h/.cpp` | Gains `QStringList categorySlotNames() const` accessor that returns a 16-entry snapshot of `m_categoryStore` for `"DatebookDB"`; gains `QString primaryDbName() const` returning `"DatebookDB"` |
| `src/plugins/contacts/contactsbackendplugin.h/.cpp` | Same pattern for `"AddressDB"` |
| `src/plugins/memo/memoplugin.h/.cpp` (the `Kalburator::Plugin` subclass, not `MemoBlobBackend`) | Same pattern for `"MemoDB"` |
| `src/plugins/todos/todobackendplugin.h/.cpp` | Same pattern for `"ToDoDB"` |
| `src/runtime/palmruntime.h/.cpp` | Gains `void setProfile(Profile *profile)` setter. In `finishConnect()`, after each `createPalmBackend()` returns a non-null backend, calls `m_profile->setCategorySlotNames(plugin->primaryDbName(), plugin->categorySlotNames())` if `m_profile` is non-null |
| `src/kf6/kf6mainwindow.cpp` | After `m_palmRuntime` is constructed in `loadProfile()`, calls `m_palmRuntime->setProfile(m_currentProfile.get())` |

---

## 4. Graph design

### 4.1 Layout

Fixed bipartite layout — nodes are not user-repositionable:

- **Left column:** Palm DB nodes, evenly spaced vertically in this order:
  DatebookDB, AddressDB, MemoDB, ToDoDB
- **Right column:** Provider nodes, stacked top-to-bottom in
  `AccountController::providers()` order; repositioned when providers are
  added/removed (requires reload of the page)
- **Edges:** Cubic bezier curves connecting right-side anchors (Palm nodes) to
  left-side anchors (provider nodes); control points offset horizontally to
  prevent overlap near ports
- **Scroll/zoom:** `QGraphicsView` with scrollbars; Ctrl+Wheel zooms; view fits
  content on open

### 4.2 Nodes

**PalmDbNode**

- Title bar: domain icon (theme icon) + human name (e.g., "Calendar — DatebookDB")
- Port rows: one per populated slot from
  `Profile::categorySlotNames(dbName)`, sorted slot-index ascending
  - Slot 0 always shown as "Unfiled"
  - Empty-name slots hidden (never shown)
- When snapshot is absent (empty `QStringList` returned): single disabled
  placeholder row, text "Sync once to discover categories", no anchor dot
- Right-side anchor dot per active port row (8 px circle, hover highlight)

**ProviderNode**

- Title bar: provider-kind icon + provider display name (from
  `AccountController::providers()`)
- Port rows: one per collection from `AccountController::collectionsFor(id)`
- When provider state is `Connecting` or `Error`: single placeholder row with
  status text; ports disabled
- Left-side anchor dot per active port row

**Domain tagging**

Each Palm DB node is tagged with a domain matching `CollectionInfo::type`:

| Node | Domain |
|---|---|
| DatebookDB | `calendar` |
| AddressDB | `contacts` |
| MemoDB | `memos` |
| ToDoDB | `todos` |

Provider nodes don't carry a single node-level domain — each collection port
row inherits its domain directly from
`Kalburator::Sync::CollectionInfo::type` (which is one of `calendar`,
`contacts`, `memos`, `todos`). Domain compatibility is enforced **per port pair**:
a Palm DB port can connect to a provider collection port only when their
domains match. When `CollectionInfo::type` is empty or unrecognised the port
is treated as domain `unknown` and accepts connections from any Palm DB
(fallback for future backend types).

### 4.3 Edges

**MappingEdge**

- Bezier curve from Palm port right-anchor to provider port left-anchor
- States:
  - Default: blue-grey stroke
  - Selected: highlight (system palette `Highlight` colour)
  - Disabled (`SyncMapping::enabled == false`): dashed stroke, 50% opacity
  - Stale (mapped slot no longer in snapshot): orange dashed stroke; tooltip:
    "This Palm category slot is no longer known — rewire or delete this
    mapping"
- Click selects edge and populates `MappingInspectorPanel`

**Edge creation**

1. User drags from a Palm port anchor
2. A temporary rubber-band curve follows the cursor; compatible provider anchors
   light up (same domain or provider domain is `unknown`)
3. Incompatible anchors show a red tint on hover; dropping there cancels the drag
4. Dropping on a compatible anchor:
   - Duplicate guard: if a mapping with the same source and target collection
     already exists, drop is rejected (no new edge created)
   - Otherwise: new `SyncMapping` created with defaults (mode = TwoWay,
     conflict policy = `Profile::conflictResolutionPolicy()`, enabled = true);
             edge added to graph
5. `SyncMappingGraphView` emits `mappingsChanged(QJsonArray)` with the full
   updated mapping list

**Edge deletion**

Select edge → Delete key, or right-click → "Remove Mapping". Emits
`mappingsChanged`.

### 4.4 Inspector panel

`MappingInspectorPanel` occupies a fixed-height strip (≈80 px) at the bottom of
`SyncMappingsPage`. When no edge is selected, the panel shows a placeholder:
"Select a connection to edit its properties."

When an edge is selected:

| Control | Type | Values |
|---|---|---|
| Sync Mode | `QComboBox` | from `Kalburator::Sync::SyncMode`: Disabled, OneWayUpload (Palm → Provider), OneWayDownload (Provider → Palm), TwoWay (Two-Way) — displayed labels in parentheses |
| Conflict Policy | `QComboBox` | from `Kalburator::Sync::ConflictResolution`: SourceWins, TargetWins, Duplicate, Skip, AskUser, LastWriteWins. `CustomMerge` is intentionally omitted (existing `MappingRowDialog` precedent — requires merge-rule configuration not exposed in this UI) |
| Enabled | `QCheckBox` | — |

Changes update the in-memory graph immediately and are reflected in the edge
appearance (e.g., disabling dashes the line). They are not written to disk until
the user clicks Apply/OK on `SettingsDialog` — `SyncMappingsPage` accumulates
the current `QJsonArray` and hands it to the dialog's `onApply`.

### 4.5 Read-only guard

When `PalmRuntime::isRunning()` is true:

- All port anchors and edge interaction are disabled
- A banner at the top of the graph area reads: "A sync is in progress —
  mapping changes are locked"
- `SyncMappingsPage` connects `PalmRuntime::syncStarted` and
  `PalmRuntime::syncFinished` to toggle read-only state

---

## 5. Category slot name persistence

### 5.1 Profile storage

`profile.conf` gains one section per database:

```ini
[categories/DatebookDB]
slot0=Unfiled
slot1=Work
slot2=Personal

[categories/AddressDB]
slot0=Unfiled
slot1=Work
```

- `Profile::categorySlotNames(const QString &dbName) const` — returns
  `QStringList` of 16 entries (index = slot number 0–15). Slot 0 is forced to
  `"Unfiled"` if the stored value is empty. Empty string at any other index
  means unnamed/absent. Returns all-empty list if no snapshot exists yet.
- `Profile::setCategorySlotNames(const QString &dbName, const QStringList &names)`
  — `names` must have exactly 16 entries. Writes to `profile.conf` and calls
  `save()`.

### 5.2 Write-back via PalmRuntime

Plugins (the `Kalburator::Plugin` subclasses) do not carry a `Profile*`
reference. The write-back lives in `PalmRuntime::finishConnect()` instead:
after each `createPalmBackend()` returns successfully, PalmRuntime queries
the plugin's new `categorySlotNames()` accessor and calls
`m_profile->setCategorySlotNames(plugin->primaryDbName(), names)` if
`m_profile` is non-null.

PalmRuntime gains:

- `void setProfile(Profile *profile)` — called by `KF6MainWindow::loadProfile()`
  immediately after PalmRuntime is constructed
- Private member `Profile *m_profile = nullptr` — borrowed, must outlive
  PalmRuntime

Each of the four Palm DB plugins gains two accessors:

| Plugin | `primaryDbName()` | `categorySlotNames()` returns |
|---|---|---|
| `CalendarBackendPlugin` | `"DatebookDB"` | 16-entry list from `m_categoryStore` |
| `ContactsBackendPlugin` | `"AddressDB"` | same pattern |
| `MemoPlugin` (the `Kalburator::Plugin` subclass) | `"MemoDB"` | same pattern |
| `TodoBackendPlugin` | `"ToDoDB"` | same pattern |

The 16-entry list is built by querying `CategoryMappingStore::slotName(dbName, slot)`
for slots 0..15; slot 0 is forced to `"Unfiled"` if empty.

### 5.3 Graph read

`SyncMappingsPage` reads `Profile::categorySlotNames(dbName)` when constructing
each `PalmDbNode`. It does this once on page open; the graph is not live-updated
during a sync (the read-only guard is sufficient for F.3).

---

## 6. Testing

### 6.1 New tests

| Test file | Cases |
|---|---|
| `tests/runtime/tst_syncmappingsgraphview.cpp` | construct with empty profile (placeholder rows); add provider node → port rows appear; drag-connect creates edge + emits mappingsChanged; duplicate drag rejected; domain-mismatch drag rejected; delete edge emits mappingsChanged; read-only guard disables interaction |
| `tests/runtime/tst_profile_category_snapshot.cpp` | setCategorySlotNames round-trips through QTemporaryDir; slot 0 always "Unfiled"; 16-entry constraint enforced; missing section returns all-empty list |

### 6.2 Existing test baseline

95 tests must continue to pass (no regressions). The retirement of
`MappingEditorDialog`, `MappingRowDialog`, and `MappingPromptDialog` removes
their test coverage; any tests referencing them are deleted alongside the
source files.

### 6.3 Manual smoke

1. Load a profile that has never synced → Sync Mappings tab → DatebookDB node
   shows "Sync once to discover categories"
2. After a sync → reopen Sync Mappings → category slot names appear
3. Drag DatebookDB "Work" slot → Fastmail "Work Cal" → edge appears, profile
   JSON updated on Apply
4. Select edge → change to "Palm → Provider" mode → edge persists after reopen
5. Delete edge → mapping removed from profile JSON

---

## 7. Future work — category lifecycle conflict handling

**Not in scope for F.3.**

The slot-keyed binding model is correct for the common case (stable category
assignments) but is fragile when the Palm's AppInfo changes between syncs. The
full category lifecycle problem requires a dedicated design:

### 7.1 Identified scenarios

| Scenario | Current behaviour | Desired behaviour |
|---|---|---|
| Category renamed on Palm | Slot name changes in snapshot after next sync; SyncMapping slot number unchanged (correct) | Existing behaviour is acceptable |
| Category deleted + recreated on Palm (new slot) | Old SyncMapping references now-vacant slot; graph shows stale orange edge | Detect mismatch, prompt user to rewire |
| Category renamed from WildPalms UI | Not yet supported (no AppInfo write path) | Write new name into Palm AppInfo block at sync time |
| Both sides rename independently (conflict) | Not detected | Surface as a category-level conflict, analogous to record conflicts |

### 7.2 Required subsystems

1. **AppInfo write path** — `PalmBackend::writeAppBlock(dbName, bytes)` +
   serialisation of modified `CategoryAppInfo_t` back to the Palm database
2. **Category conflict model** — detect when Palm's post-sync AppInfo differs
   from the persisted snapshot; a `CategoryDiff` struct + presenter
3. **Conflict handler** — possibly `ICategoryConflictPresenter` parallel to
   `CalendarConflictHandler`; whether this belongs in libkalburator or in a
   WildPalms-specific layer is an open question for evaluation
4. **F.3 mitigation** — stale edge visual (§4.3) surfaces the problem to the
   user without automatic cleanup; no silent data loss

### 7.3 Recommended next step

Brainstorm F.5 as a dedicated "Category lifecycle + AppInfo write path" design
phase before implementation.

---

## 8. Success criteria

- SettingsDialog Sync Mappings tab shows the bipartite graph when a profile with
  accounts is loaded
- User can drag from a Palm DB port to a provider collection port and create a
  `SyncMapping`
- User can select an edge and change sync mode / conflict policy / enabled; the
  edge appearance updates immediately
- User can delete an edge
- Changes are written to `Profile::syncMappingsJson` on Apply/OK and
  `PalmRuntime::reloadMappings` is called
- `MappingEditorDialog`, `MappingRowDialog`, `MappingPromptDialog` are removed
  from the source tree and CMakeLists
- Category slot names persist in `profile.conf` and are displayed on the next
  settings open after a sync
- All 95 baseline tests pass; `tst_syncmappingsgraphview` and
  `tst_profile_category_snapshot` green
