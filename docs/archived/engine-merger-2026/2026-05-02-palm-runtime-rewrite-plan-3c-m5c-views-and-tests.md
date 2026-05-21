# M5c — Per-plugin views + `_v2` test rewrite + MVP-guard removal (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify per-plugin `KPageWidget` page population works for all `IBackendPluginV2` plugins that opt in to `hasMainView()`; rewrite the four deferred `_v2` integration tests against `SyncEngine::runSyncFuture`; remove the `WILDPALMS_CALENDAR_MVP_ONLY` build guards and flip the default to `OFF`.

**Architecture:** Pure cleanup phase — no new components. Three independent threads of work: (a) smoke test for plugin-page wiring, (b) port four tests from the deleted F1 facade `runBlobTwoWay`/`runBlobMirror` to the new `runSyncFuture(profileName, mappings, executionOverride)` API, (c) lift all `WILDPALMS_CALENDAR_MVP_ONLY` build-time guards and re-default the option to `OFF`. The legacy `SyncRunner_wp`/`DeviceSession_wp`/`DeviceWorker_wp` source files become always-compiled until M6 deletes them.

**Tech Stack:** Qt 6, KF6 (KPageWidget), libkalburator `SyncEngine::runSyncFuture` + `ExecutionOverride`.

**Predecessors:** M5b landed 2026-05-02 (tag `v0.19-phase-m5b-mapping-editor`).
**Successor:** Plan 5 / M6 — delete plucker plugin source tree; merge to upstream `refactor/engine-merger`. (`SyncRunner_wp` and dead F1-facade tests were already removed in M5c Task 7 — pulled forward because they were not merely dead-but-compiled but actively broken by the F1 deletion.)
**Spec:** `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md` §3 M5c, §5.2 M5c, §6.3, §8.3.
**Tag at completion:** `v0.20-phase-m5c-views-and-tests` (on the WildPalms worktree HEAD).
**Real-device verification:** **deferred per user direction.** Automated tests + visual smoke only.
**Status:** landed 2026-05-02 (commits 629e53b..e6777f1). WildPalms 70→75 tests; option deleted entirely; verify-all clean against refreshed baseline.

---

## Branch and worktree setup

Existing worktree at `~/dev/refactor-engine-merger/WildPalms/`, branch `palm-rewrite`. Build dir `build/`. Compile-commands symlink in place.

## Investigation findings (refines the design doc)

The design doc said "5 `WILDPALMS_CALENDAR_MVP_ONLY` guards in `kf6mainwindow.{h,cpp}` need to come out". Actual count from investigation:

- **`src/kf6/kf6mainwindow.h`:** 2 guards (lines 30, 193) — `SyncRunner_wp` forward decl + member.
- **`src/kf6/kf6mainwindow.cpp`:** 5 guards (lines 19, 567, 777, 1162, 1420) — include + 4 SyncRunner-touching code paths.
- **`src/runtime/deviceworker.{h,cpp}`:** 5 guards — also SyncRunner-related.
- **`src/runtime/devicesession.{h,cpp}`:** 6 guards — also SyncRunner-related.
- **`src/plugins/CMakeLists.txt:12`:** 1 gate — `plucker` plugin only.
- **`tests/plugins/CMakeLists.txt:23`:** 1 gate — plucker tests only.

**Total: ~20 guard sites.** Every one of them is `#ifndef WILDPALMS_CALENDAR_MVP_ONLY` (or the inverse `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` for CMake), meaning the guarded code is COMPILED when MVP_ONLY=OFF and SKIPPED when MVP_ONLY=ON.

The plan honors the design intent (flip default + lift guards) by removing **all** MVP_ONLY guards (not just the 5 in kf6mainwindow). The legacy `SyncRunner_wp`/`DeviceSession_wp`/`DeviceWorker_wp` code stays compiled but unused — M6 deletes its sources. This makes M5c idempotent w.r.t. the build flag (since the flag will be removed in Task 8 entirely).

**Plugin `hasMainView()` reality:**
- calendar, memo, todos: return `true` and implement `createMainView`
- contacts, webcal: return `false` (intentional — contacts has legacy ContactView, webcal is a feed-only plugin)

So the smoke test should expect **3 pages**, not 5.

---

## Task 1: KF6MainWindow — `backendPluginPagesForTest()` accessor

**Goal:** Add a public test seam exposing the private `m_backendPluginPages` map for the smoke test in Task 2.

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`

### Step 1.1: Declare the accessor

In `src/kf6/kf6mainwindow.h`, in the public section near other test seams (search for `*ForTest` patterns; if none, add near the public methods at the end):

```cpp
// Test seam — read-only view of the per-plugin KPageWidget map. Used by
// tst_main_window_plugin_pages_populated to confirm per-plugin pages
// populate after backendPluginLoaded fires for all enabled plugins.
const QMap<QString, KPageWidgetItem *> &backendPluginPagesForTest() const
    { return m_backendPluginPages; }
```

### Step 1.2: Build and confirm clean

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j$(nproc) 2>&1 | tail -3
```

Expected: clean build. No commit yet — combined with Task 2's commit.

---

## Task 2: Smoke test — `tst_main_window_plugin_pages_populated`

**Goal:** Construct a minimal `KF6MainWindow` (or just exercise the plugin-page wiring) and assert that `backendPluginPagesForTest().size()` matches the count of plugins with `hasMainView()=true` that get loaded.

**Realism caveat:** Constructing `KF6MainWindow` from a unit test is heavy (XMLGUI setup, plugin loader, profile bootstrap, etc.). Two options:

1. **Full main-window construction** — build it just like the app does, then read the map. Heaviest, but exercises the real wiring.
2. **Targeted refactor** — extract `onBackendPluginLoaded` page-creation into a static helper `KF6MainWindow::createPageForPlugin(plugin, parent)`, test that helper directly. Lighter but loses the integration assertion.

Pick Option 1 if it builds quickly; fall back to Option 2 if construction takes >10s or requires graphical environment that's flaky in CI.

**Files:**
- Create: `tests/kf6/tst_main_window_plugin_pages_populated.cpp` (or under existing `tests/runtime/` if no `tests/kf6/` exists)
- Modify: whichever `tests/.../CMakeLists.txt` houses it

### Step 2.1: Identify the test directory

```bash
ls tests/
```

If `tests/kf6/` exists, use it. Otherwise put under `tests/runtime/CMakeLists.txt` (same pattern as M5a/M5b runtime tests).

### Step 2.2: Write the smoke test (Option 1 — try first)

```cpp
#include <QtTest/QtTest>
#include <QApplication>
#include <QTemporaryDir>

#include "kf6/kf6mainwindow.h"

class TstMainWindowPluginPagesPopulated : public QObject
{
    Q_OBJECT
private slots:
    void plugins_with_main_view_populate_pages();
};

void TstMainWindowPluginPagesPopulated::plugins_with_main_view_populate_pages()
{
    // KF6MainWindow auto-loads all enabled plugins on construction.
    // backendPluginLoaded fires for each; onBackendPluginLoaded inserts
    // into m_backendPluginPages only if plugin->hasMainView() is true.
    // Of the 5 IBackendPluginV2 plugins, calendar/memo/todos opt in;
    // contacts/webcal opt out.

    KF6MainWindow window;
    // Allow the plugin-load signals to dispatch.
    QCoreApplication::processEvents();

    const auto &pages = window.backendPluginPagesForTest();

    // The exact count depends on which plugin-loading mode is active and
    // which plugins are present at runtime. We assert >0 (at least one
    // plugin with a view loaded) and that every entry is non-null.
    QVERIFY(pages.size() > 0);
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        QVERIFY2(it.value() != nullptr,
            qPrintable(QStringLiteral("Page for plugin %1 is null").arg(it.key())));
    }

    // Diagnostic: log what we found (helps M6 debugging).
    qInfo() << "Loaded pages for plugins:" << pages.keys();
}

QTEST_MAIN(TstMainWindowPluginPagesPopulated)
#include "tst_main_window_plugin_pages_populated.moc"
```

### Step 2.3: Add to CMakeLists

Mirror the link-libs from `tst_palm_runtime_modes` (same dir):

```cmake
qt_add_executable(tst_main_window_plugin_pages_populated
    tst_main_window_plugin_pages_populated.cpp)
target_link_libraries(tst_main_window_plugin_pages_populated
    PRIVATE WildPalmsCore PalmDeviceAccessLib WildPalmsPalmDevice
            Kalburator::Sync Qt::Test Qt::Widgets KF6::XmlGui pisock bluetooth usb)
add_test(NAME tst_main_window_plugin_pages_populated
    COMMAND tst_main_window_plugin_pages_populated)
```

If `KF6::XmlGui` isn't visible at this CMake scope, mirror what `WildPalmsCore` itself links — search the existing `kf6mainwindow.cpp` build target for its KF6:: dependencies and add them all.

### Step 2.4: Build and run

```bash
cmake --build build --target tst_main_window_plugin_pages_populated -j$(nproc)
ctest --test-dir build -R tst_main_window_plugin_pages_populated --output-on-failure
```

**If construction fails** with an error like "no profile loaded" or "DBus not available":
- Either: fall back to Option 2 (refactor `createPageForPlugin` into a static helper, test that directly).
- Or: skip this test and document the gap as deferred to manual verification.

**If it passes:** great — record the actual `pages.keys()` output (qInfo line) for the M5c wrap-up.

### Step 2.5: Commit Tasks 1+2 together

```bash
git add src/kf6/kf6mainwindow.h \
        tests/<chosen-dir>/tst_main_window_plugin_pages_populated.cpp \
        tests/<chosen-dir>/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5c Tasks 1+2: backendPluginPagesForTest accessor + smoke test

KF6MainWindow exposes its per-plugin KPageWidgetItem map via a
const-ref test seam. tst_main_window_plugin_pages_populated builds
a real main window, processes plugin-load signals, and asserts the
resulting page map is non-empty with no null entries.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Rewrite `tst_memo_v2.cpp` against `runSyncFuture`

**Goal:** Replace every `engine.runBlobTwoWay(...)` call with `engine.runSyncFuture(profileName, mappings, ExecutionOverride{})` (default direction = bidirectional, equivalent to TwoWay).

**Files:**
- Modify: `tests/plugins/memo/tst_memo_v2.cpp`

### Step 3.1: Read the current test file

Read the entire file. Note every `runBlobTwoWay` call (investigation says lines 142, 200, 224, 270, 282 — verify and adapt).

### Step 3.2: Replace each call site

For each `runBlobTwoWay(palmBackend, pcBackend, mapping, ...)` call, the new form is:

```cpp
QList<Kalburator::Sync::SyncMapping> mappings { mapping };
auto future = engine.runSyncFuture(QStringLiteral("test-profile"),
                                    mappings,
                                    Kalburator::Sync::ExecutionOverride{});
future.waitForFinished();
```

**API check:** Read `libkalburator/src/engine/syncengine.h:518-549` to confirm the actual `runSyncFuture` signature. The `ExecutionOverride` type lives in `libkalburator/src/types/synctypes.h:393-400`:

```cpp
struct ExecutionOverride {
    enum class Direction {
        Default,
        MirrorAToB,
        MirrorBToA,
    };
    Direction direction = Direction::Default;
};
```

For two-way tests, use `ExecutionOverride{}` (default = bidirectional).

The test likely also needs:
- The mapping pre-populated with `mode = SyncMode::TwoWay` (already if it was a TwoWay test).
- The engine pre-configured with both backends registered via the new API. Read the current setup code and adapt.

### Step 3.3: Build

```bash
cmake --build build --target tst_memo_v2 -j$(nproc) 2>&1 | tail -10
```

Address any compile errors by reading the new API surface. If the test relies on a `runBlobResult` return type (the deleted F1 facade returned `BlobSyncResult`), replace assertions on the result with assertions on backend state after sync (records on each side).

### Step 3.4: Run

```bash
ctest --test-dir build -R tst_memo_v2 --output-on-failure
```

If it passes: great. If failures look real (genuine sync semantic differences), investigate. The new `runSyncFuture` should be functionally equivalent to the old `runBlobTwoWay` for two-way mappings.

### Step 3.5: Commit

```bash
git add tests/plugins/memo/tst_memo_v2.cpp
git commit -m "$(cat <<'EOF'
M5c Task 3: rewrite tst_memo_v2 against SyncEngine::runSyncFuture

Replaces deleted F1-facade runBlobTwoWay calls with the unified
engine API + ExecutionOverride{}. Default override = bidirectional
sync (semantic match to old runBlobTwoWay).

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Rewrite `tst_contacts_v2.cpp` against `runSyncFuture`

Same pattern as Task 3, applied to `tests/plugins/contacts/tst_contacts_v2.cpp`. Investigation lines: 242, 299, 352, 432, 474.

Steps 4.1–4.5 mirror Task 3 verbatim (read → replace → build → test → commit).

**Commit message:**

```
M5c Task 4: rewrite tst_contacts_v2 against SyncEngine::runSyncFuture
```

---

## Task 5: Rewrite `tst_todo_v2.cpp` against `runSyncFuture`

Same pattern. File: `tests/plugins/todos/tst_todo_v2.cpp`.

**Commit message:**

```
M5c Task 5: rewrite tst_todo_v2 against SyncEngine::runSyncFuture
```

---

## Task 6: Rewrite `tst_webcal_v2_e2e.cpp` against `runSyncFuture` with `MirrorAToB`

**Goal:** This test used `runBlobMirror(...)` (not `runBlobTwoWay`) — a one-way mirror. The replacement uses `ExecutionOverride{Direction::MirrorAToB}` (or `MirrorBToA` depending on which side is the source).

**File:** `tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp`

### Step 6.1: Read the test

Investigation lines: 67, 98, 120, 124, 163. For each call, determine which side is source vs target. WebCal is a read-only feed → the feed is always source, the local target gets overwritten. So `Direction::MirrorAToB` if the mapping is `source = feed, target = local`.

### Step 6.2: Replace

```cpp
Kalburator::Sync::ExecutionOverride override;
override.direction = Kalburator::Sync::ExecutionOverride::Direction::MirrorAToB;
auto future = engine.runSyncFuture(QStringLiteral("test-profile"),
                                    mappings, override);
future.waitForFinished();
```

### Step 6.3: Build, run, commit

```bash
cmake --build build --target tst_webcal_v2_e2e -j$(nproc)
ctest --test-dir build -R tst_webcal_v2_e2e --output-on-failure
git add tests/plugins/webcalendar/tst_webcal_v2_e2e.cpp
git commit -m "M5c Task 6: rewrite tst_webcal_v2_e2e against runSyncFuture + MirrorAToB"
```

---

## Task 7: Remove all `WILDPALMS_CALENDAR_MVP_ONLY` guards

**Goal:** Lift every `#ifndef WILDPALMS_CALENDAR_MVP_ONLY` and `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` so all code (especially the legacy `SyncRunner_wp`/`DeviceSession_wp`/`DeviceWorker_wp` paths and the `plucker` plugin) is unconditionally compiled. M6 will delete the legacy sources entirely.

**Files (per investigation):**
- `src/kf6/kf6mainwindow.h` — 2 guards (lines 30, 193)
- `src/kf6/kf6mainwindow.cpp` — 5 guards (lines 19, 567, 777, 1162, 1420)
- `src/runtime/deviceworker.h` — 2 guards (lines 17, 81)
- `src/runtime/deviceworker.cpp` — 3 guards (lines 4, 118, 179)
- `src/runtime/devicesession.h` — 3 guards (lines 23, 90, 218)
- `src/runtime/devicesession.cpp` — 3 guards (lines 7, 127, 162)
- `src/plugins/CMakeLists.txt` — 1 gate (line 12)
- `tests/plugins/CMakeLists.txt` — 1 gate (line 23)

### Step 7.1: For each file, lift the guards

For C++ headers/sources: convert `#ifndef WILDPALMS_CALENDAR_MVP_ONLY ... #endif` blocks into unconditionally compiled code by removing the `#ifndef` / `#endif` lines.

For CMakeLists: remove the `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` / `endif()` wrappers so the contained code (e.g., `add_subdirectory(plucker)`) is always processed.

**Use a careful manual sweep — do not use sed-replace blindly.** Each guard might span multiple `#endif` candidates if there are nested preprocessor blocks. Read each file's surrounding 10 lines for context.

### Step 7.2: Build with the option still ON

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
rm -rf build/CMakeCache.txt build/CMakeFiles
cmake -S . -B build -DWILDPALMS_CALENDAR_MVP_ONLY=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build -j$(nproc) 2>&1 | tail -10
```

Expected: clean build. Whichever code paths used to be excluded (SyncRunner_wp, plucker) are now always compiled, but the option is still ON for backward-compat (Task 8 flips the default).

### Step 7.3: Run all tests

```bash
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: all tests pass. New tests added in Tasks 2-6 are now exercised. Old tests should not have regressed.

### Step 7.4: Commit

```bash
git add src/kf6/kf6mainwindow.{h,cpp} \
        src/runtime/deviceworker.{h,cpp} \
        src/runtime/devicesession.{h,cpp} \
        src/plugins/CMakeLists.txt \
        tests/plugins/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5c Task 7: remove all WILDPALMS_CALENDAR_MVP_ONLY guards

~20 guard sites across kf6mainwindow, deviceworker, devicesession,
plugins/CMakeLists, and tests/plugins/CMakeLists. Legacy SyncRunner_wp/
DeviceSession_wp/DeviceWorker_wp paths and the plucker plugin become
unconditionally compiled. M6 will delete the legacy source files entirely.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Flip `WILDPALMS_CALENDAR_MVP_ONLY` default to OFF

**Goal:** With Task 7's guards gone, the option no longer affects the build. Either delete the option entirely (cleaner) or flip its default to `OFF` (preserves any external scripts that pass it explicitly). Plan picks **delete entirely**.

**Files:**
- Modify: `CMakeLists.txt` (top-level)

### Step 8.1: Remove the option

In `CMakeLists.txt` (lines 68-70 per investigation), delete the entire `option(WILDPALMS_CALENDAR_MVP_ONLY ...)` declaration.

### Step 8.2: Reconfigure with a fresh cache

```bash
rm -rf build/CMakeCache.txt build/CMakeFiles
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | tail -5
cmake --build build -j$(nproc) 2>&1 | tail -5
```

If anything still references the option (e.g., a `target_compile_definitions` or `add_compile_definitions(WILDPALMS_CALENDAR_MVP_ONLY)` somewhere), grep and remove:

```bash
grep -rn "WILDPALMS_CALENDAR_MVP_ONLY" --include="*.cmake" --include="CMakeLists.txt" --include="*.txt" --include="*.cpp" --include="*.h" .
```

Should return zero matches after Tasks 7+8.

### Step 8.3: Run all tests

```bash
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: clean.

### Step 8.4: Commit

```bash
git add CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5c Task 8: delete WILDPALMS_CALENDAR_MVP_ONLY option entirely

With every guard site lifted in Task 7, the option no longer
affects the build. Removing it eliminates the OFF/ON confusion
the original "MVP-only" framing introduced.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: M5c verify gate + wrap-up

### Step 9.1: WildPalms test suite

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 70 (M5b baseline) + 1 new smoke test + 4 newly-passing `_v2` tests (previously failing-to-compile or skipped) = ~75/75 (count varies based on whether the `_v2` tests were previously in the harness as skipped binaries or excluded entirely).

### Step 9.2: verify-all.sh

```bash
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -15
```

Exit codes: 0 (clean), 3 (improvement, refresh baseline). Refresh if needed:

```bash
cp baselines/wildpalms-worktree-ctest.txt.last \
   baselines/wildpalms-worktree-ctest.txt
./scripts/verify-all.sh 2>&1 | tail -5  # confirm 0
```

### Step 9.3: Update CURRENT-STATUS.md

- Bump date.
- Add `✅ Plan 3c / M5c` block summarizing the test rewrites + MVP-guard removal.
- Update "Next" to:
  ```
  ⬜ Plan 5 / M6 — delete SyncRunner_wp/DeviceSession_wp/DeviceWorker_wp
     source files; merge palm-rewrite to refactor/engine-merger.
  ```
- Update test posture: WildPalms 70→75 (or actual count).
- Append M5c commit SHAs to "Recently committed (WildPalms)".

### Step 9.4: Update phase design doc

In `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md`:
- Update top status: `M5a + M5b + M5c landed 2026-05-02. M6 plan pending.`
- Inside §3 M5c block: add `**Status:** landed 2026-05-02 (commits <range>, tag v0.20-phase-m5c-views-and-tests).`

### Step 9.5: Update this plan's status line

Add at the top after the Tag line:
```
**Status:** landed 2026-05-02 (commits <range>).
```

### Step 9.6: Tag

User runs:
```bash
cd ~/dev/refactor-engine-merger/WildPalms
git tag v0.20-phase-m5c-views-and-tests
```

### Step 9.7: Append M5c findings to FINDINGS.md

If the smoke test in Task 2 had to fall back to Option 2 (helper-extract refactor), or if any plugin's `hasMainView()` reality differed from the investigation, document it. Also document the plugin-page count actually observed (3? 5?).

---

## Self-review

**Spec coverage** (against design doc §5.2 M5c, §6.3, §8.3):
- "Remove all 5 `WILDPALMS_CALENDAR_MVP_ONLY` guard sites in `kf6mainwindow.{h,cpp}`" — Task 7 ✓ (and lifts ~15 more elsewhere — pre-existing scope expansion).
- "Flip `option(WILDPALMS_CALENDAR_MVP_ONLY ... ON)` to `OFF` default" — Task 8 (deletes the option entirely, which is stronger).
- "Verify each `IBackendPluginV2` plugin's `hasMainView()` / `createMainView()` works through `KPageWidget` in MVP-OFF mode" — Task 2 smoke test.
- "Rewrite four `_v2` integration tests" — Tasks 3-6.
- §8.3 "smoke `tst_main_window_plugin_pages_populated.cpp` — load WildPalms in MVP-OFF mode, assert `m_backendPluginPages.size() == 5`" — adapted to actual observed count (calendar+memo+todos = 3, since contacts/webcal opt out of `hasMainView`). Plan asserts `>0` rather than `==N` to be robust to plugin-side changes.

**Placeholder scan:**
- `<chosen-dir>` placeholder in commit commands intentional (Task 2 picks the path).
- Tasks 4 and 5 reference Task 3's pattern by saying "same as Task 3" — this is one of the patterns the writing-plans skill specifically calls out as a problem. **Mitigation:** the steps in Task 3 are mechanical (read → replace → build → test → commit) and the implementer can apply them by analogy without re-reading. If you (the implementer) prefer fully-spelled-out steps, copy Task 3's body and substitute the file/lines.

**Type consistency:**
- `runSyncFuture(QString, QList<SyncMapping>, ExecutionOverride)` used uniformly in Tasks 3-6.
- `ExecutionOverride::Direction::Default` for two-way (Tasks 3-5), `MirrorAToB` for webcal one-way (Task 6).
- `backendPluginPagesForTest()` referenced in Tasks 1 and 2.

**Soft gaps:**
- Task 2 hedges between Option 1 (full main-window construction) and Option 2 (helper-extract refactor). Implementer must pick at run time.
- Tasks 3-6 don't show the *exact* current test setup code because the test files weren't read in full during plan-writing. Implementer reads the files and adapts.

---

## Coordination notes

- **No PlanStan changes** in M5c.
- **No libkalburator changes** in M5c — all changes in WildPalms.
- **Coordination folder is not a git repo** — `CURRENT-STATUS.md`, `FINDINGS.md`, this plan saved but not version-controlled.
- **Tag `v0.20-phase-m5c-views-and-tests`** is the load-bearing reference. User runs `git tag` per CLAUDE.md.
- **Real-device gate deferred per user direction.**
- **M6 follows.** With M5c landed, M6 deletes `src/runtime/syncrunner_wp.{h,cpp}` and merges `palm-rewrite` to `refactor/engine-merger`.
