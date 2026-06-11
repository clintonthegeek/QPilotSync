# Sync Patchbay — design spec

**Date:** 2026-06-11
**Status:** Approved (brainstorm complete; mockups reviewed in visual companion)
**Supersedes:** the F.3 graph view design (`2026-05-23-f3-sync-mappings-graph-design.md`) as the
mapping UI; row format and engine behavior are untouched.

## 1. Summary

`SyncPatchbayView` is a new main-window centerpiece that is simultaneously the route **editor**
and the sync **monitor**. It draws WildPalms's true three-tier topology — **Palm | Hub |
Remotes** — as a patchbay: system-drawn strands on the left tier showing what the engine
auto-generates, user-authored wires on the right tier representing persisted mapping rows.
It surfaces, for the first time in any UI, route health (`RouteStatus`), hub state (baselines,
record counts), live per-mapping run activity, and last-run history — all in one visual language.

It is built on **Graffodil** (`~/dev/Graffodil`, `Graffodil::Core`), making WildPalms the
library's first real consumer. Gaps in Graffodil are landed upstream directly (WP drives,
Graffodil's own tests gate; no handoff-doc workflow).

## 2. Motivation

- Today's F.3 graph (`src/app/mapping/`) draws a **bipartite fiction**: palm-slot → provider
  wires, hiding the hub entirely. The engine is hub-centric: every persisted row is really
  "hub slice → remote collection", and palm↔hub legs are auto-generated at connect, never
  persisted. The UI should tell the truth.
- Substrate A's `RouteStatus` (Active / WaitingForDevice / NoFreeSlot / NotARoute) is computed
  (`PalmRuntime::routeStatuses()`) but surfaced nowhere.
- Live per-mapping run signals feed only the flat `DashboardWidget`; nobody can see *which
  relationship* is syncing, failing, or stale.
- The Settings-dialog hosting is wrong for a monitor: nobody watches a sync from a modal.

## 3. Decisions (brainstorm outcomes)

| Question | Decision |
|---|---|
| Sequencing | Graph rethink first, three-tier topology in scope; hub↔remote-only sync gap (roadmap item 2) designs against this picture later |
| Expression scope | All four layers: config topology, route health, live sync activity, history & baselines |
| Placement | Main-window centerpiece; Settings graph page retires; dashboard shrinks to a strip in the final phase |
| Topology skeleton | Three-column patchbay (option A), hub visually unified as a single store |
| Palm tier | System-drawn, read-only strands (option A) — status lives left, config lives right |
| Wire language | "Signal path" (option A): domain-colored wires, chevron direction, marching animation, midpoint beads |
| Hub anatomy | Monolith (option A): one panel, domain bands inside, ghost "+ category…" rows |
| Foundation | Graffodil, extended directly as needed (no RFC workflow while WP is sole consumer) |

Mockups were reviewed in the brainstorm visual companion (`.superpowers/brainstorm/`, gitignored
and ephemeral); this spec is self-contained and authoritative.

## 4. Architecture

Three layers, strictly separated:

```
Profile rows + desiredCategoryNames + categorySlotNames
PalmRuntime::routeStatuses() + run signals          AccountController providers/collections/states
        \                  |                  /
         +---------- PatchbayModel ----------+        pure QObject data layer, no QGraphicsView
                           |
                  SyncPatchbayView                    binds model -> Graffodil GraphScene
                           |
                    Graffodil::Core                   GraphScene, IGraphNode items, GraphEdgeItem,
                                                      CreateEdgeTool, PanZoomTool, AnchorHighlight
```

- **`PatchbayModel`** (new, `src/app/patchbay/`): consumes the inputs above and exposes
  nodes / ports / strands / wires with their states as plain data + change signals.
  Unit-testable without graphics. This supplies the model layer Graffodil deliberately
  doesn't have (its `GraphScene` *is* its registry).
- **`SyncPatchbayView`** (new): `IGraphNode` implementations (`PalmNode`, `HubNode`,
  `RemoteNode`), a `GraphEdgeItem` subclass (`SignalPathWire`) for wires, a read-only edge
  item for palm strands, manual three-column layout via `LayoutResult`. Hosted as the
  KF6MainWindow central view.
- **Graffodil** provides scene, tools, anchors, bezier routing, pan/zoom, selection,
  and (after our upstream work) edge midpoint labels and dash-phase animation.

Persisted data is **unchanged**: same `SyncMapping` JSON rows
(`sourceBackend=<conduitId>`, `sourceCalendar="" | "palm:<domain>/name:<categoryName>"`,
`targetBackend="<providerId>:<collectionId>"`, `targetCalendar=<collectionId>`, mode,
conflict policy, enabled). No migration.

## 5. Scene specification

### 5.1 PalmNode (left column, one per profile device)

- Header: device name, connect state, last-HotSync time.
- One band per conduit DB, **descriptor-driven** (`PalmRuntime::conduits()` /
  `conduitcatalog`): a fifth conduit appears with zero view changes.
- Each band lists claimed category slots from the `Profile::categorySlotNames(dbName)`
  snapshot, each with a right-edge anchor.
- Device disconnected → node renders ghosted with the last-known snapshot. It never vanishes.

### 5.2 HubNode (middle column, the monolith)

- One panel spanning the column. Header: `⊙ HUB · <N> records` (sum of domain stores).
- One **band per domain** (calendar / contacts / note / todo — route-domain names per
  `palmRouteDomainForDb`, n.b. MemoDB → `note`). Bands are descriptor-driven like PalmNode.
- Band contents, in order:
  1. **Whole-domain port** ("All events", "All contacts", …) — corresponds to rows with
     empty `sourceCalendar`.
  2. **Category ports** — one per desired category (`Profile::desiredCategoryNames` union
     categories referenced by rows). Correspond to rows with
     `sourceCalendar="palm:<domain>/name:<cat>"` (engine-side: a
     `FilteredCollectionBackend` tap, `wp-route-<id>`).
  3. Ghost **"+ category…"** row (edit affordance, §7.4).
- Band footer: `baseline ✓` or `first sync pending` (from `initialSyncPending` /
  baseline state), per-domain record count. (The exact hub-store introspection accessor
  on PalmRuntime is a plan-level detail.)
- Ports have anchors on **both** edges: left edge receives palm strands, right edge
  emits wires.

### 5.3 RemoteNode (right column, one per account/provider)

- Header: provider display name + connection state (Connected / Connecting / Error, from
  `AccountController::stateFor`). Error text in header tooltip and inspector.
- One port per **(collection, domain)** pairing: a collection renders a port for each
  domain it supports per **contentTypes** (a mixed VEVENT+VTODO calendar shows both a
  calendar port and a todo port), consistent with the v0.67 contentTypes rules.
- Ghost **"Add account…"** node at the column foot → opens `AddAccountDialog`; new account
  connects and its node materializes with discovered collections.
- A row whose `targetBackend` references a missing account/collection produces a ghost
  "missing account/collection" RemoteNode so the wire is never silently dropped (§10).

## 6. Edge species and states

### 6.1 Palm strands (left tier — system-drawn, read-only)

Visualize what the engine auto-generates at `finishConnect`; not rows, therefore not editable.

| State | Rendering |
|---|---|
| Whole-domain pipe (conduit DB ↔ domain band) | solid strand, domain color, slightly thicker |
| Category strand, slot claimed | solid strand slot ↔ category port |
| `WaitingForDevice` (desired category, no device slot yet) | ghost-dashed strand from hub port toward palm column, reduced opacity |
| `NoFreeSlot` (16 slots exhausted) | no strand; amber badge on the hub category port; explanation in inspector |
| Running (palm↔hub leg dispatching during HotSync) | marching-dash animation on the strand |

### 6.2 Wires (right tier — user-authored, one wire = one persisted row)

| State | Source | Rendering |
|---|---|---|
| Two-way, healthy | `mode=TwoWay`, status Active | solid domain-colored stroke, no chevrons |
| One-way | `OneWayUpload` / `OneWayDownload` | chevrons along the wire pointing in flow direction |
| Disabled | `enabled=false` | dashed, desaturated grey |
| Broken | `NotARoute`, missing backend/collection, stale category | red stroke + ✕ bead |
| Waiting | `WaitingForDevice` on the source category | wire renders normal; the *palm strand* ghosts and the hub port carries an amber dot (config is fine; device hasn't caught up) |
| Running | per-mapping run signals | marching dashes + bead `⟳ <done>/<total>` |
| At rest | last-run history | bead with result: `✓ <relative time>` or `✗ <error>` |

### 6.3 Beads (midpoint chips)

- Default: **glyph-only dot** (✓ / ✗ / ⟳ / nothing if never run). Expands to a text capsule
  on hover or selection. This is the density-control mechanism.
- Beads are click targets: clicking selects the wire (same as clicking the stroke).
- Implemented on Graffodil's edge-label support (Phase 6c), not WP-side hacks.

## 7. Interaction model

### 7.1 Create

Drag port→port in either direction (`CreateEdgeTool`; both drag and click-click work).
During drag: compatible anchors highlight (`AnchorHighlight`), incompatible targets dim.
Validation on drop, same rules as F.3: descriptor `matchesCollection` domain compatibility
+ duplicate guard. Valid drop → new row (TwoWay / profile-default policy / enabled), wire
appears, `mappingsChanged` persists via Profile.

### 7.2 Edit

Selecting a wire opens the **inspector dock** (right side, dockable QWidget — not a popover):
sync mode, conflict policy, enabled checkbox (the F.3 inspector's surface), **plus** the
status story: RouteStatus with human explanation ("Waiting for device: category 'Work' will
be created on the Palm at next HotSync"), last run result/time, error detail.

### 7.3 Delete

Delete/Backspace on selected wire, or wire context menu → Delete. Palm strands expose no
delete affordance.

### 7.4 Category lifecycle

- "+ category…" ghost row → inline name edit → adds to `Profile::desiredCategoryNames`
  and creates the port. The reconciler (`reconcileCategories`) creates the device slot at
  next connect; until then the port's strand renders WaitingForDevice.
- Ports can exist unwired (desired category, not yet routed).
- Removing a port requires removing its wires first (context menu → Remove category;
  refuses while wires attached).
- **Rename is out of v1** — recreate instead.

### 7.5 Guards & navigation

- Read-only during sync runs (same `runStarted`/`runFinished` watch as F.3) — editing
  locked, canvas keeps animating; a slim banner states the lock.
- Pan/zoom via `PanZoomTool`; fit-to-scene on load and on profile switch.

## 8. Live data flow

- `PatchbayModel` subscribes to the **same** per-mapping signals that feed
  `SyncStatusModel` (mapping started / progress / finished, run started/finished).
- Signal → model state change → view updates wire/strand state; marching-dash phase is
  driven by a single scene-level ticker (a Graffodil dash-phase property animated by
  `QVariantAnimation`), so tests can step it manually.
- Run results land in the model as last-run history per mapping id; beads re-render.
- Provider connect-state changes re-render RemoteNode headers and port availability.

## 9. Graffodil extensions (landed upstream, directly)

| Extension | Notes |
|---|---|
| Edge midpoint labels/chips | Phase 6c spec already drafted upstream; implement there; WP beads consume it |
| Dash-offset animation hook on `GraphEdgeItem` | phase property + enable flag; manually steppable for tests |
| Read-only tool mode | attempt WP-side tool composition first (`CompositeTool`); upstream only if it proves generic |
| Three-column layout | **stays WP-side** — manual `LayoutResult`, deterministic; no library change |

Consumption mirrors the libkalburator pattern: `WILDPALMS_GRAFFODIL_SOURCE_DIR` sibling-dir
override + pinned tag fallback (`WILDPALMS_GRAFFODIL_GIT_TAG`). Graffodil work merges with
Graffodil's own ctest suite green before WP bumps its pin. (Corbomite/PlanStan are not yet
consumers; no downstream gate exists yet.)

## 10. Error handling — never silently drop

- Row → missing account/collection: red wire to a **ghost RemoteNode**, never an omitted
  edge (extends substrate A's `translateRouteSpec` no-silent-drop principle to the canvas).
- Provider connect failure: state + message on the RemoteNode header; collections last
  seen may render ghosted.
- `NoFreeSlot`: amber badge on the hub category port; inspector explains the 16-slot limit
  and which slots are occupied.
- Per-mapping run errors: ✗ bead on the wire; full error in the inspector.

## 11. Retirement & migration

| Artifact | Fate | When |
|---|---|---|
| `src/app/mapping/` (SyncMappingsGraphView, PalmDbNode, ProviderNode, MappingEdge, SyncMappingsPage, MappingInspectorPanel) | deleted | Phase 3, after edit parity is verified |
| SettingsDialog "Sync Mappings" page | removed | Phase 3 |
| `tst_syncmappingsgraphview` | superseded by patchbay view tests | Phase 3 |
| `DashboardWidget` | shrinks to a summary strip (device state, run summary); `SyncStatusModel` survives as data source | Phase 3 |
| New Profile wizard | **stays** — onboarding path; patchbay is the living editor | n/a |
| Persisted rows | unchanged, no migration | n/a |

## 12. Testing

- **Model tests** (`tst_patchbay_model`): rows + statuses + provider states in → expected
  nodes/ports/strands/wires/states out. No graphics. Covers every state in §6's tables,
  ghost-node synthesis, category add/remove, run-signal state transitions.
- **View tests** (F.3 style, against a real Graffodil scene): drag-connect emits correct
  row JSON; delete; read-only guard; bead/strand rendering states; descriptor-driven band
  enumeration (fifth-conduit seam reused).
- **Graffodil-side tests** for edge labels + dash animation land in Graffodil's suite
  before WP consumes them.
- Animation assertions step the dash-phase manually; no timers in tests.

## 13. Phasing

1. **Phase 0 — Graffodil:** edge midpoint labels (6c), dash-phase animation hook; tag;
   WP build wiring (`WILDPALMS_GRAFFODIL_*`).
2. **Phase 1 — Static patchbay:** `PatchbayModel` + view with full edit parity: three
   tiers, strands, wires, beads (history), route health, inspector dock, category
   lifecycle. Hosted as the new central view; the existing dashboard remains accessible
   (exact interim placement decided in the plan) until Phase 3.
3. **Phase 2 — Live:** run-signal animation, read-only-during-sync guard, live record
   counts on beads.
4. **Phase 3 — Consolidation:** retire `src/app/mapping/` + Settings page, dashboard →
   summary strip, polish pass (hover rings, dimming, fit/zoom ergonomics).

Each phase lands with ctest green (WP 123/123 baseline + new tests; Graffodil suite green
for Phase 0).

## 14. Out of scope / deferred

- Category **rename** (recreate instead; revisit with category-lifecycle work).
- Hub↔remote-only run **triggering** (roadmap item 2) — these wires already draw it;
  the run path is its own spec.
- Multi-collection merge UX (several calendars → one datebook) — wizard/roadmap item,
  unchanged by this design.
- Conflict-resolution UI beyond policy selection (F.5 territory).
- Theming beyond the initial palette: calendar `#5b8dd9`, contacts `#5fb878`,
  note `#d9a45b`, todo `#b07fd4`, hub chrome violet `#6a5fc4`; final values may be
  derived from the active color scheme, but domain-hue identity is fixed.
