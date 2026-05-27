# Proposal to libkalburator: pick a side on topology & backend authority

**Date:** 2026-05-27
**From:** WildPalms (a downstream `SyncEngine` / `SyncBackend` consumer)
**To:** libkalburator maintainer
**Status:** Proposal / RFC — requesting a decision, offering changes
**Relationship to other docs:** Supersedes the "implement `SyncTopology::Star`
in `SyncEngine`" framing in WildPalms' umbrella spec
(`docs/superpowers/specs/2026-05-27-three-tier-sync-architecture-design.md`
§5), which was written before the boundary audit below corrected it.

---

## 0. TL;DR

WildPalms wants a three-tier **hub-and-spoke** sync model: a Palm device, a
canonical **local hub** that owns the UI and buffers changes, and optional
**remote spokes** (CalDAV/CardDAV/Akonadi/LocalFiles) that, when present, should
**demote the hub to read-only for the user**.

While planning this, we audited libkalburator and found that **the building
blocks for this model exist as types but are wired to nothing**:

- `SyncEngine` consumes **`SyncMapping`** (flat pairwise) and nothing else.
- `LogicalCalendar` + `BackendRole` (Primary/Sync1..N/ReadOnly) are **inert
  config types** — no engine code reads them.
- `SyncTopology { Star, Mirror, Chain }` has **exactly one occurrence in the
  tree: its own declaration**.
- `discoveredWritable()` is a **per-backend self-report the engine never
  consults** before writing.
- The `LogicalCalendar → SyncMapping` translation is, by your own design, a
  **downstream responsibility** (`CalendarManager::regenerateSyncMappings()`
  emits `syncMappingRegenerationRequested()` and expects the host to do it).

So every consumer (PlanStan, WildPalms) must independently reinvent role→mapping
generation, topology, and authority enforcement. **We're asking libkalburator to
decide whether it wants to own that model or explicitly disown it**, because the
current half-built state invites consumers to assume behavior that isn't there.
Our opinion and a concrete proposal follow. WildPalms will adapt to whichever
side you pick.

---

## 1. What WildPalms is trying to build

```
          ┌──────────────────┐
 Palm ────│   WildPalms HUB  │──── CalDAV / CardDAV     (Sync1)
(device)  │  canon SQLite    │──── Akonadi              (Sync2)
 device   │  role = Primary  │──── LocalFiles .vcf/.ics (Sync3, optional)
 tier     └────────┬─────────┘
                    │ feeds
                views / editing UI
```

- **Hub** = one `GenericSqliteBackend` per domain (canon records); the view's
  data source and offline buffer; always present.
- **Palm** = device tier, synced infrequently.
- **Spokes** = optional `Sync1..N`; remotes *and* a human-readable "LocalFiles"
  spoke (reusing `RawFilesBackend`/`MarkdownFilesBackend`).
- **Authority/demotion**: no remote ⇒ hub user-editable (closed system); a
  writable remote owns a domain ⇒ hub becomes read-only **to the user** (still
  records inbound + buffers offline), WildPalms is a passive conduit.

This is the same shape `BackendRole` and `SyncTopology::Star` *describe*. We are
not inventing new vocabulary — we want the vocabulary you already shipped to
actually do something.

---

## 2. What we found (verified, with attribution)

All file:line references are in `libkalburator` unless noted.

### 2.1 The engine speaks `SyncMapping`, not `LogicalCalendar`

- `SyncEngine::setSyncMappings(const QList<SyncMapping>&)` — `syncengine.h:474`.
- `SyncEngine::runSyncFuture(...)` — `syncengine.h:536,557,566`.
- `SyncEngineWorker::Request { SyncMapping mapping; ... }` — `syncengine.h:136-142`.
- Zero references to `LogicalCalendar` anywhere under `src/engine/`.

### 2.2 `BackendRole` / `LogicalCalendar` are inert

- Defined in `types/logicalcalendar.h:39-46` (enum) and the binding/accessor
  helpers (`primaryBinding()`, `syncBindings()`, `bindingWithRole()`,
  validation enforcing exactly-one-Primary).
- **Every use is UI/config-side.** No `BackendRole` / `primaryBinding` /
  `syncBindings` reference exists under `src/engine/`. The `SyncMapping` struct
  (`synctypes.h:224-242`) has **no role field**; source and target are treated
  symmetrically.

### 2.3 `SyncTopology` is a declaration with no consumer

- `synctypes.h:45-49` — `enum class SyncTopology { Star, Mirror, Chain }` with
  doc comments ("Star … default, recommended"). A tree-wide grep finds only
  this declaration. Nothing reads it.

### 2.4 Mapping generation is explicitly punted downstream

- `CalendarManager::createCalendar(const LogicalCalendar&)` stores the logical
  calendar to config, then calls `regenerateSyncMappings()`, which **emits
  `syncMappingRegenerationRequested()`** — a host hook. libkalburator does not
  translate roles into mappings; it asks the consumer to.

### 2.5 `discoveredWritable()` is advisory, not enforced

- Declared `syncbackend.h:191-194` (`virtual bool discoveredWritable(...) const`).
- Implemented per backend (`localbackend`, `subscriptionbackend` returns false,
  `akonadibackend` checks ACLs, etc.).
- **No `src/engine/` code calls it.** The unified write path
  (`IBlobBackend::createRecord/updateRecord/deleteRecord`) is invoked
  unconditionally. A read-only backend is "protected" only by its own writes
  failing — not by a pre-flight gate.

### 2.6 Downstream (WildPalms) state, for context

- WP's live path builds `SyncMapping`s directly in `PalmRuntime` (WildPalms
  `src/runtime/palmruntime.cpp:471-487`) and never uses `LogicalCalendar`.
- WP's only `LogicalCalendar` consumer (`SyncConfigStore_WP`) is **dead code**,
  never instantiated. So WP adopting roles is greenfield, not a migration.
- WP's four domain views scan `rawfiles/*` from disk; they do not use the
  backend read API yet. (That's our problem to fix, not yours.)

---

## 3. Our opinion on libkalburator's architecture

You drew a deliberate, defensible line: **libkalburator owns sync *mechanism*
(diff, transcode via the shape graph, baseline, conflict), and the consumer owns
sync *policy* (which collections map to which, in what topology).** That line is
fine. The problem is that libkalburator **also ships the vocabulary of policy**
— `BackendRole`, `LogicalCalendar`, `SyncTopology`, `discoveredWritable()` — as
**richly-specified-but-unwired types**. That's the worst of both worlds:

1. **It implies behavior that doesn't exist.** A reader sees `BackendRole::ReadOnly`,
   `primaryBinding()`, validation enforcing exactly-one-Primary, and a
   `SyncTopology::Star` "recommended" comment, and reasonably assumes the engine
   honors them. It does not. We assumed it did, and built a spec around it.
2. **It guarantees divergence.** Each consumer reinvents role→mapping generation,
   topology expansion, and authority enforcement — differently. PlanStan and
   WildPalms will not agree on what "Star" or "demote to read-only" means.
3. **It leaves a latent correctness gap.** The engine never consulting
   `discoveredWritable()` means a read-only/subscription backend gets writes
   attempted against it and relies on incidental failure. That reads like a bug,
   not a policy choice.

**So: pick a side.**

- **Own it (our recommendation).** Make the policy vocabulary *behavioral* —
  inside libkalburator but **not inside `SyncEngine`'s loop** (the engine's
  `SyncMapping` contract is good; leave it). Add a thin, shared **coordinator**
  layer above the engine that turns `LogicalCollection` (domain-agnostic) +
  `SyncTopology` into the `SyncMapping` set the engine already runs, and add a
  real authority gate. Both consumers benefit; the types finally mean what they
  say.
- **Disown it.** Declare roles/topology to be *consumer-side translation
  scaffolding*, document it loudly at the type definitions, and consider
  deleting the unwired `SyncTopology` enum and the behavioral-looking
  `LogicalCalendar` validation so nothing implies engine support. WildPalms then
  owns its own generator and authority policy with no illusions.

Either is coherent. The current middle is not. We'd rather you own it — but if
you don't want to, say so explicitly and we'll build it consumer-side and stop
filing this kind of proposal.

---

## 4. Proposed changes (offered, if you "own it")

These are offers, not demands; shapes/names are negotiable. None touch
`SyncEngine`'s core loop.

### 4.1 Generalize `LogicalCalendar` → domain-agnostic `LogicalCollection`

`LogicalCalendar`, `CalendarBackendBinding`, and `calendarId` are calendar-named
for what is conceptually "a logical collection with N backend bindings." Now
that libkalburator spans contacts/todo/note/outline canon domains, the naming is
a smell.

- Add a `Shape::DomainId domain` to the logical-collection type and the
  binding's `calendarId` becomes `collectionId`.
- Keep `LogicalCalendar` as a thin alias/typedef (domain = calendar) for source
  compatibility during migration; deprecate over a release.

### 4.2 A shared topology generator (a coordinator, not the engine)

A free function or small class:

```cpp
// Proposed: generates the SyncMapping set the engine already consumes.
QList<Sync::SyncMapping> generateMappings(const LogicalCollection &lc,
                                          Sync::SyncTopology topology);
```

- `Star`: for the single `Primary` binding P and each enabled `Sync*` binding S
  (ordered by `syncOrder`), emit `P ↔ S`. (This is the only topology WildPalms
  needs; `Mirror`/`Chain` can stay unimplemented and assert.)
- This finally gives `SyncTopology::Star` a consumer and makes
  `CalendarManager`'s `syncMappingRegenerationRequested()` hook resolvable with a
  library-provided default instead of bespoke per-consumer code.

### 4.3 An authority / read-only enforcement gate

Two parts:

- **Enforcement (correctness):** before the unified write path writes to a
  backend, consult `discoveredWritable(collectionId)` and skip/report instead of
  attempting a doomed write. This closes 2.5 regardless of the topology
  decision and is arguably a standalone bugfix.
- **Demotion (policy):** expose a query the UI can consume, e.g. on the logical
  collection: "is the local `Primary` user-writable, given the current
  bindings?" — returning false when a writable remote `Sync*` binding owns the
  collection. The *generator* (4.2) sets mapping modes accordingly; the *query*
  drives the consumer's editing UI. WildPalms will gate its views on this.

### 4.4 Documentation

Whichever side you pick, annotate the type definitions
(`logicalcalendar.h:39`, `synctypes.h:45`) to state plainly whether the engine
honors them. That one paragraph would have saved this entire round-trip.

---

## 5. How WildPalms will consume each outcome

- **If you own it:** WP builds `LogicalCollection`s (Palm = Primary or device
  tier; hub = Primary; remotes/LocalFiles = `Sync*`), calls your generator,
  feeds the result to `setSyncMappings()`, and gates its views on your authority
  query. Minimal WP-side policy code.
- **If you disown it:** WP implements `generateMappings()` and the authority
  policy itself in `src/runtime` (it already owns `SyncMapping` construction),
  uses `BackendRole` only as inert data (or its own enum), and we drop the
  expectation of shared behavior. We'd still want the 4.3 *enforcement* bugfix.

Independently of this decision, WildPalms still owes you the **O7/O12/O15 port**
(`docs/2026-05-27-downstream-port-checklist.md`): O12/O15 are already no-ops for
WP (verified — WP builds clean against current `main`+o15 with zero source
changes), and O7 (drop the transitional ambient `ShapeRegistries`) is WP-side
work across four plugin submodules. That port is unaffected by this proposal.

---

## 6. The decision we need

Please answer:

1. **Own or disown** the role/topology/authority model?
2. If own: are 4.1 (domain-agnostic generalization), 4.2 (shared generator), and
   4.3 (enforcement + demotion query) the right shapes, or do you want different
   boundaries/names?
3. Regardless: will you take the 4.3 **enforcement** change (consult
   `discoveredWritable()` before writing) as a standalone correctness fix?

WildPalms will sequence its hub/views/gating work
(`docs/superpowers/specs/2026-05-27-three-tier-sync-architecture-design.md`)
around your answer.
