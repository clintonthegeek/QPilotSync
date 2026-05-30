# WP-side libkalburator P3 port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make WildPalms compile + test green against libkalburator's post-P3 (`neutralize-sync-core`) API by adding two `#include`s, bridging two `ISyncHost` impls via `dynamic_cast`, and reparenting six non-calendar WP backends from calendar `SyncBackend` to neutral `SyncBackendBase`.

**Architecture:** Layer 1 (Task 1) lands compile fixes so subsequent tasks can iterate against a building tree. Layer 2 (Tasks 2–7) reparents one backend per task — each is an isolated file pair (`.h` + `.cpp`) inside either the superproject or one plugin submodule. Task 8 verifies the suite against both the local lib (build-dev) and a scratch FetchContent v0.60 build (build-rcheck) for backwards-compat.

**Tech Stack:** C++17, Qt6, KCalendarCore (being deleted from non-calendar headers), CMake, libkalburator (FetchContent or `WILDPALMS_LIBKALBURATOR_SOURCE_DIR`).

**Spec:** `docs/superpowers/specs/2026-05-28-wp-libkalburator-p3-port-design.md`

---

## File map

### Modified files

| Path | Repo | Task |
|---|---|---|
| `src/palm/calendar/palmcalendarbackend.h` | superproject | 1 |
| `src/plugins/calendar/palmcalendarbackend.h` | calendar submodule | 1 |
| `src/runtime/palmruntime.cpp` (lines 79–100) | superproject | 1 |
| `src/runtime/synchost_wp.cpp` (lines 21–29) | superproject | 1 |
| `src/palm/contacts/palmcontactsbackend.{h,cpp}` | superproject | 2 |
| `src/palm/memo/palmmemobackend.{h,cpp}` | superproject | 3 |
| `src/palm/todo/palmtodobackend.{h,cpp}` | superproject | 4 |
| `src/plugins/contacts/palmcontactsbackend.{h,cpp}` | contacts submodule | 5 |
| `src/plugins/memo/memoblobbackend.{h,cpp}` | memo submodule | 6 |
| `src/plugins/todos/todoblobbackend.{h,cpp}` | todos submodule | 7 |

### New files

**None.** Pure subtraction (deleting calendar stubs) + include adjustments.

---

## Task 1 — Compile fixes (palm calendar `#include` + 2 `ISyncHost` bridges)

**Files:**
- Modify: `src/palm/calendar/palmcalendarbackend.h` (line 4 area — superproject)
- Modify: `src/plugins/calendar/palmcalendarbackend.h` (calendar submodule)
- Modify: `src/runtime/palmruntime.cpp` (lines 86–95, the `PalmSyncHost::backendById` and `backends` overrides)
- Modify: `src/runtime/synchost_wp.cpp` (lines 21–29, the `SyncHost_WP::backendById` and `backends` impls)

- [ ] **Step 1: Verify the failing build** (no test changes needed; the compiler is the test)

Run: `cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep -v fpermissive | sort -u`

Expected: 5 errors:
- `src/palm/calendar/palmcalendarbackend.h:75` invalid covariant return for `fetchItems`
- `src/palm/calendar/palmcalendarbackend.h:80` invalid covariant return for `deleteItems`
- `src/runtime/palmruntime.cpp:93` `insert(QString, SyncBackendBase*)` no matching function
- (autogen extraction duplicates of the first two)

- [ ] **Step 2: Add `#include "syncoperation.h"` to `src/palm/calendar/palmcalendarbackend.h`**

In the include block at the top of the header (currently line 4 has `#include "syncbackend.h"`), add immediately after it:

```cpp
#include "syncoperation.h"   // Kalburator::Sync::{Fetch,Push,Delete}Operation complete-type for covariant overrides
```

- [ ] **Step 3: Add the same include to `src/plugins/calendar/palmcalendarbackend.h`** (calendar submodule)

Same change as Step 2. This is inside `src/plugins/calendar/` which is a git submodule on branch `feature/canon-adoption-phase1`.

- [ ] **Step 4: Replace `PalmSyncHost::backendById` and `backends` in `src/runtime/palmruntime.cpp` lines 86–95**

The current block is:

```cpp
    Kalburator::Sync::SyncBackend* backendById(const QString &id) override {
        return m_registry ? m_registry->backendInstance(id) : nullptr;
    }
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override {
        QHash<QString, Kalburator::Sync::SyncBackend*> result;
        if (!m_registry) return result;
        for (const QString &id : m_registry->registeredInstanceIds())
            result.insert(id, m_registry->backendInstance(id));
        return result;
    }
```

Replace with:

```cpp
    Kalburator::Sync::SyncBackend* backendById(const QString &id) override {
        // Lib P3 widened backendInstance() to SyncBackendBase*; ISyncHost is a
        // calendar-domain interface (lives in calendar/), so dynamic_cast filters
        // out non-calendar backends. The engine fetches non-calendar backends
        // directly from BackendRegistry via SyncBackendBase* post-P3.
        if (!m_registry) return nullptr;
        return dynamic_cast<Kalburator::Sync::SyncBackend*>(
            m_registry->backendInstance(id));
    }
    QHash<QString, Kalburator::Sync::SyncBackend*> backends() override {
        QHash<QString, Kalburator::Sync::SyncBackend*> result;
        if (!m_registry) return result;
        for (const QString &id : m_registry->registeredInstanceIds()) {
            if (auto *cb = dynamic_cast<Kalburator::Sync::SyncBackend*>(
                    m_registry->backendInstance(id))) {
                result.insert(id, cb);
            }
        }
        return result;
    }
```

- [ ] **Step 5: Replace `SyncHost_WP::backendById` and `backends` in `src/runtime/synchost_wp.cpp` lines 21–29**

The current block stores backends in a member `QHash<QString, SyncBackend*> m_backends` populated via `registerBackend(QString, SyncBackend*)`. Since `registerBackend` takes a typed `SyncBackend*`, the m_backends hash is already filtered at insertion. **No dynamic_cast needed here** — the bug is only at the registry seam, not at registerBackend. But check whether anyone calls `SyncHost_WP::registerBackend` with a `SyncBackendBase*`:

```bash
grep -rn "registerBackend(" src/ 2>/dev/null | grep -v synchost_wp
```

If callers pass `SyncBackendBase*` (e.g. from `BackendRegistry::backendInstance`), update the `registerBackend` signature OR convert at the call site. Most likely callers pass already-typed `SyncBackend*` (calendar backends only), so no change needed.

Run the build to confirm: `cmake --build build-dev -j$(nproc) 2>&1 | grep "synchost_wp.cpp" | grep error`. If clean → no change needed in this step; remove this step from the plan.

If `synchost_wp.cpp` errors DO appear, apply the same dynamic_cast pattern as Step 4:

```cpp
// Adjust registerBackend's signature to accept SyncBackendBase* and dynamic_cast:
void SyncHost_WP::registerBackend(const QString &id, Kalburator::Sync::SyncBackendBase *backend)
{
    if (id.isEmpty() || !backend) return;
    if (auto *cb = dynamic_cast<Kalburator::Sync::SyncBackend*>(backend))
        m_backends.insert(id, cb);
}
```

- [ ] **Step 6: Verify the build is clean**

Run: `cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep -v fpermissive | head -5`

Expected: no output (no errors).

- [ ] **Step 7: Run the full test suite**

Run: `ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10`

Expected: `100% tests passed, 0 tests failed out of 117`.

- [ ] **Step 8: Commit inside the calendar submodule first**

```bash
cd src/plugins/calendar
git add palmcalendarbackend.h
git commit -m "fix: include syncoperation.h for FetchOperation/DeleteOperation covariant overrides

Lib P3 (neutralize-sync-core) moved fetchItems/deleteItems onto
SyncBackendBase returning SyncOperation*. WP's PalmCalendarBackend
overrides return FetchOperation*/DeleteOperation* (covariant) which
requires those types to be complete at the override point. The
calendar SyncBackend header no longer pulls in calendar/syncoperation.h
transitively, so include it directly.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd -
```

- [ ] **Step 9: Commit in the superproject**

```bash
git add src/palm/calendar/palmcalendarbackend.h \
        src/runtime/palmruntime.cpp \
        src/runtime/synchost_wp.cpp \
        src/plugins/calendar
git commit -m "fix(runtime,palm): port to lib P3 sync-core neutralization

Three changes to make WP build against the post-P3 libkalburator:
- src/palm/calendar/palmcalendarbackend.h: include syncoperation.h so
  FetchOperation/DeleteOperation are complete types for the covariant
  fetchItems/deleteItems overrides
- PalmSyncHost (palmruntime.cpp): dynamic_cast registry entries from
  the now-neutral SyncBackendBase* down to SyncBackend* at the
  ISyncHost boundary; non-calendar backends are silently filtered
- SyncHost_WP (synchost_wp.cpp): mirror the same bridge (or no change
  if registerBackend's callers already type-bound their input)

Calendar submodule gitlink bumped for the matching include addition.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2 — Reparent `src/palm/contacts/palmcontactsbackend` to `SyncBackendBase`

**Files:**
- Modify: `src/palm/contacts/palmcontactsbackend.h` (superproject)
- Modify: `src/palm/contacts/palmcontactsbackend.cpp` (superproject)

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 26):

```cpp
class PalmContactsBackend : public Kalburator::Sync::SyncBackendBase
```

Delete the entire "Calendar discovery (stubs)" block (lines 58–69):

```cpp
    // --- Calendar discovery (stubs) ---
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(
        const QString &collectionId,
        const QList<KCalendarCore::MemoryCalendar *> &calendars) override;
    void startSync(
        const QString &collectionId,
        KCalendarCore::MemoryCalendar *calendar,
        const QList<KCalendarCore::Incidence::Ptr> &stagedCreations,
        const QList<KCalendarCore::Incidence::Ptr> &stagedUpdates,
        const QMap<QString, QString> &stagedDeletions) override;
    void removeItem(const QString &calId, const QString &itemUid) override;
```

(Remove the section comment too.)

- [ ] **Step 2: Edit the implementation**

In `src/palm/contacts/palmcontactsbackend.cpp`, delete the bodies of `loadCalendars`, `storeCalendars`, `startSync`, `removeItem` (whatever stubs are there — likely `{}` empty bodies or `qCDebug` no-ops). Use grep to find them:

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem" src/palm/contacts/palmcontactsbackend.cpp
```

Delete each function from the cpp.

If after deletion no `KCalendarCore/*` types are referenced in the cpp, also remove the includes:

```bash
grep -n "KCalendarCore" src/palm/contacts/palmcontactsbackend.cpp
```

If empty, remove `#include <KCalendarCore/...>` lines.

- [ ] **Step 3: Build**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep palmcontactsbackend | head -5
```

Expected: no output (no errors in this file).

If errors appear (e.g. a method declared in header but not in cpp, or vice versa), reconcile.

- [ ] **Step 4: Run targeted tests, then full suite**

```bash
ctest --test-dir build-dev -R "(palmcontacts|palm_runtime|tst_hubcontactsreader|contact_view_reads_hub)" --output-on-failure
```

Expected: all selected tests pass.

```bash
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: `100% tests passed, 0 tests failed out of 117`.

- [ ] **Step 5: Commit**

```bash
git add src/palm/contacts/palmcontactsbackend.h src/palm/contacts/palmcontactsbackend.cpp
git commit -m "refactor(palm/contacts): reparent PalmContactsBackend to SyncBackendBase

Mirrors lib P3.T5: non-calendar backends inherit the neutral base.
Drops loadCalendars/storeCalendars/startSync/removeItem stubs that
satisfied the calendar SyncBackend pure-virtuals — engine reaches
this backend through dispatchBlobSync (IBlobBackend methods only).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3 — Reparent `src/palm/memo/palmmemobackend` to `SyncBackendBase`

**Files:**
- Modify: `src/palm/memo/palmmemobackend.h` (superproject)
- Modify: `src/palm/memo/palmmemobackend.cpp` (superproject)

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 18):

```cpp
class PalmMemoBackend : public Kalburator::Sync::SyncBackendBase
```

Delete lines 48–55 (calendar stubs):

```cpp
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override;
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override;
    void removeItem(const QString &, const QString &) override;
```

- [ ] **Step 2: Edit the implementation**

In `src/palm/memo/palmmemobackend.cpp`, find and delete the bodies:

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem" src/palm/memo/palmmemobackend.cpp
```

Delete each. Check for now-unused KCalendarCore includes:

```bash
grep -n "KCalendarCore" src/palm/memo/palmmemobackend.cpp
```

Remove any whose only usage was the deleted stubs.

- [ ] **Step 3: Build**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep palmmemobackend | head -5
```

Expected: no output.

- [ ] **Step 4: Run targeted + full tests**

```bash
ctest --test-dir build-dev -R "(palmmemo|palm_runtime|tst_hubmemoreader|memo_view_reads_hub)" --output-on-failure
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 117/117 pass.

- [ ] **Step 5: Commit**

```bash
git add src/palm/memo/palmmemobackend.h src/palm/memo/palmmemobackend.cpp
git commit -m "refactor(palm/memo): reparent PalmMemoBackend to SyncBackendBase

Mirrors lib P3.T5. Drops calendar pure-virtual stubs.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4 — Reparent `src/palm/todo/palmtodobackend` to `SyncBackendBase`

**Files:**
- Modify: `src/palm/todo/palmtodobackend.h` (superproject)
- Modify: `src/palm/todo/palmtodobackend.cpp` (superproject)

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 18):

```cpp
class PalmToDoBackend : public Kalburator::Sync::SyncBackendBase
```

Delete lines 48–55 (calendar stubs):

```cpp
    void loadCalendars(const QString &collectionId) override;
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override;
    void startSync(const QString &, KCalendarCore::MemoryCalendar *,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QList<KCalendarCore::Incidence::Ptr> &,
                   const QMap<QString, QString> &) override;
    void removeItem(const QString &, const QString &) override;
```

- [ ] **Step 2: Edit the implementation**

In `src/palm/todo/palmtodobackend.cpp`:

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem" src/palm/todo/palmtodobackend.cpp
```

Delete each body. Check `KCalendarCore` includes:

```bash
grep -n "KCalendarCore" src/palm/todo/palmtodobackend.cpp
```

Remove any whose only usage was the deleted stubs.

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep palmtodobackend | head -5
```

Expected: no output.

```bash
ctest --test-dir build-dev -R "(palmtodo|palm_runtime|tst_hubtodoreader|task_view_reads_hub)" --output-on-failure
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 117/117 pass.

- [ ] **Step 4: Commit**

```bash
git add src/palm/todo/palmtodobackend.h src/palm/todo/palmtodobackend.cpp
git commit -m "refactor(palm/todo): reparent PalmToDoBackend to SyncBackendBase

Mirrors lib P3.T5. Drops calendar pure-virtual stubs.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5 — Reparent `src/plugins/contacts/palmcontactsbackend` (contacts submodule)

**Files:**
- Modify: `src/plugins/contacts/palmcontactsbackend.h` (contacts submodule)
- Modify: `src/plugins/contacts/palmcontactsbackend.cpp` (contacts submodule)

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 13):

```cpp
class PalmContactsBackend final : public Kalburator::Sync::SyncBackendBase
```

Delete lines 59–71 (the inline calendar stub block):

```cpp
    // SyncBackend calendar pure-virtuals — stubs; dispatchBlobSync never calls these
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
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

- [ ] **Step 2: Edit the implementation**

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem\|::pushItems" src/plugins/contacts/palmcontactsbackend.cpp
```

Most stubs were inline in the header; the cpp probably has no bodies for these. If any are present, delete them.

Check `KCalendarCore` includes:

```bash
grep -n "KCalendarCore" src/plugins/contacts/palmcontactsbackend.cpp
```

Remove any whose only usage was the deleted stubs.

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep "plugins/contacts" | head -5
```

Expected: no output.

```bash
ctest --test-dir build-dev -R "(contactsbackendplugin|palm_runtime|contact_view_reads_hub|tst_hubcontactsreader)" --output-on-failure
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 117/117 pass.

- [ ] **Step 4: Commit in submodule**

```bash
cd src/plugins/contacts
git add palmcontactsbackend.h palmcontactsbackend.cpp
git commit -m "refactor: reparent PalmContactsBackend to SyncBackendBase

Mirrors lib P3.T5. Drops the inline calendar stubs (loadCalendars /
storeCalendars / startSync / removeItem / pushItems-with-Incidence::Ptr)
that satisfied the calendar SyncBackend pure-virtuals — engine reaches
this backend through dispatchBlobSync (IBlobBackend methods only).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd -
```

- [ ] **Step 5: Bump gitlink in superproject**

```bash
git add src/plugins/contacts
git commit -m "build(submodule): bump contacts plugin to reparented PalmContactsBackend

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6 — Reparent `src/plugins/memo/memoblobbackend` (memo submodule)

**Files:**
- Modify: `src/plugins/memo/memoblobbackend.h` (memo submodule)
- Modify: `src/plugins/memo/memoblobbackend.cpp` (memo submodule)

**Note:** Per the header's existing comment ("K.4: Calendar virtuals on SyncBackend are no-ops by default"), MemoBlobBackend does NOT override the calendar pure-virtuals — it relies on the base's defaults. So the reparent is just the inheritance change and include swap; no stub deletions needed.

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 28):

```cpp
class MemoBlobBackend : public Kalburator::Sync::SyncBackendBase
```

Update the comment block (lines 25-27) describing the inheritance — replace:

```cpp
 * K.8b: lifted from IBlobBackend to SyncBackend directly (Task 3).
 * Calendar virtuals on SyncBackend are no-ops by default (K.4).
 */
```

with:

```cpp
 * K.8b: lifted from IBlobBackend to SyncBackend directly (Task 3).
 * P3 (2026-05-28): reparented to SyncBackendBase — the calendar
 * virtuals are gone, not just no-op. dispatchBlobSync reaches us
 * through the neutral IBlobBackend methods.
 */
```

- [ ] **Step 2: Edit the implementation**

Verify no calendar override bodies exist in the cpp (memo never had explicit ones per the comment):

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem" src/plugins/memo/memoblobbackend.cpp
```

Expected: no output. If anything matches, delete it.

Check `KCalendarCore` includes:

```bash
grep -n "KCalendarCore" src/plugins/memo/memoblobbackend.cpp
```

Remove any that exist (memo is markdown-based, not iCal).

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep "plugins/memo" | head -5
```

Expected: no output.

```bash
ctest --test-dir build-dev -R "(memoblob|memobackendplugin|palm_runtime|memo_view_reads_hub|tst_hubmemoreader)" --output-on-failure
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 117/117 pass.

- [ ] **Step 4: Commit in submodule**

```bash
cd src/plugins/memo
git add memoblobbackend.h memoblobbackend.cpp
git commit -m "refactor: reparent MemoBlobBackend to SyncBackendBase

Mirrors lib P3.T5. Memo never overrode the calendar pure-virtuals
(relied on K.4 no-op defaults); the reparent is the inheritance
swap + include adjustment. dispatchBlobSync reaches the backend
through the neutral IBlobBackend surface.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd -
```

- [ ] **Step 5: Bump gitlink in superproject**

```bash
git add src/plugins/memo
git commit -m "build(submodule): bump memo plugin to reparented MemoBlobBackend

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 7 — Reparent `src/plugins/todos/todoblobbackend` (todos submodule)

**Files:**
- Modify: `src/plugins/todos/todoblobbackend.h` (todos submodule)
- Modify: `src/plugins/todos/todoblobbackend.cpp` (todos submodule)

- [ ] **Step 1: Edit the header**

Replace `#include "syncbackend.h"` (line 4) with:

```cpp
#include "syncbackendbase.h"
```

Change the class declaration (line 30):

```cpp
class TodoBlobBackend final : public Kalburator::Sync::SyncBackendBase
```

Delete lines 84–96 (the inline calendar stub block):

```cpp
    // --- SyncBackend calendar pure-virtuals — stubs; dispatchBlobSync never calls these ---
    void loadCalendars(const QString &) override {}
    void storeCalendars(const QString &,
                        const QList<KCalendarCore::MemoryCalendar *> &) override {}
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

- [ ] **Step 2: Edit the implementation**

```bash
grep -n "::loadCalendars\|::storeCalendars\|::startSync\|::removeItem\|::pushItems" src/plugins/todos/todoblobbackend.cpp
```

Most stubs were inline in the header; if any cpp bodies exist for these, delete them.

```bash
grep -n "KCalendarCore" src/plugins/todos/todoblobbackend.cpp
```

Remove any whose only usage was the deleted stubs.

- [ ] **Step 3: Build + test**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | grep -E "error:" | grep "plugins/todos" | head -5
```

Expected: no output.

```bash
ctest --test-dir build-dev -R "(todoblob|todobackendplugin|palm_runtime|task_view_reads_hub|tst_hubtodoreader)" --output-on-failure
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -5
```

Expected: 117/117 pass.

- [ ] **Step 4: Commit in submodule**

```bash
cd src/plugins/todos
git add todoblobbackend.h todoblobbackend.cpp
git commit -m "refactor: reparent TodoBlobBackend to SyncBackendBase

Mirrors lib P3.T5. Drops the inline calendar stubs (loadCalendars /
storeCalendars / startSync / removeItem / pushItems-with-Incidence::Ptr).
dispatchBlobSync reaches the backend through neutral IBlobBackend.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd -
```

- [ ] **Step 5: Bump gitlink in superproject**

```bash
git add src/plugins/todos
git commit -m "build(submodule): bump todos plugin to reparented TodoBlobBackend

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 8 — Final verification + backwards-compat check + push

**Files:** No file changes — verification only.

- [ ] **Step 1: Final clean build against the local lib (build-dev)**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -10
```

Expected: build completes with no errors.

```bash
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 117/117 pass.

- [ ] **Step 2: Scratch FetchContent build against v0.60 (backwards-compat)**

The spec requires that the port works against the older v0.60 lib too. Configure a fresh build dir WITHOUT the `WILDPALMS_LIBKALBURATOR_SOURCE_DIR` override so it uses FetchContent:

```bash
rm -rf build-rcheck
cmake -S . -B build-rcheck -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -UWILDPALMS_LIBKALBURATOR_SOURCE_DIR
cmake --build build-rcheck -j$(nproc) 2>&1 | tail -10
```

Expected: build completes with no errors against v0.60. The reparented backends inheriting `SyncBackendBase` should work because v0.60's `SyncBackendBase` ALSO has the neutral surface (`IBlobBackend` methods inherited from `IBlobBackend`); the calendar pure-virtuals being absent from the base means `SyncBackendBase` simply doesn't require them.

**If this step fails**, the v0.60 `SyncBackendBase` may not have the methods our reparented backends now override-without-base-declaration. In that case:
- Read the v0.60 `SyncBackendBase` declarations (`build-rcheck/_deps/libkalburator-src/src/sync/syncbackendbase.h`).
- If methods we override (e.g. `loadRecords`, `loadRecord`, `createRecord`) aren't on the v0.60 base, the backwards-compat break is fundamental and we accept the port is one-way (local lib only). Document in the spec.

- [ ] **Step 3: ctest the scratch build**

```bash
ctest --test-dir build-rcheck --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 117/117 pass.

- [ ] **Step 4: Cleanup**

```bash
rm -rf build-rcheck
```

- [ ] **Step 5: Push the superproject**

```bash
git push origin feature/three-tier-sync
```

- [ ] **Step 6: Push each plugin submodule that changed**

```bash
git -C src/plugins/calendar push origin feature/canon-adoption-phase1
git -C src/plugins/contacts push origin feature/canon-adoption-phase1
git -C src/plugins/memo push origin feature/canon-adoption-phase1
git -C src/plugins/todos push origin feature/canon-adoption-phase1
```

(Todos / contacts / memo / calendar were all touched. Verify with `cd src/plugins/<d> && git log origin/feature/canon-adoption-phase1..HEAD --oneline` first — only push the submodules that have unpushed commits.)

- [ ] **Step 7: No commit required.** Task 8 is verification + push. Sub-project complete.

---

## Spec coverage map

| Spec section | Task(s) |
|---|---|
| §1 Problem — error 1 (palmcalendarbackend.h:75 fetchItems covariant) | Task 1 Steps 2, 3 |
| §1 Problem — error 2 (palmcalendarbackend.h:80 deleteItems covariant) | Task 1 Steps 2, 3 |
| §1 Problem — error 3 (palmruntime.cpp:93 insert mismatch) | Task 1 Step 4 |
| §1 Problem — error 4 (palmruntime.cpp:87 same in backendById) | Task 1 Step 4 |
| §1 Problem — error 5 (synchost_wp.cpp same) | Task 1 Step 5 |
| §1 Problem — 6 non-calendar backends inheriting SyncBackend | Tasks 2–7 (one per backend) |
| §2 Goals — WP builds against post-P3 lib | Task 1 Step 6, Task 8 Step 1 |
| §2 Goals — backwards-compat against v0.60 | Task 8 Steps 2-3 |
| §2 Goals — 6 non-calendar backends on SyncBackendBase | Tasks 2–7 |
| §2 Goals — 2 ISyncHost adapters dynamic_cast bridge | Task 1 Steps 4, 5 |
| §2 Goals — calendar PalmCalendarBackend keeps SyncBackend base | Task 1 (no inheritance change, only include addition) |
| §3 Approach — Layer 1 then Layer 2 | Task 1 first, then 2–7 |
| §5 Components & files — full file table | Tasks 1–7 cover every row |
| §8 Testing — per-task ctest verification | Steps 4 of each backend task |
| §9 Success criteria — 0 errors + 117/117 against both libs | Task 8 Steps 1, 2, 3 |
| §9 Success criteria — no `SyncBackend` inheritance on 6 backends | Tasks 2-7 deliver this; verify post-Task-7 via `grep -rn "public Kalburator::Sync::SyncBackend\b" src/` matching only the 2 calendar PalmCalendarBackend |
