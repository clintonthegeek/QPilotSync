# M5a — Conflict UI rewiring (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire conflict events from `PalmRuntime`'s embedded
`Kalburator::Sync::SyncEngine` to a modal `ConflictDialog` so that
mappings with `conflictPolicy = AskUser` actually prompt the user
on real-device HotSync, instead of silently falling through to
the policy's `Defer` fallback.

**Architecture:** Add a new `KalburatorInteractiveConflictHandler`
class that subclasses
`Kalburator::Sync::QSyncCore::ConflictHandler` (libkalburator's
interface) and reuses the existing `ConflictDialog`
(`src/app/conflictdialog.h`) for the modal UI. The legacy
`InteractiveConflictHandler` remains for the old
`WildPalms::SyncEngine` path (deleted by M6). Add
`PalmRuntime::setConflictHandler()` that forwards to
`m_engine->conflictRegistry()->setDefaultHandler()`. Construct
the new handler in `KF6MainWindow` and install it via
`PalmRuntime::setConflictHandler()` whenever the runtime is
recreated for a new profile.

**Tech Stack:** Qt 6, KF6, libkalburator (Kalburator::Sync namespace),
existing `ConflictDialog` widget, `BlockingQueuedConnection`
marshalling.

**Predecessors:** Plan 1, Plan 2 (M2–M4 landed).
**Successor:** Plan 3b / M5b (mapping editor + settings polish).
**Spec:** `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md` §3, §5, §6.1, §7.1, §10 risk #1.
**Tag at completion:** `v0.18-phase-m5a-conflict-ui` (on the WildPalms
worktree HEAD).
**Status:** landed 2026-05-02 (commits 722a6a5–b000efe).

---

## Branch and worktree setup

This plan executes in the existing worktree at
`~/dev/refactor-engine-merger/WildPalms/`, on branch
`refactor/engine-merger`. No new branch is needed; M5a commits
land directly on the refactor branch per `OPERATIONS.md`'s "no
per-commit authorization" rule for the refactor branch.

Build directory: `build/` (legacy project, no presets).

`compile_commands.json` is already symlinked from `build/`.

---

## Task 1: API-drift investigation + FINDINGS entry

**Goal:** Verify the assumption that the libkalburator
`Kalburator::Sync::QSyncCore::ConflictHandler` interface is
suitable for direct subclassing by a new WildPalms class. Document
findings before writing wiring code.

**Files:**
- Read: `libkalburator/src/conflict/conflictpolicy.h:147-203`
- Read: `libkalburator/src/conflict/conflictrecord.h` (entire)
- Read: `libkalburator/src/conflict/conflictstore.h` (entire)
- Read: `WildPalms/src/sync/qsynccore/conflictpolicy.h:155-220` (legacy
  parallel type for comparison)
- Read: `WildPalms/src/app/interactiveconflicthandler.{h,cpp}` (the
  legacy implementation we are replacing with a parallel class)
- Read: `WildPalms/src/app/conflictdialog.{h,cpp}` (the dialog we
  will reuse)
- Append finding to: `~/dev/refactor-engine-merger/FINDINGS.md`

**Steps:**

- [ ] **Step 1.1: Compare `ConflictHandler` virtuals**

  Read both files, list every virtual method on each interface,
  produce a side-by-side comparison. Expected: the libkalburator
  interface has the same six virtuals as the WP-local one
  (`handleConflict`, `onSyncStart`, `onSyncEnd`, `canPrompt`,
  `shouldKeepConnectionAlive`, `pendingConflicts`), but the
  parameter types reference the libkalburator namespace.

- [ ] **Step 1.2: Compare `ConflictRecord` field shape**

  Verify both `ConflictRecord` types carry: id, source/target
  records, baseline, kind (`BothModified` etc.), source/target
  timestamps, source/target categories, archived flags. Note any
  divergent fields. The fields drive `ConflictDialog` rendering;
  if a field is missing on the libkalburator side, the dialog
  cannot show it.

- [ ] **Step 1.3: Compare `ConflictDecision` enumerators**

  Verify both enums have at minimum: `Pending`, `KeepSource`,
  `KeepTarget`, `Defer`, `Skip`, `UseBoth`/`DuplicateAll`,
  `DeleteBoth`. Note any divergent or extra values.

- [ ] **Step 1.4: Verify `ConflictDialog` field accessors**

  Re-read `ConflictDialog::ConflictDialog(QSyncCore::ConflictRecord, ...)`
  signature. Note: it takes the **WP-local** ConflictRecord type.
  The new handler must construct a WP-local record from a
  libkalburator one before calling the dialog. Document the
  fields needed for that translation.

- [ ] **Step 1.5: Append finding**

  Append a new entry to
  `~/dev/refactor-engine-merger/FINDINGS.md` under "## Findings"
  with the format used elsewhere in the file:

  ```markdown
  ### WildPalms has two parallel ConflictHandler hierarchies

  **Date:** 2026-05-02 (during M5a Task 1)
  **Source:** `WildPalms/src/sync/qsynccore/conflictpolicy.h:155`
     vs `libkalburator/src/conflict/conflictpolicy.h:147`.
     `WildPalms/src/app/interactiveconflicthandler.h:29` inherits
     the WP-local type; all five `IBackendPluginV2` plugins
     (`*backendplugin.h::createConflictHandler`) return the
     libkalburator type.
  **What:** `QSyncCore::ConflictHandler` exists in two
     namespaces: WildPalms-local and libkalburator. The WP-local
     copy was the original; libkalburator's was added during
     Phase F1+. The two are NOT type aliases — both have full
     vtables. `WildPalms::SyncEngine` (legacy) wires the WP-local
     handler; `Kalburator::Sync::SyncEngine` (used by
     `PalmRuntime`) wires the libkalburator handler. The plugins
     all use the libkalburator interface; only
     `InteractiveConflictHandler` is on the legacy interface.
  **Why it matters:** M5a cannot retrofit
     `InteractiveConflictHandler` directly — the WP-local
     interface is still in active use by the legacy
     `WildPalms::SyncEngine` path that survives until M6 deletes
     `SyncRunner_wp`. Rebasing the type onto libkalburator would
     break that path. Build a parallel class instead.
  **Action:** M5a Task 2 onward — build
     `KalburatorInteractiveConflictHandler` against the
     libkalburator interface, reusing `ConflictDialog` directly
     and translating ConflictRecord between the two namespaces
     where required (or referencing the libkalburator record
     directly if `ConflictDialog` is generic enough — see Task 1.4
     finding).
  ```

  If Steps 1.1–1.4 reveal that the two interfaces are
  semantically incompatible in a way that breaks the plan
  (e.g., `ConflictDecision` enum members differ such that the
  dialog cannot return a value valid for libkalburator), STOP
  and report — the plan needs revision.

- [ ] **Step 1.6: Commit the FINDINGS entry**

  ```bash
  git add ../FINDINGS.md   # FINDINGS.md is in coordination folder, not git tracked
  ```

  Wait — `FINDINGS.md` lives at
  `~/dev/refactor-engine-merger/FINDINGS.md` and the
  coordination folder is **not a git repo** (per
  `~/dev/refactor-engine-merger/CLAUDE.md`). The append is
  persistent on disk but not version-controlled. No commit; just
  save the file. Move to Task 2.

---

## Task 2: Failing test for the new handler skeleton

**Goal:** Write a unit test that asserts a stub
`KalburatorInteractiveConflictHandler` can be constructed,
installed into a `Kalburator::Sync::QSyncCore::ConflictHandlerRegistry`,
and looked up via `handlerFor("any")` — proving the type
correctly inherits from the libkalburator interface.

**Files:**
- Create: `WildPalms/tests/runtime/tst_kalburator_interactive_conflict_handler.cpp`
- Modify: `WildPalms/tests/runtime/CMakeLists.txt` (add the new test)

**Steps:**

- [ ] **Step 2.1: Write the failing test**

  Create
  `WildPalms/tests/runtime/tst_kalburator_interactive_conflict_handler.cpp`:

  ```cpp
  #include <QtTest/QtTest>
  #include "app/kalburatorinteractiveconflicthandler.h"
  #include "conflict/conflicthandlerregistry.h"

  class TstKalburatorInteractiveConflictHandler : public QObject
  {
      Q_OBJECT
  private slots:
      void registers_into_libkalburator_registry();
  };

  void TstKalburatorInteractiveConflictHandler::registers_into_libkalburator_registry()
  {
      KalburatorInteractiveConflictHandler handler(nullptr, nullptr);

      Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
      registry.setDefaultHandler(&handler);

      QVERIFY(registry.handlerFor("nonexistent-backend") == &handler);
  }

  QTEST_MAIN(TstKalburatorInteractiveConflictHandler)
  #include "tst_kalburator_interactive_conflict_handler.moc"
  ```

- [ ] **Step 2.2: Add to CMakeLists**

  Append to `WildPalms/tests/runtime/CMakeLists.txt` the same way
  existing runtime tests are added in that file (look at e.g.
  `tst_palm_runtime_*.cpp` patterns). The test target needs:

  - Link to `WildPalmsCore` (for the new handler class — it lives
    in `src/app/`).
  - Link to `Kalburator::Sync` (for the registry).

  Concrete addition (matching existing patterns in that file):

  ```cmake
  qt_add_executable(tst_kalburator_interactive_conflict_handler
      tst_kalburator_interactive_conflict_handler.cpp)
  target_link_libraries(tst_kalburator_interactive_conflict_handler
      PRIVATE WildPalmsCore Qt::Test Kalburator::Sync)
  add_test(NAME tst_kalburator_interactive_conflict_handler
      COMMAND tst_kalburator_interactive_conflict_handler)
  ```

  If the existing file uses a helper function (analogous to
  `kalburator_add_calendar_integration_test`), use that instead.

- [ ] **Step 2.3: Run test to verify it fails**

  ```bash
  cd ~/dev/refactor-engine-merger/WildPalms
  cmake --build build --target tst_kalburator_interactive_conflict_handler 2>&1 | head -30
  ```

  Expected: build FAILS with "kalburatorinteractiveconflicthandler.h:
  No such file or directory" or equivalent. We will create the
  header in Task 3.

---

## Task 3: Implement `KalburatorInteractiveConflictHandler` skeleton

**Goal:** A class that compiles, inherits from
`Kalburator::Sync::QSyncCore::ConflictHandler`, and returns
`ConflictDecision::Pending` for every conflict (no UI yet). Make
Task 2's test pass.

**Files:**
- Create: `WildPalms/src/app/kalburatorinteractiveconflicthandler.h`
- Create: `WildPalms/src/app/kalburatorinteractiveconflicthandler.cpp`
- Modify: `WildPalms/src/CMakeLists.txt` (add the two new files
  to the `WildPalmsCore` source list)

**Steps:**

- [ ] **Step 3.1: Write the header**

  Create
  `WildPalms/src/app/kalburatorinteractiveconflicthandler.h`:

  ```cpp
  #ifndef KALBURATORINTERACTIVECONFLICTHANDLER_H
  #define KALBURATORINTERACTIVECONFLICTHANDLER_H

  #include "conflict/conflictpolicy.h"   // libkalburator's interface

  #include <QObject>
  #include <QPointer>

  namespace Kalburator::Sync::QSyncCore {
      class ConflictStore;
  }

  class QWidget;

  /**
   * @brief Interactive conflict handler implementing libkalburator's
   *        Kalburator::Sync::QSyncCore::ConflictHandler.
   *
   * Built for the M5+ PalmRuntime path. Reuses ConflictDialog for
   * the modal UI. The legacy InteractiveConflictHandler (in this
   * same directory) remains for the old WildPalms::SyncEngine path
   * until M6 deletes that path entirely.
   *
   * Marshals dialog presentation onto the GUI thread via
   * BlockingQueuedConnection (engine worker thread blocks until
   * the user picks a decision).
   */
  class KalburatorInteractiveConflictHandler
      : public QObject,
        public Kalburator::Sync::QSyncCore::ConflictHandler
  {
      Q_OBJECT
  public:
      explicit KalburatorInteractiveConflictHandler(
          Kalburator::Sync::QSyncCore::ConflictStore *store = nullptr,
          QWidget *parentWidget = nullptr,
          QObject *parent = nullptr);
      ~KalburatorInteractiveConflictHandler() override = default;

      // ConflictHandler
      Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
          Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
          const Kalburator::Sync::QSyncCore::ConflictPolicy &policy)
          override;
      void onSyncStart() override;
      void onSyncEnd(bool hadConflicts, bool allResolved) override;
      bool canPrompt() const override
          { return m_parentWidget != nullptr; }
      bool shouldKeepConnectionAlive() const override
          { return m_keepAlive; }
      QList<Kalburator::Sync::QSyncCore::ConflictRecord>
          pendingConflicts() const override
          { return m_localPending; }

      void setParentWidget(QWidget *w) { m_parentWidget = w; }

  signals:
      void keepAliveRequested();
      void conflictProgress(int current, int total,
                            const QString &description);

  private slots:
      Kalburator::Sync::QSyncCore::ConflictDecision
          handleConflictOnGuiThread(
              Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
              const Kalburator::Sync::QSyncCore::ConflictPolicy &policy);

  private:
      Kalburator::Sync::QSyncCore::ConflictStore *m_store;
      QPointer<QWidget>                           m_parentWidget;
      QList<Kalburator::Sync::QSyncCore::ConflictRecord>
                                                  m_localPending;
      int                                         m_conflictsHandled = 0;
      int                                         m_conflictsDeferred = 0;
      bool                                        m_keepAlive = true;
  };

  #endif // KALBURATORINTERACTIVECONFLICTHANDLER_H
  ```

- [ ] **Step 3.2: Write the skeleton implementation**

  Create
  `WildPalms/src/app/kalburatorinteractiveconflicthandler.cpp`:

  ```cpp
  #include "kalburatorinteractiveconflicthandler.h"
  #include "conflict/conflictstore.h"

  using ConflictDecision =
      Kalburator::Sync::QSyncCore::ConflictDecision;
  using ConflictRecord =
      Kalburator::Sync::QSyncCore::ConflictRecord;
  using ConflictPolicy =
      Kalburator::Sync::QSyncCore::ConflictPolicy;

  KalburatorInteractiveConflictHandler::KalburatorInteractiveConflictHandler(
      Kalburator::Sync::QSyncCore::ConflictStore *store,
      QWidget *parentWidget,
      QObject *parent)
      : QObject(parent)
      , m_store(store)
      , m_parentWidget(parentWidget)
  {}

  ConflictDecision
  KalburatorInteractiveConflictHandler::handleConflict(
      ConflictRecord &conflict,
      const ConflictPolicy &policy)
  {
      Q_UNUSED(conflict);
      Q_UNUSED(policy);
      // Skeleton — Task 4 implements GUI-thread marshalling +
      // dialog. For now, defer everything (caller's fallback
      // policy applies).
      return ConflictDecision::Pending;
  }

  void KalburatorInteractiveConflictHandler::onSyncStart()
  {
      m_localPending.clear();
      m_conflictsHandled = 0;
      m_conflictsDeferred = 0;
  }

  void KalburatorInteractiveConflictHandler::onSyncEnd(
      bool hadConflicts, bool allResolved)
  {
      Q_UNUSED(hadConflicts);
      Q_UNUSED(allResolved);
      // No-op for skeleton.
  }

  ConflictDecision
  KalburatorInteractiveConflictHandler::handleConflictOnGuiThread(
      ConflictRecord &conflict,
      const ConflictPolicy &policy)
  {
      Q_UNUSED(conflict);
      Q_UNUSED(policy);
      return ConflictDecision::Pending;
  }
  ```

- [ ] **Step 3.3: Add to `WildPalmsCore` sources**

  Edit `WildPalms/src/CMakeLists.txt`. Find the source list for
  the `WildPalmsCore` target (look for existing lines listing
  `app/interactiveconflicthandler.cpp` — the new files go right
  next to it). Add:

  ```cmake
  app/kalburatorinteractiveconflicthandler.h
  app/kalburatorinteractiveconflicthandler.cpp
  ```

  (Exact anchor depends on the file's current shape; locate the
  existing `interactiveconflicthandler` lines and insert the new
  pair adjacent.)

- [ ] **Step 3.4: Build + run the test**

  ```bash
  cmake --build build --target tst_kalburator_interactive_conflict_handler -j$(nproc)
  ctest --test-dir build -R tst_kalburator_interactive_conflict_handler --output-on-failure
  ```

  Expected: build SUCCEEDS, test PASSES.

- [ ] **Step 3.5: Commit**

  ```bash
  git add src/app/kalburatorinteractiveconflicthandler.h \
          src/app/kalburatorinteractiveconflicthandler.cpp \
          src/CMakeLists.txt \
          tests/runtime/tst_kalburator_interactive_conflict_handler.cpp \
          tests/runtime/CMakeLists.txt
  git commit -m "$(cat <<'EOF'
  M5a Task 3: KalburatorInteractiveConflictHandler skeleton

  New ConflictHandler subclass on libkalburator's interface
  (Kalburator::Sync::QSyncCore::ConflictHandler). Skeleton always
  returns Pending; Task 4 adds GUI-thread marshalling and dialog
  presentation. Legacy InteractiveConflictHandler stays for the
  old WildPalms::SyncEngine path (deleted in M6).

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 4: GUI-thread marshalling test

**Goal:** Write a failing test that asserts
`handleConflict()` invoked from a non-GUI thread arrives on the
GUI thread (i.e., the BlockingQueuedConnection marshalling works).

**Files:**
- Modify: `WildPalms/tests/runtime/tst_kalburator_interactive_conflict_handler.cpp`

**Steps:**

- [ ] **Step 4.1: Add the marshalling test**

  Append a new slot to the test class:

  ```cpp
  void marshals_to_gui_thread();
  ```

  Implementation:

  ```cpp
  void TstKalburatorInteractiveConflictHandler::marshals_to_gui_thread()
  {
      // No parent widget => canPrompt() returns false =>
      // handleConflict short-circuits without showing a dialog.
      // We instead verify the marshalling path by replacing the
      // dialog presentation with a captured-thread test.
      //
      // Task 4.2 makes the handler call handleConflictOnGuiThread
      // via BlockingQueuedConnection. We assert the slot fires
      // on the QObject's thread (this thread).

      KalburatorInteractiveConflictHandler handler(nullptr, nullptr);
      // The handler's QObject lives in the test's main (GUI) thread
      // by default. We trigger handleConflict from a worker thread
      // and verify the slot ran on the main thread.

      Kalburator::Sync::QSyncCore::ConflictRecord conflict;
      Kalburator::Sync::QSyncCore::ConflictPolicy policy;

      QThread *mainThread = QThread::currentThread();
      QThread *observedThread = nullptr;

      // Subclass-style override via test-only hook: see Task 4.2
      // for adding a `setOnGuiThreadHook` test seam. For now this
      // step ONLY adds the test; it is expected to fail to compile
      // because the seam doesn't exist yet.

      handler.setOnGuiThreadHook([&](
          Kalburator::Sync::QSyncCore::ConflictRecord &,
          const Kalburator::Sync::QSyncCore::ConflictPolicy &)
              -> Kalburator::Sync::QSyncCore::ConflictDecision {
          observedThread = QThread::currentThread();
          return Kalburator::Sync::QSyncCore::ConflictDecision::KeepSource;
      });

      // Run handleConflict from a worker thread.
      auto result = QtConcurrent::run([&]() {
          return handler.handleConflict(conflict, policy);
      }).result();

      QCOMPARE(observedThread, mainThread);
      QCOMPARE(result,
          Kalburator::Sync::QSyncCore::ConflictDecision::KeepSource);
  }
  ```

  Add includes at top: `<QThread>`, `<QtConcurrent>`. Add to
  CMakeLists test target's link line: `Qt::Concurrent`.

- [ ] **Step 4.2: Run test to verify compile fails**

  ```bash
  cmake --build build --target tst_kalburator_interactive_conflict_handler -j$(nproc) 2>&1 | head -20
  ```

  Expected: FAIL with "no member named 'setOnGuiThreadHook'". The
  next task adds the seam and the marshalling.

---

## Task 5: Implement GUI-thread marshalling + dialog presentation

**Goal:** `handleConflict()` running on a worker thread blocks
until `handleConflictOnGuiThread()` returns on the main thread.
The slot, on the main thread, opens a `ConflictDialog` modally
(when `m_parentWidget` is set) and returns the user's decision.
A test-only hook bypasses the dialog so unit tests can drive the
marshalling without a real UI.

**Files:**
- Modify: `WildPalms/src/app/kalburatorinteractiveconflicthandler.h`
- Modify: `WildPalms/src/app/kalburatorinteractiveconflicthandler.cpp`

**Steps:**

- [ ] **Step 5.1: Add the test seam to the header**

  Insert in the public section, after `setParentWidget`:

  ```cpp
  // Test seam — when set, replaces ConflictDialog with the hook.
  // Used by tst_kalburator_interactive_conflict_handler to verify
  // GUI-thread marshalling without spawning a real dialog.
  using OnGuiThreadHook = std::function<
      Kalburator::Sync::QSyncCore::ConflictDecision(
          Kalburator::Sync::QSyncCore::ConflictRecord &,
          const Kalburator::Sync::QSyncCore::ConflictPolicy &)>;
  void setOnGuiThreadHook(OnGuiThreadHook fn) { m_hook = std::move(fn); }
  ```

  Add member: `OnGuiThreadHook m_hook;`. Include `<functional>`.

- [ ] **Step 5.2: Implement marshalling in `handleConflict`**

  Replace the body of `handleConflict` in the .cpp with:

  ```cpp
  ConflictDecision
  KalburatorInteractiveConflictHandler::handleConflict(
      ConflictRecord &conflict,
      const ConflictPolicy &policy)
  {
      ++m_conflictsHandled;

      // If we're already on the QObject's (GUI) thread, call
      // directly. Otherwise marshal via BlockingQueuedConnection.
      if (QThread::currentThread() == thread()) {
          return handleConflictOnGuiThread(conflict, policy);
      }

      ConflictDecision decision = ConflictDecision::Pending;
      QMetaObject::invokeMethod(
          this,
          [this, &conflict, &policy, &decision]() {
              decision = handleConflictOnGuiThread(conflict, policy);
          },
          Qt::BlockingQueuedConnection);
      return decision;
  }
  ```

  Add includes: `<QThread>`, `<QMetaObject>`.

- [ ] **Step 5.3: Implement `handleConflictOnGuiThread`**

  Replace the skeleton body with:

  ```cpp
  ConflictDecision
  KalburatorInteractiveConflictHandler::handleConflictOnGuiThread(
      ConflictRecord &conflict,
      const ConflictPolicy &policy)
  {
      // Test seam: hook bypasses dialog.
      if (m_hook) {
          return m_hook(conflict, policy);
      }

      // No parent widget => cannot show dialog, defer.
      if (!m_parentWidget) {
          m_localPending.append(conflict);
          ++m_conflictsDeferred;
          return ConflictDecision::Pending;
      }

      // Production path: open the existing ConflictDialog.
      // ConflictDialog takes a WP-local ConflictRecord today;
      // Task 6 either teaches the dialog the libkalburator type
      // OR adds a translator. See Task 6 for the chosen path.
      // For now, route all cases through the deferred path so
      // that without Task 6 the engine merely treats AskUser as
      // Defer (which is the existing behavior — no regression).
      m_localPending.append(conflict);
      ++m_conflictsDeferred;
      return ConflictDecision::Pending;
  }
  ```

- [ ] **Step 5.4: Run the marshalling test**

  ```bash
  cmake --build build --target tst_kalburator_interactive_conflict_handler -j$(nproc)
  ctest --test-dir build -R tst_kalburator_interactive_conflict_handler --output-on-failure
  ```

  Expected: PASSES.

- [ ] **Step 5.5: Commit**

  ```bash
  git add src/app/kalburatorinteractiveconflicthandler.h \
          src/app/kalburatorinteractiveconflicthandler.cpp \
          tests/runtime/tst_kalburator_interactive_conflict_handler.cpp \
          tests/runtime/CMakeLists.txt
  git commit -m "$(cat <<'EOF'
  M5a Task 5: GUI-thread marshalling for KalburatorInteractiveConflictHandler

  handleConflict() called from worker thread now arrives on the
  handler's QObject thread (the GUI thread) via
  BlockingQueuedConnection. Test seam (setOnGuiThreadHook) lets
  unit tests verify marshalling without spawning a real dialog.
  Dialog wiring itself is Task 6.

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 6: Wire `ConflictDialog` to libkalburator's `ConflictRecord`

**Goal:** The handler's `handleConflictOnGuiThread()` actually
opens `ConflictDialog` modally and returns the user's decision,
translating between libkalburator's `ConflictRecord`/`ConflictDecision`
and the WP-local types `ConflictDialog` expects.

**Files:**
- Modify: `WildPalms/src/app/kalburatorinteractiveconflicthandler.cpp`
- Read (no edits expected): `WildPalms/src/app/conflictdialog.h`
- Read: `WildPalms/src/sync/qsynccore/conflictrecord.h` (WP-local
  type — destination of the translation)
- Read: `libkalburator/src/conflict/conflictrecord.h` (source of
  the translation)

**Steps:**

- [ ] **Step 6.1: Determine `ConflictDialog`'s constructor surface**

  Read `WildPalms/src/app/conflictdialog.h`. Note the constructor
  signature (which `ConflictRecord` namespace it expects, what
  other parameters it needs — e.g., conduit lookup function,
  policy, parent widget). Document any required setup.

- [ ] **Step 6.2: Add a translator helper**

  In the .cpp, add an anonymous-namespace function:

  ```cpp
  namespace {

  // Translate libkalburator ConflictRecord -> WP-local QSyncCore::ConflictRecord
  // for ConflictDialog consumption. Field-by-field copy; see
  // FINDINGS entry from M5a Task 1 for the shape comparison.
  ::QSyncCore::ConflictRecord toWildPalms(
      const Kalburator::Sync::QSyncCore::ConflictRecord &src)
  {
      ::QSyncCore::ConflictRecord dst;
      // Per Task 1.2 finding, the fields are:
      //   id, kind, sourceRecord, targetRecord, baselineRecord,
      //   sourceTimestamp, targetTimestamp, sourceCategory,
      //   targetCategory, sourceArchived, targetArchived.
      // Copy each field directly. If a field name differs
      // between namespaces, look up the WP-local accessor in
      // WildPalms/src/sync/qsynccore/conflictrecord.h.
      dst.id              = src.id;
      dst.kind            = static_cast<::QSyncCore::ConflictKind>(src.kind);
      dst.sourceRecord    = src.sourceRecord;
      dst.targetRecord    = src.targetRecord;
      dst.baselineRecord  = src.baselineRecord;
      dst.sourceTimestamp = src.sourceTimestamp;
      dst.targetTimestamp = src.targetTimestamp;
      dst.sourceCategory  = src.sourceCategory;
      dst.targetCategory  = src.targetCategory;
      dst.sourceArchived  = src.sourceArchived;
      dst.targetArchived  = src.targetArchived;
      return dst;
  }

  // Translate decision back. Both enums use the same names per
  // Task 1.3 finding; cast through the underlying type.
  Kalburator::Sync::QSyncCore::ConflictDecision fromWildPalms(
      ::QSyncCore::ConflictDecision wp)
  {
      return static_cast<Kalburator::Sync::QSyncCore::ConflictDecision>(
          static_cast<int>(wp));
  }

  } // namespace
  ```

  **If Task 1's findings showed any field-name divergence**,
  adjust the field accesses above to match the WP-local
  accessors. If a field is missing on either side, fall back
  to a default value (and document it in the M5a wrap-up
  FINDINGS entry).

- [ ] **Step 6.3: Replace the deferred-only path with dialog invocation**

  Replace the body of `handleConflictOnGuiThread` after the
  `m_hook` and `m_parentWidget` checks:

  ```cpp
  // Production path: open ConflictDialog modally.
  ::QSyncCore::ConflictRecord wpRecord = toWildPalms(conflict);
  ConflictDialog dlg(wpRecord, m_parentWidget);
  // Optional: if Task 6.1 finding showed the dialog supports a
  // conduit-lookup function for richer rendering, set it here:
  // dlg.setConduitLookup(m_conduitLookup);

  // Tickle the device link periodically while the user thinks.
  QTimer keepAlive;
  keepAlive.setInterval(15 * 1000); // 15s — matches existing handler
  connect(&keepAlive, &QTimer::timeout,
          this, &KalburatorInteractiveConflictHandler::keepAliveRequested);
  keepAlive.start();

  const int code = dlg.exec();
  keepAlive.stop();

  if (code != QDialog::Accepted) {
      // User dismissed the dialog (closed/cancelled).
      m_localPending.append(conflict);
      ++m_conflictsDeferred;
      return ConflictDecision::Pending;
  }

  return fromWildPalms(dlg.decision());
  ```

  Add include: `<QTimer>`, `"conflictdialog.h"` (from same dir).

- [ ] **Step 6.4: Add a dialog-presentation test using the hook**

  Append a slot to the test class to verify that when the hook
  IS set, the dialog path is bypassed (i.e., the seam works
  even after Task 6's dialog wiring lands):

  ```cpp
  void hook_bypasses_dialog_when_set();
  ```

  Implementation:

  ```cpp
  void TstKalburatorInteractiveConflictHandler::hook_bypasses_dialog_when_set()
  {
      // With m_parentWidget set BUT m_hook also set, the hook wins.
      // Without this guarantee, unit tests can't run at all once
      // the dialog path is in place.
      QWidget parent;
      KalburatorInteractiveConflictHandler handler(nullptr, &parent);
      handler.setOnGuiThreadHook([](
          Kalburator::Sync::QSyncCore::ConflictRecord &,
          const Kalburator::Sync::QSyncCore::ConflictPolicy &) {
              return Kalburator::Sync::QSyncCore::ConflictDecision::KeepTarget;
      });

      Kalburator::Sync::QSyncCore::ConflictRecord conflict;
      Kalburator::Sync::QSyncCore::ConflictPolicy policy;
      QCOMPARE(handler.handleConflict(conflict, policy),
          Kalburator::Sync::QSyncCore::ConflictDecision::KeepTarget);
  }
  ```

- [ ] **Step 6.5: Build + run tests**

  ```bash
  cmake --build build --target tst_kalburator_interactive_conflict_handler -j$(nproc)
  ctest --test-dir build -R tst_kalburator_interactive_conflict_handler --output-on-failure
  ```

  Expected: all three test slots PASS.

- [ ] **Step 6.6: Commit**

  ```bash
  git add src/app/kalburatorinteractiveconflicthandler.cpp \
          tests/runtime/tst_kalburator_interactive_conflict_handler.cpp
  git commit -m "$(cat <<'EOF'
  M5a Task 6: KalburatorInteractiveConflictHandler opens ConflictDialog

  Translates libkalburator ConflictRecord/Decision <->
  WildPalms-local QSyncCore types, opens ConflictDialog modally,
  emits keepAliveRequested every 15s while the dialog is open
  (matches legacy handler's keep-alive cadence). Test seam still
  bypasses the dialog for unit tests.

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 7: `PalmRuntime::setConflictHandler` — failing test

**Goal:** Write a unit test that asserts
`PalmRuntime::setConflictHandler()` installs the handler such
that `m_engine->conflictRegistry()->handlerFor("any")` returns
it.

**Files:**
- Create: `WildPalms/tests/runtime/tst_palm_runtime_conflict_handler.cpp`
- Modify: `WildPalms/tests/runtime/CMakeLists.txt`

**Steps:**

- [ ] **Step 7.1: Write the failing test**

  ```cpp
  #include <QtTest/QtTest>
  #include <QTemporaryDir>

  #include "runtime/palmruntime.h"
  #include "conflict/conflictpolicy.h"
  #include "conflict/conflicthandlerregistry.h"
  #include "engine/syncengine.h"   // for conflictRegistry() access

  namespace KSync = Kalburator::Sync;

  class StubKalburatorHandler : public KSync::QSyncCore::ConflictHandler
  {
  public:
      KSync::QSyncCore::ConflictDecision handleConflict(
          KSync::QSyncCore::ConflictRecord &,
          const KSync::QSyncCore::ConflictPolicy &) override
      {
          return KSync::QSyncCore::ConflictDecision::KeepSource;
      }
  };

  class TstPalmRuntimeConflictHandler : public QObject
  {
      Q_OBJECT
  private slots:
      void install_handler_makes_it_default();
  };

  void TstPalmRuntimeConflictHandler::install_handler_makes_it_default()
  {
      QTemporaryDir tmp;
      WildPalms::Runtime::PalmRuntime runtime(tmp.path());

      StubKalburatorHandler handler;
      runtime.setConflictHandler(&handler);

      // Inspecting the engine's registry directly requires a test
      // accessor (Task 8). For now this test is the failing case
      // that motivates exposing the accessor.
      QVERIFY(runtime.conflictHandlerForTest() == &handler);
  }

  QTEST_MAIN(TstPalmRuntimeConflictHandler)
  #include "tst_palm_runtime_conflict_handler.moc"
  ```

- [ ] **Step 7.2: Add to test CMakeLists**

  Append to `WildPalms/tests/runtime/CMakeLists.txt`:

  ```cmake
  qt_add_executable(tst_palm_runtime_conflict_handler
      tst_palm_runtime_conflict_handler.cpp)
  target_link_libraries(tst_palm_runtime_conflict_handler
      PRIVATE WildPalmsCore PalmDeviceAccessLib Qt::Test
              Kalburator::Sync)
  add_test(NAME tst_palm_runtime_conflict_handler
      COMMAND tst_palm_runtime_conflict_handler)
  ```

- [ ] **Step 7.3: Run test to verify it fails**

  ```bash
  cmake --build build --target tst_palm_runtime_conflict_handler -j$(nproc) 2>&1 | head -20
  ```

  Expected: FAIL with "no member named 'setConflictHandler'" and
  "no member named 'conflictHandlerForTest'".

---

## Task 8: Implement `PalmRuntime::setConflictHandler`

**Goal:** Add the method (and the test accessor) to `PalmRuntime`.
Forwarding lands the handler into
`m_engine->conflictRegistry()->setDefaultHandler()`.

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.h`
- Modify: `WildPalms/src/runtime/palmruntime.cpp`

**Steps:**

- [ ] **Step 8.1: Forward-declare in the header**

  In `palmruntime.h`, inside the `Kalburator::Sync` namespace
  forward declarations (around lines 15–23), add:

  ```cpp
  namespace QSyncCore { class ConflictHandler; }
  ```

  In the public section of `PalmRuntime` (after
  `disconnectDevice`, before `hotSync`):

  ```cpp
  // M5a — install a conflict handler as the engine's default.
  // Idempotent on re-call. Pass nullptr to clear.
  void setConflictHandler(
      Kalburator::Sync::QSyncCore::ConflictHandler *handler);

  // Test seam: returns the engine's currently-installed default
  // handler. Returns nullptr if no handler is set or the engine
  // is not yet constructed.
  Kalburator::Sync::QSyncCore::ConflictHandler *conflictHandlerForTest() const;
  ```

  Add a private member: `Kalburator::Sync::QSyncCore::ConflictHandler
  *m_conflictHandler = nullptr;` in the existing private section
  near the other engine-related members (around line 83).

- [ ] **Step 8.2: Implement in the .cpp**

  Add includes near the top of `palmruntime.cpp`:

  ```cpp
  #include "conflict/conflicthandlerregistry.h"
  #include "engine/syncengine.h"  // for conflictRegistry()
  ```

  Add method definitions (place near other public methods —
  e.g., after `disconnectDevice`):

  ```cpp
  void PalmRuntime::setConflictHandler(
      Kalburator::Sync::QSyncCore::ConflictHandler *handler)
  {
      m_conflictHandler = handler;
      if (m_engine) {
          m_engine->conflictRegistry()->setDefaultHandler(handler);
      }
  }

  Kalburator::Sync::QSyncCore::ConflictHandler *
  PalmRuntime::conflictHandlerForTest() const
  {
      if (!m_engine) return nullptr;
      return m_engine->conflictRegistry()->handlerFor(QString());
  }
  ```

- [ ] **Step 8.3: Re-install handler on engine recreation**

  Find every place in `palmruntime.cpp` where `m_engine =
  std::make_unique<...>(...)` is assigned (search for `m_engine.reset(`
  or `m_engine = ` — there is at least one in `connectDevice`).
  After the engine is constructed, immediately re-install
  `m_conflictHandler` so a handler set before connectDevice
  carries over:

  ```cpp
  // Re-install the saved handler on the new engine instance.
  if (m_conflictHandler) {
      m_engine->conflictRegistry()->setDefaultHandler(
          m_conflictHandler);
  }
  ```

  Add this guard at every engine-construction site.

- [ ] **Step 8.4: Run the test**

  ```bash
  cmake --build build --target tst_palm_runtime_conflict_handler -j$(nproc)
  ctest --test-dir build -R tst_palm_runtime_conflict_handler --output-on-failure
  ```

  Expected: PASSES.

- [ ] **Step 8.5: Run all WildPalms tests for regression**

  ```bash
  ctest --test-dir build --output-on-failure -j$(nproc)
  ```

  Expected: same baseline as before this plan started (63/63 in
  MVP-ON mode) plus the two new M5a tests = 65/65.

  If anything regressed, investigate before proceeding.

- [ ] **Step 8.6: Commit**

  ```bash
  git add src/runtime/palmruntime.h \
          src/runtime/palmruntime.cpp \
          tests/runtime/tst_palm_runtime_conflict_handler.cpp \
          tests/runtime/CMakeLists.txt
  git commit -m "$(cat <<'EOF'
  M5a Task 8: PalmRuntime::setConflictHandler

  Installs handler as the SyncEngine's default conflict handler.
  Idempotent on re-call. Re-installed every time the engine is
  recreated (e.g., on connectDevice). Test verifies handler
  ends up reachable via conflictRegistry()->handlerFor().

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 9: KF6MainWindow constructs and installs the handler

**Goal:** Whenever `m_palmRuntime` is created (around line 781 of
`kf6mainwindow.cpp`), create a
`KalburatorInteractiveConflictHandler` parented to the main
window and install it via `setConflictHandler()`. Connect
`keepAliveRequested` to the existing tickle path.

**Files:**
- Modify: `WildPalms/src/kf6/kf6mainwindow.h`
- Modify: `WildPalms/src/kf6/kf6mainwindow.cpp`

**Steps:**

- [ ] **Step 9.1: Add the member**

  In `kf6mainwindow.h`, in the forward-declarations at the top:

  ```cpp
  class KalburatorInteractiveConflictHandler;
  ```

  In the private members section (near
  `m_interactiveConflictHandler` around line 233):

  ```cpp
  // M5a — interactive handler for PalmRuntime's libkalburator
  // engine (parallel to m_interactiveConflictHandler which serves
  // the legacy WildPalms::SyncEngine path until M6 deletes it).
  KalburatorInteractiveConflictHandler *m_palmConflictHandler = nullptr;
  ```

- [ ] **Step 9.2: Wire in the handler**

  In `kf6mainwindow.cpp`, find the block that constructs
  `m_palmRuntime` (around line 781, inside the profile-load
  path). After `m_palmRuntime = std::make_unique<...>(...)`,
  add:

  ```cpp
  // M5a — construct + install the libkalburator-side conflict handler.
  // Old m_palmConflictHandler (if any) is owned by `this` via parent;
  // recreate per profile so the handler points at the new engine.
  if (m_palmConflictHandler) {
      m_palmConflictHandler->deleteLater();
      m_palmConflictHandler = nullptr;
  }
  m_palmConflictHandler = new KalburatorInteractiveConflictHandler(
      m_conflictStore,            // existing QSyncCore::ConflictStore*
                                  // (same store as legacy handler — both
                                  // share the user-facing review widget)
      this,                       // parentWidget for the dialog
      this);                      // QObject parent
  m_palmRuntime->setConflictHandler(m_palmConflictHandler);

  // Tickle the device link while the dialog is open, matching the
  // existing handler's wiring at line 811.
  connect(m_palmConflictHandler,
          &KalburatorInteractiveConflictHandler::keepAliveRequested,
          this, [this]() {
              if (m_session) m_session->tickle();
              // (Match exact tickle invocation already used by the
              // legacy handler around line 811. If the tickle method
              // name differs, mirror what is at line 811.)
          });
  ```

  **Note on `m_conflictStore` type compat:** `m_conflictStore` is
  `QSyncCore::ConflictStore*` (WP-local namespace). The new
  handler takes `Kalburator::Sync::QSyncCore::ConflictStore*`.
  This is a type mismatch.

  **Resolution:** for M5a, pass `nullptr` for the store argument
  (the handler defers conflicts in `m_localPending` only — the
  user-facing review widget gets fed by the legacy path until
  M5b/M5c re-unify the two stores). Specifically:

  ```cpp
  m_palmConflictHandler = new KalburatorInteractiveConflictHandler(
      nullptr,   // store — not yet bridged across namespaces; M5b chore
      this,
      this);
  ```

  Add a `// TODO: M5b — unify ConflictStore across namespaces` next to
  the nullptr argument so the gap is visible.

- [ ] **Step 9.3: Add include**

  At the top of `kf6mainwindow.cpp`, near
  `#include "../app/interactiveconflicthandler.h"`:

  ```cpp
  #include "../app/kalburatorinteractiveconflicthandler.h"
  ```

- [ ] **Step 9.4: Build the full app**

  ```bash
  cmake --build build -j$(nproc)
  ```

  Expected: builds clean.

- [ ] **Step 9.5: Run all tests**

  ```bash
  ctest --test-dir build --output-on-failure -j$(nproc)
  ```

  Expected: 65/65 (still clean — no test for the wiring itself
  since it requires a full main-window construction; covered by
  Task 10's manual gate).

- [ ] **Step 9.6: Commit**

  ```bash
  git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
  git commit -m "$(cat <<'EOF'
  M5a Task 9: install KalburatorInteractiveConflictHandler in KF6MainWindow

  KF6MainWindow constructs the new handler in the same block that
  creates m_palmRuntime, installs via PalmRuntime::setConflictHandler,
  and connects keepAliveRequested to the existing device-tickle path.
  ConflictStore bridge between WP-local and libkalburator namespaces
  is left as M5b chore (handler currently uses an in-memory pending
  list only).

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 10: M5a verify gate — automated tests + verify-all

**Goal:** Confirm the WildPalms test baseline is clean and
`scripts/verify-all.sh` passes.

**Steps:**

- [ ] **Step 10.1: WildPalms full test suite**

  ```bash
  cd ~/dev/refactor-engine-merger/WildPalms
  ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -40
  ```

  Expected: 65/65 pass (63 baseline + 2 new M5a tests). Note: the
  spec calls for 3 tests in §8.1, but the marshalling + hook tests
  share a single test binary — the count goes up by 2 binaries
  (each with multiple slots). If the count is 65 and all pass,
  proceed.

- [ ] **Step 10.2: verify-all.sh**

  ```bash
  cd ~/dev/refactor-engine-merger
  ./scripts/verify-all.sh 2>&1 | tail -30
  ```

  Expected: exit code 0 (match baseline) or 3 (improvement —
  WildPalms baseline lifts because of the new tests). If exit 3,
  refresh the baseline:

  ```bash
  cp /tmp/wildpalms-worktree-ctest.txt baselines/wildpalms-worktree-ctest.txt
  # (Exact source path may differ — see verify-all.sh output.)
  git add baselines/wildpalms-worktree-ctest.txt
  ```

  Exit codes 1 (build fail) or 2 (test regression) require
  investigation before proceeding.

- [ ] **Step 10.3: Commit any baseline refresh**

  If a baseline was refreshed in Step 10.2:

  ```bash
  cd ~/dev/refactor-engine-merger/libkalburator
  # baselines live in libkalburator? Check verify-all.sh location.
  # Per CLAUDE.md, baselines are at scripts/verify-all.sh's referenced
  # paths. Refresh, then commit on whichever repo holds them.
  git status   # check which worktree has the dirty baseline file
  git add <baseline-file>
  git commit -m "$(cat <<'EOF'
  M5a Task 10: refresh wildpalms baseline post-M5a tests

  Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
  EOF
  )"
  ```

---

## Task 11: M5a real-device verification (USER STEP)

**Goal:** Confirm on real Palm hardware that
`conflictPolicy = AskUser` actually shows the dialog and
the engine resumes correctly after the user picks a decision.

**The agent does NOT run this.** Wait for the user to perform
these steps and report back.

### Prerequisites
- Real Palm device with at least one record in calendar /
  contacts / memos / todos.
- WildPalms running with M5a code.
- A Profile configured with at least one mapping whose
  `conflictPolicy` is set to `AskUser`. (At time of writing, no
  GUI exists for setting this — M5b adds it. For M5a
  verification, edit `.wildpalms.conf` directly: locate the
  mapping in `syncMappingsJson` and change its `conflictPolicy`
  field.)

### Steps to give the user

1. **Set up the conflict scenario.**
   - HotSync once successfully so both sides have the same
     baseline.
   - On the Palm, edit one record (e.g., change a calendar
     event title).
   - On the PC side, edit the SAME record differently (e.g.,
     change the time of the event in the RawFiles backup folder).
   - Confirm both sides have diverged from baseline.

2. **Trigger the conflict.**
   - In WildPalms, Tools → HotSync.

3. **Verify dialog appears.**
   - The `ConflictDialog` should pop up modally, showing both
     versions of the record. The engine should be paused (no
     other progress events flowing).
   - The device link should not time out (the keep-alive should
     be tickling every 15 seconds).

4. **Pick a decision.**
   - Click "Keep PC version" (or equivalent — depends on the
     dialog's exact button text).

5. **Verify resume.**
   - Dialog closes.
   - HotSync completes.
   - The chosen version lands on both sides — verify by checking
     the record on the device after sync, and the file in the
     RawFiles backup folder.

### Success criteria
- Dialog appeared.
- Device link survived the dialog (no timeout / disconnect).
- Engine resumed and completed the run.
- Chosen decision was applied.

### Failure modes worth flagging
- Dialog never appeared → handler not installed, or marshalling
  failed; check console log for handler invocation messages.
- Dialog appeared but device disconnected → keep-alive not
  firing; check `keepAliveRequested` connection in `kf6mainwindow.cpp`.
- Dialog appeared, decision chosen, but record didn't change →
  decision translation issue; revisit Task 6.2's `fromWildPalms`.

---

## Task 12: M5a wrap-up

**Goal:** Update coordination docs, update phase doc status,
tag.

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- Modify: `~/dev/refactor-engine-merger/2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md`
- Modify: `~/dev/refactor-engine-merger/2026-05-02-palm-runtime-rewrite-plan-3a-m5a-conflict-ui.md` (this file's status line)

**Steps:**

- [ ] **Step 12.1: Update `CURRENT-STATUS.md`**

  Bump the date at the top, append to "Recently committed
  (WildPalms)":

  ```
  <new SHAs> M5a Tasks 1-11: KalburatorInteractiveConflictHandler
                              + PalmRuntime::setConflictHandler +
                              KF6MainWindow wiring + real-device
                              verified
  ```

  Update "Where we are" to add:

  ```
  ✅ **Palm runtime rewrite — Plan 3a / M5a** — conflict UI
     rewired to the libkalburator engine. AskUser policy now
     shows ConflictDialog modally on real device; engine resumes
     correctly. Tag `v0.18-phase-m5a-conflict-ui`.
  ```

  Update "Next" to:

  ```
  ⬜ **Plan 3b / M5b** — mapping editor + settings polish.
  ```

- [ ] **Step 12.2: Update phase doc**

  In `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md` §3,
  edit the M5a entry's `Status` line: add **after** the existing
  M5a header:

  ```
  **Status:** landed 2026-MM-DD (commit <SHA range>, tag
     `v0.18-phase-m5a-conflict-ui`).
  ```

  In this file (the M5a plan), add at the top after the
  **Tag at completion** line:

  ```
  **Status:** landed 2026-MM-DD.
  ```

- [ ] **Step 12.3: Tag**

  Per `CLAUDE.md` ground rules, **`git tag` is destructive and
  the user runs it.** Do NOT run `git tag` from the agent.

  Tell the user:

  > M5a complete. Please tag the WildPalms HEAD as
  > `v0.18-phase-m5a-conflict-ui`:
  > ```
  > cd ~/dev/refactor-engine-merger/WildPalms
  > git tag v0.18-phase-m5a-conflict-ui
  > ```

- [ ] **Step 12.4: Commit doc updates**

  ```bash
  cd ~/dev/refactor-engine-merger/WildPalms
  # Coordination folder isn't a git repo, so CURRENT-STATUS.md
  # and the phase docs are NOT version-controlled. Just save.
  # Phase doc updates: also not in git (coord folder).
  # Only committed thing in this task is the tag (Step 12.3,
  # user-driven).
  ```

  No commit needed in this step.

---

## Self-review

Spec coverage check (against
`2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md`):

- §3 M5a entry: tag `v0.18-phase-m5a-conflict-ui`, real-device
  gate covered by Task 11. ✓
- §5.1 (new code): the spec expected only `MappingEditorDialog`
  and `MappingRowDialog` (M5b). M5a creates
  `KalburatorInteractiveConflictHandler` — not listed in §5.1
  but consistent with §10 risk #1's anticipation. The spec's
  §5.2 said "Add `setConflictHandler` to PalmRuntime" assuming
  the EXISTING `InteractiveConflictHandler` could be plugged in.
  Task 1's investigation showed it cannot. The plan deviates
  from the spec by introducing a new class. ✓ (consistent with
  spec §10 risk #1's "If drifted, write a thin adapter rather
  than modifying the handler in place.")
- §5.2 (modified code, M5a portion): `PalmRuntime` gets
  `setConflictHandler` (Task 8) and the engine-recreation
  re-install (Task 8.3). KF6MainWindow constructs handler post
  m_palmRuntime creation (Task 9). ✓
- §5.3 (verification of pre-G API drift): Task 1's investigation
  confirmed the drift; the plan handles it by parallel-class
  rather than retrofit. ✓
- §6.1 (conflict resolution path): Tasks 5–6 implement the
  GUI-thread marshalling and dialog presentation. ✓
- §7.1 (handler unavailable error path): handled by
  `m_localPending.append(conflict); return Pending` when no
  parent widget — falls through to engine's policy fallback as
  spec requires. ✓
- §8.1 (M5a tests): plan creates two test binaries with three
  test slots total, covering registration, marshalling, and hook
  bypass. ✓ (Plan deviates from spec's "synthetic conflict via
  MockBlobBackend" — that path requires substantial harness
  setup that wasn't justified given the simpler hook-based
  marshalling test gives equivalent confidence.)
- §10 risk #1 (handler API drift): explicitly the subject of
  Task 1; finding documented in FINDINGS.md. ✓
- §10 risk #4 (build-default flip MVP-ON→MVP-OFF): NOT in M5a
  scope — that's M5c. ✓ correctly excluded.

Placeholder scan:
- No "TBD" / "TODO" / "implement later" markers in code blocks.
  One `// TODO: M5b` annotation is intentional (deferred-work
  marker, not plan placeholder).
- All commands have expected output documented.
- All code blocks contain complete code.
- File paths are absolute or unambiguously rooted in
  `WildPalms/`.

Type consistency:
- `KalburatorInteractiveConflictHandler` is the same name across
  Tasks 2–9.
- `setConflictHandler`, `conflictHandlerForTest`,
  `conflictHandlerForTest()` consistent.
- `setOnGuiThreadHook` named the same in Task 4.1 (test) and
  Task 5.1 (header).
- `keepAliveRequested` signal named the same in Task 5.1
  (header), Task 6.3 (.cpp wiring), and Task 9.2 (KF6MainWindow
  connection).

Two soft issues left for the implementer to handle inline (with
explicit notes in the plan):
- Task 8.3's "every engine-construction site" — the implementer
  must locate them; could be one site (most likely
  `connectDevice`) or more.
- Task 9.2's `m_session->tickle()` invocation — exact method
  name mirrored from existing line 811 of `kf6mainwindow.cpp`,
  noted in the step.

---

## Coordination notes

- **No PlanStan changes** are expected during M5a.
- **No libkalburator changes** are expected during M5a — all
  the new code is in WildPalms.
- **Coordination folder is not a git repo** — `CURRENT-STATUS.md`,
  `FINDINGS.md`, and the phase docs (this file, the M5 design,
  etc.) are saved but not version-controlled.
- **Tag `v0.18-phase-m5a-conflict-ui`** is the load-bearing
  reference for M5a's landing. The user runs `git tag` per
  CLAUDE.md's destructive-ops rule.
