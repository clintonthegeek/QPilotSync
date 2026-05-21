# Conflict-handler port — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate WP's 12 `QSyncCore::*` *conflict-type* consumers onto libkalburator's `Kalburator::Conflict::*`; relocate `ConnectionBehavior` fully to `PalmBackendConfig`; collapse the include-guard-collision bridge (`conflictdialogbridge` + `palmruntimebridgeinstall`); delete `src/sync/qsynccore/` (the conflict types after migration, plus the unused JSON `BaselineStore` / `IdMappingStore` / synccommon types as dead-code).

**Architecture:** Conflict types are byte-identical to upstream (the bridge proves this via memcpy). The qsynccore stores and `synccommon.h` value types are *dead code* — zero consumers in WP since the SQLite-backed `Kalburator::Storage::*` stores and the relocated `Sync::*` types in `src/core/synctypes.h` superseded them. So the work splits into: (1) sed-rename the 9 conflict types in 12 consumer files, (2) extend the handler with an optional `PalmBackendConfig*`, (3) collapse the bridge, (4) delete the whole qsynccore directory. Mid-implementation builds will fail between Task 4 and Task 6 (the bridge can't include both type namespaces); final verification at Task 7.

**Tech Stack:** C++/Qt6, CMake, libkalburator (consumed unchanged), pilot-link.

**Spec:** `docs/superpowers/specs/2026-05-21-conflict-handler-port-design.md`

---

## Task 1: Pre-flight — capture baselines + audit

**Files:**
- Read: `src/sync/qsynccore/conflictpolicy.h` (verify `connectionBehavior` field locations)

- [ ] **Step 1: Capture WP ctest baseline**

Run: `cd /home/clinton/dev/WildPalms/build-dev && ctest --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed, 0 tests failed out of 74`. Save the count for later comparison.

- [ ] **Step 2: Capture PlanStan ctest baseline**

Run: `cd /home/clinton/dev/PlanStan/build-dev && ctest 2>&1 | tail -5`
Expected: capture whatever the current pass/fail counts are. Note them; they should be identical after the port.

If `/home/clinton/dev/PlanStan/build-dev` doesn't exist, skip Step 2 and add a note in the final commit message that PlanStan baseline could not be captured. **Do not** configure or build PlanStan as part of this port — it's a sanity check, not a hard gate.

- [ ] **Step 3: Audit `ConflictPolicy::connectionBehavior` reads**

Run:
```bash
cd /home/clinton/dev/WildPalms && grep -rn "\.connectionBehavior\|->connectionBehavior\|connectionBehavior =\|connectionTimeoutSeconds" --include="*.cpp" --include="*.h" src/ tests/ 2>/dev/null | grep -v build | grep -v archived
```

Expected: every hit should be one of:
1. The `QSyncCore::` declaration itself (in `src/sync/qsynccore/conflictpolicy.h`) — being deleted.
2. The parallel declaration in `src/palm/conflict/palmbackendconfig.h` — staying.
3. UI write paths (`settingsdialog.cpp:341,358`, `profilepropertiesdialog.h`) — being rewired in Task 3.
4. The comment in `src/app/conflict/conflictdialogbridge.cpp:33` — file dies in Task 7.

**If any hit reads the field during sync (not during UI construction or QSettings load/save):** stop, document it, and either route it through `PalmBackendConfig` in Task 2 or escalate to user. Document findings inline as a comment in the eventual commit message.

- [ ] **Step 4: Verify the 12-consumer file count**

Run:
```bash
cd /home/clinton/dev/WildPalms && grep -rln "QSyncCore::" --include="*.cpp" --include="*.h" src/ tests/ 2>/dev/null | grep -v build | sort -u | wc -l
```

Expected: `12` (this is the source of the "12 consumer files" claim; if it differs, list the actual files and adjust task scope accordingly).

- [ ] **Step 5: No commit yet** — this is read-only investigation.

---

## Task 2: Extend `KalburatorInteractiveConflictHandler` with optional `PalmBackendConfig*` param

The handler's `shouldKeepConnectionAlive()` currently returns a fixed bool. We want it to consult `PalmBackendConfig::connectionBehavior` if a config is supplied. The constructor gets a new optional parameter, default-null for backward compatibility with existing tests that don't care.

**Files:**
- Modify: `src/app/conflict/kalburatorinteractiveconflicthandler.h`
- Modify: `src/app/conflict/kalburatorinteractiveconflicthandler.cpp`

- [ ] **Step 1: Add `PalmBackendConfig*` parameter to header**

In `src/app/conflict/kalburatorinteractiveconflicthandler.h`, add a forward declaration near the top (after existing includes, before the class):

```cpp
namespace WildPalms::Palm { struct PalmBackendConfig; }
```

Find the existing constructor declaration:
```cpp
explicit KalburatorInteractiveConflictHandler(
    Kalburator::Conflict::ConflictStore *store = nullptr,
    QWidget *parentWidget = nullptr,
    QObject *parent = nullptr);
```

Change it to:
```cpp
explicit KalburatorInteractiveConflictHandler(
    Kalburator::Conflict::ConflictStore *store = nullptr,
    QWidget *parentWidget = nullptr,
    QObject *parent = nullptr,
    const WildPalms::Palm::PalmBackendConfig *palmConfig = nullptr);
```

Add a private member at the end of the private section:
```cpp
const WildPalms::Palm::PalmBackendConfig *m_palmConfig = nullptr;
```

- [ ] **Step 2: Update constructor and `shouldKeepConnectionAlive()` in cpp**

In `src/app/conflict/kalburatorinteractiveconflicthandler.cpp`, add a `#include "palm/conflict/palmbackendconfig.h"` near the existing includes.

Find the constructor implementation. Update its signature to match the header and add member init:

```cpp
KalburatorInteractiveConflictHandler::KalburatorInteractiveConflictHandler(
    Kalburator::Conflict::ConflictStore *store,
    QWidget *parentWidget,
    QObject *parent,
    const WildPalms::Palm::PalmBackendConfig *palmConfig)
    : QObject(parent)
    , m_store(store)
    , m_parentWidget(parentWidget)
    , m_palmConfig(palmConfig)
{
}
```

(Adjust to match whatever the existing constructor body actually does — keep the existing initializers, add `m_palmConfig(palmConfig)`.)

Find `shouldKeepConnectionAlive() const`. It currently returns `m_keepAlive`. Update the override implementation (in the .cpp, not the header):

The header currently has the body inline: `bool shouldKeepConnectionAlive() const override { return m_keepAlive; }`. Move the implementation out of the header into the cpp. Replace the inline definition in the header with a declaration:

```cpp
bool shouldKeepConnectionAlive() const override;
```

Then add to the cpp:

```cpp
bool KalburatorInteractiveConflictHandler::shouldKeepConnectionAlive() const
{
    if (m_palmConfig) {
        return m_palmConfig->connectionBehavior !=
               WildPalms::Palm::ConnectionBehavior::DisconnectAndDefer;
    }
    return m_keepAlive;
}
```

- [ ] **Step 3: Rebuild**

Run: `cd /home/clinton/dev/WildPalms && cmake --build build-dev --target WildPalmsAppConflict 2>&1 | tail -10`
Expected: build succeeds. (We're inside an isolated edit; nothing else has been touched yet.)

- [ ] **Step 4: Run the conflict-handler test**

Run: `cd /home/clinton/dev/WildPalms/build-dev && ctest --output-on-failure -R "palm_runtime_conflict_handler" 2>&1 | tail -8`
Expected: `tst_palm_runtime_conflict_handler` still passes (the new param is optional with default null).

- [ ] **Step 5: No commit yet** — keep going through the bigger rename.

---

## Task 3: Rewire settings dialog through `PalmBackendConfig`

The profile-settings-UI flow currently writes its `ConnectionBehavior` value into `QSyncCore::ConflictPolicy`. We need to redirect that write to `PalmBackendConfig::connectionBehavior` so the value survives the qsynccore deletion.

**Files:**
- Modify: `src/settingsdialog.cpp`
- (Possibly) Modify: `src/widgets/dialogs/profilepropertiesdialog.h` / `.cpp`

- [ ] **Step 1: Inspect the existing wiring**

Run:
```bash
cd /home/clinton/dev/WildPalms && grep -n "conflictConnectionBehavior\|m_connectionBehaviorCombo\|m_syncConnectionCombo\|setConflictConnectionBehavior" src/settingsdialog.cpp src/widgets/dialogs/profilepropertiesdialog.* 2>/dev/null | head -20
```

This locates the load and save paths. The accessor `Profile::conflictConnectionBehavior()` returns a string (likely "KeepAlive", "DisconnectAndDefer", "TimeoutThenDefer"). The QSettings key name stays.

- [ ] **Step 2: Determine the destination of the value**

The destination is wherever `PalmBackendConfig` is constructed from profile settings. Search:
```bash
cd /home/clinton/dev/WildPalms && grep -rn "PalmBackendConfig\b" --include="*.cpp" src/ 2>/dev/null | grep -v build | head
```

Find the construction site (likely in `src/runtime/palmruntime.cpp` or `src/kf6/kf6mainwindow.cpp`). The pattern should be:

```cpp
PalmBackendConfig cfg;
cfg.connectionBehavior = connectionBehaviorFromString(profile->conflictConnectionBehavior());
cfg.connectionTimeoutSeconds = profile->conflictConnectionTimeoutSeconds(); // if such an accessor exists
```

If no such construction site exists yet, add one wherever the `KalburatorInteractiveConflictHandler` is instantiated (which the bridge collapse in Task 7 will also touch). Pass the resulting `cfg` into the handler.

- [ ] **Step 3: Confirm that `Profile::setConflictConnectionBehavior` still writes the same QSettings key**

Look at `Profile::setConflictConnectionBehavior` and `conflictConnectionBehavior` (in `src/profile.cpp` or similar). The QSettings key should NOT change — only the in-memory destination of the value changes.

- [ ] **Step 4: No code edits yet if findings are clean**

If the existing settings dialog already pushes the value into `Profile` (a separate string getter/setter) and `Profile` is the source-of-truth, no edits are needed in Task 3 — the value just needs a consumer in Task 7 (handler installation). Document this in the task journal.

If the settings dialog currently calls something like `policy.connectionBehavior = ...` directly (writing into a QSyncCore type), that line must change to write into the in-memory `PalmBackendConfig` or be removed. The expected behaviour is the dialog writes to `Profile`, not to a policy.

- [ ] **Step 5: No commit yet.**

---

## Task 4: Rename `QSyncCore::` (conflict types only) → `Kalburator::Conflict::` in non-bridge consumers

Mass-rename across 9 files (everything EXCEPT `conflictdialogbridge.{cpp,h}` and `palmruntimebridgeinstall.cpp`, which die wholesale in Task 5). `src/sync/syncstate.{cpp,h}` is *not* in this list — it already uses `Kalburator::Storage::*` for its stores and has no `QSyncCore::*` references.

**Files:**
- Modify: `src/app/conflictdialog.h`
- Modify: `src/app/conflictdialog.cpp`
- Modify: `src/app/conflictreviewwidget.h`
- Modify: `src/app/conflictreviewwidget.cpp` (if it references QSyncCore)
- Modify: `src/app/conflict/kalburatorinteractiveconflicthandler.cpp`
- Modify: `src/plugins/contacts/contactsconflicthandler.h`
- Modify: `src/plugins/todos/todoconflicthandler.h`
- Modify: `src/widgets/dialogs/conflictreviewdialog.h`
- Modify: `src/widgets/dialogs/conflictreviewdialog.cpp`
- Modify: `tests/runtime/tst_palm_runtime_conflict_handler.cpp`

- [ ] **Step 0: Sanity-check the file list**

Before running sed, confirm these are exactly the files that reference any `QSyncCore::` symbol (excluding the bridge files and the qsynccore dir itself):

```bash
cd /home/clinton/dev/WildPalms && grep -rln "QSyncCore::" --include="*.cpp" --include="*.h" src/ tests/ 2>/dev/null \
    | grep -v build | grep -v archived \
    | grep -v "src/sync/qsynccore/" \
    | grep -v "src/app/conflict/conflictdialogbridge" \
    | grep -v "src/app/conflict/palmruntimebridgeinstall" \
    | sort -u
```

Expected output: the 10 files listed above (some plugin handlers may live in `.h` only). If the set differs, adjust the `FILES` variable below.

- [ ] **Step 1: Sed namespace rename (conflict types only)**

Run (from repo root `/home/clinton/dev/WildPalms`):
```bash
FILES="src/app/conflictdialog.h src/app/conflictdialog.cpp src/app/conflictreviewwidget.h src/app/conflictreviewwidget.cpp src/app/conflict/kalburatorinteractiveconflicthandler.cpp src/plugins/contacts/contactsconflicthandler.h src/plugins/todos/todoconflicthandler.h src/widgets/dialogs/conflictreviewdialog.h src/widgets/dialogs/conflictreviewdialog.cpp tests/runtime/tst_palm_runtime_conflict_handler.cpp"

# Only the conflict types move. Stores/synccommon types in qsynccore are
# dead code with zero consumers — they die with the directory in Task 6.
sed -i 's/QSyncCore::ConflictRecord/Kalburator::Conflict::ConflictRecord/g' $FILES
sed -i 's/QSyncCore::RecordSnapshot/Kalburator::Conflict::RecordSnapshot/g' $FILES
sed -i 's/QSyncCore::ConflictPolicy/Kalburator::Conflict::ConflictPolicy/g' $FILES
sed -i 's/QSyncCore::ConflictStore/Kalburator::Conflict::ConflictStore/g' $FILES
sed -i 's/QSyncCore::ConflictHandler/Kalburator::Conflict::ConflictHandler/g' $FILES
sed -i 's/QSyncCore::ConflictDecision/Kalburator::Conflict::ConflictDecision/g' $FILES
sed -i 's/QSyncCore::AutoResolveStrategy/Kalburator::Conflict::AutoResolveStrategy/g' $FILES
sed -i 's/QSyncCore::PromptStrategy/Kalburator::Conflict::PromptStrategy/g' $FILES
sed -i 's/QSyncCore::FallbackBehavior/Kalburator::Conflict::FallbackBehavior/g' $FILES

# `using namespace QSyncCore;` (e.g. conflictdialog.cpp:16) → libkalburator namespace
sed -i 's|^using namespace QSyncCore;|using namespace Kalburator::Conflict;|g' $FILES
```

After this step, `grep -rn 'QSyncCore::' $FILES` should return zero hits (all conflict symbols are renamed; nothing else from qsynccore is referenced in these files).

- [ ] **Step 2: Update `#include` directives in the renamed files**

Run (still from repo root):
```bash
# Remove WP-local qsynccore includes that are now wrong
for f in $FILES; do
    sed -i '/#include.*"sync\/qsynccore\//d' "$f"
    sed -i '/#include.*"\.\.\/sync\/qsynccore\//d' "$f"
    sed -i '/#include.*"\.\.\/\.\.\/sync\/qsynccore\//d' "$f"
done

# Verify all qsynccore includes are gone from the consumer files
grep -n "sync/qsynccore" $FILES 2>/dev/null
```

Expected: the final grep returns no output. If any include remains, inspect and remove manually.

- [ ] **Step 3: Add upstream includes where needed**

For each consumer file that previously included `sync/qsynccore/conflictrecord.h`, it now needs `<conflictrecord.h>` (or the unqualified header — libkalburator's include paths are already on the search path; the bridge proves this). Inspect each file:

```bash
for f in $FILES; do echo "=== $f ==="; grep -n "^#include" "$f" | head -10; done
```

If a file uses `Kalburator::Conflict::ConflictRecord` but has no header for it, add:
```cpp
#include "conflictrecord.h"
```
(or `conflictpolicy.h`, `conflictstore.h`, etc., as needed — match the type it uses).

For the storage types, add:
```cpp
#include "baselinestore.h"     // for Kalburator::Storage::BaselineStore
#include "idmappingstore.h"    // for Kalburator::Storage::IDMappingStore
```

The right header is whichever upstream file declares the type. Look up via:
```bash
grep -rln "class BaselineStore\|class IDMappingStore" /home/clinton/dev/libkalburator/src/
```

- [ ] **Step 4: Update any namespace alias / forward declarations**

If any file has `namespace QSyncCore { class ConflictStore; }` (forward decl), replace it with `namespace Kalburator::Conflict { class ConflictStore; }`. Check:
```bash
grep -n "^namespace QSyncCore" $FILES
```
If hits exist, edit them manually.

- [ ] **Step 5: Build will fail here — that's expected**

Run: `cd /home/clinton/dev/WildPalms && cmake --build build-dev 2>&1 | tail -30`
Expected: compile failures. The bridge files (`conflictdialogbridge.{cpp,h}` and `palmruntimebridgeinstall.cpp`) still reference `QSyncCore::*` types — they get fixed in Task 7 by deletion.

The point of this step is to see *which* failures remain. They should all be in:
- `src/app/conflict/conflictdialogbridge.cpp`
- `src/app/conflict/palmruntimebridgeinstall.cpp`
- `src/kf6/kf6mainwindow.cpp` (uses the bridge functions)
- (Possibly) `src/CMakeLists.txt` related missing-header errors

If failures occur in any OTHER file, fix them inline before continuing — that indicates an incomplete rename.

- [ ] **Step 6: No commit yet.**

---

## Task 5: Collapse the bridge — inline `showAndGetDecision` and `createAndInstall`

The bridge files become unnecessary once the include-guard collision is gone (which will happen when `src/sync/qsynccore/` is deleted in Task 8). For this task we delete the bridge files and inline their two responsibilities at the call sites.

**Files:**
- Delete: `src/app/conflict/conflictdialogbridge.h`
- Delete: `src/app/conflict/conflictdialogbridge.cpp`
- Delete: `src/app/conflict/palmruntimebridgeinstall.cpp`
- Modify: `src/app/conflict/kalburatorinteractiveconflicthandler.cpp` (inline `showAndGetDecision`)
- Modify: `src/kf6/kf6mainwindow.cpp` (inline `createAndInstall` / `destroyHandler` / `connectKeepAlive`)
- Modify: `src/CMakeLists.txt` (remove bridge source files from compile lists; remove WildPalmsAppConflict refs if needed)
- Modify: `src/app/conflict/CMakeLists.txt` (if a sub-list exists; verify)

- [ ] **Step 1: Inline the dialog call inside `kalburatorinteractiveconflicthandler.cpp`**

Find the spot that calls `ConflictDialogBridge::showAndGetDecision(...)` (look around line 28 onwards based on prior investigation). The current shape:

```cpp
// ... constructs BridgePolicy bp from the Kalburator::Conflict::ConflictPolicy ...
int rawDecision = ConflictDialogBridge::showAndGetDecision(&wpRecord, bp, m_parentWidget);
return static_cast<Kalburator::Conflict::ConflictDecision>(rawDecision);
```

Replace with a direct call to `ConflictDialog`:

```cpp
#include "../conflictdialog.h"     // direct include now safe (no guard collision after Task 8)
// ...
ConflictDialog dlg(conflict, policy, m_parentWidget);
dlg.exec();
return dlg.decision();             // returns Kalburator::Conflict::ConflictDecision
```

Note: the include and the inline call must compile at the END of the port (after Task 8 deletes WP qsynccore). They will NOT compile right now during Task 5 because the WP-local conflict headers still exist with their colliding guards. That's expected; we accept the broken intermediate state.

Remove the `#include "conflictdialogbridge.h"` and any `BridgePolicy` construction code from `kalburatorinteractiveconflicthandler.cpp`.

- [ ] **Step 2: Inline `createAndInstall` into `kf6mainwindow.cpp`**

Open `src/kf6/kf6mainwindow.cpp`. Find around line 505-513:
```cpp
ConflictDialogBridge::destroyHandler(m_palmConflictHandler);
m_palmConflictHandler = ConflictDialogBridge::createAndInstall(
    /* runtime */, /* parentWidget */, /* qobjParent */);
ConflictDialogBridge::connectKeepAlive(m_palmConflictHandler, /* callback */);
```

Replace with direct construction. The pattern (look at `palmruntimebridgeinstall.cpp` for what was happening inside):

```cpp
#include "../app/conflict/kalburatorinteractiveconflicthandler.h"
#include "../palm/conflict/palmbackendconfig.h"
// ...

delete m_palmConflictHandler;
m_palmConflictHandler = new KalburatorInteractiveConflictHandler(
    nullptr,            // ConflictStore — keep as nullptr; M5b chore deferred
    /* parentWidget */,
    /* qobjParent */,
    &m_palmBackendConfig  /* if a member already exists; otherwise nullptr */);
m_palmRuntime->setConflictHandler(m_palmConflictHandler);

QObject::connect(m_palmConflictHandler,
                 &KalburatorInteractiveConflictHandler::keepAliveRequested,
                 this, &KF6MainWindow::onPalmConflictHandlerKeepAlive);
```

Verify the variable names by reading the existing `kf6mainwindow.cpp` around the bridge call sites. The bridge's `createAndInstall` and `destroyHandler` together do exactly the above three operations (construct, install on runtime, return as QObject*); inline them.

Replace `m_palmConflictHandler` with the same QObject* it was before (the bridge returned `QObject*`; the new direct construction returns `KalburatorInteractiveConflictHandler*`). Keep the member type as `QObject*` if other code paths depend on that. (Inspect the header for the declared type.)

Remove the bridge include from `kf6mainwindow.cpp`:
```cpp
// DELETE this line:
#include "../app/conflict/conflictdialogbridge.h"
```

Add the new direct includes:
```cpp
#include "../app/conflict/kalburatorinteractiveconflicthandler.h"
```

- [ ] **Step 3: Delete the bridge source files**

Run:
```bash
cd /home/clinton/dev/WildPalms && git rm src/app/conflict/conflictdialogbridge.h src/app/conflict/conflictdialogbridge.cpp src/app/conflict/palmruntimebridgeinstall.cpp
```

- [ ] **Step 4: Remove the bridge files from CMake build lists**

Look at `src/CMakeLists.txt` for any explicit listing of the bridge files. Also check `src/app/conflict/CMakeLists.txt` if one exists. Remove any line that mentions `conflictdialogbridge.cpp`, `palmruntimebridgeinstall.cpp`.

If the files are picked up via glob, no CMake edit is needed.

Verify:
```bash
grep -rn "conflictdialogbridge\|palmruntimebridgeinstall" src/CMakeLists.txt src/app/conflict/CMakeLists.txt 2>/dev/null
```
Expected: empty.

- [ ] **Step 5: Build still won't succeed**

Run: `cd /home/clinton/dev/WildPalms && cmake --build build-dev 2>&1 | tail -20`
Expected: compile failures, but ideally fewer than after Task 4. Remaining failures should be guard-collision issues (WP and libkalburator both define `QSYNCCORE_CONFLICTRECORD_H` etc.) hit when `kalburatorinteractiveconflicthandler.cpp` now includes `conflictdialog.h` directly. These dissolve in Task 6.

- [ ] **Step 6: No commit yet.**

---

## Task 6: Delete `src/sync/qsynccore/` + trim `src/CMakeLists.txt`

Now the WP-local copies go. After Task 4's conflict-type rename, nothing in `src/` or `tests/` references any qsynccore symbol. The directory contains:

- Conflict types (`conflictrecord.{h,cpp}`, `conflictpolicy.{h,cpp}`, `conflictstore.{h,cpp}`) — superseded by upstream `Kalburator::Conflict::*` (now in use by the renamed consumers).
- Store types (`baselinestore.{h,cpp}` JSON / `idmappingstore.{h,cpp}` JSON) — **dead code with zero constructors**. `Kalburator::Storage::*` (SQLite) is what WP actually uses, via `syncstate.cpp` + `palmruntime.cpp`.
- `synccommon.h` value types (`IdMapping`, `SyncStats`, `SyncResult`, `DataLossWarning`, `RecordId`) — **dead code, zero consumers**. Already relocated to `src/core/synctypes.h` (namespace `Sync`) by E.16.

Deletion is risk-free. After this step the include-guard collision is gone and Task 5's direct dialog include in the handler compiles cleanly.

**Files:**
- Delete: `src/sync/qsynccore/` (entire directory, 8 files)
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Delete the directory**

Run:
```bash
cd /home/clinton/dev/WildPalms && git rm -r src/sync/qsynccore/
```

Expected: 8 files removed (synccommon.h, conflictpolicy.{h,cpp}, conflictrecord.{h,cpp}, conflictstore.{h,cpp}, baselinestore.{h,cpp}, idmappingstore.{h,cpp}).

- [ ] **Step 2: Trim `src/CMakeLists.txt`**

Open `src/CMakeLists.txt`. Find and delete lines 43-53 (the qsynccore source list block). The actual lines from the pre-port grep:

```
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.cpp
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.cpp
    sync/qsynccore/baselinestore.h
    sync/qsynccore/conflictrecord.cpp
    sync/qsynccore/conflictrecord.h
    sync/qsynccore/conflictstore.cpp
    sync/qsynccore/conflictstore.h
    sync/qsynccore/conflictpolicy.cpp
    sync/qsynccore/conflictpolicy.h
```

Also find and delete the secondary block around lines 236-238:
```
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.h
```

Also delete the now-stale comment around line 152 ("WildPalms' local sync/qsynccore/conflictpolicy.h").

Verify:
```bash
grep -n "qsynccore" /home/clinton/dev/WildPalms/src/CMakeLists.txt
```
Expected: no output.

- [ ] **Step 3: Build should now succeed (or at least: failures should be different)**

Run: `cd /home/clinton/dev/WildPalms && cmake --build build-dev 2>&1 | tail -30`

Two possible outcomes:
1. **Build succeeds.** Excellent — proceed to Task 7.
2. **Build fails.** Inspect the failures carefully. Common causes:
   - A consumer file's `#include "sync/qsynccore/..."` was missed in Task 4 Step 2 — find and remove.
   - A consumer file used a type name that doesn't exist upstream (e.g. some helper function we forgot). Read the libkalburator header to find the equivalent, or add a thin WP-side helper. **Do not** resurrect a qsynccore file.
   - A symbol from `synccommon.h` that wasn't part of the spec's rename list is still referenced (e.g., `SyncResult`, `SyncStats`, `DataLossWarning`). The `synctypes.h` (in `src/core/`) was E.16's relocation home for these — verify they're available there.

Fix any remaining failures and re-run. Once the build is green, proceed.

- [ ] **Step 4: No commit yet.**

---

## Task 7: Full verification — build + ctest 74/74

**Files:** none

- [ ] **Step 1: Clean build**

Run: `cd /home/clinton/dev/WildPalms && cmake --build build-dev 2>&1 | tail -5`
Expected: `[100%] Built target wildpalms` (or similar — full build green, no warnings about deleted files).

- [ ] **Step 2: Full ctest**

Run: `cd /home/clinton/dev/WildPalms/build-dev && ctest --output-on-failure 2>&1 | tail -8`
Expected: `100% tests passed, 0 tests failed out of 74`.

- [ ] **Step 3: Targeted conflict-test verification**

Run:
```bash
cd /home/clinton/dev/WildPalms/build-dev && ctest --output-on-failure -R "conflict|palm_runtime" 2>&1 | tail -15
```
Expected: every conflict-handler-related test passes, including `tst_palm_runtime_conflict_handler`, `tst_contactsconflicthandler`, `tst_calendarconflicthandler` (if it exists), `tst_todoconflicthandler`.

- [ ] **Step 4: PlanStan sanity check**

Run: `cd /home/clinton/dev/PlanStan/build-dev && ctest 2>&1 | tail -5` (only if Step 2 of Task 1 succeeded)
Expected: identical pass/fail counts to the Task 1 baseline.

If PlanStan ctest changed (more failures than baseline), **stop and investigate** — this port should be zero-impact for PlanStan; a regression here indicates something leaked into libkalburator that shouldn't have. (Likely cause: an accidental edit in `~/dev/libkalburator/` — verify with `cd ~/dev/libkalburator && git status`.)

- [ ] **Step 5: Verify exit criteria from the spec**

Run:
```bash
cd /home/clinton/dev/WildPalms

# (a) qsynccore directory gone
test ! -d src/sync/qsynccore && echo "(a) PASS: qsynccore deleted"

# (b) bridge files gone
test ! -f src/app/conflict/conflictdialogbridge.h && echo "(b1) PASS: bridge header deleted"
test ! -f src/app/conflict/conflictdialogbridge.cpp && echo "(b2) PASS: bridge impl deleted"
test ! -f src/app/conflict/palmruntimebridgeinstall.cpp && echo "(b3) PASS: install bridge deleted"

# (c) no QSyncCore:: references in tracked files
test $(git grep -l "QSyncCore::" -- 'src/**' 'tests/**' 2>/dev/null | wc -l) -eq 0 \
    && echo "(c) PASS: no QSyncCore:: references in tracked code"
```

Each line should print PASS. If any FAILs (no output for a line), fix it before proceeding.

- [ ] **Step 6: No commit yet** — docs update first.

---

## Task 8: Update tracking docs

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`

- [ ] **Step 1: Update Phase-E spec E.16 row**

Open `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`. Find the E.16 row (around line 595). It currently lists deferral (a) as the conflict-handler rebind still gating `src/sync/` delete. Update that bullet from 🟡 to ✅ with the migration date and commit ref (will be known after Task 9).

For now, leave the date as `YYYY-MM-DD` and the commit ref as `XXXXXXX`; update after Task 9 lands.

Concretely, in the E.16 row text, change:
```
(a) `InteractiveConflictHandler` rebind from WP-internal `QSyncCore` to ...
```
to:
```
(a) ✅ **Done <DATE>.** `KalburatorInteractiveConflictHandler` derives from
`Kalburator::Conflict::ConflictHandler`. All ~12 WP consumers migrated
from `QSyncCore::*` to `Kalburator::Conflict::*` + `Kalburator::Storage::*`.
`src/sync/qsynccore/` deleted. Bridge collapsed
(`conflictdialogbridge` + `palmruntimebridgeinstall` removed).
Commit `<COMMIT>`.
```

- [ ] **Step 2: Update integration plan E.16 deferral (a)**

Open `docs/plans/2026-04-20-libkalburator-integration.md`. Find the E.16 deferral block. Change the (a) bullet from 🟡 to ✅ with the same wording as above (without the spec cross-ref since this IS the integration plan).

Also update the E.16 top-line status if appropriate: with (a) and (c) both done now, the remaining deferrals are (b) ✅ via removal, (d) LocalBlobBackend cross-id-space, and (e) ✅ namespace rename. So E.16 is much closer to fully landed — the only outstanding pieces are LocalBlobBackend and a final src/sync/ tidy. Consider rephrasing the E.16 row to reflect that.

- [ ] **Step 3: Stage doc changes**

Run:
```bash
cd /home/clinton/dev/WildPalms && git add -f docs/plans/2026-04-20-libkalburator-integration.md
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
```

(`docs/plans/` is gitignored by convention, hence `-f`.)

- [ ] **Step 4: No commit yet.**

---

## Task 9: Final commit + post-flight

**Files:** none beyond what's been staged.

- [ ] **Step 1: Final ctest before commit**

Run: `cd /home/clinton/dev/WildPalms/build-dev && ctest --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed, 0 tests failed out of 74`. (Repeat from Task 7 to confirm no regression slipped in during Task 8 edits — paranoid but cheap.)

- [ ] **Step 2: Stage everything**

Run:
```bash
cd /home/clinton/dev/WildPalms && git add -A src/ tests/ \
    && git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
    && git add -f docs/plans/2026-04-20-libkalburator-integration.md \
    && git status --short | head -30
```

Expected output should show:
- `D src/sync/qsynccore/*` (8 files deleted)
- `D src/app/conflict/conflictdialogbridge.{h,cpp}` (2 files deleted)
- `D src/app/conflict/palmruntimebridgeinstall.cpp` (1 file deleted)
- `M src/app/conflictdialog.{h,cpp}` (renamed types)
- `M src/app/conflictreviewwidget.h` (and possibly .cpp)
- `M src/app/conflict/kalburatorinteractiveconflicthandler.{h,cpp}` (param add + sed)
- `M src/plugins/contacts/contactsconflicthandler.h`
- `M src/plugins/todos/todoconflicthandler.h`
- `M src/widgets/dialogs/conflictreviewdialog.{h,cpp}`
- `M src/CMakeLists.txt`
- `M src/kf6/kf6mainwindow.cpp` (bridge call sites inlined)
- `M src/settingsdialog.cpp` (only if Task 3 actually edited it)
- `M tests/runtime/tst_palm_runtime_conflict_handler.cpp`
- `M docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
- `M docs/plans/2026-04-20-libkalburator-integration.md`

If anything unexpected is staged (e.g., changes in `~/dev/libkalburator/` accidentally getting staged through a submodule reference), unstage it.

- [ ] **Step 3: Commit**

Run:
```bash
git commit -m "$(cat <<'EOF'
refactor(conflict): port QSyncCore → Kalburator; delete src/sync/qsynccore/

E.16 deferral (a). Migrate all ~12 WP consumers from QSyncCore::* to
libkalburator's Kalburator::Conflict::* and Kalburator::Storage::*.
Relocate ConnectionBehavior fully to PalmBackendConfig (the WP-side
device-policy home; QSyncCore copy of the enum goes away with the
directory delete). Collapse the include-guard-collision bridge:
conflictdialogbridge.{h,cpp} + palmruntimebridgeinstall.cpp deleted;
their two roles (showAndGetDecision, createAndInstall) inlined at
the call sites in kalburatorinteractiveconflicthandler.cpp and
kf6mainwindow.cpp respectively.

Net effect:
- src/sync/qsynccore/ deleted (8 files)
- 3 bridge files deleted (~150 lines including their wall-of-comments)
- 10 consumer files renamed via sed
- KalburatorInteractiveConflictHandler gains optional PalmBackendConfig*
  ctor param; shouldKeepConnectionAlive() consults it when present
- src/sync/ reduced to syncstate.{cpp,h} + syncbackend.h
- Zero libkalburator changes; PlanStan ctest unchanged

WP ctest 74/74 passes; PlanStan ctest unchanged from baseline.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Post-flight verification**

Run:
```bash
cd /home/clinton/dev/WildPalms && git log -1 --stat | head -25
```

Confirm the commit landed with expected file counts (≈12 deletions, ≈14 modifications).

- [ ] **Step 5: Backfill date + commit SHA into tracking docs**

The doc updates in Task 8 used placeholder `<DATE>` and `<COMMIT>`. Now we know both. Run:
```bash
cd /home/clinton/dev/WildPalms
TODAY=$(date +%Y-%m-%d)
SHA=$(git log -1 --format="%h")
sed -i "s/<DATE>/$TODAY/g; s/<COMMIT>/$SHA/g" \
    docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
    docs/plans/2026-04-20-libkalburator-integration.md

git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git add -f docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "docs: backfill date + commit ref for E.16 deferral (a) closure"
```

- [ ] **Step 6: Done.**

Report back to user with:
- Commit SHAs of both commits
- Before/after WP ctest count (should be 74/74 both)
- PlanStan ctest result (should match baseline)
- Anything unexpected encountered during the port

## Self-review notes (delete after reading)

**Spec coverage check:**

- ✅ Step 1 (namespace renames) → Task 4
- ✅ Step 2 (`ConflictPolicy::connectionBehavior` references) → Tasks 1, 3
- ✅ Step 3 (dialog constructors) → Task 4 (the sed covers the constructor signatures since they textually contain `QSyncCore::`)
- ✅ Step 4 (delete the bridge) → Task 5
- ✅ Step 5 (delete `src/sync/qsynccore/`) → Task 6
- ✅ Step 6 (syncstate update) → Task 4 (syncstate is in the rename list)
- ✅ Step 7 (`IdMappingStore` casing fix) → Task 4 (sed includes the casing migration)
- ✅ ConnectionBehavior relocation → Tasks 2 (handler extension) + 3 (settings wiring)
- ✅ Bridge collapse → Task 5
- ✅ Testing strategy → Tasks 1, 7
- ✅ Acceptance criteria → Task 7 Step 5
- ✅ Out-of-scope items → not present in plan (correctly excluded)

**Placeholder check:** scanned. No TBD/TODO/"fill in details" markers. Two intentional placeholders (`<DATE>`, `<COMMIT>`) are backfilled in Task 9 Step 5.

**Type consistency check:** types and signatures match between Task 2 (`KalburatorInteractiveConflictHandler` constructor) and Task 5 (its use in `kf6mainwindow.cpp`). The `PalmBackendConfig` namespace `WildPalms::Palm` is used consistently.
