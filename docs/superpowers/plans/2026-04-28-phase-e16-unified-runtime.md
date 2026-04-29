# Phase E.16 — Unified Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans (or
> superpowers:subagent-driven-development for parallelizable steps).

## Progress log — 2026-04-28

**Landed in seven commits on `main`:**

| Commit  | Scope                                                                                        |
| ------- | -------------------------------------------------------------------------------------------- |
| 72ca4c7 | Spec + plan committed.                                                                       |
| de1dbf7 | Task 1 (synctypes relocation) + Task 2/3 (SyncRunner impl + 11 tests).                       |
| 47bb35b | Task 5 (all 7 plugins' legacy halves deleted; submodule pointer bumps + superproject diff). |
| cc8e56e | Plan progress checkpoint mid-session.                                                        |
| a7323c3 | Task 4 (DeviceSession + DeviceWorker SyncRunner overloads; KF6MainWindow menu-handler swap). |
| 4b43f89 | Real `PalmDeviceConnection` wired into SyncRunner via new `pilotlinkconnectionfactory`. Resolves the AUTOMOC cycle by compiling the factory inside `WildPalmsCore` (Core PRIVATE-links PalmDevice; PalmDevice gets Core's include paths via `INTERFACE_INCLUDE_DIRECTORIES` query, no link). PalmDevice's AUTOMOC disabled (no Q_OBJECT classes there). |
| 167d914 | `SyncRunner::run` startTime preservation across per-mode dispatch (was reporting 0 ms).      |

**Real-device smoke test (Palm m505, 2026-04-28):**

- Profile: `~/PalmSync/Clinton/`.
- HotSync via Tools menu → routed through SyncRunner → BlobSyncEngine (`twoWayWithBaseline` against per-plugin LocalBlobBackend, baselines persisted to `<state-dir>/.wildpalms-sync.db`).
- 621 records flowed Palm → PC: 582 calendar (`palm:calendar/<slot>/` `.ics`), 21 contacts (`palm:contact/<slot>/` `.vcf`), 9 memos (`palm:memo/` `.md` with YAML frontmatter), 9 todos (`palm:todo/<slot>/` `.ics`).
- Stats split `Palm: Created: 0 / PC: Created: 621` is correct first-sync semantics (engine processes `hasA && !hasB && !hasBase` records by creating on B without touching A).

**Final test count:** ctest 71/71 passing. Eleven new SyncRunner test methods cover all six SyncMode dispatch paths against MockBlobBackend; four legacy mapper tests deleted with their mappers.

**Status of original tasks:**

- ✅ Task 1 — `synctypes.h` relocated.
- ✅ Task 2 — `SyncRunner` skeleton + first test.
- ✅ Task 3 — Six SyncMode policies + tests.
- ✅ Task 4 — App-layer call-site migration (DeviceSession/DeviceWorker overloads + KF6MainWindow menu wiring + `m_syncRunner->setKPilotLink()` lifecycle).
- ✅ Task 5 — Plugin legacy halves deleted across all 7 plugins.
- ⏸ Task 6 — Deletion of `src/sync/`, `ConduitManager`, IConduit family. **Deferred.** The legacy code still builds and is constructed at startup, but is unreachable from the Tools menu (and from the deleted plugin manifests). Removing it requires migrating `InteractiveConflictHandler` first (see Deferred Items below).
- ⏸ Task 7 — `WildPalms::FullSync` → `WildPalms::Runtime` namespace rename. **Deferred.** Cosmetic; will ride along with Task 6.
- ⏸ Task 8 — Final spec row flip + memory entry. **Partial.** Memory entry written for "E.16 partial". Parent spec row not flipped to ✅ since Task 6/7 still pending; instead, recorded as "Landed (partial) 2026-04-28".

## Deferred items (carried into a future phase)

1. **`InteractiveConflictHandler` rebind to `Kalburator::Sync::QSyncCore::ConflictHandler`.** Currently the handler is wired against WP-internal `QSyncCore::ConflictHandler` (defined in `src/sync/qsynccore/conflictpolicy.h`); SyncRunner does not consult it. Conflicts on the new path auto-resolve via per-mode policy (NewerWins for HotSync/FullSync, source/target wins for Copy modes). UX regression — no interactive prompts on conflict. **Required before `src/sync/` can be deleted.**

2. **WebCalendar plugin cross-thread parenting bug.** Smoke-test log:
   > `QObject: Cannot create children for a parent that is in a different thread. (Parent is WildPalms::WebcalPlugin::WebcalBackendPlugin, parent's thread is mainThread, current thread is QThread(...))`
   The plugin instance is created on the main thread; `SyncRunner::run()` calls `createBackends()` from the worker thread. WebCal plugin's body does `new SomeQObject(this)` with `this` = the plugin, violating Qt's same-thread parenting rule. Currently harmless because no WebCal feeds were configured during the smoke test (the bad backend goes unused), but will surface once feeds are added. Fix lives in the WebCal submodule — drop the parent-this parenting in `createBackends`.

3. **Multi-collection plugins re-read Palm DB N times per sync.** Calendar (1 collection) reads DatebookDB once. Contacts/ToDos (4 collections each, one per category slot in their AppInfo) trigger `loadRecords()` per collection, and the plugin's blob backend re-reads the entire Palm DB each time and filters by category. 4× DLP I/O over USB. Functionally correct; performance optimization. Fix: per-DB read cache inside each plugin's blob backend.

4. **Cross-id-space duplicates on second HotSync (untested).** `LocalBlobBackend` rewrites record ids to absolute file paths; the engine matches records by id-string equality across both backends. On the second sync, the engine sees PC-side records as "new" because their ids (paths) don't match Palm-side ids (`MemoDB:N`). Not yet observed by the user — first sync only. Fix path: per-plugin `IDMappingStore` between Palm and Local sides (matches the V2 plugin tests' MockBlobBackend approach but persistent).

5. **Library-graph unused-include drift.** kf6mainwindow.cpp now has dead includes (`categoryinfo.h`, `syncstate.h`, `palmdeviceconnection.h`). clangd flags them; removing is mechanical and can ride with Task 6.

## Resumption guide

When picking E.16 back up (probably under a new phase row, e.g. E.16b):

1. Plan a focused chunk for the InteractiveConflictHandler rewrite. The shape mismatch between WP's `QSyncCore::ConflictHandler` (operates on `ConflictRecord` with `source`/`target` content blocks + `ConflictPolicy` parameter) and Kalburator's `Kalburator::Sync::QSyncCore::ConflictHandler` (same shape, different namespace) means much of the body can port directly; the surgery is mostly include paths and namespace prefixes plus the `ConduitLookupFn` substitution.
2. Once the handler talks to libkalburator's types, register it on SyncRunner's `ConflictHandlerRegistry` per-plugin-id before each `engine.twoWayWithBaseline()` call.
3. Then `src/sync/` can be deleted in one atomic commit (Core no longer references any types from it).
4. Then namespace rename + final spec/memory flip.

Earlier sub-task descriptions below are kept for reference; many are now history.



**Goal:** Delete legacy `IConduit` ABI + Client-Mode runtime; introduce
`WildPalms::Runtime::SyncRunner` to drive the new ABI under all six
Tools-menu sync modes; migrate app-layer call sites; preserve user-visible
UX. End state: `src/sync/` gone, `ConduitManager` gone, all seven plugins
loaded only via `BackendPluginManager`, WP `ctest` green.

**Spec:** `docs/superpowers/specs/2026-04-28-phase-e16-unified-runtime-design.md`.

---

## File map

```
RELOCATED:
  src/sync/synctypes.h → src/core/synctypes.h     (git mv + include path updates)

NEW:
  src/runtime/syncrunner_wp.{h,cpp}                    (~600 LOC orchestrator)
  src/runtime/palmbackuprestore.{h,cpp}                (~250 LOC backup/restore impl)
  tests/runtime/tst_syncrunner.cpp                     (~400 LOC, 6 test methods)

MODIFIED:
  src/runtime/CMakeLists.txt                           (+SyncRunner sources)
  src/runtime/{conflictresolver,conflictpresenter,
               calendarcollection,synchost,
               syncconfigstore}_wp.{h,cpp}             (namespace rename)
  src/kf6/kf6mainwindow.{h,cpp}                        (drop SyncEngine + ConduitManager;
                                                        wire SyncRunner via DeviceSession)
  src/palm/devicesession.{h,cpp}                       (requestSync signature swap)
  src/palm/deviceworker.{h,cpp}                        (doSync signature swap)
  src/app/interactiveconflicthandler.{h,cpp}           (rewrite against new types)
  src/app/conflictdialog.{h,cpp}                       (drop IConduit dependency)
  src/CMakeLists.txt                                   (drop kf6 conduitmanager + sync subdir)
  src/kf6/CMakeLists.txt                               (drop conduitmanager from sources)
  src/plugins/*/CMakeLists.txt                         (drop V2 toggle, V2 unconditional)
  tests/CMakeLists.txt                                 (drop legacy tests, add SyncRunner)
  tests/test_fullsync_*.cpp                            (namespace use updates)

DELETED:
  src/sync/                                            (whole tree minus relocated synctypes.h)
  src/core/iconduit.h
  src/core/isyncconduit.h
  src/core/itoolconduit.h
  src/kf6/conduitmanager.{h,cpp}
  src/plugins/{memo,calendar,todos,contacts,webcalendar,plucker,install}/{name}conduit.{h,cpp}
  src/plugins/{memo,calendar,todos,contacts,webcalendar,plucker,install}/{name}-conduit.json
  tests/test_syncengine.cpp
  tests/test_localfilebackend.cpp
  tests/test_syncstate.cpp
  tests/test_conflict.cpp                              (review + delete or rewrite)
  tests/test_multidatabase.cpp                         (review + delete or rewrite)
```

---

## Strategy

The plan is sequenced so the build stays linkable at each task boundary
where reasonable; deletes batch at the end. The `WILDPALMS_*_PLUGIN_V2`
toggles default ON so the V2 plugins are always loaded — flipping them
to unconditional and deleting the legacy halves is the same patch.

We use subagents for parallelizable mechanical batches (per-plugin
deletions, namespace rename) and execute the non-trivial code (SyncRunner
+ tests, call-site migration) in the main session.

---

## Task 1: Relocate `synctypes.h`

- [ ] Step 1: `git mv src/sync/synctypes.h src/core/synctypes.h`. Update header guard `SYNCTYPES_H` → `WILDPALMS_CORE_SYNCTYPES_H`.
- [ ] Step 2: Find all includes of `"sync/synctypes.h"`, `"../sync/synctypes.h"`, `"synctypes.h"` (when relative to src/sync).
  ```
  grep -rln 'sync/synctypes.h\|"synctypes.h"' src/ tests/
  ```
- [ ] Step 3: Rewrite each to `#include "core/synctypes.h"` or relative-path equivalent. Record the per-file include style; many use angle-bracket-with-include-dir, others use double-quote relative paths. Match the surrounding style.
- [ ] Step 4: Verify build still compiles (legacy code still links — `src/sync/conduit.h` etc. include `synctypes.h` and need the new path):
  ```
  cmake --build build-dev 2>&1 | tail -20
  ```
- [ ] Step 5: Don't commit — work continues into Task 2.

## Task 2: Build `SyncRunner` skeleton + first test

Test-driven. Skeleton compiles + links + the simplest mode (HotSync against a single plugin) passes before adding more modes.

- [ ] Step 1: Create `src/runtime/syncrunner_wp.h` with the API per spec §"New: WildPalms::Runtime::SyncRunner". Empty `run(SyncMode)` initially (just emits started + finished with empty stats). Namespace `WildPalms::Runtime` (already adopting the rename).
- [ ] Step 2: Create `src/runtime/syncrunner_wp.cpp` with the empty stub.
- [ ] Step 3: Add to `src/runtime/CMakeLists.txt` sources list.
- [ ] Step 4: Create `tests/runtime/tst_syncrunner.cpp`. First test method `runHotSyncWithSingleStubPlugin_dispatchesToBlobSyncEngine` — registers a stub `IBackendPlugin` that returns a `MockBlobBackend` Palm side, creates a `LocalBlobBackend` PC side rooted in `QTemporaryDir`, calls `runner.run(HotSync, {"stub"})`, verifies `blobSyncEngine.twoWayWithBaseline` ran (one record propagated end-to-end).
- [ ] Step 5: Wire `tests/CMakeLists.txt` to build the new test (use `add_runtime_test`).
- [ ] Step 6: Implement enough of `SyncRunner::run` to make the test pass. Algorithm:
  - For each enabled plugin id, look up the plugin via `BackendPluginManager::plugin(id)`.
  - Call `plugin->createBackends(host, device)`. Take ownership of the returned `IBlobBackend*`.
  - Construct (or look up) the per-plugin `LocalBlobBackend` rooted at `<syncPath>/<plugin-id>/`.
  - Construct the per-plugin `BlobBaselineStore` rooted at `<stateDir>/<plugin-id>/baseline/`.
  - Iterate `plugin-blob->availableCollections()`. For each collection, call `BlobSyncEngine::twoWayWithBaseline(plugin-blob, local-blob, collectionId, mappingId=plugin-id+":"+collectionId, baselineStore, handlers=conflictRegistry, conflicts=conflictStore, policy=defaultPolicy)`.
  - Aggregate stats into the `SyncResult`.
- [ ] Step 7: Build + run test:
  ```
  cmake --build build-dev --target tst_syncrunner
  ctest --test-dir build-dev -R tst_syncrunner --output-on-failure
  ```
  Expect: PASS. If not, debug before adding more modes.
- [ ] Step 8: Don't commit yet.

## Task 3: Implement remaining `SyncMode` policies

For each remaining mode, add a test method first (red), then make it green.

- [ ] Step 1: `runFullSync_invalidatesBaseline_treatsAsFirstSync`. FullSync clears the per-plugin baseline before invoking `twoWayWithBaseline`. Verify by populating a baseline that disagrees with current state and observing FullSync overrides it.
- [ ] Step 2: `runCopyPalmToPC_palmWinsOnConflicts`. Stage a record on both sides with different content. After CopyPalmToPC, PC content matches Palm content. Implementation: use a `ConflictPolicy` set to `PalmWins` for this run.
- [ ] Step 3: `runCopyPCToPalm_pcWinsOnConflicts`. Mirror of above with `PCWins`.
- [ ] Step 4: `runBackup_dumpsPalmDatabasesToArchive_skipsPC`. Backup bypasses BlobSyncEngine. Add `src/runtime/palmbackuprestore.{h,cpp}` housing `dumpAllDatabases(PalmDeviceConnection*, const QString &archiveDir)`. Test verifies the archive directory contains one `.pdb` per claimed database and the PC-side `LocalBlobBackend` is unchanged.
- [ ] Step 5: `runRestore_pushesArchiveBackToPalm_overwritesPalmState`. `restoreAllDatabases(PalmDeviceConnection*, const QString &archiveDir)`. Verify Palm-side mock receives the records.
- [ ] Step 6: Port any backup/restore wire details from `src/sync/conduit.cpp::Conduit::backup()` / `Conduit::restore()` into `palmbackuprestore.cpp`. Read these methods carefully — they're the spec.
- [ ] Step 7: Build + run all six tests pass:
  ```
  ctest --test-dir build-dev -R tst_syncrunner --output-on-failure
  ```
- [ ] Step 8: Don't commit yet.

## Task 4: Migrate app-layer call sites to SyncRunner

Now that SyncRunner works in isolation, swap it into the app.

- [ ] Step 1: `DeviceSession::requestSync` signature change.
  - In `src/palm/devicesession.h`: replace `void requestSync(Sync::SyncMode mode, Sync::SyncEngine *engine)` with `void requestSync(Sync::SyncMode mode, WildPalms::Runtime::SyncRunner *runner, const QStringList &enabledPluginIds)`.
  - In `src/palm/devicesession.cpp`: update implementation. The `QMetaObject::invokeMethod(m_worker, "doSync", ...)` invocation forwards the runner pointer + mode + plugin ids instead of mode + conduit ids + engine.
  - Drop the `#include "../sync/syncengine.h"` and `#include "../sync/synctypes.h"` (synctypes.h moves to core; pick up via `#include "core/synctypes.h"`).
  - `m_pendingSyncMode`, `m_pendingSyncEngine` member: drop the SyncEngine pointer. Keep `m_pendingSyncMode`. Add `m_pendingRunner` + `m_pendingPluginIds`.
- [ ] Step 2: `DeviceWorker::doSync` signature change.
  - In `src/palm/deviceworker.h`: replace `void doSync(int mode, const QStringList &conduitIds, Sync::SyncEngine *engine, const QString &stateDir, const QString &syncPath)` with `void doSync(int mode, const QStringList &enabledPluginIds, WildPalms::Runtime::SyncRunner *runner)`.
  - In `src/palm/deviceworker.cpp`: replace body. The new body:
    - Calls `dlp_OpenConduit(m_socket)` (preserved).
    - Wires runner cancellation: `connect(this, ..., runner, &SyncRunner::requestCancel, ...)` or sets a callback.
    - Calls `runner->run(static_cast<Sync::SyncMode>(mode), enabledPluginIds)`.
    - Forwards runner signals (progress, log, finished) to its own outgoing signals.
  - Drop legacy includes.
- [ ] Step 3: `KF6MainWindow` migration.
  - In `src/kf6/kf6mainwindow.h`: drop `class ConduitManager;` forward decl, drop `m_conduitManager`, `m_syncEngine`, `m_conduitPages`, `m_conduitGUIClients`. Add `m_syncRunner` (owned `WildPalms::Runtime::SyncRunner*`).
  - In `src/kf6/kf6mainwindow.cpp`: drop `#include "conduitmanager.h"`, `#include "../sync/syncengine.h"`. Drop `initializeSyncEngine()` entirely. Rename `initializeConduits()` → `initializePlugins()`; body shrinks to only the `m_backendPluginManager` + `m_syncRunner` construction. Drop `onConduitLoaded`, `onConduitUnloading`. Update menu handlers (`onHotSync`/`onFullSync`/`onCopyPalmToPC`/`onCopyPCToPalm`/`onBackup`/`onRestore`) to call `m_session->requestSync(mode, m_syncRunner, m_enabledPluginIds)`.
  - The `enabledPluginIds` list comes from `m_backendPluginManager->loadedPluginIds()` (or equivalent — add the helper if missing).
  - View-page wiring: existing `onConduitLoaded` adds a KPageWidgetItem per legacy view. Replace with the V2 equivalent in `onBackendPluginLoaded` — call `plugin->hasMainView()` and `createMainView()` (already in IBackendPlugin). Most plugins already have these wired in their backend-plugin classes; some (memo, contacts) need the existing `MemoView`/`ContactView` widgets to be returned.
  - **Side-task:** for each plugin that has a `view.{h,cpp}` but doesn't yet implement `hasMainView`/`createMainView` in its `*BackendPlugin`, add the methods. Quick hop through the seven plugins.
- [ ] Step 4: `InteractiveConflictHandler` migration.
  - Read `src/app/interactiveconflicthandler.{h,cpp}` and `src/app/conflictdialog.{h,cpp}`.
  - Replace `IConduit*` and `Sync::Conduit*` references with the new conflict types from `Kalburator::Sync::QSyncCore::ConflictHandler` API.
  - Goal: `InteractiveConflictHandler` provides a `ConflictHandler*` that `BlobSyncEngine` can call into via the `ConflictHandlerRegistry`. Wire `m_syncRunner` to register this handler via `m_backendPluginManager`.
  - The widgets `conflictdialog.{h,cpp}`, `conflictreviewwidget.{h,cpp}`, `conflictreviewdialog.{h,cpp}` are pure UI and stay; their consumed types come from `core/synctypes.h` which survives.
- [ ] Step 5: Configure + build:
  ```
  cmake -S . -B build-dev
  cmake --build build-dev 2>&1 | tail -30
  ```
  Expect linkable build with legacy plugin halves still present (V2 toggles still ON, conduits side by side).
- [ ] Step 6: Run ctest:
  ```
  ctest --test-dir build-dev --output-on-failure
  ```
  Expect: pre-deletion baseline still passes (≥ 76 + 1 new SyncRunner test = ≥ 77).
- [ ] Step 7: Don't commit — proceed to deletion.

## Task 5: Delete legacy plugin halves (subagent-parallelizable)

For each of the seven plugins, delete the legacy conduit + manifest, drop the V2 toggle in CMake, make V2 unconditional. Each plugin is independent.

- [ ] Step 1: Dispatch a single subagent per plugin in parallel using superpowers:dispatching-parallel-agents. Each subagent's prompt:
  > Plugin: `<name>` at `src/plugins/<name>/`.
  > Delete: `<name>conduit.cpp`, `<name>conduit.h`, `<name>-conduit.json` (and any other `*-conduit.json` for the plugin), plus any extra legacy-only sources you find.
  > Edit `CMakeLists.txt`: delete the `option(WILDPALMS_<NAME>_PLUGIN_V2 ...)` line and the surrounding `if (WILDPALMS_<NAME>_PLUGIN_V2)` / `endif()` wrapper, leaving the V2 sources unconditional. Drop any references to the deleted files from the sources list.
  > Verify `grep -rn "<Name>Conduit\|<name>conduit.h\|<name>-conduit.json" src/ tests/` returns nothing inside `src/plugins/<name>/`.
  > Do not commit.
- [ ] Step 2: After all subagents return, configure + build:
  ```
  cmake -S . -B build-dev 2>&1 | tail -10
  cmake --build build-dev 2>&1 | tail -30
  ```
  Expect: legacy ConduitManager will fail to load now-deleted manifests but compilation/linking still succeeds.
- [ ] Step 3: Run ctest. The legacy `test_syncengine` / `test_localfilebackend` / `test_syncstate` tests may already start failing because they construct conduits via ConduitManager which can't find them anymore. Note failures but don't fix — Task 6 deletes those tests.
- [ ] Step 4: Don't commit yet.

## Task 6: Delete legacy ABI + ConduitManager + src/sync/

The deletion can land atomically with the prior task's plugin deletions in one big commit, since intermediate states don't fully build/test.

- [ ] Step 1: `KF6MainWindow` already had references to ConduitManager/SyncEngine ripped out in Task 4. Verify the remaining tree:
  ```
  grep -rn "IConduit\|ConduitManager\|Sync::SyncEngine\|Sync::Conduit\b\|isyncconduit\|itoolconduit\|src/sync/" src/ tests/ --include='*.h' --include='*.cpp' | grep -v 'src/sync/' | grep -v '\.json'
  ```
  Anything that surfaces here is a stranded reference; fix in this task.
- [ ] Step 2: Delete the directory:
  ```
  cd /home/clinton/dev/WildPalms
  git rm -r src/sync
  git rm src/core/iconduit.h src/core/isyncconduit.h src/core/itoolconduit.h
  git rm src/kf6/conduitmanager.h src/kf6/conduitmanager.cpp
  ```
- [ ] Step 3: Update `src/CMakeLists.txt`:
  - Drop `add_subdirectory(sync)` if present.
  - Drop `WildPalmsSync` (or whatever the legacy library target was) from any `target_link_libraries` line.
- [ ] Step 4: Update `src/kf6/CMakeLists.txt`: drop `conduitmanager.cpp` / `conduitmanager.h` from sources.
- [ ] Step 5: Update `tests/CMakeLists.txt`:
  - Drop `add_executable(test_syncengine ...)`, `test_localfilebackend`, `test_syncstate` blocks and their `add_test` lines.
  - Review `test_conflict` and `test_multidatabase`. If they test legacy-only features, delete. If they cover surviving types (e.g. `test_conflict.cpp` may test `Sync::ConflictResolution` enum behavior that still applies), retain after fixing includes (`#include "core/synctypes.h"`).
  - `git rm tests/test_syncengine.cpp tests/test_localfilebackend.cpp tests/test_syncstate.cpp` (and others if confirmed legacy).
- [ ] Step 6: Configure + build:
  ```
  cmake -S . -B build-dev 2>&1 | tail -20
  cmake --build build-dev 2>&1 | tail -30
  ```
  Expect: clean build. If errors:
  - "undefined reference to `Sync::SyncEngine::...`" → app-layer migration in Task 4 missed a spot; fix.
  - "fatal error: sync/synctypes.h" → an include path slipped through Task 1; fix.
  - "fatal error: iconduit.h" → a stranded include; fix.
- [ ] Step 7: Run ctest:
  ```
  ctest --test-dir build-dev --output-on-failure
  ```
  Expect: clean pass.

## Task 7: Cosmetic — `WildPalms::FullSync` → `WildPalms::Runtime`

Mechanical sed across the renamed runtime files + any test fixtures.

- [ ] Step 1:
  ```
  cd /home/clinton/dev/WildPalms
  for f in src/runtime/calendarcollection_wp.h src/runtime/calendarcollection_wp.cpp \
           src/runtime/conflictpresenter_wp.h  src/runtime/conflictpresenter_wp.cpp  \
           src/runtime/conflictresolver_wp.h   src/runtime/conflictresolver_wp.cpp   \
           src/runtime/syncconfigstore_wp.h    src/runtime/syncconfigstore_wp.cpp    \
           src/runtime/synchost_wp.h           src/runtime/synchost_wp.cpp; do
      sed -i 's/WildPalms::FullSync/WildPalms::Runtime/g' "$f"
  done
  grep -rln "WildPalms::FullSync\|namespace FullSync" src/ tests/
  ```
  Iterate the sed across any other surviving references until grep returns nothing.
- [ ] Step 2: Build + ctest:
  ```
  cmake --build build-dev 2>&1 | tail -10 && ctest --test-dir build-dev --output-on-failure
  ```
  Expect: green.

## Task 8: Final verification + commit + spec/memory updates

- [ ] Step 1: Final cleanliness sweep:
  ```
  grep -rn "IConduit\|ConduitManager\|Sync::SyncEngine\|Sync::Conduit\b\|isyncconduit\|itoolconduit\|src/sync/\|WildPalms::FullSync" src/ tests/
  ```
  Expect: zero hits.
- [ ] Step 2: Commit the bulk change as one atomic commit per the established pattern (or split into 2-3 commits if size warrants):
  ```
  git add -A
  git status --short    # sanity-check before commit
  git commit -m "$(cat <<'EOF'
  refactor(runtime): collapse Client Mode + Full Sync into one runtime (E.16)
  
  ... full message describing deletions, the SyncRunner, the namespace
  rename, and the kept-UX-modes ...
  EOF
  )"
  ```
- [ ] Step 3: Update parent spec row (E.16) in `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` from pending to ✅ with landed-2026-04-28 stamp + plan link + LOC summary.
- [ ] Step 4: Commit spec update separately:
  ```
  git commit -m "docs(phase-e): mark E.16 (unified runtime) landed 2026-04-28"
  ```
- [ ] Step 5: Write memory entry `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e16_unified_runtime.md`:
  - type: project
  - body: phase landed; legacy IConduit deleted; SyncRunner is the new orchestrator; FullSync namespace gone; SyncMode preserved as UX policy in core/synctypes.h.
  - Append index entry to MEMORY.md.

---

## Self-review checklist

- [ ] Tasks ordered so each boundary has a sane build state where reasonable.
- [ ] No placeholders / TBDs in plan body (only the file map's `(TBD)` for LOC counts that can't be predicted exactly).
- [ ] Acceptance criteria from spec map to plan steps:
  - "WP cmake builds" → Tasks 4, 6, 7 each verify build.
  - "WP ctest passes" → Tasks 3, 4, 6, 7 each verify ctest.
  - "grep returns no IConduit / ConduitManager / src/sync/" → Task 8 Step 1.
  - "grep returns no WildPalms::FullSync" → Task 7 Step 1.
  - "All 6 menu actions still drive a sync" → Task 3 covers all six modes via tst_syncrunner.
  - "Parent spec row + memory entry" → Task 8 Steps 3-5.
- [ ] Risks from spec section 5 each have a plan-step landing point:
  - Backup/Restore wire fidelity → Task 3 Step 6.
  - FullSync baseline-invalidation → Task 3 Step 1.
  - InteractiveConflictHandler shape adapter → Task 4 Step 4 surfaces if needed.
  - Pilot-link socket vs PalmDeviceConnection → Task 4 Step 2.
  - Cross-plugin pairing (palm:calendar/<slot>) → not touched in E.16; webcal/plucker remain mirror-only and SyncRunner skips them or invokes their own action paths (e.g. install action). Surface in plan during Task 2 Step 6 if it blocks.

---

**Total tasks:** 8.
**Approximate effort:** ~600 LOC new, ~9,000 LOC deleted, 6 new test methods, 1 large refactor commit + 1 spec-update commit.
**Final test count target:** post-E.15b 76 baseline minus 3-5 deleted legacy tests plus 1 new tst_syncrunner = ≥ 72. ctest must be green.
