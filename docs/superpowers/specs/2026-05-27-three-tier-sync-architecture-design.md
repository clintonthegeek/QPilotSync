# Three-Tier Sync Architecture (Hub-and-Spoke) — Umbrella Spec

**Date:** 2026-05-27
**Status:** Approved architecture; umbrella spec for a multi-sub-project effort
**Scope:** WildPalms + libkalburator (cross-repo)

---

## 1. Problem

Two surface symptoms exposed a deeper architectural gap:

1. **The contacts conduit is invisible in the main view.** By design —
   `ContactsBackendPlugin::hasMainView()` returns `false`
   (`src/plugins/contacts/contactsbackendplugin.h:64`), and the sidebar loop
   (`src/kf6/kf6mainwindow.cpp:675-680`) only registers Calendar, Memo, Todo. A
   legacy `ContactView` exists but was never wired into the V2 sidebar (deferred
   to "E.16").

2. **The sync plumbing is incomplete and uneven.** The *live* path
   (`PalmRuntime`) runs a flat queue of pairwise `SyncMapping`s that wire each
   Palm category slot to a local `RawFilesBackend` writing **raw Palm wire
   bytes**. `RawFilesBackend::suffixFor("palm_contact_0")` returns the string
   verbatim, so files are named `*.palm_contact_0`, not `*.vcf`/`*.ics`. The
   views scan for `*.vcf`/`*.ics`/`*.md`, so **only Memo shows data** — it is the
   lone domain whose peer is `MarkdownFilesBackend` (overrides the suffix to
   `md` and writes real markdown). Calendar and Todo views are equally empty;
   nobody noticed because the canon sync work shipped device-unverified. On
   disk, `~/.wildpalms/profile1/rawfiles/*/` contains only `_shapes.json` — zero
   record files.

The real issue: **WildPalms has no canonical local store and no concept of
backend authority.** It treats every target as an equal pairwise peer, has
accreted three competing config/state layers, and has not adopted the topology
and role primitives libkalburator already ships.

### Why a bigger answer than "add a .vcf backend"

We have been sloppy about the *roles* of our sync targets and uneven about
adopting current libkalburator features. The right fix is to define those roles
precisely and let the library do the heavy lifting, not to bolt another
per-domain file backend onto a flat engine.

---

## 2. Goals / Non-Goals

### Goals

- A single **canonical local hub** owned by WildPalms that informs all views,
  buffers changes across infrequent Palm syncs and offline periods, and is
  present + visible in the conduit list the moment the app opens.
- A precise, uniform **role model** across all four PIM domains (calendar,
  contacts, todo, note) — Palm as a device tier, the hub as the authoritative
  local Primary, optional remotes as sync spokes.
- **Authority-aware editability**: WildPalms is a full editor in a closed
  (Palm-only) system, and a *passive conduit* (hub locked to the user) when a
  remote owns a domain.
- All conduit views (including contacts) display data uniformly, reading from
  the hub.
- Adopt current libkalburator (`main` + topology work) rather than diverging.

### Non-Goals

- No per-domain bespoke file backends. Human-readable `.vcf`/`.ics`/`.md` files
  become an **optional user-added spoke** ("LocalFiles"), reusing the existing
  `RawFilesBackend`/`MarkdownFilesBackend` — not the hub.
- No new conflict-resolution model; existing handlers carry over.
- Not implementing `Mirror`/`Chain` topologies — only `Star`.
- The detailed per-sub-project design lives in each sub-project's own spec; this
  umbrella defines the target and the decomposition only.

---

## 3. Current State (what is live vs. dead)

- **Live:** `PalmRuntime` owns a `Kalburator::Sync::SyncEngine` +
  `Kalburator::Storage::BaselineStore` (`src/runtime/palmruntime.cpp:117,131`)
  and runs raw pairwise `SyncMapping`s. No roles, no hub.
- **Host layer (half-wired):** `SyncHost_WP` (`ISyncHost`) + `SyncConfigStore_WP`
  (`ISyncConfigStore`) already speak `Kalburator::Sync::LogicalCalendar`
  (`src/runtime/syncconfigstore_wp.cpp`), persisted under
  `fullsync/logicalCalendars` — but the live engine path does not drive role
  bindings through it.
- **Legacy:** `src/sync/syncstate.{h,cpp}` and a second JSON
  `WildPalms::Sync::BaselineStore` (`src/sync/journal/baselinestore.*`), mostly
  gutted in E.16; ConduitManager deleted.

### libkalburator primitives already available (verified)

- `BackendRole { Primary, Sync1..N, ReadOnly }` with `syncOrder` and per-binding
  `metadata` (`src/types/logicalcalendar.h:39-83`). The authority/demotion
  vocabulary already exists.
- `GenericSqliteBackend` — a shape-aware SQLite store for *any* domain
  (`src/universal/genericsqlitebackend.h`). The natural hub.
- `Kalburator::Storage::BaselineStore` — the live diff/anchor store.
- Read-only enforcement hooks (`discoveredWritable()`, marker files).
- `SyncTopology { Star, Mirror, Chain }` (`src/types/synctypes.h:45-49`) —
  **named but unimplemented**; the engine runs a flat pairwise queue.

**Gap:** `LogicalCalendar` is calendar-named/typed; `Star` and role-driven
demotion are not implemented; WP's live path uses none of the role model.

---

## 4. Target Architecture

```
          ┌──────────────────┐
 Palm ────│   WildPalms HUB  │──── CalDAV / CardDAV     (Sync1)
(device)  │  canon SQLite    │──── Akonadi              (Sync2)
 Sync     │  role = Primary  │──── LocalFiles .vcf/.ics (Sync3, optional)
 binding  └────────┬─────────┘
                    │ feeds
                views / editing UI
```

- **Hub (Tier 2, Primary):** one `GenericSqliteBackend` per domain holding canon
  records. Created at profile init, before any device connect. It is the view's
  data source, the offline buffer, and the always-present default conduit.
- **Palm (Tier 1):** a `Sync` binding (device tier), synced infrequently. Its
  changes land in the hub and buffer there until spokes are reachable.
- **Spokes (Tier 3, `Sync1..N`, optional):** user-added. CalDAV/CardDAV/Akonadi
  and a "LocalFiles" spoke (existing `RawFilesBackend`/`MarkdownFilesBackend`,
  opt-in, producing inspectable `.vcf`/`.ics`/`.md`).
- **Authority / demotion:**
  - No remote spoke for a domain → the hub is user-editable (closed system).
  - A writable remote spoke owns a domain → the hub demotes to
    read-only-*to-the-user* (still records inbound changes and buffers
    Palm-side edits while offline); WildPalms becomes a passive conduit.
  - Surfaced via an authority query the UI consumes to gate editing. **Where
    this is enforced is an open question** (see §5 + the libkalburator proposal).

### Data flow

`Star` means: Palm → hub (apply device changes to canon), then hub → each
enabled spoke ordered by `syncOrder`. Shape routing
(`palm-encoding → canon → spoke-encoding`) is already handled by the shape
graph; all spokes route *through* the Primary hub rather than pairwise against
the device.

**Mechanism note (corrected after boundary audit):** libkalburator's
`SyncEngine` already runs *any* `SyncMapping` list — it does **not** consume
`LogicalCalendar`/`BackendRole`, which are inert types today. So `Star` is **not
an engine change**; it is a *generator* (`bindings + topology → SyncMapping set`)
plus an authority/demotion policy. Whether that generator + policy lives in
libkalburator (shared) or in WildPalms (consumer) is the subject of
`docs/2026-05-27-libkalburator-topology-authority-proposal.md` and gates §5.

---

## 5. libkalburator Changes — PENDING A DECISION

> **This section is gated by `docs/2026-05-27-libkalburator-topology-authority-
> proposal.md`.** The boundary audit showed `SyncEngine` consumes `SyncMapping`
> only and that `BackendRole`/`LogicalCalendar`/`SyncTopology` are inert. So the
> "Star generator + authority policy" can live either in libkalburator (shared
> with PlanStan) or in WildPalms (consumer). The proposal asks libkalburator to
> pick a side. Per standing policy, any libkalburator change is delivered via
> handoff + PlanStan-green; the user has sanctioned proposing semantic changes.

**If libkalburator "owns it" (proposal §4):**
1. Generalize `LogicalCalendar` → a domain-agnostic `LogicalCollection`
   (`DomainId` + `collectionId`), so all domains reuse `BackendRole` bindings.
2. Add a shared mapping **generator** (`bindings + SyncTopology → SyncMapping`),
   in a coordinator layer **above `SyncEngine`** (the engine loop is unchanged).
3. Add an authority gate: consult `discoveredWritable()` in the write path
   (a standalone correctness fix), plus a demotion query the UI consumes.

**If libkalburator "disowns it":** the generator + authority policy move to §6
(WildPalms), using `BackendRole` only as inert data; we still request the
`discoveredWritable()` enforcement fix.

---

## 6. WildPalms Changes

4. **Port to current libkalburator (prerequisite, mechanical).** Per
   `libkalburator/docs/2026-05-27-downstream-port-checklist.md`:
   - **O7:** construct one `Shape::ShapeRegistries` at the composition root and
     inject the same instance into both `PluginManager` and `SyncEngine`; drop
     the transitional no-`ShapeRegistries` overloads.
   - **O12:** update every `SyncBackend` override to the new
     `pushItems`/`startSync` signatures (no `TranscodingPlan` argument).
   - **O15:** remove all use of deleted `SyncTransaction` / `CalendarPluginWriter`
     / `*IncidenceItem` classes; rewrite any rollback-asserting tests to the
     retry-safe contract (inject failure → assert `!success` → clear → re-sync →
     assert convergence).
   - *This is fully specified by the checklist and does not need its own design
     spec — it executes.*
5. **Stand up the per-domain canon hub** (`GenericSqliteBackend`, `Primary`),
   created at profile init; express Palm + spokes as role bindings; let the
   engine's `Star` run them. Retire the per-slot raw-Palm-byte default mappings
   in `finishConnect`.
6. **Rewrite the conduit views to read the hub** via the backend record API
   instead of scanning `rawfiles/*`. (Full view rewrites are in scope.) Contacts
   gets a V2 main view; all four domains display uniformly. The original
   "contacts invisible" symptom dissolves here.
7. **Editability gating + conduit default-visibility:** views are editable only
   when the domain is not remote-owned (driven by the engine's authority query);
   hub conduits appear in the list on launch, before device connect.
8. **Reconcile accreted layers:** retire the legacy JSON
   `WildPalms::Sync::BaselineStore` / `syncstate` remnants and the half-wired
   `SyncConfigStore_WP` path so there is one coherent config + state model
   (role bindings + `Kalburator::Storage::BaselineStore`).

---

## 7. Decomposition & Sequencing

Each piece below (except the port) gets its own spec → plan → implementation
cycle.

| # | Sub-project | Repo | Depends on |
|---|-------------|------|-----------|
| A | Port to current libkalburator (O7/O12/O15) | WildPalms | — |
| B | Generalize `LogicalCalendar` + implement `Star` + demotion semantic | libkalburator (handoff) | A |
| C | Canon hub + role bindings; engine runs Star | WildPalms | B |
| D | Rewrite views to read the hub (incl. contacts V2 view) | WildPalms | C |
| E | Editability gating + conduit default-visibility | WildPalms | C, D |
| F | Reconcile/retire legacy config + state layers | WildPalms | C |

Order: **A → B → (C) → (D, E) → F.** The "contacts visible + all views show
data" outcome lands at **D**.

---

## 8. Open Validation Items (resolved in sub-project specs, not here)

- Confirm `GenericSqliteBackend` comfortably serves multiple canon collections
  per domain as a hub (one DB per domain vs. one DB many collections).
- Confirm the exact read/observe API the rewritten views use against the hub
  backend (record enumeration + change notification).
- Determine how Palm category slots map onto hub collections under Star (the
  category-routing graph in `src/app/mapping/` must target hub collections, not
  raw per-slot file backends).
- Decide the persistence format for role bindings (extend `Profile`
  `mappings.conf` vs. the `fullsync/logicalCalendars` group) as part of F.

---

## 9. Success Criteria

- On opening a fresh profile (before connecting a Palm), all four domain
  conduits appear, backed by an empty canon hub, and the views render.
- After a Palm sync with no remote configured, all four views (including
  contacts) display the synced records and are editable.
- Adding a remote spoke (e.g. CardDAV) for a domain demotes that domain's hub to
  read-only in the UI; sync flows Palm ↔ hub ↔ remote through `Star` in one run;
  offline Palm edits buffer in the hub and propagate when the remote is reachable.
- Adding a "LocalFiles" spoke produces inspectable `.vcf`/`.ics`/`.md` without
  being the hub.
- PlanStan's ctest baseline stays green across the libkalburator changes.
