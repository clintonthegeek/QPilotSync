# Dashboard Status Panel Redesign — Design Spec

**Date:** 2026-05-26
**Branch:** feature/canon-adoption-phase1 (or a dedicated `feature/dashboard-redesign`)
**Status:** Design — awaiting implementation plan

## Problem

The top status strip (`DashboardWidget`, `src/widgets/dashboard/`) is half-wired and
not useful enough to replace reading the log:

- **It never reflects disconnection.** `onDeviceDisconnected()`
  (`kf6mainwindow.cpp:882`) updates only the `QStatusBar` and fires a notification;
  it never calls `m_dashboardWidget->updateStatus(...)`. Once connected, the panel
  shows "Connected" (green) forever — even after the device is unplugged. This is the
  primary reported bug.
- **It is a passive display updated at exactly two moments** — profile load
  (`kf6mainwindow.cpp:649`) and successful connection (`:1037`). Intermediate states
  (detecting, handshaking, syncing, per-conduit progress, disconnecting) only appear
  in the log/statusbar.
- **Live signals already emitted are thrown away.** `PalmRuntime::runProgress`
  (`palmruntime.h:169`) is declared but never emitted; libkalburator's `SyncEngine`
  emits `syncStarted`, `progressUpdated`, `phaseChanged`, `fetchProgress`,
  `writeProgress` during a run and `PalmRuntime` connects to none of them.
- **Three public widget methods are dead** — `setSyncing()`, `setListening()`
  (except once at init), `setLastSyncSummary()`. The orphaned slot
  `KF6MainWindow::onDeviceStatusChanged()` (`:1239`) is connected to nothing.

**Goal:** turn the panel into a live narrator of device + sync state, so the log
becomes an audit trail the user *can* open but never *has* to.

## Engine reporting constraint (drives the chip behavior)

`PalmRuntime::runAllMappings()` (`palmruntime.cpp:583`) hands all enabled mapping ids
to `SyncEngine::runSyncFuture(ids)` at once and receives `QList<SyncResult>` only when
the whole run finishes. During the run the engine emits per-mapping signals
(`syncStarted(mappingId)`, `progressUpdated(idx,total,msg)`, `phaseChanged`,
`fetchProgress`, `writeProgress`) but has **no per-mapping "finished" signal**.

Consequence for per-conduit chips:
- **Live:** which conduit is active, within-conduit fetch/write progress, phase.
- **Per-conduit final counts (`+a ~m −d`):** only available from the
  `QList<SyncResult>` at run end. A chip therefore goes
  `pending → ⟳ active (with progress) → ✓ done`, with the numeric counts populating
  when the run completes. A conduit is marked `done` when the *next* conduit's
  `syncStarted` fires (the last one at `runFinished`).

Making counts land per-conduit *live* would require a new per-mapping completion
signal in libkalburator. Per project workflow (WildPalms is the consumer; do not edit
libkalburator from WP), that is a **handoff doc + phase 2**, not part of this work.

## Architecture

Separate the panel's *state* from its *rendering*.

### `SyncStatusModel` (new — `src/widgets/dashboard/syncstatusmodel.{h,cpp}`)
A `QObject` with **no UI**, holding the panel's entire state and exposing it for
rendering. Fully unit-testable by feeding it signals and asserting state.

State it owns:
- `LinkState` (enum): `Listening`, `Detected`, `Handshaking`, `Connected`,
  `Syncing`, `Disconnected`.
- Device identity (from `Profile::deviceFingerprint()`): display name, OS version,
  RAM free.
- Profile identity: name, `lastSyncTime`, auto-sync plan
  (`autoSyncOnConnect` + `defaultSyncType`).
- Per-conduit list: ordered entries `{ mappingId, label, iconName, ChipState,
  current, total, created, modified, deleted }` where `ChipState ∈ {Pending, Active,
  Done, Error, Interrupted}`.
- Current overall progress (`current/total`, message) + active conduit id.
- Conflict count.
- Last-run digest: `{ modeLabel, totalChanges, conflictCount, durationSecs, success }`.

Public slots (fed by sources):
- `onDeviceDetected()`, `onDeviceLost()`
- `onConnectionStarted()`, `onConnectionComplete(bool, QString)`,
  `onDeviceDisconnected()`
- `onRunStarted(QString modeLabel)`, `onRunProgress(int,int,QString)`,
  `onRunFinished(PalmRunResult)`
- `onMappingSyncStarted(QString id, QString label, QString icon)`,
  `onMappingSyncProgress(QString id, int phase, int cur, int total)`
- `onConflictCountChanged(int)`
- `setProfile(Profile*)` / `setMappings(QList<SyncMapping>)` to seed the conduit row.

Signals: granular change notifications the widget renders from
(`linkStateChanged`, `conduitsChanged`, `progressChanged`, `digestChanged`,
`conflictsChanged`) plus user-action signals the panel raises
(`syncRequested`, `cancelRequested`, `resolveConflictsRequested`).

### `DashboardWidget` (rewritten — pure view)
Renders from `SyncStatusModel`; owns no logic and no `bool m_connected`. Connects the
model's change signals to render methods, and forwards button clicks to the model's
action signals. Two-tier layout (below). The dead methods (`setSyncing`,
`setListening`, `setLastSyncSummary`) are removed.

### `KF6MainWindow` (wiring only)
Owns one `SyncStatusModel`. On profile load, wires sources into it (same per-profile
rewiring pattern already used for `PalmRuntime`):
- `PalmDeviceMonitor::palmDetected/palmDisconnected` → `onDeviceDetected/onDeviceLost`
- `PalmRuntime::connectionStarted/connectionComplete/deviceDisconnected`
- `PalmRuntime::runStarted/runProgress/runFinished`
- `PalmRuntime::mappingSyncStarted/mappingSyncProgress` (new — see plumbing)
- conflict count (`onConflictDetected` increments; badge refresh already exists)
- model action signals → existing handlers (`startSync` for the active profile's
  `defaultSyncType`, engine cancel, `onConflictBadgeClicked`).
The orphaned `onDeviceStatusChanged()` slot is deleted.

## PalmRuntime plumbing (per-conduit forwarding)

In the engine-setup path (where `m_engine` is constructed / `setSyncMappings` is
called), connect the engine's existing signals and re-emit them tagged with mapping
identity. New `PalmRuntime` signals:

```cpp
void mappingSyncStarted(const QString &mappingId, const QString &label,
                        const QString &iconName);
void mappingSyncProgress(const QString &mappingId, int phase,
                         int current, int total);
```

- `SyncEngine::syncStarted(mappingId)` → resolve label/icon via
  `resolveMappingLabel(mapping)` (maps `mapping.sourceBackend` → plugin in
  `m_palmPlugins` → `mainViewName()` / `mainViewIcon()`), emit `mappingSyncStarted`.
- `SyncEngine::fetchProgress/writeProgress/phaseChanged/progressUpdated` → emit
  `mappingSyncProgress` with a phase code and current/total.
- Wire `runProgress` (overall idx/total) from `SyncEngine::progressUpdated`.
- Per-conduit final counts: in `runAllMappings().then(...)`, the existing
  `QList<SyncResult>` is walked; emit a per-mapping done/counts update (model fills
  the chips). (No engine change required.)

All engine signals are emitted on the worker thread; connections use the default
(queued) delivery into `PalmRuntime` on the main thread — safe.

## Connection state machine

```
Listening ──palmDetected──▶ Detected ──connectionStarted──▶ Handshaking
   ▲                                                            │
   │                                          connectionComplete(true)
   │                                                            ▼
Disconnected ◀─palmDisconnected / deviceDisconnected──── Connected (idle)
   ▲                                                            │
   └──────────palmDisconnected (mid-sync)──── Syncing ◀──runStarted
```

- **The core fix:** both `palmDisconnected` (udev, instant on unplug) and
  `deviceDisconnected` (link teardown) drive the model to `Disconnected` immediately.
- `connectionComplete(false, err)` → back to `Listening`, headline shows the error.
- `palmDisconnected` while `Syncing` → `Disconnected`, the active conduit chip →
  `Interrupted`, headline "Disconnected — sync interrupted."

## Layout — two-tier strip (~140px, always shown)

**Idle / connected:**
```
┌──────────────────────────────────────────────────────────────────────────┐
│ [📱] Palm m515 (Clinton)     │ My Palm Sync            │  Ready to sync      │
│  ● Connected   (green)       │ Synced 3 min ago        │   [ Sync Now ]      │
│  Palm OS 5.2 · 8 MB free     │ Auto-sync on connect    │                     │
├──────────────────────────────────────────────────────────────────────────┤
│ Calendar ·   Contacts ·   Memos ·   ToDo ·   Plucker ·   WebCal ·          │
└──────────────────────────────────────────────────────────────────────────┘
```

**During sync** (top-right morphs to progress bar; chips light up):
```
│ ...                          │ ...                     │ ⟳ Syncing Contacts  │
│                              │                         │  [████████··] 12/45 │
│                              │                         │   [ Cancel ]        │
├──────────────────────────────────────────────────────────────────────────┤
│ Calendar ✓+3~2  Contacts ⟳12/45  Memos ·  ToDo ·  Plucker ·  WebCal ✓      │
└──────────────────────────────────────────────────────────────────────────┘
```

**After sync** (digest + persisted chips + conflict echo):
```
│                              │ Synced just now         │ ✓ HotSync complete  │
│                              │                         │  14 changes · 8s    │
│                              │                         │   [ Sync Now ]   ⚠2 │
```

- **Top row:** left = device icon + name + state dot + ext info; center = profile +
  relative last-sync + auto-sync plan; right = morphing "now" zone (headline /
  progress bar) + primary button + conflict echo.
- **Bottom row:** ordered conduit chips, always present (dim dots idle, animated
  during sync, ✓/✗ with counts after).

## Per-conduit chip states
`Pending` (dim `·`) → `Active` (⟳ spinner + within-conduit `cur/total`) →
`Done` (✓ + `+a ~m −d`, counts at run end) → `Error` (✗ red) / `Interrupted`
(⚠ on mid-sync disconnect). Disabled mappings are hidden.

## Interactive control surface
Primary button morphs with `LinkState`:
- `Disconnected` → hidden; headline hint "Press HotSync on your Palm."
- `Connected` (idle) → **Sync Now** → runs profile's `defaultSyncType`.
- `Syncing` → **Cancel** → engine cancellation (the cancel watcher already exists in
  `runAllMappings`).
- conflicts pending → **Resolve N** → reuses `onConflictBadgeClicked`.

## Conflicts
Clickable conflict echo (`⚠ N`) in the panel, wired to the existing
`onConflictBadgeClicked`. The statusbar badge is **kept** as-is (no removal).

## Polish
- Spinner on active chip/headline (`QTimer`-rotated themed icon).
- Relative last-sync time ("3 min ago"), refreshed by a 60s `QTimer`.
- Gentle pulse on the device icon while `Listening`.

## Error handling
- Connection failure → `Listening` + error headline; button hidden.
- Mid-sync disconnect → `Disconnected`, active chip `Interrupted`, digest marks failure.
- `runFinished(success=false)` → digest "finished with errors" (red), error text in
  headline.
- Unknown/unregistered device detected → state `Detected`; existing
  unregistered-device flow unchanged (panel just reflects detection).

## Testing
- `tst_syncstatusmodel` (new): drive the model through synthetic sequences and assert
  state, chip states, progress, conflict count, and digest:
  - detect → handshake → connect → sync 6 conduits → finish (counts populate)
  - conflict arrives mid-run → conflict count + button label
  - **unplug mid-sync** → `Disconnected` + active chip `Interrupted`
  - connection failure → back to `Listening`
- Pure logic, no device, no event loop dependence — fits the ctest baseline gate.
- Widget rendering covered by a light smoke test (construct, drive model, no crash).

## Out of scope (phase 2 candidates)
- Per-conduit **live** counts (needs a libkalburator per-mapping completion signal —
  handoff doc).
- Structured `LogWidget` entries / in-panel recent-activity ticker (today `LogWidget`
  is `append(text)` only).
- Persisting the full last-sync digest across restarts (would need a `Profile` field;
  on fresh load the panel shows relative time from `lastSyncTime` only).

## Files touched
- New: `src/widgets/dashboard/syncstatusmodel.{h,cpp}`,
  `tests/.../tst_syncstatusmodel.cpp`.
- Rewritten: `src/widgets/dashboard/dashboardwidget.{h,cpp}`.
- Modified: `src/runtime/palmruntime.{h,cpp}` (engine-signal forwarding, new signals),
  `src/kf6/kf6mainwindow.{h,cpp}` (wiring; delete orphaned slot).
- CMake: register new sources + test.
