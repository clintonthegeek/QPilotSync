# Phase E.15b — fullsync → runtime Relocation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move five `_wp` host-interface file pairs from `src/fullsync/` into `src/runtime/`, fold `WildPalmsFullSync` into `WildPalmsRuntime`, delete the now-empty `src/fullsync/` subtree.

**Architecture:** Pure mechanical relocation. `git mv` preserves history. Header guards rename `WILDPALMS_FULLSYNC_*` → `WILDPALMS_RUNTIME_*`. WildPalmsRuntime promotes Kalburator::Sync from PRIVATE-include to PUBLIC-link. Test helper renames `add_fullsync_test` → `add_runtime_test`. No semantic change; existing tests pass.

**Tech Stack:** Bash (`git mv`, `sed`), CMake.

**Spec:** `docs/superpowers/specs/2026-04-26-phase-e15b-fullsync-relocation-design.md`

---

## File structure

```
DELETED:
  src/fullsync/CMakeLists.txt
  src/fullsync/                    (entire dir empty after moves)

MOVED (git mv, src/fullsync/X → src/runtime/X):
  calendarcollection_wp.{h,cpp}
  conflictpresenter_wp.{h,cpp}
  conflictresolver_wp.{h,cpp}
  syncconfigstore_wp.{h,cpp}
  synchost_wp.{h,cpp}

MODIFIED:
  src/runtime/calendarcollection_wp.h    header guard rename
  src/runtime/conflictpresenter_wp.h     header guard rename
  src/runtime/conflictresolver_wp.h      header guard rename
  src/runtime/syncconfigstore_wp.h       header guard rename
  src/runtime/synchost_wp.h              header guard rename
  src/runtime/CMakeLists.txt             add 5 source pairs + promote Kalburator::Sync
  src/CMakeLists.txt                     drop add_subdirectory(fullsync) + drop deferral comments
  tests/CMakeLists.txt                   rename helper add_fullsync_test → add_runtime_test;
                                          link WildPalmsRuntime; update 3 call sites

UNCHANGED:
  tests/test_fullsync_*.cpp              filenames + content stay the same
```

---

## Task 1: `git mv` files + rename header guards

**Files:**
- Move 5 `.cpp` + 5 `.h` from `src/fullsync/` to `src/runtime/`
- Edit 5 header guards

- [ ] **Step 1: Verify clean tree**

```bash
cd /home/clinton/dev/WildPalms
git status --short
```
Expected: clean (or only Testing/, build-dev/, and untracked plan/spec docs from this session). If anything else, stop.

- [ ] **Step 2: Move all 10 files with `git mv`**

```bash
cd /home/clinton/dev/WildPalms
for stem in calendarcollection_wp conflictpresenter_wp conflictresolver_wp syncconfigstore_wp synchost_wp; do
    git mv "src/fullsync/${stem}.h"   "src/runtime/${stem}.h"
    git mv "src/fullsync/${stem}.cpp" "src/runtime/${stem}.cpp"
done
git status --short
```
Expected: 10 `R` (renamed) lines.

- [ ] **Step 3: Rename header guards in each moved `.h`**

```bash
cd /home/clinton/dev/WildPalms
for stem in calendarcollection_wp conflictpresenter_wp conflictresolver_wp syncconfigstore_wp synchost_wp; do
    sed -i 's/WILDPALMS_FULLSYNC_/WILDPALMS_RUNTIME_/g' "src/runtime/${stem}.h"
done
grep -h "^#ifndef WILDPALMS\|^#define WILDPALMS" \
    src/runtime/calendarcollection_wp.h \
    src/runtime/conflictpresenter_wp.h  \
    src/runtime/conflictresolver_wp.h   \
    src/runtime/syncconfigstore_wp.h    \
    src/runtime/synchost_wp.h
```
Expected: 10 lines, all starting with `WILDPALMS_RUNTIME_*` (no `FULLSYNC` left).

- [ ] **Step 4: Verify no other code references the old guard prefix**

```bash
grep -rn "WILDPALMS_FULLSYNC" /home/clinton/dev/WildPalms/src /home/clinton/dev/WildPalms/tests
```
Expected: no results.

- [ ] **Step 5: Don't commit yet** — Task 2 + Task 3 + Task 4 land in the same commit at the end of Task 4 because intermediate states don't build cleanly.

---

## Task 2: Rewire `WildPalmsRuntime` CMake

**Files:**
- Modify: `src/runtime/CMakeLists.txt`

- [ ] **Step 1: Replace `src/runtime/CMakeLists.txt` contents**

Write the file with this content:

```cmake
# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8) plus
# Full-Sync host-interface implementations (relocated from src/fullsync/
# in E.15b). Houses BackendPluginManager + PluginActionManager + shared
# metadata helpers + InstallSourceCollector + the SyncCoordinator-facing
# WP host implementations consumed by libkalburator.

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_library(WildPalmsRuntime STATIC
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
    backendpluginmanager.h
    backendpluginmanager.cpp
    pluginactionmanager.h
    pluginactionmanager.cpp
    simpleactioncontext.h
    simpleactioncontext.cpp
    # Phase E.15a — folder + cross-plugin blob aggregator for the
    # install action.
    installsourcecollector.h
    installsourcecollector.cpp
    # Phase E.15b — Full-Sync host-interface implementations relocated
    # from src/fullsync/. WildPalmsFullSync no longer exists.
    calendarcollection_wp.h
    calendarcollection_wp.cpp
    conflictpresenter_wp.h
    conflictpresenter_wp.cpp
    conflictresolver_wp.h
    conflictresolver_wp.cpp
    syncconfigstore_wp.h
    syncconfigstore_wp.cpp
    synchost_wp.h
    synchost_wp.cpp
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        Qt::Widgets       # QIcon, QWidget in core headers (ibackendplugin.h)
        KF6::CoreAddons
        # Phase E.15b — fullsync host impls implement Kalburator::Sync
        # interfaces with virtual methods; vtable emission requires an
        # actual link, not just an include path.
        Kalburator::Sync
    # WildPalmsCore is exposed only on the INTERFACE link line — consumers
    # that pull in WildPalmsRuntime get the shared lib — but WildPalmsRuntime
    # itself does NOT link WildPalmsCore. This breaks the Phase-E.9 cycle
    # where KF6MainWindow (inside WildPalmsCore) needs BackendPluginManager.
    # WildPalmsCore is the SHARED consumer; linking Runtime PRIVATE there
    # embeds these static objects, and the Core-side symbols they need
    # (ipluginaction.cpp / ibackendplugin.cpp) are already compiled into
    # the same .so.
    INTERFACE
        WildPalmsCore
)

set_target_properties(WildPalmsRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

(Note: the previous PRIVATE `target_include_directories ... BEFORE` block for `Kalburator::Sync` is removed — promoting to PUBLIC link makes the include path automatic via target propagation.)

- [ ] **Step 2: Don't commit yet.**

---

## Task 3: Drop `add_subdirectory(fullsync)` + delete `src/fullsync/CMakeLists.txt`

**Files:**
- Modify: `src/CMakeLists.txt`
- Delete: `src/fullsync/CMakeLists.txt`

- [ ] **Step 1: Edit `src/CMakeLists.txt`**

Find and remove these blocks. There are three locations:

a) Lines around 131-136 — remove the comment about the "fullsync→runtime relocation deferred to E.15":
```cmake
        # Phase E.9 — new-ABI plugin manager used by KF6MainWindow.
        # WildPalmsRuntime PUBLIC-links WildPalmsCore; linking it PRIVATE
        # here embeds its static objects without creating a consumer
        # cycle. The fullsync→runtime relocation deferred to E.15 keeps
        # this dependency light.
        WildPalmsRuntime
```
→ replace with:
```cmake
        # Phase E.9 — new-ABI plugin manager used by KF6MainWindow.
        # WildPalmsRuntime PUBLIC-links WildPalmsCore; linking it PRIVATE
        # here embeds its static objects without creating a consumer
        # cycle.
        WildPalmsRuntime
```

b) Lines around 222-225 — remove the `add_subdirectory(fullsync)` and its leading comment block:
```cmake
# Full Sync Mode host-interface implementations (Phase D of libkalburator
# integration). Keeps the Kalburator::Sync dependency out of WildPalmsCore
# until Phase F wires the UI.
add_subdirectory(fullsync)
```
→ delete entirely.

c) Lines around 254-258 — remove the comment about "Leaves src/fullsync/ alone":
```cmake
# New-ABI plugin managers (Phase E.8 of libkalburator integration).
# Consumes the plugin-ABI headers from src/core/ and Kalburator::Sync.
# Leaves src/fullsync/ alone — the fullsync → runtime relocation lands
# in E.15.
add_subdirectory(runtime)
```
→ replace with:
```cmake
# Plugin runtime — managers, simple action context, install source
# collector, and (since E.15b) the Full-Sync host-interface
# implementations consumed by libkalburator.
add_subdirectory(runtime)
```

- [ ] **Step 2: Delete `src/fullsync/CMakeLists.txt`**

```bash
cd /home/clinton/dev/WildPalms
git rm src/fullsync/CMakeLists.txt
```

- [ ] **Step 3: Verify the directory is now empty**

```bash
ls /home/clinton/dev/WildPalms/src/fullsync 2>/dev/null
```
Expected: empty output (directory exists but is now empty after git mv'd everything out and removed CMakeLists.txt).

- [ ] **Step 4: Remove the empty directory**

```bash
rmdir /home/clinton/dev/WildPalms/src/fullsync
```
Expected: silent success. (`rmdir` only removes empty dirs — it'll error if anything's left.)

- [ ] **Step 5: Don't commit yet.**

---

## Task 4: Rename test helper + update call sites + verify

**Files:**
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Edit `tests/CMakeLists.txt`**

Find the helper function block around lines 127-146 and replace:

```cmake
# ============================================================
# Helper for fullsync-only tests (Phase D). Links WildPalmsFullSync +
# Kalburator::Sync, stays out of the WildPalmsCore/pisock link line.
# ============================================================

function(add_fullsync_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsFullSync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_fullsync_test(test_fullsync_calendarcollection test_fullsync_calendarcollection.cpp)
add_fullsync_test(test_fullsync_syncconfigstore    test_fullsync_syncconfigstore.cpp)
```

→ replace with:

```cmake
# ============================================================
# Helper for runtime-only tests (Phase D originally; consolidated under
# WildPalmsRuntime in E.15b). Links WildPalmsRuntime + Kalburator::Sync,
# stays out of the WildPalmsCore/pisock link line.
# ============================================================

function(add_runtime_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsRuntime
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_runtime_test(test_fullsync_calendarcollection test_fullsync_calendarcollection.cpp)
add_runtime_test(test_fullsync_syncconfigstore    test_fullsync_syncconfigstore.cpp)
```

(Note: the test executable filenames stay `test_fullsync_*` — those reference behavior coverage, not the relocated library. Renaming the .cpp files would just churn git history for no gain.)

- [ ] **Step 2: Find any other call site of `add_fullsync_test`**

```bash
grep -rn "add_fullsync_test\|add_runtime_test\|WildPalmsFullSync" /home/clinton/dev/WildPalms/src /home/clinton/dev/WildPalms/tests --include="CMakeLists.txt"
```
Expected: only `add_runtime_test` references in `tests/CMakeLists.txt`. There may be a third `add_fullsync_test(test_fullsync_synchost ...)` line further down — if so, change it to `add_runtime_test`. If `WildPalmsFullSync` shows up anywhere, hunt it down.

- [ ] **Step 3: Configure**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -10
```
Expected: `Configuring done` then `Generating done` with no errors. If CMake complains about `WildPalmsFullSync` being undefined or `src/fullsync/CMakeLists.txt` missing, those are leftover references to fix.

- [ ] **Step 4: Build the runtime + the moved test executables**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target WildPalmsRuntime test_fullsync_calendarcollection test_fullsync_syncconfigstore test_fullsync_synchost 2>&1 | tail -15
```
Expected: clean builds. Watch for:
- "undefined reference to `Kalburator::Sync::...`" → Kalburator::Sync isn't linked PUBLIC; revisit Task 2.
- "Cannot find source file: calendarcollection_wp.cpp" → Task 2 forgot a source line.

- [ ] **Step 5: Run the three fullsync-test executables**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^test_fullsync_' --output-on-failure
```
Expected: PASS for all 3.

- [ ] **Step 6: Run the full WP test suite to verify nothing else regressed**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```
Expected: 76/76 PASS (same count as post-E.15a).

- [ ] **Step 7: Final cleanliness check**

```bash
grep -rn "WildPalmsFullSync\|src/fullsync" /home/clinton/dev/WildPalms/src /home/clinton/dev/WildPalms/tests
```
Expected: no results.

- [ ] **Step 8: Commit (single commit covers Tasks 1–4)**

```bash
cd /home/clinton/dev/WildPalms
git add -A src/runtime src/fullsync src/CMakeLists.txt tests/CMakeLists.txt
git status --short
```
Expected: 10 R (renames), 1 D (deleted CMakeLists.txt), 2-3 M (modified CMakeLists). Then:

```bash
git commit -m "$(cat <<'EOF'
refactor(runtime): fold src/fullsync/ into src/runtime/ (E.15b)

Mechanical relocation deferred from E.8. The five host-interface .cpp/.h
pairs (calendarcollection_wp, conflictpresenter_wp, conflictresolver_wp,
syncconfigstore_wp, synchost_wp) move from src/fullsync/ to src/runtime/
via git mv (history preserved). WildPalmsFullSync static lib disappears;
its sources fold into WildPalmsRuntime, which gains a PUBLIC link to
Kalburator::Sync (was a PRIVATE include path) so the relocated impls'
vtables emit correctly.

Header guards renamed WILDPALMS_FULLSYNC_* → WILDPALMS_RUNTIME_* (no
caller references the literal). _wp filename suffix kept to dodge name
clashes with libkalburator's CalendarCollection / SyncHost / etc.
QSettings group keys "fullsync/..." in syncconfigstore_wp.cpp keep their
literal strings (renaming would migrate user-config behavior; out of
scope for a mechanical relocation).

Test helper add_fullsync_test → add_runtime_test; three call sites
updated. test_fullsync_* executable filenames preserved (cover
behavior, not the renamed library).

Phase E.15b — completes the E.15 split started in E.15a.
EOF
)"
```

---

## Task 5: Parent-spec row + memory entry

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (flip E.15b row)
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e15b_fullsync_runtime.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

- [ ] **Step 1: Edit parent spec — flip the E.15b row to ✅**

Find the line currently reading:

```
| **E.15b** | `git mv src/fullsync/* src/runtime/`; fold `WildPalmsFullSync` static lib into `WildPalmsRuntime`; update `WildPalmsCore` link list. Mechanical relocation deferred from E.8. | WP | E.15a | WP ctest passes; libkalburator ctest passes; library graph still builds. |
```

Replace with:

```
| ✅ **E.15b** | `git mv src/fullsync/* src/runtime/` complete; `WildPalmsFullSync` static lib folded into `WildPalmsRuntime`; Kalburator::Sync promoted to PUBLIC link on Runtime. Header guards renamed WILDPALMS_FULLSYNC_* → WILDPALMS_RUNTIME_*. `_wp` filename suffix retained (avoids name clash with libkalburator types). QSettings group keys `"fullsync/..."` in `syncconfigstore_wp.cpp` retained verbatim (config-compat). Test helper renamed `add_fullsync_test` → `add_runtime_test`. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e15b-fullsync-relocation.md`. | WP | E.15a | WP ctest passes (76/76); library graph one node smaller; no behavior change. |
```

- [ ] **Step 2: Create the memory file**

`/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e15b_fullsync_runtime.md`:

```markdown
---
name: Phase E.15b — fullsync → runtime relocation landed
description: E.15b landed 2026-04-26; fullsync static lib folded into runtime; mechanical, no behavior change
type: project
---

E.15b landed 2026-04-26. Mechanical relocation deferred from E.8.
The five host-interface .cpp/.h pairs that lived at src/fullsync/
(`calendarcollection_wp`, `conflictpresenter_wp`,
`conflictresolver_wp`, `syncconfigstore_wp`, `synchost_wp`) now live
at src/runtime/ alongside the plugin managers. WildPalmsFullSync
static library no longer exists; sources fold into WildPalmsRuntime.

**Why:** the dual-static-lib split was a Phase-D quarantine boundary
that's no longer needed once the plugin ABI runtime exists. Folding
collapses the library graph by one node.

**How to apply:** Future code referencing fullsync host impls
includes from `src/runtime/` not `src/fullsync/`. Header guard
prefix is `WILDPALMS_RUNTIME_*` (was `WILDPALMS_FULLSYNC_*`).
Test helper to write Kalburator-linked tests is `add_runtime_test`
(was `add_fullsync_test`). The `_wp` filename suffix stays — it
prevents collision with libkalburator types (`CalendarCollection`,
`SyncHost`, `ConflictPresenter`, etc.).

**Not changed:** QSettings group keys `"fullsync/..."` in
syncconfigstore_wp.cpp kept verbatim — they're persistent
user-config keys. Test executable filenames `test_fullsync_*.cpp`
kept (they cover behavior, not the renamed library; renaming the
.cpp files would just churn git history). E.16 still owns deletion
of the legacy IConduit family; it's the next big step.

WildPalmsRuntime now PUBLIC-links Kalburator::Sync. Previously it
had only a PRIVATE include path (added in E.15a for
installsourcecollector.cpp); the fullsync impls implement
Kalburator interfaces with virtual methods, so vtable emission
needs the actual link.
```

- [ ] **Step 3: Append memory index entry**

Add after the existing E.15a line in MEMORY.md:

```
- [project_phase_e15b_fullsync_runtime.md](project_phase_e15b_fullsync_runtime.md) — E.15b landed 2026-04-26; fullsync static lib folded into WildPalmsRuntime; mechanical, no behavior change
```

- [ ] **Step 4: Commit the parent spec change**

```bash
cd /home/clinton/dev/WildPalms
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "docs(phase-e): mark E.15b (fullsync→runtime relocation) landed 2026-04-26"
```

The memory updates persist locally; no commit needed.

---

## Self-review checklist

- [ ] Every task lists exact file paths.
- [ ] No placeholders (TBD/TODO/etc.).
- [ ] CMake link target consistency: `WildPalmsRuntime` everywhere `WildPalmsFullSync` used to appear.
- [ ] Header guard prefix consistency: `WILDPALMS_RUNTIME_*` in every renamed header.
- [ ] Final commit covers all of Tasks 1-4 atomically (intermediate states don't build).
- [ ] Acceptance criteria from spec map to plan steps:
  - "cmake configures cleanly" → Task 4 Step 3
  - "ctest 76/76 passes" → Task 4 Step 6
  - "git log --follow shows history preserved" → side effect of Task 1's `git mv`
  - "no WildPalmsFullSync / src/fullsync references remain" → Task 4 Step 7
  - "memory + spec row" → Task 5

---

**Total tasks:** 5 (atomic 4-task commit then a memory/spec commit).
**Expected pre-E.15b test count:** 76 → post-E.15b: 76 (no new tests; relocation is behavior-preserving).
