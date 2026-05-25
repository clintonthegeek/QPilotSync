# Adopt libkalburator `feature/canon-upgrade-convergence` — Phase 1: Track to Green

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make WildPalms compile and pass its full test suite against libkalburator's
`feature/canon-upgrade-convergence` branch, with **zero behavior change** to any sync path.

**Architecture:** libkalburator deleted `src/transcoding/` and removed the `TranscodingPlan`
parameter from `SyncBackend::startSync` / `pushItems` (those virtuals now have default empty
bodies). The only work in this phase is repointing the build at the new branch and stripping
the removed parameter everywhere WildPalms still names it. We do **not** migrate any conduit
onto the new canon encodings here — calendar still transforms in its backend, todo/memo stay
`(blob, raw)`, contacts stays palm↔vcard4. That migration is later, separate plans (see the
Roadmap appendix).

**Tech Stack:** C++/Qt6, CMake (legacy build dir conventions — dev build dir is `build-dev`),
ctest, KCalendarCore/KContacts, libkalburator consumed via CMake `FetchContent` with a local-
source override. The four conduits (`src/plugins/{calendar,contacts,memo,todos}`) are git
**submodules** (`wildpalms-conduit-*`); backend edits are submodule commits + superproject
pointer bumps.

---

## Context the engineer needs

- **What broke and why.** On `main`, `Kalburator::Sync::SyncBackend` declared
  `startSync(...)` and `pushItems(...)` with a trailing `const TranscodingPlan& plan`
  parameter. On the canon branch that parameter is gone and the methods have default bodies.
  Every WildPalms class that overrides those methods still spells the old parameter, so it no
  longer matches the base virtual and fails to compile. `storeItems` / `updateItem` were also
  removed but WildPalms never overrode them. WildPalms includes **no** `src/transcoding/`
  headers and uses **no** `transcodingWarning` in `src/`, so nothing else breaks.

- **Two backend layers exist, both affected.**
  1. **Live plugin submodule backends** (`src/plugins/{calendar,contacts,todos}/*.h`): the
     overrides are empty no-op stubs whose only purpose was to satisfy the (formerly pure)
     base virtuals. They appear in 3 of the 4 conduits — **memo's `MemoBlobBackend` does not
     override them and needs no change.**
  2. **Legacy backends** (`src/palm/{calendar,contacts,todo,memo}/palm*backend.{h,cpp}`):
     real overrides. These libs (`WildPalmsPalmCalendar` etc.) ALSO hold live shared helpers
     (`CategoryMappingStore`, `parseCategoryAppInfo`), so **do not delete the libs** — only
     strip the parameter from the backend classes. `src/palm/calendar` is the only one that
     `#include`s `transcodingplan.h` and the only one with a real `pushItems` override + an
     internal call passing `TranscodingPlan{}`.

- **One test** passes `TranscodingPlan{}`: `tests/palmcalendar/tst_palmcalendarbackend.cpp`.

- **Build/test commands** (this project, legacy layout, dev dir `build-dev`):
  - Configure (local libkalburator override → sibling on the canon branch):
    `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`
  - Build: `cmake --build build-dev -j"$(nproc)"`
  - Test: `ctest --test-dir build-dev --output-on-failure`

- **The "test" for a compile-fix task** is the build itself: it fails (red) before the edit
  with a named error, and the file compiles (green) after. The whole-suite ctest run in
  Task 9 is the regression gate proving zero behavior change.

---

## File inventory (everything this phase touches)

| File | Change |
|------|--------|
| `../libkalburator` (sibling) | Already on `feature/canon-upgrade-convergence`; we build against it |
| `CMakeLists.txt` (superproject) | Repoint `WILDPALMS_LIBKALBURATOR_GIT_TAG` to a canon-branch SHA (Task 10) |
| `src/plugins/calendar/palmcalendarbackend.h` (submodule) | Strip `TranscodingPlan` param from `startSync` + `pushItems` stubs |
| `src/plugins/contacts/palmcontactsbackend.h` (submodule) | Same |
| `src/plugins/todos/todoblobbackend.h` (submodule) | Same |
| `src/palm/calendar/palmcalendarbackend.{h,cpp}` | Remove `#include "transcodingplan.h"`; strip param from `startSync`+`pushItems` decls/defs; drop arg from internal `pushItems(...)` call; remove `using ...TranscodingPlan;` |
| `src/palm/contacts/palmcontactsbackend.{h,cpp}` | Strip param from `startSync` decl/def |
| `src/palm/todo/palmtodobackend.{h,cpp}` | Strip param from `startSync` decl/def |
| `src/palm/memo/palmmemobackend.{h,cpp}` | Strip param from `startSync` decl/def |
| `tests/palmcalendar/tst_palmcalendarbackend.cpp` | Drop `TranscodingPlan{}` args + the `using` |

---

### Task 1: Branch setup (superproject + submodules + sibling)

**Files:** none edited; git state only.

- [ ] **Step 1: Confirm the sibling libkalburator is on the canon branch**

Run:
```bash
git -C ../libkalburator rev-parse --abbrev-ref HEAD
```
Expected: `feature/canon-upgrade-convergence`. If not:
```bash
git -C ../libkalburator fetch origin && git -C ../libkalburator checkout feature/canon-upgrade-convergence && git -C ../libkalburator pull
```

- [ ] **Step 2: Create the WildPalms superproject feature branch**

```bash
git checkout -b feature/canon-adoption-phase1
```

- [ ] **Step 3: Put each affected submodule on a working branch**

The three conduits we edit must be on a branch (not detached HEAD) so commits stick:
```bash
for s in calendar contacts todos; do
  git -C "src/plugins/$s" checkout -b feature/canon-adoption-phase1 || git -C "src/plugins/$s" checkout feature/canon-adoption-phase1
done
git -C src/plugins/calendar rev-parse --abbrev-ref HEAD   # confirm: feature/canon-adoption-phase1
```

- [ ] **Step 4: Commit the branch point (no-op marker)**

No file change yet; nothing to commit. Proceed.

---

### Task 2: Repoint the build at the canon branch and capture the RED state

**Files:** none edited; this establishes the failing build that the next tasks fix.

- [ ] **Step 1: Configure against the sibling canon checkout**

Run:
```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)
```
Expected: configures successfully; status line `libkalburator: using local source at .../libkalburator`.

- [ ] **Step 2: Build and confirm it fails on the removed parameter**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep -iE "TranscodingPlan|marked 'override'|no member named" | head
```
Expected: compile errors referencing `TranscodingPlan` and/or "method does not override a base
class method" in the plugin and `src/palm/*` backends. This is the RED state we resolve.

---

### Task 3: Strip `TranscodingPlan` from the calendar plugin backend (submodule)

**Files:**
- Modify: `src/plugins/calendar/palmcalendarbackend.h` (the `startSync` and `pushItems` stubs)

- [ ] **Step 1: Edit the stubs**

Replace this block:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }
```
with:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &) override { return nullptr; }
```

- [ ] **Step 2: Rebuild and confirm this file's errors are gone**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep "plugins/calendar/palmcalendarbackend.h" | head
```
Expected: no output (no remaining errors in this file).

- [ ] **Step 3: Commit inside the submodule**

```bash
git -C src/plugins/calendar add palmcalendarbackend.h
git -C src/plugins/calendar commit -m "fix: drop removed TranscodingPlan param from SyncBackend stubs

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: Strip `TranscodingPlan` from the contacts plugin backend (submodule)

**Files:**
- Modify: `src/plugins/contacts/palmcontactsbackend.h`

- [ ] **Step 1: Edit the stubs**

Replace this block:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }
```
with:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &) override { return nullptr; }
```

- [ ] **Step 2: Rebuild and confirm this file's errors are gone**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep "plugins/contacts/palmcontactsbackend.h" | head
```
Expected: no output.

- [ ] **Step 3: Commit inside the submodule**

```bash
git -C src/plugins/contacts add palmcontactsbackend.h
git -C src/plugins/contacts commit -m "fix: drop removed TranscodingPlan param from SyncBackend stubs

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: Strip `TranscodingPlan` from the todos plugin backend (submodule)

**Files:**
- Modify: `src/plugins/todos/todoblobbackend.h`

- [ ] **Step 1: Edit the stubs**

Replace this block:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &,
                   const Kalburator::Sync::TranscodingPlan &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &,
        const Kalburator::Sync::TranscodingPlan &) override { return nullptr; }
```
with:
```cpp
    void startSync(const QString &,
                   KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override {}
    void removeItem(const QString &, const QString &) override {}
    Kalburator::Sync::PushOperation *pushItems(
        const QString &,
        const QList<KCalendarCore::Incidence::Ptr> &) override { return nullptr; }
```

- [ ] **Step 2: Rebuild and confirm this file's errors are gone**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep "plugins/todos/todoblobbackend.h" | head
```
Expected: no output.

- [ ] **Step 3: Commit inside the submodule**

```bash
git -C src/plugins/todos add todoblobbackend.h
git -C src/plugins/todos commit -m "fix: drop removed TranscodingPlan param from SyncBackend stubs

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 6: Strip `TranscodingPlan` from the legacy `src/palm/calendar` backend

This is the heaviest of the legacy four: it includes the deleted header, has a real `pushItems`
override, and calls `pushItems(..., TranscodingPlan{})` internally.

**Files:**
- Modify: `src/palm/calendar/palmcalendarbackend.h`
- Modify: `src/palm/calendar/palmcalendarbackend.cpp`

- [ ] **Step 1: Remove the deleted include (`.h` line ~5)**

Delete this line from `src/palm/calendar/palmcalendarbackend.h`:
```cpp
#include "transcodingplan.h"
```

- [ ] **Step 2: Strip the param from the `startSync` declaration (`.h`)**

Change the `startSync` declaration's last parameter line from:
```cpp
        const Kalburator::Sync::TranscodingPlan &plan) override;
```
to:
```cpp
        const QMap<QString, QString> &stagedDeletions) override;
```
(i.e. the previous parameter — `stagedDeletions` — becomes the last one; delete the
`TranscodingPlan` line. Match the base signature in
`../libkalburator/src/calendar/syncbackend.h:150-154`.)

- [ ] **Step 3: Strip the param from the `pushItems` declaration (`.h`)**

Change the `pushItems` declaration's last parameter line from:
```cpp
        const Kalburator::Sync::TranscodingPlan &plan) override;
```
to close the parameter list on the previous (`items`) parameter:
```cpp
        const QList<KCalendarCore::Incidence::Ptr> &items) override;
```

- [ ] **Step 4: Remove the `using` and strip the definitions (`.cpp`)**

Delete this line near the top of `src/palm/calendar/palmcalendarbackend.cpp`:
```cpp
using Kalburator::Sync::TranscodingPlan;
```
In the `startSync` definition, delete the trailing parameter line
`const Kalburator::Sync::TranscodingPlan &plan)` and close the list on `stagedDeletions`.
In the `pushItems` definition, delete the trailing `const TranscodingPlan &plan` parameter
and close the list on `items`.

- [ ] **Step 5: Drop the argument from the internal `pushItems` call (`.cpp`)**

Change:
```cpp
        auto *op = pushItems(calendarIdForSlot(it.key()), it.value(), TranscodingPlan{});
```
to:
```cpp
        auto *op = pushItems(calendarIdForSlot(it.key()), it.value());
```

- [ ] **Step 6: Rebuild and confirm this file's errors are gone**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep "palm/calendar/palmcalendarbackend" | head
```
Expected: no output.

- [ ] **Step 7: Commit (superproject working tree)**

```bash
git add src/palm/calendar/palmcalendarbackend.h src/palm/calendar/palmcalendarbackend.cpp
git commit -m "fix(palm-calendar): drop removed TranscodingPlan from legacy backend

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 7: Strip `TranscodingPlan` from legacy contacts/todo/memo backends

These three have only a `startSync` override taking the plan (no `pushItems`, no include, no
internal call). The edit is identical in each.

**Files:**
- Modify: `src/palm/contacts/palmcontactsbackend.{h,cpp}`
- Modify: `src/palm/todo/palmtodobackend.{h,cpp}`
- Modify: `src/palm/memo/palmmemobackend.{h,cpp}`

- [ ] **Step 1: In each `.h`, strip the `startSync` param**

In each header, the `startSync` declaration's last parameter line reads:
```cpp
                   const Kalburator::Sync::TranscodingPlan &) override;
```
Delete that line and close the list on the previous parameter so the declaration matches the
base (`stagedDeletions` becomes last). For contacts the declaration spans several lines; for
todo and memo it is the compact form:
```cpp
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override;
```

- [ ] **Step 2: In each `.cpp`, strip the `startSync` definition param**

Each definition ends with `const Kalburator::Sync::TranscodingPlan &) {}`. Remove that trailing
parameter so the body reads, e.g. for todo/memo:
```cpp
void PalmToDoBackend::startSync(const QString &, KCalendarCore::MemoryCalendar *,
                                const QList<KCalendarCore::Incidence::Ptr> &,
                                const QList<KCalendarCore::Incidence::Ptr> &,
                                const QMap<QString, QString> &) {}
```
(`PalmMemoBackend::startSync` is identical; `PalmContactsBackend::startSync` is the multi-line
form — same removal of the final `TranscodingPlan` parameter.)

- [ ] **Step 3: Rebuild and confirm these files' errors are gone**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | grep -E "palm/(contacts|todo|memo)/palm" | head
```
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add src/palm/contacts/palmcontactsbackend.h src/palm/contacts/palmcontactsbackend.cpp \
        src/palm/todo/palmtodobackend.h src/palm/todo/palmtodobackend.cpp \
        src/palm/memo/palmmemobackend.h src/palm/memo/palmmemobackend.cpp
git commit -m "fix(palm): drop removed TranscodingPlan from legacy contacts/todo/memo backends

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8: Fix the test that constructs `TranscodingPlan{}`

**Files:**
- Modify: `tests/palmcalendar/tst_palmcalendarbackend.cpp`

- [ ] **Step 1: Remove the `using` (line ~145)**

Delete:
```cpp
using Kalburator::Sync::TranscodingPlan;
```

- [ ] **Step 2: Drop the `TranscodingPlan{}` argument at all five call sites**

At each `pushItems(...)` call (lines ~229, ~261, ~287, ~345, ~375) remove the trailing
`TranscodingPlan{}` argument. Each call currently ends like:
```cpp
                                 TranscodingPlan{});
```
Change it so the previous argument is last, e.g.:
```cpp
    auto *op = backend.pushItems(calId, items);
```
(Adjust to each call's actual prior argument; the mechanical change is "delete the final
`, TranscodingPlan{}` argument".)

- [ ] **Step 3: Build the test and confirm it compiles**

Run:
```bash
cmake --build build-dev -j"$(nproc)" --target tst_palmcalendarbackend 2>&1 | tail -5
```
Expected: builds with no `TranscodingPlan` errors.

- [ ] **Step 4: Run the test**

Run:
```bash
ctest --test-dir build-dev -R palmcalendarbackend --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/palmcalendar/tst_palmcalendarbackend.cpp
git commit -m "test(palm-calendar): drop removed TranscodingPlan arg from pushItems calls

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8b: Port contacts loss-builders to the four-kind LossProfile API

**Discovered during execution (2026-05-24).** The canon branch made a **second** breaking
change the handoff did not flag: `Kalburator::Shape::LossProfile` dropped its `level` field
(`LossLevel` enum) and `dropped` set in favor of `QHash<PropertyId, LossKind> affected`
(`LossKind{Dropped, Simplified, Reversible, Degraded}`). This is additive for a pure *consumer*
of composed loss, but WildPalms's contacts stage *constructs* `LossProfile`s, so it breaks.
Blast radius is exactly one file (no tests reference the old API). Faithful port, zero behavior
change: every old `dropped` member → `affected[prop] = LossKind::Dropped`; old `Lossless` →
empty profile.

**Files:**
- Modify: `src/plugins/contacts/palmtovcardtransformation.cpp` (submodule)

- [ ] **Step 1: Replace the two loss-builder bodies**

Replace:
```cpp
LossProfile vcardToPalmLoss()
{
    LossProfile p;
    p.level = LossLevel::IntraDomainLossy;
    p.dropped.insert(PropertyId{QStringLiteral("photo")});
    p.dropped.insert(PropertyId{QStringLiteral("anniversary")});
    p.dropped.insert(PropertyId{QStringLiteral("kind")});
    p.dropped.insert(PropertyId{QStringLiteral("member")});
    p.dropped.insert(PropertyId{QStringLiteral("lang")});
    p.dropped.insert(PropertyId{QStringLiteral("gender")});
    p.dropped.insert(PropertyId{QStringLiteral("related")});
    p.dropped.insert(PropertyId{QStringLiteral("geo")});
    p.dropped.insert(PropertyId{QStringLiteral("tz")});
    p.dropped.insert(PropertyId{QStringLiteral("x-custom")});
    return p;
}

LossProfile palmToVCardLoss()
{
    LossProfile p;
    p.level = LossLevel::Lossless;
    return p;
}
```
with:
```cpp
LossProfile vcardToPalmLoss()
{
    LossProfile p;
    // Palm AddressDB has no slot for these vCard 4.0 properties — genuinely dropped.
    // (Richer Reversible/providerExtras treatment is Phase 2, not track-to-green.)
    p.affected.insert(PropertyId{QStringLiteral("photo")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("anniversary")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("kind")},        LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("member")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("lang")},        LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("gender")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("related")},     LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("geo")},         LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("tz")},          LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("x-custom")},    LossKind::Dropped);
    return p;
}

LossProfile palmToVCardLoss()
{
    return {};  // lossless: palm -> vcard4 preserves everything via X-WP-PALM-* stamps
}
```

- [ ] **Step 2: Build the contacts plugin and confirm it compiles**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | grep -iE "palmtovcardtransformation|LossProfile|LossLevel" | head`
Expected: no output.

- [ ] **Step 3: Commit inside the contacts submodule**

```bash
git -C src/plugins/contacts add palmtovcardtransformation.cpp
git -C src/plugins/contacts commit -m "fix: port loss-builders to four-kind LossProfile API

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 9: Full build + full suite green (the zero-regression gate)

**Files:** none edited.

- [ ] **Step 1: Clean build from scratch against the canon branch**

Run:
```bash
cmake --build build-dev -j"$(nproc)" 2>&1 | tail -15
```
Expected: build completes; no errors. If any `TranscodingPlan` / override errors remain, return
to the matching task above.

- [ ] **Step 2: Run the entire test suite**

Run:
```bash
ctest --test-dir build-dev --output-on-failure 2>&1 | tail -25
```
Expected: all tests pass. (Compare the pass count to the pre-adoption baseline on `main` — it
must not drop. This proves zero behavior change.)

- [ ] **Step 3: Verify the contacts shape path still composes (palm → vcard4 → canon)**

Contacts is the one domain already on the shape graph; its canonical likely moved from
`(contacts, vcard4)` to a `+canon` encoding with `vcard4` demoted to a peer. The contacts
plugin tests exercise this. Confirm they pass specifically:
```bash
ctest --test-dir build-dev -R contacts --output-on-failure 2>&1 | tail -20
```
Expected: PASS. If the engine reports "pipeline unavailable" / "to-shape not registered" for
contacts, the palm↔vcard4 edge no longer reaches the canonical hub — STOP and record it; that is
a Phase 2 (contacts re-point) finding, not a Phase 1 fix. Note it in the handoff and proceed
only if green.

---

### Task 10: Pin to the canon SHA and bump submodule pointers

**Files:**
- Modify: `CMakeLists.txt` (superproject)
- Superproject gitlinks for the three edited submodules

- [ ] **Step 1: Capture the canon-branch commit SHA**

Run:
```bash
git -C ../libkalburator rev-parse HEAD
```
Record the SHA (call it `<CANON_SHA>`).

- [ ] **Step 2: Repoint the FetchContent tag**

In `CMakeLists.txt`, change the default tag line:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.54-mass-delete-guard" CACHE STRING
```
to the captured SHA:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "<CANON_SHA>" CACHE STRING
```
(Pin to the SHA, not the branch name, for reproducibility. Swap to a release tag once the canon
branch merges to libkalburator `main`.)

- [ ] **Step 3: Verify a clean FetchContent build (no local override) also works**

Run:
```bash
cmake -S . -B build-canon-verify -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-canon-verify -j"$(nproc)" 2>&1 | tail -5
```
Expected: fetches `<CANON_SHA>` from Codeberg and builds clean. (Delete `build-canon-verify`
after; it's a one-off check.)

- [ ] **Step 4: Stage submodule pointer bumps + the CMake change and commit**

```bash
git add CMakeLists.txt src/plugins/calendar src/plugins/contacts src/plugins/todos
git status   # confirm the three gitlinks show "new commits" and CMakeLists.txt is modified
git commit -m "build: adopt libkalburator canon-convergence branch (Phase 1, track-to-green)

Strip removed TranscodingPlan param from all SyncBackend overrides; pin libkalburator
to <CANON_SHA> on feature/canon-upgrade-convergence. No behavior change: calendar still
transforms in-backend, todo/memo still blob/raw, contacts still palm<->vcard4.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 5: Push superproject + submodule branches**

```bash
for s in calendar contacts todos; do git -C "src/plugins/$s" push -u origin feature/canon-adoption-phase1; done
git push -u origin feature/canon-adoption-phase1
```

---

## Phase 1 done when

- WildPalms builds against the canon branch both via local-source override and via pinned-SHA
  FetchContent.
- Full ctest suite green, pass count ≥ the `main` baseline.
- Contacts shape path verified composing to the new canonical (Task 9 Step 3).
- Submodule pointers + CMake pin committed and pushed.
- No conduit behavior changed.

---

## Roadmap appendix — later phases (each its own plan)

These are **not** in this plan. Each should get its own `docs/superpowers/plans/` document,
written against the confirmed in-tree canon API **after Phase 1 lands**, and shipped as its own
PR gated on the matching `tst_*_canon_roundtrip.cpp` contract in libkalburator.

- **Phase 2 — Contacts re-point + providerExtras.** Re-target the existing `ContactsDomainExtension`
  edges at the `+canon` encoding (or confirm peer-bridging suffices), and move the `X-WP-PALM-*`
  round-trip stamps (RECORDID, CATEGORY-SLOT, SECRET) into the canon `providerExtras` bag declared
  `Reversible`. Mostly verification + a representation swap.
- **Phase 3 — Calendar onto canon (highest value).** Move `DatebookCodec` from in-backend
  conversion into a `(calendar, palm) ↔ canon` shape stage with an honest four-kind `LossProfile`;
  flip `PalmCalendarBackend::nativeShapes()` to `(calendar, palm)`. Retires calendar's legacy
  in-backend transform; recurrence now rides the rich canon.
- **Phase 4 — ToDo onto canon.** Mirror contacts: `(todo, palm)` native + edges to
  `(todo, +canon)` using the existing `todoicstranscoder` logic moved into a stage; flip
  `nativeShapes()` off `(blob, raw)`.
- **Phase 5 — Memo onto canon.** Decide whether Markdown+frontmatter stays a WP-specific peer
  encoding or maps to canon text; type the backend accordingly.
- **Phase 6 — Loss-policy + warning UX.** Surface `Pipeline::composedLoss()`/`summary()` in the
  sync UI and honor `SyncMapping.lossPolicy` (Abort/Warn/Proceed), using `losslessValues` to
  avoid value-dependent false warnings.
- **Cleanups.** Adopt the `ShapeRegistries` injecting constructors (drop the process-global
  default before libkalburator removes it); re-pin to the merged release tag; assess whether the
  legacy `src/palm/*` backend classes (now param-stripped) are fully dead and removable.

**Do not ship any of Phases 2-6 to users until libkalburator's canon branch merges to `main`
and each migrated domain is device-verified (POSE64 / real hardware HotSync) — the canon
(de)serialization has synthetic round-trip coverage only, no live-vendor testing.**

---

## Self-review notes

- **Spec coverage:** the one breaking change (TranscodingPlan removal) is covered for all 4
  affected backend layers (3 live submodules + 4 legacy `src/palm`) and the 1 affected test;
  memo's submodule backend confirmed not to need a change; the optional `ShapeRegistries`
  adoption is correctly deferred (non-breaking). Build repoint + SHA pin + submodule bumps
  covered.
- **No behavior change:** every edit is a parameter removal against a base method that now
  defaults; no logic touched. The Task 9 suite run + pass-count comparison is the gate.
- **Type consistency:** the stripped signatures match the canon base in
  `../libkalburator/src/calendar/syncbackend.h:150-170` (`startSync` ends on `stagedDeletions`;
  `pushItems` ends on `items`).
- **Multi-repo discipline:** submodule edits are committed in-submodule (Tasks 3-5) and the
  pointers bumped in the superproject (Task 10); legacy `src/palm` edits are plain superproject
  commits (Tasks 6-7).
