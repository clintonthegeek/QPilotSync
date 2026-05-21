# Palm Runtime Rewrite — Plan 2 of 4: WildPalms calendar-MVP through all sync modes (M2 + M3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land milestones M2 + M3 from the design spec on a new `palm-rewrite` branch in WildPalms: a working app where all six legacy KPilot sync modes (HotSync, FullSync, Backup, Restore, CopyPalmToPC, CopyPCToPalm) execute end-to-end against a real Palm device, using the calendar plugin only. Other four plugins (memo, contacts, todo, webcal) are explicitly disabled in CMake until Plan 3.

**Architecture:** New `PalmDeviceAccess` owns the link thread and self-marshals `IPalmDatabaseAccess` calls via `BlockingQueuedConnection` — fixes the latent threading bug from FINDINGS. New `PalmRuntime` orchestrates the six sync modes. New `IBackendPlugin` contract returns a Palm-only `IBlobBackend` (plus optional `DomainPlugin` registration). Existing `KF6MainWindow` shell preserved (`KXmlGuiWindow` + `KActionCollection` + per-plugin views); its mode-dispatch + runner-management internals get rewired to call `PalmRuntime` directly. Real-device verification gates M2 and M3.

**Tech Stack:** C++20, Qt6, KF6 (KConfig, KCoreAddons, KXmlGui, KActionCollection), CMake, ctest. pilot-link for DLP. Working directory: `~/dev/refactor-engine-merger/WildPalms/`. Build dir: `build/`. New branch: `palm-rewrite` off `refactor/engine-merger`.

**Spec:** `~/dev/refactor-engine-merger/2026-05-01-palm-runtime-rewrite-design.md` §4-§9.

**Plan 1 prerequisite (LANDED):** `~/dev/refactor-engine-merger/2026-05-01-palm-runtime-rewrite-plan-1-libkalburator.md`. M1 commits `3991894..ba57555` on libkalburator's `refactor/engine-merger` branch provide: dynamic `DomainPlugin` registration, `ExecutionOverride`-based mirror direction on `runSyncFuture`, deletion of the F1 facade.

**Out of scope for this plan:**
- M4: migration of memo, contacts, todo, webcal plugins to the new contract.
- M5: settings dialog, mapping editor UI, conflict-resolution dialog, per-plugin view re-wiring.
- M6: deletion of `SyncRunner_wp`/`DeviceSession`/`DeviceWorker`/`TickleWorker` source files (they remain in tree but become dead code at end of M3).
- M7: merge back to `refactor/engine-merger`.

These get separate plans after M2-M3 lands. Reasoning: each subsequent plan's task shape depends on what actually surfaces during M2-M3 (e.g., real-device threading bugs in `PalmDeviceAccess`, KF6MainWindow rewiring complications, etc.).

---

## File Structure

### Created files (in WildPalms)

- `WildPalms/src/runtime/palmdeviceaccess.{h,cpp}` — link-thread owner; wraps `KPilotLink`; exposes `IPalmDatabaseAccess` interface as self-marshalling.
- `WildPalms/src/runtime/palmruntime.{h,cpp}` — top-level orchestrator; six sync-mode methods returning `QFuture<PalmRunResult>`.
- `WildPalms/src/runtime/palmrunresult.h` — small struct: per-plugin sync stats + error list; result type for all six modes.
- `WildPalms/src/core/ibackendplugin_v2.h` — new plugin contract. (Naming: keep the old `ibackendplugin.h` in tree for M2-M3 since memo/contacts/todo/webcal still use it; M4 deletes the old and renames v2 → canonical.)
- `WildPalms/tests/runtime/tst_palm_device_access.cpp` — proves marshalling.
- `WildPalms/tests/runtime/tst_palm_runtime_hotsync.cpp` — exercises HotSync mode with MockBlobBackend pair.
- `WildPalms/tests/runtime/tst_palm_runtime_modes.cpp` — exercises FullSync / Copy* / Backup / Restore.
- `WildPalms/tests/runtime/CMakeLists.txt` — registers the three new tests.

### Modified files

- `WildPalms/CMakeLists.txt` — disable build of memo/contacts/todo/webcal plugins for the duration of M2-M3 (re-enabled in Plan 3 / M4). Add the new runtime/test directories.
- `WildPalms/src/CMakeLists.txt` — wire in `runtime/palmdeviceaccess` and `runtime/palmruntime` translation units; conditionally include only the calendar plugin's contributions.
- `WildPalms/src/plugins/calendar/calendarbackendplugin.{h,cpp}` (or wherever the calendar plugin lives — discover during Task 11) — migrate to new `IBackendPlugin` v2 contract.
- `WildPalms/src/palm/calendar/palmcalendarbackend.{h,cpp}` — `IBlobBackend` methods now call through `PalmDeviceAccess` (which self-marshals); no per-method `QMetaObject::invokeMethod` needed in the backend itself.
- `WildPalms/src/kf6/kf6mainwindow.{h,cpp}` — Tools-menu actions rewired to call `PalmRuntime` methods directly. The `m_session->requestSync(mode, m_syncRunner)` chain is unhooked from each action; the actions themselves stay (KAction-registered); the handlers change.
- `WildPalms/src/runtime/syncrunner_wp.{h,cpp}` — DOES NOT change in this plan. Remains in tree but no longer invoked from Tools menu (M6 deletes).
- `WildPalms/src/palm/devicesession.{h,cpp}` and `WildPalms/src/palm/deviceworker.{h,cpp}` — DO NOT change in this plan. The `KPilotLink` connection-lifecycle handling stays here for M2-M3; only the `requestSync(mode, runner)` method becomes uncalled from Tools menu (M6 deletes).
- `WildPalms/CMakeLists.txt` and any test CMakeLists referencing memo/contacts/todo/webcal/plucker_v2/webcal_v2_e2e tests — disable those tests for M2-M3 (re-enabled in M4).

### Coordination files (updated, not in WildPalms tree)

- `~/dev/refactor-engine-merger/CURRENT-STATUS.md` — bumped at end of plan; M2 + M3 completion noted.
- `~/dev/refactor-engine-merger/FINDINGS.md` — append any non-obvious discoveries.

---

## Branch and worktree setup

This plan operates on a NEW branch in the WildPalms worktree:

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git checkout -b palm-rewrite
```

The libkalburator and PlanStan worktrees stay on `refactor/engine-merger`. They're not modified by this plan.

`scripts/verify-all.sh` is no longer the canonical green check during this plan — WildPalms is intentionally broken and being rewritten. Use targeted ctest invocations on the WildPalms `build/` instead.

At plan end (after M3 real-device gate passes), WildPalms's `palm-rewrite` branch will have a working calendar-only HotSync. M4 starts on the same branch.

---

## Task 1: Branch setup + plugin disable

**Files:**
- Modify: `WildPalms/CMakeLists.txt`
- Modify: any plugin/test CMakeLists currently registering memo/contacts/todo/webcal/plucker_v2/webcal_v2_e2e plugins or tests.

- [ ] **Step 1: Create branch + verify state**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
git status                                # verify clean working tree on refactor/engine-merger
git checkout -b palm-rewrite
git branch --show-current                  # expect: palm-rewrite
```

If `git status` shows unstaged changes, STOP — surface to the user. There should be none.

- [ ] **Step 2: Inventory plugins and tests to disable**

```bash
grep -rn "add_subdirectory.*plugins\|kalburator_add.*test\|add_test" \
    WildPalms/CMakeLists.txt \
    WildPalms/src/CMakeLists.txt \
    WildPalms/src/plugins/*/CMakeLists.txt \
    WildPalms/tests/CMakeLists.txt 2>/dev/null | head -40
```

Make a list of which subdirectories build memo / contacts / todo / webcal plugins, and which test executables exercise them (likely `tst_memo_v2`, `tst_todo_v2`, `tst_webcal_v2_e2e`, `tst_plucker_v2_e2e`, `tst_palmbackend_roundtrip`, `tst_palm_contacts_backend`, `tst_palm_memo_backend`, `tst_palm_todo_backend`).

- [ ] **Step 3: Add a CMake option for the calendar-MVP**

In top-level `WildPalms/CMakeLists.txt`, after the existing `option(...)` declarations, add:

```cmake
# Plan 2 (M2-M3): build only the calendar plugin while the new
# PalmRuntime + IBackendPlugin v2 contract bake. M4 re-enables.
option(WILDPALMS_CALENDAR_MVP_ONLY
    "Build only the calendar Palm plugin and disable memo/contacts/todo/webcal during the runtime rewrite"
    ON)
```

- [ ] **Step 4: Gate plugin and test builds on the option**

Wherever a memo/contacts/todo/webcal plugin or test is added, wrap with:

```cmake
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(plugins/memo)
    add_subdirectory(plugins/contacts)
    # ... etc
endif()
```

For tests:

```cmake
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(tests/plugins/memo)
    add_test(NAME tst_memo_v2 COMMAND tst_memo_v2)
    # ... etc
endif()
```

(Adapt to the actual file structure. The principle: anything that touches the four-disabled-plugins or their backends is gated on `WILDPALMS_CALENDAR_MVP_ONLY=OFF`.)

The `SyncRunner_wp` source files stay built — they're called by `DeviceSession` even though Tools menu won't invoke them. M6 deletes.

- [ ] **Step 5: Verify the gated build configures**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
rm -rf build  # clean reconfigure
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_CALENDAR_MVP_ONLY=ON 2>&1 | tail -20
```

Expected: configure succeeds. The build will still fail at compile because `SyncRunner_wp` still references the deleted F1 facade (per Plan 1's intentional breakage). That's fine for now — Tasks 2-15 will not depend on `SyncRunner_wp` building.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt $(... other modified CMake files)
git commit -m "$(cat <<'EOF'
chore(build): WILDPALMS_CALENDAR_MVP_ONLY option for Plan 2 scope

Disables memo/contacts/todo/webcal plugin + test builds for the
duration of the WildPalms PalmRuntime rewrite. Re-enabled in Plan 3
(M4) when each plugin is migrated to the new IBackendPlugin contract.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Failing test for `PalmDeviceAccess` thread marshalling

**Files:**
- Create: `WildPalms/src/runtime/palmdeviceaccess.h` (skeleton — declarations only)
- Create: `WildPalms/tests/runtime/tst_palm_device_access.cpp`
- Create: `WildPalms/tests/runtime/CMakeLists.txt`
- Modify: top-level CMake or `WildPalms/tests/CMakeLists.txt` to include the new tests subdir.

This test proves that `IPalmDatabaseAccess` calls dispatched to `PalmDeviceAccess` from any thread end up on its private link thread. The test injects a thread-id-capturing mock implementation and asserts the captured thread id is the link thread.

- [ ] **Step 1: Create skeleton header**

`WildPalms/src/runtime/palmdeviceaccess.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_PALMDEVICEACCESS_H
#define WILDPALMS_RUNTIME_PALMDEVICEACCESS_H

#include <QObject>
#include <QThread>
#include <memory>

#include "palm/sync/ipalmdatabaseaccess.h"  // IPalmDatabaseAccess interface

class KPilotLink;

namespace WildPalms::Runtime {

/**
 * @brief Owns the Palm link thread; exposes IPalmDatabaseAccess to
 *        callers running on any thread.
 *
 * Internally, every IPalmDatabaseAccess method is forwarded to a
 * private QThread that owns the KPilotLink (or test-injected
 * IPalmDatabaseAccess implementation). Forwarding uses
 * Qt::BlockingQueuedConnection so the calling thread blocks until
 * the link thread returns the result. This makes Palm IBlobBackend
 * implementations safe to call from the engine worker thread without
 * each backend implementing its own marshalling.
 */
class PalmDeviceAccess : public QObject,
                         public WildPalms::PalmSync::IPalmDatabaseAccess
{
    Q_OBJECT
public:
    /// Ownership: PalmDeviceAccess takes ownership of `impl`. The
    /// concrete production implementation wraps KPilotLink; tests
    /// inject a mock IPalmDatabaseAccess.
    explicit PalmDeviceAccess(
        std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> impl,
        QObject *parent = nullptr);
    ~PalmDeviceAccess() override;

    // IPalmDatabaseAccess — all forward to `m_impl` on the link thread.
    QStringList availableDatabases() const override;
    bool        hasDatabase(const QString &dbName) const override;
    bool        createDatabase(const QString &dbName) override;
    QList<WildPalms::PalmSync::PalmRecord>
                readAllRecords(const QString &dbName) const override;
    std::optional<WildPalms::PalmSync::PalmRecord>
                readRecord(const QString &dbName, std::uint32_t recordId) const override;
    std::uint32_t createRecord(const QString &dbName,
                               const WildPalms::PalmSync::PalmRecord &record) override;
    bool        updateRecord(const QString &dbName,
                             const WildPalms::PalmSync::PalmRecord &record) override;
    bool        deleteRecord(const QString &dbName,
                             std::uint32_t recordId) override;
    QList<WildPalms::PalmSync::PalmRecord>
                recordsModifiedSince(const QString &dbName,
                                     const QDateTime &since) const override;
    QList<std::uint32_t>
                recordsDeletedSince(const QString &dbName,
                                    const QDateTime &since) const override;
    QByteArray  readAppBlock(const QString &dbName) const override;
    bool        supportsDeleteTracking() const override;

    /// Test introspection: the underlying link thread, for assertions.
    QThread *linkThread() const { return m_linkThread.get(); }

private:
    std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> m_impl;
    std::unique_ptr<QThread>                                  m_linkThread;
    QObject                                                   *m_implOwner = nullptr;  // lives on m_linkThread
};

} // namespace WildPalms::Runtime

#endif
```

(No .cpp yet — Task 3 implements. Header alone won't compile until Task 3 because methods aren't defined.)

- [ ] **Step 2: Create test file**

`WildPalms/tests/runtime/tst_palm_device_access.cpp`:

```cpp
#include <QTest>
#include <QThread>
#include <QSignalSpy>
#include <atomic>

#include "runtime/palmdeviceaccess.h"
#include "palm/sync/ipalmdatabaseaccess.h"

using namespace WildPalms::Runtime;
using namespace WildPalms::PalmSync;

namespace {

/// Mock IPalmDatabaseAccess that captures the thread id of every call.
class ThreadCapturingMock : public IPalmDatabaseAccess {
public:
    QStringList availableDatabases() const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return { QStringLiteral("MemoDB") };
    }
    bool hasDatabase(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    bool createDatabase(const QString &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    QList<PalmRecord> readAllRecords(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    std::optional<PalmRecord> readRecord(const QString &, std::uint32_t) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return std::nullopt;
    }
    std::uint32_t createRecord(const QString &, const PalmRecord &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return 1;
    }
    bool updateRecord(const QString &, const PalmRecord &) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    bool deleteRecord(const QString &, std::uint32_t) override {
        m_lastCallThread.store(QThread::currentThreadId());
        return true;
    }
    QList<PalmRecord> recordsModifiedSince(const QString &, const QDateTime &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    QList<std::uint32_t> recordsDeletedSince(const QString &, const QDateTime &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    QByteArray readAppBlock(const QString &) const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return {};
    }
    bool supportsDeleteTracking() const override {
        m_lastCallThread.store(QThread::currentThreadId());
        return false;
    }

    Qt::HANDLE lastCallThread() const { return m_lastCallThread.load(); }

private:
    mutable std::atomic<Qt::HANDLE> m_lastCallThread{nullptr};
};

}  // namespace

class TestPalmDeviceAccess : public QObject {
    Q_OBJECT
private slots:
    void readAllRecords_dispatchedToLinkThread() {
        auto mock = std::make_unique<ThreadCapturingMock>();
        ThreadCapturingMock *mockRaw = mock.get();
        PalmDeviceAccess access(std::move(mock));

        // Caller's thread id (this is the test's main thread).
        const Qt::HANDLE callerTid = QThread::currentThreadId();
        const Qt::HANDLE linkTid   = reinterpret_cast<Qt::HANDLE>(
            access.linkThread()->currentThreadId());

        // Sanity: the link thread is a different thread than the caller's.
        QVERIFY(callerTid != access.linkThread()->currentThreadId());

        // Issue a call. It should dispatch to the link thread.
        (void)access.readAllRecords(QStringLiteral("MemoDB"));

        // The mock captured the thread id of its actual invocation.
        // It must be the link thread, NOT the caller's.
        const Qt::HANDLE seen = mockRaw->lastCallThread();
        QVERIFY2(seen != callerTid,
                 "readAllRecords ran on the caller's thread; marshalling failed");
        // Note: Qt::HANDLE comparison across QThread::currentThreadId()
        // (which returns Qt::HANDLE) works on most platforms but may
        // need adjustment.
    }

    void allMethodsMarshall() {
        // Smoke-test: every IPalmDatabaseAccess method should land on
        // the link thread. We just call each and verify the captured
        // tid matches the link thread.
        auto mock = std::make_unique<ThreadCapturingMock>();
        ThreadCapturingMock *mockRaw = mock.get();
        PalmDeviceAccess access(std::move(mock));

        const Qt::HANDLE callerTid = QThread::currentThreadId();

        access.availableDatabases();           QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.hasDatabase("X");               QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.createDatabase("X");            QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readAllRecords("X");            QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readRecord("X", 1);             QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.createRecord("X", PalmRecord{});QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.updateRecord("X", PalmRecord{});QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.deleteRecord("X", 1);           QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.recordsModifiedSince("X", {});  QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.recordsDeletedSince("X", {});   QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.readAppBlock("X");              QVERIFY(mockRaw->lastCallThread() != callerTid);
        access.supportsDeleteTracking();       QVERIFY(mockRaw->lastCallThread() != callerTid);
    }
};

QTEST_GUILESS_MAIN(TestPalmDeviceAccess)
#include "tst_palm_device_access.moc"
```

- [ ] **Step 3: Create test CMakeLists**

`WildPalms/tests/runtime/CMakeLists.txt`:

```cmake
function(wildpalms_add_runtime_test TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt6::Core
            Qt6::Test
            WildPalmsCore  # adapt to the actual lib name
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

wildpalms_add_runtime_test(tst_palm_device_access)
```

- [ ] **Step 4: Wire into top-level test build**

Locate where `WildPalms/tests/CMakeLists.txt` adds subdirectories. Add:

```cmake
add_subdirectory(runtime)
```

If the top-level `tests/CMakeLists.txt` doesn't exist or has different structure, mirror what the existing `tests/palm/`, `tests/palmsync/`, etc. do. Use the existing pattern.

- [ ] **Step 5: Configure + build, observe expected failure**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake -S . -B build -DWILDPALMS_CALENDAR_MVP_ONLY=ON 2>&1 | tail -10
cmake --build build --target tst_palm_device_access 2>&1 | tail -20
```

Expected: link failure — `PalmDeviceAccess` member functions are declared but not defined. Save the error output for confirmation.

- [ ] **Step 6: Do NOT commit.** Test + skeleton header land with Task 3's implementation.

---

## Task 3: Implement `PalmDeviceAccess`

**Files:**
- Modify: `WildPalms/src/runtime/palmdeviceaccess.h` (already created in Task 2)
- Create: `WildPalms/src/runtime/palmdeviceaccess.cpp`
- Modify: `WildPalms/src/CMakeLists.txt` (or wherever runtime sources are listed) — add the new translation unit.

The implementation pattern: `PalmDeviceAccess` lives on whatever thread it's constructed on. Internally, it spins up an `m_linkThread` and moves an `m_implOwner` QObject onto it. The owner holds `m_impl` (the actual `IPalmDatabaseAccess`). Each public method on `PalmDeviceAccess` uses `QMetaObject::invokeMethod(m_implOwner, ..., Qt::BlockingQueuedConnection, ...)` to dispatch to the link thread.

Qt's `QMetaObject::invokeMethod` with `BlockingQueuedConnection` and a return value works via `Q_RETURN_ARG`. But `IPalmDatabaseAccess` returns non-trivial types (`QList<PalmRecord>`, `std::optional<PalmRecord>`). Use the lambda overload of `invokeMethod` (Qt 5.10+, well-supported in Qt 6):

```cpp
template <typename Fn>
auto callOnLinkThread(Fn&& fn) const -> decltype(fn()) {
    using Result = decltype(fn());
    Result result;
    QMetaObject::invokeMethod(m_implOwner,
                              [&fn, &result]() { result = fn(); },
                              Qt::BlockingQueuedConnection);
    return result;
}
```

Use this helper for every IPalmDatabaseAccess method.

- [ ] **Step 1: Implement constructor + thread setup**

```cpp
PalmDeviceAccess::PalmDeviceAccess(
    std::unique_ptr<IPalmDatabaseAccess> impl,
    QObject *parent)
    : QObject(parent)
    , m_impl(std::move(impl))
    , m_linkThread(std::make_unique<QThread>())
{
    Q_ASSERT(m_impl);
    m_linkThread->setObjectName(QStringLiteral("PalmLinkThread"));

    // The "owner" is a sacrificial QObject we move onto the link thread
    // so we can target it with QMetaObject::invokeMethod. m_impl is
    // accessed only from the link thread; m_implOwner holds it
    // logically by being the lambda capture target.
    m_implOwner = new QObject();
    m_implOwner->moveToThread(m_linkThread.get());
    connect(m_linkThread.get(), &QThread::finished,
            m_implOwner, &QObject::deleteLater);

    m_linkThread->start();
}
```

- [ ] **Step 2: Implement destructor**

```cpp
PalmDeviceAccess::~PalmDeviceAccess() {
    if (m_linkThread && m_linkThread->isRunning()) {
        m_linkThread->quit();
        m_linkThread->wait();
    }
    // m_implOwner was deleted via deleteLater on QThread::finished.
    // m_impl's destruction must happen on the link thread (since
    // KPilotLink is link-thread-affine). We accomplish this by
    // moving the impl into m_implOwner's lambda captures during
    // calls; m_impl outlives the lambdas via shared ownership. For
    // cleanest teardown, schedule m_impl.reset() on the link thread
    // before quit:
    // (Actually, simpler: m_impl is a unique_ptr held by *this*.
    //  Reset it on the link thread via an invokeMethod sync call
    //  before quit. See destructor pattern below.)
}
```

**Refinement**: the cleanest pattern is to release `m_impl` on the link thread before stopping the thread. Adjust the destructor:

```cpp
PalmDeviceAccess::~PalmDeviceAccess() {
    if (m_linkThread && m_linkThread->isRunning()) {
        // Release m_impl on the link thread (KPilotLink is link-thread-affine)
        QMetaObject::invokeMethod(m_implOwner,
            [this]() { m_impl.reset(); },
            Qt::BlockingQueuedConnection);
        m_linkThread->quit();
        m_linkThread->wait();
    }
}
```

- [ ] **Step 3: Implement the helper template**

The template needs to live in the header (templates aren't ABI-stable across TUs) OR be expanded explicitly per-method in the cpp. For simplicity and to avoid template noise in the public header, expand explicitly per-method:

```cpp
QStringList PalmDeviceAccess::availableDatabases() const {
    QStringList result;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &result]() { result = m_impl->availableDatabases(); },
        Qt::BlockingQueuedConnection);
    return result;
}

bool PalmDeviceAccess::hasDatabase(const QString &dbName) const {
    bool result = false;
    QMetaObject::invokeMethod(m_implOwner,
        [this, &dbName, &result]() { result = m_impl->hasDatabase(dbName); },
        Qt::BlockingQueuedConnection);
    return result;
}

// ... and so on for all 12 methods.
```

For void-returning methods (none in IPalmDatabaseAccess but noting the pattern), drop the result:

```cpp
QMetaObject::invokeMethod(m_implOwner,
    [this, ...]() { m_impl->theMethod(...); },
    Qt::BlockingQueuedConnection);
```

- [ ] **Step 4: Add to CMake**

In `WildPalms/src/CMakeLists.txt` (or wherever `WildPalmsCore` library sources are listed), add `runtime/palmdeviceaccess.cpp` to the source list.

- [ ] **Step 5: Build + test**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake --build build --target tst_palm_device_access 2>&1 | tail -10
ctest --test-dir build -R tst_palm_device_access -V 2>&1 | tail -20
```

Expected: BOTH test slots pass.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/palmdeviceaccess.{h,cpp} \
        src/CMakeLists.txt \
        tests/runtime/tst_palm_device_access.cpp \
        tests/runtime/CMakeLists.txt \
        $(... wherever you wired the test subdir in)
git commit -m "$(cat <<'EOF'
feat(runtime): PalmDeviceAccess — link-thread owner with self-marshalling

Wraps IPalmDatabaseAccess and dispatches every call to a private link
thread via Qt::BlockingQueuedConnection. Fixes the latent threading
bug from FINDINGS (engine worker thread != Palm link thread; Palm
backends previously trusted their caller). Per-method invokeMethod
lambdas; impl release scheduled on link thread before quit for clean
teardown.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: New `IBackendPlugin` v2 contract — header + minimal compile

**Files:**
- Create: `WildPalms/src/core/ibackendplugin_v2.h`
- Reference (do not modify yet): `WildPalms/src/core/ibackendplugin.h` (the old contract — used by disabled plugins; lives until M4)

This task only lands the new header. Task 5 will exercise it via a stub plugin in tests.

- [ ] **Step 1: Create the new contract header**

`WildPalms/src/core/ibackendplugin_v2.h`:

```cpp
#ifndef WILDPALMS_IBACKENDPLUGIN_V2_H
#define WILDPALMS_IBACKENDPLUGIN_V2_H

#include "iplugin.h"

#include <QIcon>
#include <QStringList>
#include <memory>

class QWidget;

namespace Kalburator::Sync {
    class IBlobBackend;
    class QSyncCore::ConflictHandler;  // adjust namespace per actual decl
}
namespace Kalburator::Shape {
    class DomainRegistry;
}

namespace WildPalms::Runtime {
    class PalmDeviceAccess;
}

namespace WildPalms {

/**
 * @brief Plugin contract v2 for the Palm runtime rewrite.
 *
 * Differences from v1 (ibackendplugin.h, deprecated):
 * - Returns ONLY a Palm-side IBlobBackend. PC-side is configured by
 *   the user per-mapping, not chosen by the plugin.
 * - Optional registerDomain() hook lets plugins introducing non-stock
 *   domains (e.g. an "office" domain for DocsToGo) register a
 *   DomainPlugin with libkalburator's DomainRegistry at plugin-load
 *   time.
 * - Receives PalmDeviceAccess (self-marshalling) instead of the
 *   raw PalmDeviceConnection.
 *
 * Discovery via KPluginFactory + .json metadata is unchanged from v1.
 */
class IBackendPluginV2 : public IPlugin {
public:
    // ── Identity ─────────────────────────────────────────────────────
    // pluginId(), displayName() inherited from IPlugin

    // ── Database claims ──────────────────────────────────────────────
    virtual QStringList claimedDatabases() const = 0;

    // ── Palm-side backend ────────────────────────────────────────────
    /**
     * The plugin's IBlobBackend for the Palm side. Must declare
     * nativeShapes() correctly so the engine can compile pipelines
     * to whatever PC-side backend the user has mapped to. Caller
     * takes ownership.
     *
     * The returned backend's IPalmDatabaseAccess methods MUST go
     * through the supplied PalmDeviceAccess (which self-marshals to
     * the link thread). Backends that hold the device pointer
     * directly without going through PalmDeviceAccess will be
     * called from the engine worker thread and break on real
     * hardware (see FINDINGS).
     */
    virtual std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) = 0;

    // ── Optional domain registration ─────────────────────────────────
    /**
     * Called once at plugin load, BEFORE any sync begins. Allows
     * plugins introducing non-stock domains to register a
     * DomainPlugin. Default is no-op for plugins using stock domains
     * (calendar, memo, contacts, todo).
     */
    virtual void registerDomain(Kalburator::Shape::DomainRegistry &) {}

    // ── Conduit ordering ─────────────────────────────────────────────
    virtual QStringList runBefore() const { return {}; }
    virtual QStringList runAfter()  const { return {}; }

    // ── Optional conflict handler ────────────────────────────────────
    virtual Kalburator::Sync::QSyncCore::ConflictHandler *
        createConflictHandler() { return nullptr; }

    // ── GUI surface ──────────────────────────────────────────────────
    virtual bool     hasMainView()    const { return false; }
    virtual QString  mainViewName()   const { return {}; }
    virtual QIcon    mainViewIcon()   const { return {}; }
    virtual QWidget *createMainView(QWidget *parent) const { return nullptr; }
};

}  // namespace WildPalms

#endif
```

- [ ] **Step 2: Verify header compiles standalone**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
echo '#include "core/ibackendplugin_v2.h"' > /tmp/check.cpp
# Use the project include paths to verify it parses; if you have a build target that includes it, just build that.
cmake --build build 2>&1 | tail -5
```

Expected: clean build — the new header is unused at this point but should not break anything. If the existing build already had errors (e.g. SyncRunner_wp's facade-deletion breakage), they remain.

- [ ] **Step 3: Commit**

```bash
git add src/core/ibackendplugin_v2.h
git commit -m "$(cat <<'EOF'
feat(core): IBackendPluginV2 contract — Palm-only backend, optional domain reg

New plugin contract for the runtime rewrite. v1 (ibackendplugin.h)
remains for the four disabled plugins until M4 migrates them. v2
returns only a Palm IBlobBackend (PC side becomes user-config) and
adds a registerDomain() hook for plugins introducing non-stock
domains via libkalburator's dynamic DomainPlugin registration (Plan 1
M1).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `PalmRuntime` skeleton + `PalmRunResult` type

**Files:**
- Create: `WildPalms/src/runtime/palmrunresult.h`
- Create: `WildPalms/src/runtime/palmruntime.h`
- Create: `WildPalms/src/runtime/palmruntime.cpp`
- Modify: `WildPalms/src/CMakeLists.txt`

Skeleton only — class with constructor, member declarations, stub method bodies. Tasks 6-10 add real behavior.

- [ ] **Step 1: PalmRunResult**

`WildPalms/src/runtime/palmrunresult.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_PALMRUNRESULT_H
#define WILDPALMS_RUNTIME_PALMRUNRESULT_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QDateTime>

namespace WildPalms::Runtime {

/**
 * @brief Aggregated result of a PalmRuntime sync operation.
 *
 * Per-plugin record-counts (created/updated/deleted/unchanged/errors),
 * cumulative duration, and any error messages. Used by all six
 * PalmRuntime modes; the GUI consumes via QFutureWatcher.
 */
struct PalmRunResult {
    struct PluginStats {
        int created   = 0;
        int updated   = 0;
        int deleted   = 0;
        int unchanged = 0;
        int conflicts = 0;
        int errors    = 0;
    };

    bool                          success = true;
    QString                       errorMessage;       // first error, if any
    QStringList                   logLines;
    QHash<QString, PluginStats>   perPluginStats;     // pluginId → stats
    QDateTime                     startTime;
    QDateTime                     endTime;

    qint64 durationMs() const {
        if (!startTime.isValid() || !endTime.isValid()) return 0;
        return startTime.msecsTo(endTime);
    }
};

}  // namespace WildPalms::Runtime

Q_DECLARE_METATYPE(WildPalms::Runtime::PalmRunResult)

#endif
```

- [ ] **Step 2: PalmRuntime header skeleton**

`WildPalms/src/runtime/palmruntime.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_PALMRUNTIME_H
#define WILDPALMS_RUNTIME_PALMRUNTIME_H

#include <QObject>
#include <QFuture>
#include <QString>
#include <QStringList>
#include <memory>

#include "palmrunresult.h"

class KPilotLink;

namespace Kalburator::Sync {
    class SyncEngine;
    class BackendRegistry;
    class SyncMapping;
}

namespace WildPalms {
class IBackendPluginV2;
}

namespace WildPalms::Runtime {

class PalmDeviceAccess;

/**
 * @brief Top-level Palm-sync orchestrator. Replaces SyncRunner_wp,
 *        DeviceSession's mode-dispatch, DeviceWorker's runner-handling.
 *
 * Owns PalmDeviceAccess (which owns the link thread). Consumes
 * libkalburator's SyncEngine for Group A/B modes (HotSync/FullSync,
 * Copy*) and runs pure runtime loops for Group C (Backup/Restore).
 *
 * GUI (KF6MainWindow) holds a PalmRuntime and connects Tools-menu
 * actions directly to its mode methods.
 */
class PalmRuntime : public QObject {
    Q_OBJECT
public:
    explicit PalmRuntime(const QString &profilePath,
                         QObject *parent = nullptr);
    ~PalmRuntime() override;

    // ── Device lifecycle ─────────────────────────────────────────────
    /// Wires a live KPilotLink. Constructs PalmDeviceAccess around it
    /// and instantiates Palm IBlobBackends for each enabled plugin.
    void connectDevice(KPilotLink *link);

    /// Tears down PalmDeviceAccess, releases backends, leaves the
    /// runtime ready for a future connectDevice() call.
    void disconnectDevice();

    bool isDeviceConnected() const;

    // ── Sync modes ───────────────────────────────────────────────────
    QFuture<PalmRunResult> hotSync();
    QFuture<PalmRunResult> fullSync();
    QFuture<PalmRunResult> copyPalmToPC();
    QFuture<PalmRunResult> copyPCToPalm();
    QFuture<PalmRunResult> backup();
    QFuture<PalmRunResult> restore();

    // ── Plugin / mapping inspection ──────────────────────────────────
    QList<QString> enabledPluginIds() const;       // for GUI display
    QList<Kalburator::Sync::SyncMapping> palmMappings() const;

    // ── Test seam ────────────────────────────────────────────────────
    /// Inject a pre-built PalmDeviceAccess (typically wrapping a mock
    /// IPalmDatabaseAccess). Used by tests instead of connectDevice.
    void setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess>);

signals:
    void deviceConnected();
    void deviceDisconnected();
    void runStarted(QString modeLabel);
    void runProgress(int current, int total, QString message);
    void runLog(QString message);
    void runFinished(PalmRunResult);

private:
    QString                                m_profilePath;
    std::unique_ptr<PalmDeviceAccess>      m_device;
    std::unique_ptr<Kalburator::Sync::SyncEngine> m_engine;
    std::unique_ptr<Kalburator::Sync::BackendRegistry> m_registry;
    QList<std::shared_ptr<WildPalms::IBackendPluginV2>> m_plugins;
    // ... mappings, baseline store, etc. — fleshed out in subsequent tasks
};

}  // namespace WildPalms::Runtime

#endif
```

- [ ] **Step 3: Skeleton .cpp with stub bodies**

`WildPalms/src/runtime/palmruntime.cpp`:

```cpp
#include "palmruntime.h"
#include "palmdeviceaccess.h"

#include <QPromise>
#include <QtConcurrent>

namespace WildPalms::Runtime {

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
{
    qRegisterMetaType<PalmRunResult>();
    // m_engine / m_registry constructed in subsequent tasks
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::connectDevice(KPilotLink *link) {
    Q_UNUSED(link);
    // Implemented in Task 6 (after Plan 2's calendar plugin migration).
}

void PalmRuntime::disconnectDevice() {
    m_device.reset();
    emit deviceDisconnected();
}

bool PalmRuntime::isDeviceConnected() const {
    return m_device != nullptr;
}

void PalmRuntime::setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess> device) {
    m_device = std::move(device);
    emit deviceConnected();
}

QList<QString> PalmRuntime::enabledPluginIds() const {
    QList<QString> ids;
    for (const auto &p : m_plugins) ids.append(p->pluginId());
    return ids;
}

QList<Kalburator::Sync::SyncMapping> PalmRuntime::palmMappings() const {
    return {};  // Implemented in Task 7 (mapping store)
}

QFuture<PalmRunResult> PalmRuntime::hotSync() {
    // Stub: return a synchronous successful future. Task 8 implements.
    QPromise<PalmRunResult> p;
    auto f = p.future();
    PalmRunResult r;
    r.success = true;
    p.start();
    p.addResult(r);
    p.finish();
    return f;
}

// fullSync, copyPalmToPC, copyPCToPalm, backup, restore — same stub pattern
QFuture<PalmRunResult> PalmRuntime::fullSync()      { return hotSync(); }
QFuture<PalmRunResult> PalmRuntime::copyPalmToPC()  { return hotSync(); }
QFuture<PalmRunResult> PalmRuntime::copyPCToPalm()  { return hotSync(); }
QFuture<PalmRunResult> PalmRuntime::backup()        { return hotSync(); }
QFuture<PalmRunResult> PalmRuntime::restore()       { return hotSync(); }

}  // namespace WildPalms::Runtime
```

- [ ] **Step 4: Add to CMake**

In `WildPalms/src/CMakeLists.txt` (or appropriate sub-CMakeLists), add:

```
runtime/palmruntime.cpp
```

to the source list. The header `palmrunresult.h` is header-only; if there's an explicit headers list, add it there too.

- [ ] **Step 5: Build**

```bash
cmake --build build 2>&1 | tail -10
```

Expected: clean. (PalmRuntime is currently unused; just verifies it compiles.)

- [ ] **Step 6: Commit**

```bash
git add src/runtime/palmruntime.{h,cpp} src/runtime/palmrunresult.h \
        src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): PalmRuntime + PalmRunResult skeleton

Top-level orchestrator class with stub bodies for the six sync modes.
Subsequent tasks wire engine + plugins + mappings; Task 8 implements
hotSync against MockBlobBackend for tests; M3 implements the other
five modes.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Migrate `PalmCalendarBackend` to use `PalmDeviceAccess`

**Files:**
- Modify: `WildPalms/src/palm/calendar/palmcalendarbackend.{h,cpp}`

`PalmCalendarBackend` today takes a raw `IPalmDatabaseAccess*` (typically the production wrapper around `KPilotLink`). It calls methods on that pointer synchronously, trusting its caller to be on the link thread.

After this task: `PalmCalendarBackend` takes a `PalmDeviceAccess*` (which IS-A `IPalmDatabaseAccess` since `PalmDeviceAccess` inherits from it). The same pointer-method calls now self-marshal because `PalmDeviceAccess`'s implementation does so.

The change is mostly a type-name swap in the constructor, plus updating any header forward-declarations. Method bodies that call `m_device->readAllRecords(...)` etc. stay the same — they go through `PalmDeviceAccess` now.

- [ ] **Step 1: Read `palmcalendarbackend.h`**

```bash
cat WildPalms/src/palm/calendar/palmcalendarbackend.h
```

Note the existing constructor signature — it likely takes `WildPalms::PalmSync::IPalmDatabaseAccess *device`. The pointer's type doesn't NEED to change, since `PalmDeviceAccess` IS-A `IPalmDatabaseAccess`. So this task may be a no-op at the source-file level — what matters is that callers of `PalmCalendarBackend` pass a `PalmDeviceAccess*` (which they will, in subsequent tasks).

If the constructor takes `IPalmDatabaseAccess *` and stores it as such, you don't need to change `PalmCalendarBackend`. Just verify that the code is correct as-is.

- [ ] **Step 2: Verify**

```bash
grep -n "IPalmDatabaseAccess" WildPalms/src/palm/calendar/palmcalendarbackend.h
grep -n "IPalmDatabaseAccess" WildPalms/src/palm/calendar/palmcalendarbackend.cpp
```

If it stores the access via the interface type, the constructor accepts a `PalmDeviceAccess*` transparently (because of inheritance). This task is then a no-op except for confirming the polymorphism works.

- [ ] **Step 3: If the backend stores the concrete `KPilotLink-wrapping` impl rather than the interface**

Adjust the member type to `IPalmDatabaseAccess*`. Pass-through.

- [ ] **Step 4: Build**

```bash
cmake --build build --target WildPalmsCore 2>&1 | tail -5
```

Expected: clean.

- [ ] **Step 5: Commit if you changed anything**

```bash
git add src/palm/calendar/palmcalendarbackend.{h,cpp}
git commit -m "$(cat <<'EOF'
refactor(palm-calendar): accept IPalmDatabaseAccess* (PalmDeviceAccess polymorphism)

PalmCalendarBackend can now be passed either the production
KPilotLink-wrapping access OR a self-marshalling PalmDeviceAccess.
The latter is what the new PalmRuntime hands it; the indirection
fixes the latent threading bug from FINDINGS.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

If no changes were needed, skip the commit and note in the report.

---

## Task 7: Migrate calendar plugin to `IBackendPluginV2`

**Files:**
- Modify: `WildPalms/src/plugins/calendar/calendarbackendplugin.{h,cpp}` (or wherever the calendar plugin's main class lives — find via `grep -rn "class.*CalendarBackendPlugin" WildPalms/src/plugins/`)

The calendar plugin currently implements `IBackendPlugin` (v1). We migrate it to `IBackendPluginV2`:

- Drop `createBackends(host, device)` (returning struct).
- Add `createPalmBackend(PalmDeviceAccess*)` returning `unique_ptr<IBlobBackend>`.
- The PC-side backend the v1 contract returned (typically a `LocalBlobBackend`) is dropped — PC side is now per-mapping.
- Inherit from `IBackendPluginV2` instead of `IBackendPlugin`.
- Keep `claimedDatabases()`, `runBefore/runAfter`, `createConflictHandler`, the GUI surface methods unchanged.

- [ ] **Step 1: Read the existing plugin class**

```bash
find WildPalms/src/plugins -name "calendar*backend*.h"
```

Read the file. Note its current inheritance (`IBackendPlugin`), the body of `createBackends`, and how it constructs the `PalmCalendarBackend` instance.

- [ ] **Step 2: Update the header**

Change the inheritance from `IBackendPlugin` to `IBackendPluginV2`. Update the include from `"core/ibackendplugin.h"` to `"core/ibackendplugin_v2.h"`. Replace `createBackends` declaration with:

```cpp
std::unique_ptr<Kalburator::Sync::IBlobBackend>
    createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;
```

Forward-declare `WildPalms::Runtime::PalmDeviceAccess` at the top.

- [ ] **Step 3: Update the cpp**

Replace `createBackends`'s body. It should now return ONLY the Palm calendar backend, constructed against the supplied `PalmDeviceAccess` (passed as the `IPalmDatabaseAccess*` since the latter is the base interface):

```cpp
std::unique_ptr<Kalburator::Sync::IBlobBackend>
CalendarBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    return std::make_unique<WildPalms::PalmCalendar::PalmCalendarBackend>(
        device,                           // PalmDeviceAccess IS-A IPalmDatabaseAccess
        QStringLiteral("palm-device:0")); // device id; TODO: derive from KPilotLink
}
```

(Adapt to the actual PalmCalendarBackend constructor signature.)

The old PC-side LocalBlobBackend creation is removed entirely. PC side is per-mapping config, not plugin choice (per spec §6).

- [ ] **Step 4: Build**

```bash
cmake --build build --target calendarbackendplugin 2>&1 | tail -10
```

(Use whatever the plugin target is named — discover via grep in CMakeLists.)

Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add src/plugins/calendar/calendarbackendplugin.{h,cpp}
git commit -m "$(cat <<'EOF'
refactor(plugin-calendar): migrate to IBackendPluginV2 contract

createBackends → createPalmBackend(PalmDeviceAccess*). PC-side
backend dropped; user-mapping selects it now (spec §6).
PalmCalendarBackend constructed against the self-marshalling
PalmDeviceAccess.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Failing test for `PalmRuntime::hotSync` end-to-end

**Files:**
- Create: `WildPalms/tests/runtime/tst_palm_runtime_hotsync.cpp`
- Modify: `WildPalms/tests/runtime/CMakeLists.txt`

Tests `PalmRuntime::hotSync()` by injecting a mock `PalmDeviceAccess` (over a thread-safe mock `IPalmDatabaseAccess` with a known calendar-DB record set). Verifies the future completes with success and the per-plugin stats reflect the records moved.

Bigger conceptually than Task 5 because we need:
- A mock `IPalmDatabaseAccess` with seeded records.
- A way to register the calendar plugin against the runtime in test-mode (without going through KPluginFactory's loading machinery).
- A way to seed mappings into the runtime.
- A PC-side backend (use libkalburator's `MockBlobBackend` or `LocalBlobBackend` against `QTemporaryDir`).

Defer the test infrastructure complexity to Task 9 (which builds the production PalmRuntime fully). For Task 8, write the simplest test that drives `hotSync` from a constructed runtime. If it fails because Task 9's wiring isn't done, that's the red phase.

- [ ] **Step 1: Write the test**

```cpp
#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "runtime/palmrunresult.h"

// MockPalmDatabaseAccess: holds seeded records, returns them on
// readAllRecords. (Adapt from the ThreadCapturingMock in
// tst_palm_device_access.cpp; rename here to focus on data, not
// threading.)

class TestPalmRuntimeHotSync : public QObject {
    Q_OBJECT
private slots:
    void hotSync_calendar_emptyTarget_records_propagate() {
        QTemporaryDir profileDir;
        WildPalms::Runtime::PalmRuntime runtime(profileDir.path());

        // Seed mock device with one calendar event.
        // [Construct mock IPalmDatabaseAccess; seed CalendarDB with a record;
        //  wrap in PalmDeviceAccess; setDeviceAccessForTest.]
        // [Register calendar plugin manually (see Task 9 for the helper).]
        // [Configure a SyncMapping: source=calendar plugin, target=LocalBlobBackend
        //  rooted at profileDir, sourceCollection="calendar", targetCollection="cal".]

        auto future = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 5000);
        const auto result = future.resultAt(0);

        QVERIFY(result.success);
        QCOMPARE(result.perPluginStats.value("calendar").created, 1);
    }
};

QTEST_GUILESS_MAIN(TestPalmRuntimeHotSync)
#include "tst_palm_runtime_hotsync.moc"
```

(The bracketed setup steps depend on Task 9's API. Write the test calls assuming reasonable APIs — `runtime.registerPluginForTest(plugin)`, `runtime.setMappingsForTest(mappings)` — and Task 9 provides them.)

- [ ] **Step 2: Register in CMakeLists**

```cmake
wildpalms_add_runtime_test(tst_palm_runtime_hotsync)
```

- [ ] **Step 3: Build + observe failure**

```bash
cmake --build build --target tst_palm_runtime_hotsync 2>&1 | tail -10
```

Expected: compile failure if Task 9's helpers (`registerPluginForTest`, `setMappingsForTest`) don't exist yet, OR test failure at runtime if they exist as stubs but `hotSync` isn't real yet.

- [ ] **Step 4: Do NOT commit.** Test lands with Task 9's implementation.

---

## Task 9: Implement `PalmRuntime::hotSync` end-to-end

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.{h,cpp}`

Wires the runtime's internal SyncEngine, BackendRegistry, BlobBaselineStore, and mapping store. Implements `hotSync()` to delegate to `engine.runSyncFuture(palmMappingIds)`.

Add test-only helpers (`registerPluginForTest`, `setMappingsForTest`) that bypass KPluginFactory — production uses `connectDevice()` to load plugins, but tests need direct injection.

- [ ] **Step 1: Add test-only API to header**

```cpp
public:
    // ── Test seam (in addition to setDeviceAccessForTest) ────────────
    void registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2>);
    void setMappingsForTest(QList<Kalburator::Sync::SyncMapping>);
```

- [ ] **Step 2: Add SyncEngine + BackendRegistry construction**

Construct in PalmRuntime's ctor:

```cpp
PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
    , m_baselineStore(std::make_unique<Kalburator::Sync::BlobBaselineStore>(
        QDir(profilePath).filePath(QStringLiteral(".wildpalms-sync.db"))))
    , m_registry(std::make_unique<Kalburator::Sync::BackendRegistry>())
    , m_engine(std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), /*host=*/nullptr))
{
    qRegisterMetaType<PalmRunResult>();
}
```

(Add the corresponding `unique_ptr` members to the header.)

- [ ] **Step 3: Implement `registerPluginForTest`**

```cpp
void PalmRuntime::registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2> plugin) {
    m_plugins.append(plugin);
    if (m_device) {
        // Construct the Palm backend now so it's available for hotSync.
        auto backend = plugin->createPalmBackend(m_device.get());
        m_registry->registerBackend(backend.release());
        // (Adapt to whatever ownership model BackendRegistry uses.)
    }
}
```

- [ ] **Step 4: Implement `setMappingsForTest`**

```cpp
void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
    m_engine->setSyncMappings(m_mappings);
}
```

- [ ] **Step 5: Implement `hotSync`**

```cpp
QFuture<PalmRunResult> PalmRuntime::hotSync() {
    emit runStarted(QStringLiteral("HotSync"));

    // Collect Palm mapping IDs.
    QStringList palmMappingIds;
    for (const auto &m : m_mappings) {
        palmMappingIds.append(m.id);  // TODO: filter to only Palm mappings via resourceId() prefix
    }

    if (palmMappingIds.isEmpty()) {
        PalmRunResult r;
        r.success = true;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        emit runFinished(r);
        return QtFuture::makeReadyFuture(r);
    }

    auto engineFuture = m_engine->runSyncFuture(palmMappingIds);

    // Wrap engineFuture's QList<SyncResult> into our PalmRunResult.
    return engineFuture.then([this](QList<Kalburator::Sync::SyncResult> results) {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();  // approximate
        for (const auto &sr : results) {
            // Aggregate per-plugin stats from SyncResult.
            // (SyncResult shape per libkalburator/src/core/syncresult.h; adapt.)
            // ...
        }
        r.endTime = QDateTime::currentDateTimeUtc();
        r.success = std::all_of(results.cbegin(), results.cend(),
                                [](const auto &sr) { return sr.success; });
        emit runFinished(r);
        return r;
    });
}
```

(Adapt to the actual SyncResult type. If `runSyncFuture` returns the form documented in libkalburator's CLAUDE.md, this should slot in.)

- [ ] **Step 6: Build + run the test from Task 8**

```bash
cmake --build build --target tst_palm_runtime_hotsync 2>&1 | tail -10
ctest --test-dir build -R tst_palm_runtime_hotsync -V 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/palmruntime.{h,cpp} \
        tests/runtime/tst_palm_runtime_hotsync.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): PalmRuntime::hotSync delegates to engine.runSyncFuture

Wires internal SyncEngine + BackendRegistry + BlobBaselineStore.
hotSync collects Palm mapping IDs and delegates to runSyncFuture,
wrapping the per-mapping SyncResult list into PalmRunResult.
Test-only registerPluginForTest / setMappingsForTest seams let tests
inject plugins + mappings without going through KPluginFactory.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Implement `PalmRuntime::connectDevice` (production path)

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.cpp`

Production code path: KF6MainWindow obtains a live `KPilotLink` (existing wiring), passes it to `PalmRuntime::connectDevice(link)`. The runtime constructs `PalmDeviceAccess` around the production `IPalmDatabaseAccess` wrapper, loads enabled plugins (via the existing `BackendPluginManager`), and instantiates each plugin's Palm backend against the access object.

The plugin loading is pre-existing; we just adapt to read the new `IBackendPluginV2` interface. Since for M2-M3 only the calendar plugin is enabled (per Task 1's CMake gate), this is a single-plugin code path.

- [ ] **Step 1: Implement connectDevice**

```cpp
void PalmRuntime::connectDevice(KPilotLink *link) {
    Q_ASSERT(link);
    if (m_device) {
        qWarning("PalmRuntime::connectDevice: device already connected");
        return;
    }

    // Construct the production IPalmDatabaseAccess wrapping KPilotLink.
    // (Use whatever the existing WildPalms code uses — likely a
    // KPilotLinkAccess or PilotLinkAccess class. Discover.)
    auto productionImpl = std::make_unique<WildPalms::PalmSync::KPilotLinkAccess>(link);
    m_device = std::make_unique<PalmDeviceAccess>(std::move(productionImpl));

    // Load plugins via BackendPluginManager (existing infrastructure).
    // For M2-M3 this enumerates only the calendar plugin (others are
    // CMake-disabled).
    // ... use the existing manager; iterate and registerPluginForTest()
    //     on each (rename: registerPlugin or similar).

    emit deviceConnected();
}
```

(The exact `KPilotLinkAccess` class name and `BackendPluginManager` API are codebase-specific. Discover by reading `src/runtime/backendpluginmanager.{h,cpp}` and `src/palm/sync/`.)

- [ ] **Step 2: Build + verify the production path compiles**

```bash
cmake --build build --target WildPalmsCore 2>&1 | tail -10
```

Expected: clean.

- [ ] **Step 3: Commit**

```bash
git add src/runtime/palmruntime.cpp
git commit -m "$(cat <<'EOF'
feat(runtime): PalmRuntime::connectDevice — production KPilotLink path

Wraps a live KPilotLink in PalmDeviceAccess and loads enabled
v2-contract plugins via BackendPluginManager. M2-M3 only loads the
calendar plugin (per WILDPALMS_CALENDAR_MVP_ONLY); M4 enables the
others.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Rewire `KF6MainWindow`'s HotSync action to call `PalmRuntime`

**Files:**
- Modify: `WildPalms/src/kf6/kf6mainwindow.{h,cpp}`

This is the M2 MVP integration point. After this task, the existing HotSync action in the Tools menu invokes `m_runtime->hotSync()` instead of the old `m_session->requestSync(SyncMode::HotSync, m_syncRunner)`.

The other Tools-menu actions (FullSync/Backup/Restore/Copy*) are temporarily disabled (greyed out) — M3 re-enables each as its `PalmRuntime` method lands.

- [ ] **Step 1: Add `PalmRuntime` member to KF6MainWindow**

```cpp
// In kf6mainwindow.h, near the existing m_session, m_syncRunner members:
std::unique_ptr<WildPalms::Runtime::PalmRuntime> m_palmRuntime;
```

Forward-declare `WildPalms::Runtime::PalmRuntime`. Include in cpp.

- [ ] **Step 2: Construct in KF6MainWindow's ctor**

After the existing setup (around where `m_syncRunner` is constructed), add:

```cpp
m_palmRuntime = std::make_unique<WildPalms::Runtime::PalmRuntime>(
    m_currentProfile->stateDirectoryPath(),
    this);

// Forward log/progress signals to existing GUI sinks.
connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runLog,
        this, &KF6MainWindow::onSyncLogMessage);
connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runFinished,
        this, &KF6MainWindow::onSyncFinished);
```

(Adapt slot names to match existing handlers — `onSyncLogMessage`, `onSyncFinished` are placeholders.)

- [ ] **Step 3: Wire device connection lifecycle**

When the existing code receives a live `KPilotLink` (search for where `setKPilotLink` is currently called), also feed it to PalmRuntime:

```cpp
m_palmRuntime->connectDevice(m_deviceLink);
```

And on disconnection:

```cpp
m_palmRuntime->disconnectDevice();
```

- [ ] **Step 4: Rewire HotSync action**

Find the action handler currently doing `m_session->requestSync(Sync::SyncMode::HotSync, m_syncRunner)` (around `kf6mainwindow.cpp:1761`). Replace with:

```cpp
void KF6MainWindow::onHotSyncTriggered() {
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        QMessageBox::warning(this, tr("HotSync"),
                             tr("No Palm device connected."));
        return;
    }
    auto future = m_palmRuntime->hotSync();
    auto *watcher = new QFutureWatcher<WildPalms::Runtime::PalmRunResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, watcher, [this, watcher]() {
        const auto result = watcher->result();
        // Report success / log to status bar / etc.
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}
```

The original `m_session->requestSync` call is removed.

- [ ] **Step 5: Disable the other Tools-menu actions**

For each of the FullSync / CopyPalmToPC / CopyPCToPalm / Backup / Restore action setups (in whatever method registers them with KActionCollection), temporarily set `setEnabled(false)` with a tooltip:

```cpp
fullSyncAction->setEnabled(false);
fullSyncAction->setToolTip(tr("Disabled during runtime rewrite (M3 re-enables)"));
```

(Repeat for the other four. They re-enable in Tasks 13-17.)

- [ ] **Step 6: Build + confirm app starts**

```bash
cmake --build build --target wildpalms 2>&1 | tail -10
```

Expected: clean. The app should launch (manual test in M2 verification, not now).

- [ ] **Step 7: Commit**

```bash
git add src/kf6/kf6mainwindow.{h,cpp}
git commit -m "$(cat <<'EOF'
feat(gui): KF6MainWindow HotSync action wired to PalmRuntime

m_palmRuntime member holds the new orchestrator. HotSync's Tools-menu
handler now calls runtime.hotSync() and watches the QFuture. Old
m_session->requestSync(...) chain is unhooked from this action only;
remaining mode actions disabled with tooltip until M3 lands them.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: M2 verify gate — build + automated tests

**Files:** none modified.

- [ ] **Step 1: Clean build of WildPalms**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
rm -rf build
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_CALENDAR_MVP_ONLY=ON 2>&1 | tail -10
cmake --build build 2>&1 | tail -15
```

Expected: clean. If `SyncRunner_wp` still fails to compile (Plan 1's intentional breakage), that's because we haven't excluded it from the build. Decide:
- (a) Exclude `syncrunner_wp.{h,cpp}` from the build via a conditional in `src/CMakeLists.txt` (gated on `WILDPALMS_CALENDAR_MVP_ONLY`). Cleaner.
- (b) Stub out the F1-facade calls in `syncrunner_wp.cpp` to make it compile (return error). Janky but quick.

**Recommend (a).** Exclude `SyncRunner_wp` and `DeviceSession`/`DeviceWorker`'s mode-dispatch from the build entirely; M6 deletes them. The DeviceSession/DeviceWorker connection-lifecycle code stays — only the runner-handling parts are excluded.

If this surfaces additional breakage (other code calling into `SyncRunner_wp` or `DeviceSession::requestSync`), excise those calls in this task or note in report.

- [ ] **Step 2: Run automated tests**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -15
```

Expected: all enabled tests pass — at minimum `tst_palm_device_access` (2 slots) and `tst_palm_runtime_hotsync` (1+ slots).

- [ ] **Step 3: Commit any cleanup**

If you excluded SyncRunner_wp from the build (Step 1a), commit the CMake change:

```bash
git add src/CMakeLists.txt $(other modified CMake files)
git commit -m "$(cat <<'EOF'
chore(build): exclude SyncRunner_wp + DeviceSession::requestSync from MVP build

The mode-dispatch code in those files no longer compiles after Plan 1
deleted the F1 facade. M6 deletes the source entirely; for now we
exclude from the build behind WILDPALMS_CALENDAR_MVP_ONLY.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: M2 real-device verification (USER STEP)

**Files:** none modified.

⚠ **This task requires a real Palm device. The implementer subagent CANNOT complete it autonomously. The implementer's job is to write the test script and HAND IT TO THE USER. The user reports back with results.**

- [ ] **Step 1: Implementer prepares the script**

Write a section in the report to the controller:

```markdown
## M2 Real-Device Test Script (for the user)

### Prerequisites
- A real Palm device with pilot-link connectivity working (cradle, USB, or serial as configured).
- A known-good test calendar dataset on the Palm — at minimum 5 events:
  one one-time, one recurring (weekly), one all-day, one with an alarm,
  one with attached notes.
- A clean WildPalms profile (delete `~/.local/share/wildpalms/<profile>/.wildpalms-sync.db`
  if it exists, to start with no baselines).

### Steps
1. Build and launch:
   ```
   cd ~/dev/refactor-engine-merger/WildPalms
   ./build/wildpalms  # or whatever the executable is named
   ```
2. Verify the GUI shows the existing KF6MainWindow shell with a Tools menu.
3. Confirm Tools menu shows: HotSync (enabled), FullSync (greyed out, hover tooltip),
   Backup/Restore/CopyPalmToPC/CopyPCToPalm (all greyed out).
4. Connect the Palm device.
5. Trigger Tools → HotSync.
6. Observe:
   - The sync-log dock or status bar (if present) should show progress.
   - The future completes within reasonable time (< 30s for a small dataset).
   - No app freeze (GUI remains responsive — try clicking other menus).
7. Verify on the PC side: open the configured PC backend (RawFiles dump, Akonadi
   collection, or wherever the calendar mapping points) and confirm all 5 events
   are present.
8. Modify one event on the PC side (change its summary).
9. Trigger HotSync again.
10. Verify the modified event syncs back to Palm (open it on the Palm and check).
11. Modify a different event on the Palm side.
12. Trigger HotSync.
13. Verify the Palm change appears on PC.

### Success
All 13 steps pass; no crashes; no GUI freezes; data integrity preserved both directions.

### Failure modes worth flagging
- If GUI freezes during sync: PalmDeviceAccess marshalling isn't keeping the GUI thread free.
  Likely the Tools-menu handler is doing the wait synchronously — re-check QFutureWatcher.
- If Palm calls fail with a thread-affinity error: the marshalling isn't reaching the
  link thread; investigate PalmDeviceAccess::connectDevice setup.
- If records appear duplicated on PC after HotSync: the create/update path in
  dispatchBlobSync may not be picking the right backend; check baseline store v3
  contents.
- If sync hangs forever: nested QEventLoop in SyncEngineWorker may not be receiving
  the Palm thread's completions; investigate cancellation channel.

Report the outcome to the controller as: PASSED, FAILED, or BLOCKED (with details).
```

- [ ] **Step 2: Implementer reports back to controller**

Status: NEEDS_USER_VERIFICATION
- Description of M2 functionality landed
- The above test script
- Wait for the user (via controller) to perform the test and report results.

⚠ **CONTROLLER**: when this task's implementer reports NEEDS_USER_VERIFICATION, surface the test script to the human user and pause execution. Do NOT proceed to Task 14 until the user confirms M2 verification passed. If the user reports failure, dispatch a debugging task with the failure details.

---

## Task 14: Implement `PalmRuntime::fullSync`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.{h,cpp}`

FullSync = HotSync + clear baselines first.

- [ ] **Step 1: Implementation**

```cpp
QFuture<PalmRunResult> PalmRuntime::fullSync() {
    emit runStarted(QStringLiteral("FullSync"));

    // Reset baselines for each Palm mapping.
    for (const auto &m : m_mappings) {
        m_baselineStore->clearMappingV3(m.id);
    }

    // Then run HotSync semantics.
    return hotSync();  // emits runFinished from inside
}
```

- [ ] **Step 2: Re-enable the FullSync action in KF6MainWindow**

```cpp
fullSyncAction->setEnabled(true);
fullSyncAction->setToolTip({});  // clear the placeholder
```

Wire the action's handler:

```cpp
void KF6MainWindow::onFullSyncTriggered() {
    if (!m_palmRuntime || !m_palmRuntime->isDeviceConnected()) {
        QMessageBox::warning(this, tr("FullSync"), tr("No Palm device connected."));
        return;
    }
    auto future = m_palmRuntime->fullSync();
    // ... QFutureWatcher pattern as in HotSync
}
```

- [ ] **Step 3: Test slot**

Add to `tests/runtime/tst_palm_runtime_modes.cpp` (create if not yet existing per Task 15):

```cpp
    void fullSync_clearsBaselines_thenRunsHotSync() {
        // Setup: device with 1 record; mapping with seeded baseline pointing
        // at a stale record. After fullSync, the baseline should be reset
        // and the actual record's baseline reflects current state.
        // ...
    }
```

- [ ] **Step 4: Build + test + commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add src/runtime/palmruntime.{h,cpp} src/kf6/kf6mainwindow.{h,cpp} \
        tests/runtime/tst_palm_runtime_modes.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): FullSync — clear baselines then HotSync

Re-enables the FullSync Tools-menu action. baselineStore.clearMappingV3
for each Palm mapping, then delegates to hotSync().

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: Implement `PalmRuntime::copyPalmToPC` and `copyPCToPalm`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.{h,cpp}`
- Modify: `WildPalms/src/kf6/kf6mainwindow.{h,cpp}`
- Create: `WildPalms/tests/runtime/tst_palm_runtime_modes.cpp` (if not yet)
- Modify: `WildPalms/tests/runtime/CMakeLists.txt`

These are engine-driven mirror modes using Plan 1's `ExecutionOverride`.

- [ ] **Step 1: Implementation**

```cpp
QFuture<PalmRunResult> PalmRuntime::copyPalmToPC() {
    return runMirror(Kalburator::Sync::ExecutionOverride::Direction::MirrorAToB,
                     QStringLiteral("CopyPalmToPC"));
}

QFuture<PalmRunResult> PalmRuntime::copyPCToPalm() {
    return runMirror(Kalburator::Sync::ExecutionOverride::Direction::MirrorBToA,
                     QStringLiteral("CopyPCToPalm"));
}

// Helper:
QFuture<PalmRunResult> PalmRuntime::runMirror(
    Kalburator::Sync::ExecutionOverride::Direction direction,
    const QString &modeLabel)
{
    emit runStarted(modeLabel);

    Kalburator::Sync::ExecutionOverride ov;
    ov.direction = direction;

    // Run each Palm mapping with the override.
    // (engine.runSyncFuture(mappingId, override) is single-mapping;
    //  iterate and accumulate. If multi-mapping override is needed,
    //  this is where the loop goes. For M3, single-mapping per call
    //  is fine since calendar is the only enabled plugin.)
    QStringList palmMappingIds;
    for (const auto &m : m_mappings) palmMappingIds.append(m.id);

    if (palmMappingIds.isEmpty()) {
        PalmRunResult r;
        r.success = true;
        r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
        emit runFinished(r);
        return QtFuture::makeReadyFuture(r);
    }

    // Single mapping for now (calendar only); multi-mapping would
    // QtConcurrent::map over palmMappingIds.
    auto engineFuture = m_engine->runSyncFuture(palmMappingIds.first(), ov);
    return engineFuture.then([this](Kalburator::Sync::SyncResult sr) {
        PalmRunResult r;
        r.success = sr.success;
        // ... aggregate stats
        emit runFinished(r);
        return r;
    });
}
```

Add `runMirror` declaration as a private helper in the header.

- [ ] **Step 2: Re-enable Tools-menu actions**

```cpp
copyPalmToPCAction->setEnabled(true);
copyPCToPalmAction->setEnabled(true);
// Wire handlers analogous to onHotSyncTriggered.
```

- [ ] **Step 3: Test slots**

```cpp
    void copyPalmToPC_overwritesPC() {
        // Setup: PC has record X; Palm has record Y. After CopyPalmToPC,
        // PC has only Y, X is gone.
        // ...
    }

    void copyPCToPalm_overwritesPalm() {
        // Symmetric.
        // ...
    }
```

- [ ] **Step 4: Build + test + commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add src/runtime/palmruntime.{h,cpp} src/kf6/kf6mainwindow.{h,cpp} \
        tests/runtime/tst_palm_runtime_modes.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(runtime): CopyPalmToPC and CopyPCToPalm via ExecutionOverride mirror

Both modes route through engine.runSyncFuture(mappingId, override) with
the appropriate MirrorAToB / MirrorBToA direction. Re-enables the two
Copy actions in the Tools menu.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16: Implement `PalmRuntime::backup`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.{h,cpp}`
- Modify: `WildPalms/src/kf6/kf6mainwindow.{h,cpp}`
- Modify: `WildPalms/tests/runtime/tst_palm_runtime_modes.cpp`

Pure runtime loop, no engine involvement. For each enabled plugin, instantiate a `RawFilesBackend` rooted at `<backupRoot>/<pluginId>/<collectionId>/`, walk records on Palm, write missing or differing ones to the file backup, never delete.

- [ ] **Step 1: Implementation**

```cpp
QFuture<PalmRunResult> PalmRuntime::backup() {
    emit runStarted(QStringLiteral("Backup"));

    return QtConcurrent::run([this]() -> PalmRunResult {
        PalmRunResult r;
        r.startTime = QDateTime::currentDateTimeUtc();

        for (const auto &plugin : m_plugins) {
            const QString pluginId = plugin->pluginId();
            // Find backend for this plugin in registry.
            auto *palmBackend = m_registry->backendForBackendId(/* plugin's backend id */);
            if (!palmBackend) continue;

            for (const auto &col : palmBackend->availableCollections()) {
                // Construct a per-collection RawFilesBackend.
                const QString backupPath = QDir(m_backupRoot)
                    .filePath(pluginId + "/" + col.id);
                Kalburator::Sync::RawFilesBackend backupSink(backupPath);
                backupSink.createCollection(col);

                // Walk Palm records.
                for (const auto &rec : palmBackend->loadRecords(col.id)) {
                    auto existing = backupSink.loadRecord(rec.id);
                    if (existing && existing->contentHash == rec.contentHash) {
                        ++r.perPluginStats[pluginId].unchanged;
                    } else if (existing) {
                        if (backupSink.updateRecord(rec)) {
                            ++r.perPluginStats[pluginId].updated;
                        } else {
                            ++r.perPluginStats[pluginId].errors;
                        }
                    } else {
                        if (!backupSink.createRecord(col.id, rec).isEmpty()) {
                            ++r.perPluginStats[pluginId].created;
                        } else {
                            ++r.perPluginStats[pluginId].errors;
                        }
                    }
                }
                // Note: never deletes from backupSink — Backup is additive.
            }
        }

        r.endTime = QDateTime::currentDateTimeUtc();
        r.success = true;
        for (const auto &stats : r.perPluginStats) {
            if (stats.errors > 0) { r.success = false; break; }
        }
        QMetaObject::invokeMethod(this, [this, r]() { emit runFinished(r); });
        return r;
    });
}
```

(The `m_backupRoot` member: add to header; default to `<profilePath>/backup/` if not configured.)

- [ ] **Step 2: Re-enable Backup action; wire handler**

Same pattern as HotSync.

- [ ] **Step 3: Test slot**

```cpp
    void backup_additive_preservesPCOnlyRecords() {
        // Setup: Palm has {a, b}; backup root has {a-old, c}. After Backup,
        // backup root has {a (updated), b (created), c (preserved)}.
        // ...
    }
```

- [ ] **Step 4: Build + test + commit**

---

## Task 17: Implement `PalmRuntime::restore`

**Files:**
- Modify: `WildPalms/src/runtime/palmruntime.{h,cpp}`
- Modify: `WildPalms/src/kf6/kf6mainwindow.{h,cpp}`
- Modify: `WildPalms/tests/runtime/tst_palm_runtime_modes.cpp`

Pure runtime loop. For each plugin, construct the backup-side `RawFilesBackend`, clear the Palm side of each collection, then write every backup record back.

- [ ] **Step 1: Implementation**

Same pattern as Backup, but inverted: load from backupSink, write to palmBackend, delete Palm records absent from backup.

- [ ] **Step 2: Confirmation dialog before action**

Restore is destructive. Add a confirmation dialog in KF6MainWindow's handler:

```cpp
void KF6MainWindow::onRestoreTriggered() {
    if (QMessageBox::question(this, tr("Restore"),
        tr("Restore is destructive. All Palm records not in the backup "
           "WILL BE DELETED. Continue?"),
        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    auto future = m_palmRuntime->restore();
    // ... watcher pattern
}
```

- [ ] **Step 3: Test slot, build, commit.**

---

## Task 18: M3 verify gate — automated tests

**Files:** none modified.

- [ ] **Step 1: Full WildPalms test suite**

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake --build build
ctest --test-dir build --output-on-failure 2>&1 | tail -20
```

Expected: all enabled tests pass.

- [ ] **Step 2: Confirm Tools menu enables all six actions**

Build the GUI, launch it briefly (no device needed), confirm Tools menu shows all six modes enabled.

```bash
./build/wildpalms &
sleep 2; pkill wildpalms  # quick smoke test
```

(Manual visual confirmation; the smoke test just verifies no startup crash.)

---

## Task 19: M3 real-device verification (USER STEP)

⚠ **Same pattern as Task 13 — implementer writes test script for user, controller pauses for user confirmation.**

- [ ] **Step 1: Implementer prepares M3 test script**

Test each of the six modes against a real Palm device:

```markdown
## M3 Real-Device Test Script (for the user)

### Prerequisites
Same as M2 plus: a configured backup root path; backup-mode mappings registered
in profile config (or via the test harness).

### Steps
For each of the 6 modes, follow this template:
1. Establish a known initial state on Palm and PC.
2. Trigger the mode via Tools menu.
3. Observe completion (no freeze, log shows expected progress).
4. Verify resulting state on Palm and PC.

Mode-specific verification:

- **HotSync**: bidirectional changes propagate; conflicts appear in dialog (M5 detail).
- **FullSync**: same as HotSync but baseline reset; verify a previously-unchanged
  record on the side that was the "winner" gets re-synced.
- **CopyPalmToPC**: PC ends as exact mirror of Palm (PC-only records gone).
- **CopyPCToPalm**: Palm ends as exact mirror of PC (Palm-only records gone).
- **Backup**: PC backup root accumulates Palm records additively; PC-only records
  in the backup root are preserved.
- **Restore**: Palm overwritten from backup; Palm records absent from backup deleted.

### Success
All six modes complete without crash, GUI stays responsive, data integrity matches
each mode's documented semantics.
```

- [ ] **Step 2: Hand off to user.** Wait for confirmation before declaring M3 done.

---

## Task 20: M3 wrap-up — CURRENT-STATUS update

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`

Coordination folder isn't a git repo; file edit only.

- [ ] **Step 1: Update date + add M2/M3 entry to "Where we are"**

```markdown
✅ **Palm runtime rewrite — Plan 2 (M2 + M3 calendar-MVP)** — landed YYYY-MM-DD.
   New PalmRuntime + PalmDeviceAccess on `palm-rewrite` branch in WildPalms.
   All six sync modes wired and verified against real Palm device with the
   calendar plugin only. memo/contacts/todo/webcal disabled via
   WILDPALMS_CALENDAR_MVP_ONLY (Plan 3 / M4 re-enables). New IBackendPluginV2
   contract; calendar plugin migrated. KF6MainWindow shell preserved;
   Tools-menu actions rewired to PalmRuntime. SyncRunner_wp / DeviceSession
   mode-dispatch excluded from build (M6 deletes source).
```

- [ ] **Step 2: Update "Next" to point at Plan 3 (M4)**

```markdown
⬜ **Palm runtime rewrite — Plan 3 (M4 plugin migrations)** — to be drafted.
   Migrate memo, contacts, todo, webcal to IBackendPluginV2; re-enable in
   CMake; verify each against real device. ~1 week estimated.
```

- [ ] **Step 3: Append M2/M3 commits to "Recently committed"**

Use `git log --oneline -25` in the WildPalms worktree on `palm-rewrite` branch.

---

## Self-Review (before declaring complete)

**Spec coverage check:**
- §4 (PalmDeviceAccess + IBackendPluginV2) — Tasks 2, 3, 4 ✓
- §5 (PalmRuntime) — Tasks 5, 9, 10, 14, 15, 16, 17 ✓
- §5.2 (threading model — self-marshalling backends) — Tasks 2, 3 ✓
- §5.3 (six sync modes) — Tasks 9, 14, 15, 16, 17 ✓
- §6 (config model) — partial: enabledPluginIds + mappings via test seam only; full settings UI deferred to M5 ✓
- §7 (GUI shell) — KF6MainWindow rewiring in Tasks 11, 14, 15, 16, 17 ✓
- §8 (M2 milestone) — Tasks 1-13 ✓
- §8 (M3 milestone) — Tasks 14-19 ✓
- §9 (testing strategy — real-device gates) — Tasks 13, 19 explicitly require user verification ✓

**Tasks deferred to subsequent plans:**
- M4: per-plugin migration (memo/contacts/todo/webcal) → Plan 3
- M5: settings dialog + mapping editor + conflict dialog + per-plugin views wiring → Plan 4
- M6: source deletion (SyncRunner_wp etc.) → Plan 5
- M7: merge to refactor/engine-merger → Plan 5

**Type / signature consistency:**
- `PalmDeviceAccess` IS-A `IPalmDatabaseAccess` — used throughout consistently
- `IBackendPluginV2::createPalmBackend(PalmDeviceAccess*)` — same signature in Tasks 4, 7, 9, 10
- `PalmRuntime::registerPluginForTest(shared_ptr<IBackendPluginV2>)` — same in Tasks 8, 9
- `ExecutionOverride::Direction::{MirrorAToB, MirrorBToA}` — Plan 1 §3.2 type, used in Tasks 15

**Placeholder scan:**
- Three explicit `(adapt to actual codebase X)` notes in Tasks 7, 10, 16 where the implementer needs to discover existing class names. These are not placeholders — they're acknowledgments that the plan can't pre-determine codebase-specific names.

**Real-device gates:**
- Tasks 13 and 19 explicitly mark themselves as requiring user verification. Implementer subagents will report `NEEDS_USER_VERIFICATION`; controller must pause for user input.

---

## Coordination notes for the controller

- This plan operates on a NEW branch (`palm-rewrite`) in WildPalms only. libkalburator stays on `refactor/engine-merger` (with M1 commits already landed).
- `verify-all.sh` is unreliable during this plan because WildPalms is intentionally rewriting. Use targeted ctest in WildPalms's `build/` for verification.
- Real-device tests (Tasks 13, 19) cannot be subagent-completed. Controller must surface the test scripts to the user and wait for confirmation.
- If M2 verification (Task 13) fails, debug WITH the user before proceeding to M3 — the failure is likely a real bug that subsequent tasks will compound.
- Task 12 documents an edge case where `SyncRunner_wp` may need to be excluded from the MVP build. The exclusion is part of M2's scope; M6 deletes source.
