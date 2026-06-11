# Configuration UX umbrella: two doors, one mechanism

**Date:** 2026-06-11
**Status:** Approved umbrella design. Decomposes into three sub-projects, each
with its own spec → plan → implementation cycle.
**Sub-project specs:**
- A — substrate: `2026-06-11-config-substrate-design.md` (written; **IMPLEMENTED
  2026-06-11 on `main`** via `docs/superpowers/plans/2026-06-11-config-substrate.md`
  — conduit descriptor, names-first routes + status, LocalFolderContribution,
  device AppInfo write + reconciler, fifth-conduit proof. ctest 123/123. The
  first live AppInfo write joins clobber-sync Task 12 in the hardware-verification
  queue.)
- B — wizard: not yet written
- C — graph editor: not yet written

---

## 1. Problem

WildPalms has three configuration surfaces (New Profile wizard, the F.3
bipartite mapping graph, the Settings Accounts page) over one persistence
model, with three different mental models and three different levels of
expressiveness. The wizard can only bind one collection per conduit; the graph
can express category-slot routes but only against account providers; nothing
can configure a non-account backend; and every surface plus the runtime
hardcodes the four stock conduits (~7 sites, five of them `dynamic_cast`
chains), so a third-party conduit cannot participate without patching
WildPalms source.

Meanwhile the underlying mechanism — libkalburator's LogicalCollection roles,
Star topology generation, FilteredCollectionBackend category slicing,
registry-driven BackendContributions, and the domain layer — affords far more
than any surface exposes.

## 2. Shape of the solution

Two user-facing doors over one shared mechanism, deliberately unequal:

**The wizard is the safe door.** Account-level only, a few sensible defaults,
and an explicitly destructive premise: the PC/cloud side is authoritative, and
the first HotSync **clobbers** the Palm's PIM data and category tables to
match the configuration. The user checks the calendars and contact lists to
sync; the categories are made on the Palm. No data-preserving options exist
here — that removes the entire class of merge/dupe hazards from the path most
users take.

**The graph editor is the power door.** The F.3 graph is essential, not
legacy: the functionality demands a graph editor. It grows from two columns to
three — **Palm | Hub | Sources** — making the hub's per-domain collections
explicit nodes (authority-demotion state visible), and the Sources column
lists *every* registered provider: accounts and credential-less local sources
alike. Everything data-preserving, mixed, exotic, or dangerous (anything that
preserves Palm data and risks propagating dupes onto live CalDAV servers)
lives here, with danger affordances. Hub-only routes (e.g. hub → export
folder) become expressible, giving the known hub↔remote-only sync gap a
natural home when it closes.

**The shared mechanism** is what makes the two doors two views instead of two
systems: one row schema both doors write, one provider/source abstraction, one
conduit descriptor contract, one category model.

## 3. Locked decisions (from the 2026-06-11 brainstorm)

1. **Everything addable is a provider.** Local folders, ICS feeds, future
   document stores are credential-less `BackendContribution`s in the same
   registry as DAV/Akonadi. One lifecycle, one Add surface. (Akonadi already
   proves the pattern.)
2. **The wizard is account-level, multi-select, clobber-armed.** Per conduit,
   a checkbox list of matching collections: one checked = whole-domain route;
   several = one category route per collection, auto-named from the
   collection; none = hub-only. Review warns; the destructive sync fires only
   after a one-time confirmation at first device connect ("armed + confirm").
3. **Categories are names first; the device reconciles.** Routes bind to
   category *names*. At connect, names resolve to existing slots or WildPalms
   claims a free slot and **writes AppInfo** (the deferred F.5 write path is
   now in scope). Exhaustion and mismatches surface as visible route status —
   never a silent drop.
4. **The graph is the canonical advanced editor; no routing table.** A
   per-conduit table was considered and rejected — it would replace the graph
   with something worse. The wizard does not try to cover advanced scenarios.
5. **Three-column graph.** Palm | Hub | Sources, hub explicit.
6. **Conduits are enumerated, never assumed.** All surfaces and the runtime
   iterate registered conduit descriptors. Stock conduits stay statically
   loaded for now; the mechanism must not care.

## 4. Sub-projects and build order

**A — Substrate** (spec: `2026-06-11-config-substrate-design.md`):
the conduit descriptor contract (kills the hardcoded-4 sites), the unified
source model (first credential-less contribution), names-first categories with
device reconciliation + AppInfo write, and the persistence additions
(name-based route rows, desired category tables, `initialSyncPending`).
Pure mechanism; unblocks both doors.

**B — Wizard**: rebuild the bindings page as per-conduit collection
checkboxes; auto-named category routes; clobber arming + first-connect
confirmation (reusing F.2.5 clobber machinery and the substrate's category
writer). Depends on A.

**C — Graph editor**: three-column upgrade; sources column fed by the unified
provider registry; desired-category status badges; danger affordances for
data-preserving edges; hub-only routes. Depends on A; benefits from B's
field experience.

Order: **A → B → C.**

## 5. Explicitly out of scope (tracked elsewhere)

- **Hub↔remote-only sync** (running routes without a Palm connected): the
  graph design leaves room for it; the sync path itself is a separate effort.
- **Dynamic `.so` conduit discovery**: third-party conduits implement the
  descriptor and register in the load batch; how their binaries get loaded is
  a later problem.
- **Category-lifecycle conflict handling** (renames propagating both ways,
  F.5's full scope): A ships the write path and reconciler only.
- **Create-collection-on-server** sub-flow (F.1c deferral, unchanged).
