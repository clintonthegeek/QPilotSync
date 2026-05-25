# libkalburator — `feature/canon-upgrade-convergence` consumer handoff

**From:** libkalburator maintainers
**Date:** 2026-05-24
**About:** what changed in libkalburator between the `main` you consume today and the
`feature/canon-upgrade-convergence` branch, how to point your build at that branch, and
how the library is laid out now so you can navigate it.

This document is **only about libkalburator**. It does not tell you how to change your own
code — it tells you what the library now looks like so you can decide how to adopt it.

---

## 1. TL;DR

- libkalburator had **two** parallel record-conversion subsystems (`src/transcoding/` and
  the shape graph `src/shape/`). This branch **deletes `src/transcoding/`**; the shape
  graph is now the **single** transformation mechanism.
- The calendar/contacts/todo canonical encodings were upgraded from flat vendor text
  (iCal/vCard/vtodo) to **rich JSON superset encodings** (`+canon`) able to hold
  Google/Microsoft-Graph–level richness, with the legacy text encodings kept as lossy peers.
- The loss model went from a single severity level to a **four-kind per-property taxonomy**
  (`Dropped` / `Simplified` / `Reversible` / `Degraded`).
- **One breaking API change** for anyone subclassing a backend: the `TranscodingPlan`
  parameter is **gone** from `SyncBackend::pushItems` / `startSync` (see §4). Everything
  else is additive.
- The work is on branch `feature/canon-upgrade-convergence` (off `main`); the suite is
  **111–112 / 112 green** (one pre-existing async flake, `tst_providerlifecycle`,
  unrelated to this work).

---

## 2. Getting the branch and building against it

libkalburator's remote is **Codeberg**:

```
git@codeberg.org:clintonthegeek/libkalburator.git
```

Branch name: `feature/canon-upgrade-convergence`.

### If you consume it as a sibling checkout (flat `../libkalburator` layout)

```bash
cd ../libkalburator               # the sibling dir your build expects
git fetch origin
git checkout feature/canon-upgrade-convergence
git pull
```

Then reconfigure your build so the sibling is recompiled against the new sources.

### If you consume it via CMake `FetchContent`

Point the declaration at the branch:

```cmake
FetchContent_Declare(
  libkalburator
  GIT_REPOSITORY git@codeberg.org:clintonthegeek/libkalburator.git
  GIT_TAG        feature/canon-upgrade-convergence
)
```

(Use a pinned commit SHA instead of the branch name once you want a reproducible build.)

### Build profile

The standalone/default build is:

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

Default feature flags: `KALBURATOR_HAVE_ORG_IO=OFF`, `KALBURATOR_HAVE_AKONADI=OFF`.
Nothing in this branch changes the default flag set.

---

## 3. What changed structurally (`main` → this branch)

68 commits. The directory-level shape of the library changed:

| Path | On `main` | On this branch |
|------|-----------|----------------|
| `src/transcoding/` | present (registry, router, plan, RRULE/property transcoders, **plus** two diff engines) | **deleted** |
| `src/diff/` | — | **new** — `incidencediff` + `syncdiff` relocated here (they were never "transcoding"; they are conflict/diff engines used library-wide) |
| `src/shape/` | the graph + a single-level loss model | grown: four-kind loss model, versioned canonical spine, canon-JSON envelope/differ/merger, injectable registries bundle |

New headers you may now `#include` (all additive):

- `src/shape/canonenvelope.h` — the canon JSON envelope (`_canon` metadata stamp, `uid`,
  `providerExtras` bag) + parse/serialize helpers.
- `src/shape/canonjsondiffer.h` / `canonjsonmerger.h` — reusable, domain-agnostic
  differ/merger over canon JSON (coarse per-`PropertyId` granularity; ignores
  `providerExtras` and `_canon`).
- `src/shape/shaperegistries.h` — the `ShapeRegistries` bundle (see §5).
- `src/{calendar,contacts,todo}/*canonproperties.h` — the canon property catalogues per
  domain.
- `src/{calendar,contacts,todo}/*canonstages.h` — the (de)serialization stages that bridge
  each legacy encoding to/from `+canon` (e.g. `icalcanonstages.h`, `vcardcanonstages.h`,
  `vtodocanonstages.h`, plus `orgicalcanonstages.h` for the org-mode RRULE edge).

---

## 4. The one breaking change: `TranscodingPlan` is gone

`TranscodingPlan` and the whole `src/transcoding/` machinery were deleted. Any code that
**overrides** the virtual write methods on `SyncBackend` / `SyncBackendBase` with the old
plan-carrying signature, or that `#include`s a transcoding header, will not compile.

**On `main`** (`src/calendar/syncbackend.h`) the signatures were:

```cpp
virtual void startSync(const QString &collectionId,
                       KCalendarCore::MemoryCalendar* calendar,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                       const QMap<QString, QString> &stagedDeletions,
                       const TranscodingPlan& plan = TranscodingPlan{});   // <-- removed

// pushItems had a convenience 2-arg form forwarding to a 3-arg form:
PushOperation* pushItems(const QString &calendarId,
                         const QList<KCalendarCore::Incidence::Ptr> &items);          // forwarded ...
virtual PushOperation* pushItems(const QString &calendarId,
                                 const QList<KCalendarCore::Incidence::Ptr> &items,
                                 const TranscodingPlan& plan);                         // <-- removed
```

**On this branch** the `TranscodingPlan` parameter is removed everywhere; the surviving
signatures are:

```cpp
virtual void startSync(const QString &collectionId,
                       KCalendarCore::MemoryCalendar* calendar,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
                       const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
                       const QMap<QString, QString> &stagedDeletions);

virtual PushOperation* pushItems(const QString &calendarId,
                                 const QList<KCalendarCore::Incidence::Ptr> &items);
```

Why: conversion is no longer the backend's concern. A backend declares the shape it speaks
(`nativeShapes()` / `shapeFor()`); the **engine** routes the record through the shape graph
to that shape before it reaches `pushItems`. There is nothing for a per-call plan to carry.

`storeItems` / `updateItem` (the older deprecated write entry points) are also gone — they
had already lost all in-tree callers before this branch.

---

## 5. Optional, non-breaking: injecting the registries bundle

The three shape registries are now grouped into one value type:

```cpp
// src/shape/shaperegistries.h
struct ShapeRegistries {
    TransformationRegistry   transformation;   // shapes, edges, canonical spine, compile()
    DomainRegistry           domain;           // domain definitions
    DomainOperationsRegistry operations;       // per-domain differ/merger factories
};
```

`SyncEngine` and `PluginManager` each gained a **preferred injecting constructor** that
takes `ShapeRegistries&` by reference (the composition root owns one bundle and hands the
same reference to both):

```cpp
explicit SyncEngine(BackendRegistry*, ISyncHost*, Shape::ShapeRegistries&, QObject* = nullptr);
PluginManager(Sync::BackendRegistry*, Shape::ShapeRegistries&);
```

**You do not have to adopt this.** The old constructors still exist and bind to a
process-global default bundle (`defaultShapeRegistries()`, reached by the `::instance()`
accessors). That global is documented, transitional scaffolding — it is scheduled for
removal once consumers move to the injecting constructors, but until then the old
construction path compiles and behaves exactly as before.

---

## 6. The loss-warning channel

The old `transcodingWarning` signal still exists on the backend base. After convergence the
loss information is sourced from the **composed `LossProfile` of the pipeline**, not from a
transcoding pass. The relevant API:

- `Pipeline::composedLoss()` → a `LossProfile` for the whole compiled path.
- `LossProfile` (`src/shape/lossprofile.h`) — `affected` maps each changed `PropertyId` to a
  `LossKind`:
  - `Dropped` — target cannot represent it; information is gone.
  - `Simplified` — survives in reduced form (e.g. a complex RRULE reduced to a basic rule).
  - `Reversible` — moved into an extension / `X-` property; a round-trip is lossless.
  - `Degraded` — mapped through a lossy many-to-one vocabulary; the original is kept verbatim.
- `LossProfile::summary()` → human-readable string (e.g. `"drops gender; simplifies rrule"`).
- `LossProfile::compose()` keeps the **more severe** kind per property when stacking edges.
- New on this branch: `LossProfile::losslessValues` — an optional per-property set of values
  that do **not** materialize as loss (so a value-dependent `Degraded` edge, e.g.
  `classification` where only the MS `personal` value actually degrades, does not warn for
  `public`/`private`/`confidential`).

The four-kind taxonomy is exactly what lets a transformation distinguish
"reversible-via-X-property" from "dropped" — the distinction surfaces in both the warning
text and any loss-policy gate that reads the composed profile.

---

## 7. The canon encodings, concretely

Each of the three rich canons is its own JSON model keyed by `PropertyId`, values typed per
`PropertyKind`, composites nested as JSON. It is **not** a clone of any vendor and not raw
iCal/vCard. The legacy text encodings are demoted to lossy **peer** encodings that bridge
to/from canon through registered shape edges.

- **Recurrence** is stored as raw RFC5545 text and treated by the differ as one opaque field
  (it is only parsed at vendor edges that need it).
- **Vendor-only / round-trippable fields** are stashed in the `providerExtras` bag (namespaced,
  carried verbatim, never a conflict axis) and declared `Reversible` in the loss profile —
  so a `canon → peer → canon` cycle recovers them byte-for-byte.
- The canon is behind a **versioned canonical spine**: future canon versions are appended,
  existing peer edges are never rewritten, and an unchanged peer automatically reaches the
  new head version through the registered bridge edges.

The org-mode RRULE simplification that used to live in `src/transcoding/` is now just the
shape edge `canon → org-ical` (`Simplified` loss; the original RRULE is preserved in
`X-ORIGINAL-RRULE` and restored on the way back). It is only reachable when the org backend
is built (`KALBURATOR_HAVE_ORG_IO=ON`); the edge and its loss/warning are present in the
default build, the org backend wiring is not.

---

## 8. Where to read more (inside the libkalburator repo)

The campaign that produced this branch is documented under `docs/campaign/`:

- `docs/campaign/INVARIANTS.md` — the rules the converged design holds to (chiefly: extend
  the shape graph, never fork a third mechanism).
- `docs/campaign/STATUS.md` — current state (converged), the four-plan sequence, and the
  locked design decisions ledger.
- `docs/campaign/FINDINGS.md` — open watch items and the discipline log.

The four landed plans, in order:

- `docs/2026-05-23-plan-1-shape-core-foundations.md`
- `docs/2026-05-23-plan-2-per-engine-registries.md`
- `docs/2026-05-24-plan-3-canon-encodings.md`
- `docs/2026-05-24-plan-4-calendar-convergence.md`

Design set:

- `docs/2026-05-23-canon-upgrade-and-convergence-design.md`
- `docs/2026-05-23-canon-schema-design.md`
- `docs/2026-05-23-vendor-api-shapes-reference.md`

Tests worth reading as executable specs of the new mechanism:

- `tests/shape/tst_canonical_spine.cpp` — versioned spine + append-only upgrades.
- `tests/shape/tst_loss_profile.cpp` — the four-kind compose semantics.
- `tests/shape/tst_canonjson_diff_merge.cpp` — the canon differ/merger contract.
- `tests/{contacts,todo,calendar}/tst_*_canon_roundtrip.cpp` — each domain's round-trip and
  loss contract.
- `tests/calendar/tst_orgical_canon_roundtrip.cpp` — RRULE simplification as a shape edge.

---

## 9. Status / caveats

- Full suite: **112 / 112** when the pre-existing `tst_providerlifecycle` async flake passes,
  **111 / 112** when it flakes. That flake predates this branch and is tracked as a known,
  unrelated issue.
- No live vendor (Google / Microsoft Graph) calls have been exercised; the canon
  (de)serialization is covered by synthetic round-trip tests only.
- The branch is not yet merged to `main`. Build against the branch (or a pinned commit on
  it) until the merge lands.
