# Sub-Project D — Conflict Surfacing Wired — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make AskUser conflicts visible. PalmRuntime re-emits the embedded SyncEngine's `conflictDetected` signal; `KF6MainWindow` listens, maintains an unresolved-count from `SyncConflictStore`, shows a status-bar badge, and opens the existing `ConflictReviewDialog` non-modally on click.

**Architecture:** Library-side (libkalburator) already does the heavy lifting: the engine emits `conflictDetected` and persists conflicts to `SyncConflictStore`. This plan adds the WildPalms-side listener + UI badge + dialog wiring. PalmRuntime forwards the signal without exposing the engine (cleaner abstraction boundary). KF6MainWindow tracks the count; the badge is a flat `QPushButton` in the status bar; clicking opens `ConflictReviewDialog` (already shipped — see `src/widgets/dialogs/conflictreviewdialog.{h,cpp}`).

**Tech Stack:** Qt6, KF6, C++17.

**Spec:** `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` §4.4

**Build / test commands:**
- `cmake --build build-dev -j$(nproc)`
- `ctest --test-dir build-dev --output-on-failure -j$(nproc)`

**File inventory:**

Modified:
- `src/runtime/palmruntime.h` — add `conflictDetected` Q_SIGNAL.
- `src/runtime/palmruntime.cpp` — connect `m_engine`'s `conflictDetected` to `this` in ctor.
- `src/kf6/kf6mainwindow.h` — `m_pendingConflictCount`, `m_conflictBadge`, `onConflictDetected` slot, `refreshConflictBadge()` helper.
- `src/kf6/kf6mainwindow.cpp` — implement slot + helper, construct badge in ctor, wire to PalmRuntime in loadProfile, open dialog on badge click.

New:
- `tests/runtime/tst_kf6mainwindow_conflict_badge.cpp` — tests badge appearance + dialog wiring.
- `tests/runtime/CMakeLists.txt` — register new test.

**Dependency:** Plans A + B + C. Without A, conflict signals are spurious (every record looks conflicted). Without C, the default policy still defers silently (some conflicts may still occur for AskUser mappings, but the path isn't testable end-to-end without C ensuring most syncs auto-resolve). Plan D's tests stub the signal directly, so the dependency is conceptual rather than build-time.

**Cross-repo:** none. libkalburator's `SyncEngine::conflictDetected` signal + `SyncConflictStore` already exist.

---

## Task 1: PalmRuntime — re-emit signal

**Files:**
- Modify: `src/runtime/palmruntime.h`
- Modify: `src/runtime/palmruntime.cpp`

- [ ] **Step 1: Declare the signal in the header**

In `src/runtime/palmruntime.h`, add to the existing `signals:` block (or create one near the public API):

```cpp
signals:
    /// Forwarded from the embedded SyncEngine. Fires every time a
    /// mapping with policy=AskUser encounters a conflict that the
    /// engine cannot auto-resolve. The conflict is also persisted
    /// to SyncConflictStore (engine-side); this signal exists so
    /// the WildPalms UI can update a pending-count display in real
    /// time without polling.
    void conflictDetected(const Kalburator::Sync::ConflictInfo &info);
```

Add `#include <synctypes.h>` near the other libkalburator includes if `ConflictInfo` isn't already visible.

- [ ] **Step 2: Connect in the ctor**

In `src/runtime/palmruntime.cpp`, locate the `PalmRuntime` constructor body. Right after the `m_engine = std::make_unique<...>(...)` line (around line 132-133), add:

```cpp
    QObject::connect(m_engine.get(),
                     &Kalburator::Sync::SyncEngine::conflictDetected,
                     this, &PalmRuntime::conflictDetected);
```

- [ ] **Step 3: Build, confirm clean**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
```

Expected: success. No behavior change yet (signal just forwards; no listener wired).

- [ ] **Step 4: Run existing palm-runtime tests for regressions**

```bash
ctest --test-dir build-dev -R "tst_palm_runtime" --output-on-failure 2>&1 | tail -10
```

Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp
git commit -m "runtime: PalmRuntime re-emits SyncEngine::conflictDetected

Forwards the embedded engine's conflict signal so consumers don't
need to reach into m_engine. KF6MainWindow listens to this in the
next commit. SyncConflictStore persistence is still engine-owned;
this signal is purely a UI-update notification."
```

---

## Task 2: KF6MainWindow — failing badge tests

**Files:**
- Create: `tests/runtime/tst_kf6mainwindow_conflict_badge.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

The test uses two seams: a `runConflictDetectedForTest(ConflictInfo)` method on KF6MainWindow that the production code's slot wraps, and a `pendingConflictCountForTest()` accessor.

- [ ] **Step 1: Write the test file**

```cpp
// tests/runtime/tst_kf6mainwindow_conflict_badge.cpp
#include <QtTest/QtTest>
#include <QPushButton>

#include "../../src/kf6/kf6mainwindow.h"
#include "../wildpalms_qtest_main.h"

#include <synctypes.h>

class TstKf6MainWindowConflictBadge : public QObject
{
    Q_OBJECT
private slots:
    void badgeHiddenWhenZeroConflicts();
    void badgeShowsCountAfterConflictDetected();
    void badgeIncrementsAcrossMultipleSignals();
};

namespace {
Kalburator::Sync::ConflictInfo makeInfo(const QString &id)
{
    Kalburator::Sync::ConflictInfo info;
    info.mappingId = id;
    info.sourceId  = QStringLiteral("test:1");
    info.targetId  = QStringLiteral("test:1");
    return info;
}
} // namespace

void TstKf6MainWindowConflictBadge::badgeHiddenWhenZeroConflicts()
{
    KF6MainWindow win;
    QCOMPARE(win.pendingConflictCountForTest(), 0);
    QVERIFY(!win.conflictBadgeForTest()->isVisible() ||
            win.conflictBadgeForTest()->isHidden());
}

void TstKf6MainWindowConflictBadge::badgeShowsCountAfterConflictDetected()
{
    KF6MainWindow win;
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m1")));
    QCOMPARE(win.pendingConflictCountForTest(), 1);
    QVERIFY(win.conflictBadgeForTest()->text().contains(QStringLiteral("1")));
}

void TstKf6MainWindowConflictBadge::badgeIncrementsAcrossMultipleSignals()
{
    KF6MainWindow win;
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m1")));
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m2")));
    win.runConflictDetectedForTest(makeInfo(QStringLiteral("m3")));
    QCOMPARE(win.pendingConflictCountForTest(), 3);
    QVERIFY(win.conflictBadgeForTest()->text().contains(QStringLiteral("3")));
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowConflictBadge)
#include "tst_kf6mainwindow_conflict_badge.moc"
```

- [ ] **Step 2: Register the test**

In `tests/runtime/CMakeLists.txt`, after the existing `tst_massdeleteguardpresenter` (or another sibling kf6-window test), add:

```cmake
add_executable(tst_kf6mainwindow_conflict_badge
    tst_kf6mainwindow_conflict_badge.cpp)
target_link_libraries(tst_kf6mainwindow_conflict_badge
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_conflict_badge
         COMMAND tst_kf6mainwindow_conflict_badge)
set_tests_properties(tst_kf6mainwindow_conflict_badge PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

Match the link-library list of the existing `tst_kf6mainwindow_forget_profile` block (same heavyweight test pattern; needs the full Palm runtime link chain because constructing KF6MainWindow pulls in PalmRuntime).

- [ ] **Step 3: Build, expect compile failure**

```bash
cmake --build build-dev --target tst_kf6mainwindow_conflict_badge 2>&1 | tail -15
```

Expected: compile error — `runConflictDetectedForTest`, `pendingConflictCountForTest`, `conflictBadgeForTest` not members of `KF6MainWindow`. Red phase.

- [ ] **Step 4: Commit**

```bash
git add tests/runtime/tst_kf6mainwindow_conflict_badge.cpp \
    tests/runtime/CMakeLists.txt
git commit -m "test: failing tests for KF6MainWindow conflict badge (red phase)"
```

---

## Task 3: KF6MainWindow — implement badge + slot

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Add includes + members + slot to the header**

In `src/kf6/kf6mainwindow.h`:

Near the other libkalburator includes:

```cpp
#include <synctypes.h>   // ConflictInfo
```

In the private member section (near `m_profileMenuController` / `m_massDeleteGuard`), add:

```cpp
    int          m_pendingConflictCount = 0;
    QPushButton *m_conflictBadge = nullptr;
```

Add a forward declaration for `QPushButton` at the top of the file if not already present:

```cpp
class QPushButton;
```

In the `public:` section, add the test seams:

```cpp
    // Test seams (F.2 sub-project D).
    int pendingConflictCountForTest() const { return m_pendingConflictCount; }
    QPushButton *conflictBadgeForTest() const { return m_conflictBadge; }
    void runConflictDetectedForTest(const Kalburator::Sync::ConflictInfo &info) {
        onConflictDetected(info);
    }
```

In the `private slots:` section (with `onSwitchProfile` etc.), add:

```cpp
    void onConflictDetected(const Kalburator::Sync::ConflictInfo &info);
    void onConflictBadgeClicked();
```

In the `private:` member-functions section, add:

```cpp
    void refreshConflictBadge();
```

- [ ] **Step 2: Construct the badge in the ctor**

In `src/kf6/kf6mainwindow.cpp`, add `#include <QPushButton>` near the other Qt-widget includes.

In the ctor body — after the existing status bar setup (search for `statusBar()` to find where the status bar is first touched), or near the end of the ctor before `setupGUI`:

```cpp
    m_conflictBadge = new QPushButton(this);
    m_conflictBadge->setFlat(true);
    m_conflictBadge->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_conflictBadge->setVisible(false);
    statusBar()->addPermanentWidget(m_conflictBadge);
    connect(m_conflictBadge, &QPushButton::clicked,
            this, &KF6MainWindow::onConflictBadgeClicked);
```

- [ ] **Step 3: Wire the signal from PalmRuntime in loadProfile**

In `KF6MainWindow::loadProfile` (find via `grep "KF6MainWindow::loadProfile" src/kf6/kf6mainwindow.cpp`), after the `m_palmRuntime = std::make_unique<...>(...)` block:

```cpp
    connect(m_palmRuntime.get(),
            &WildPalms::Runtime::PalmRuntime::conflictDetected,
            this, &KF6MainWindow::onConflictDetected);
```

Also reset the count when loading a fresh profile (seed from SyncConflictStore if it has pre-existing unresolved entries):

```cpp
    // Reset badge for the freshly-loaded profile. If the profile's
    // SyncConflictStore has unresolved entries from a previous
    // session, the engine will not re-emit conflictDetected for them
    // — we read the count once here. (TODO: when SyncConflictStore
    // exposes a count() accessor, use it. For now, start at 0 and
    // the badge will populate as new conflicts come in.)
    m_pendingConflictCount = 0;
    refreshConflictBadge();
```

- [ ] **Step 4: Implement the slots + refresh helper**

Add to `src/kf6/kf6mainwindow.cpp`, in an appropriate section (near other private slots):

```cpp
void KF6MainWindow::onConflictDetected(const Kalburator::Sync::ConflictInfo &info)
{
    Q_UNUSED(info);
    ++m_pendingConflictCount;
    refreshConflictBadge();
}

void KF6MainWindow::onConflictBadgeClicked()
{
    if (!m_palmRuntime) return;
    // Open the existing ConflictReviewDialog non-modally.
    // The dialog reads from SyncConflictStore which the engine
    // populated during sync.
    auto *dlg = new ConflictReviewDialog(
        m_palmRuntime->syncConflictStore(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QDialog::finished, this, [this]() {
        // Refresh count after dialog closes (user may have resolved
        // some). TODO: SyncConflictStore should expose count(); for
        // now we conservatively reset to 0 on dialog close — the
        // next sync will repopulate any still-unresolved.
        m_pendingConflictCount = 0;
        refreshConflictBadge();
    });
    dlg->show();
}

void KF6MainWindow::refreshConflictBadge()
{
    if (m_pendingConflictCount > 0) {
        m_conflictBadge->setText(
            i18n("%1 conflicts pending", m_pendingConflictCount));
        m_conflictBadge->setVisible(true);
    } else {
        m_conflictBadge->setVisible(false);
    }
}
```

Add the include for `ConflictReviewDialog` near other dialog includes:

```cpp
#include "../widgets/dialogs/conflictreviewdialog.h"
```

The `m_palmRuntime->syncConflictStore()` accessor needs to exist. If it doesn't, add a small accessor to PalmRuntime — see Step 5.

- [ ] **Step 5: Add SyncConflictStore accessor to PalmRuntime (if missing)**

```bash
grep -n "syncConflictStore\|SyncConflictStore" /home/clinton/dev/WildPalms/src/runtime/palmruntime.h
```

If no accessor exists, add one:

In `src/runtime/palmruntime.h` (public section):

```cpp
    /// Borrowed pointer to the embedded engine's SyncConflictStore.
    /// Used by the conflict UI to read deferred conflicts. May be
    /// nullptr if the engine wasn't given a store.
    Kalburator::Sync::SyncConflictStore *syncConflictStore() const {
        return m_engine ? m_engine->syncConflictStore() : nullptr;
    }
```

If `m_engine->syncConflictStore()` exists in libkalburator's SyncEngine API (per the engine .h at line 409: `SyncConflictStore *syncConflictStore() const`), this just forwards. No change to libkalburator needed.

Forward-declare `SyncConflictStore` near the top of `palmruntime.h`:

```cpp
namespace Kalburator::Sync { class SyncConflictStore; }
```

- [ ] **Step 6: Build + run the badge test**

```bash
cmake --build build-dev --target tst_kf6mainwindow_conflict_badge 2>&1 | tail -10
ctest --test-dir build-dev -R tst_kf6mainwindow_conflict_badge --output-on-failure
```

Expected: all 3 tests PASS. Paste the per-test PASS output.

- [ ] **Step 7: Run broader regression**

```bash
ctest --test-dir build-dev -R "tst_kf6mainwindow|tst_main_window|tst_palm_runtime" --output-on-failure 2>&1 | tail -15
```

Expected: all pass.

- [ ] **Step 8: Commit**

```bash
git add src/runtime/palmruntime.h src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "kf6: conflict badge + on-demand ConflictReviewDialog

KF6MainWindow listens to PalmRuntime::conflictDetected; maintains
a pending count; renders a status-bar QPushButton (\"N conflicts
pending\") that's hidden when count==0; clicking opens the
existing ConflictReviewDialog non-modally over the profile's
SyncConflictStore. Conflicts that the user resolves persist via
the store and apply on next sync (existing libkalburator behavior).

This wires the F.2 'real IConflictPresenter' deliverable: the
engine no longer silently defers AskUser conflicts — the user sees
them and can choose to resolve at any time. Resolutions don't
require pausing the sync (Unmonitored model preserved)."
```

---

## Task 4: Manual smoke + full ctest

**Files:** none modified

- [ ] **Step 1: Full ctest**

```bash
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 80+ tests pass; new tst_kf6mainwindow_conflict_badge added.

- [ ] **Step 2: Manual smoke (deferred to user)**

For manual verification, the user can:
1. Create a profile via File → Profile → New.
2. Use MappingEditorDialog to set a mapping's `conflictPolicy = AskUser` explicitly.
3. Modify both Palm and PC sides of the same record to create a true conflict.
4. HotSync.
5. Expect the status-bar badge to show "1 conflict pending"; click it; expect ConflictReviewDialog to open.

This manual test is deferred to the user; no automated commit gates on it.

---

## Verification checklist

- [ ] `PalmRuntime::conflictDetected` Q_SIGNAL declared + connected from `m_engine`.
- [ ] `KF6MainWindow` has `m_conflictBadge` and `onConflictDetected` slot.
- [ ] Badge hidden when count==0; visible with "%n conflicts pending" otherwise.
- [ ] Clicking the badge opens `ConflictReviewDialog` non-modally.
- [ ] `tst_kf6mainwindow_conflict_badge` passes (3 cases).
- [ ] Full ctest baseline (80+) holds.
- [ ] Spec §4.4 requirements satisfied.

**Out of scope for this plan:**
- SyncConflictStore exposing a `count()` accessor — the current TODO comment in `refreshConflictBadge` notes this; for now we reset on dialog close and rely on signals to populate. A follow-up libkalburator commit could add a real count.
- `ConflictReviewDialog` internals — already shipped; this plan only wires it up.
- Real-time conflict resolution during sync (Monitored mode) — out of scope; Unmonitored + deferred queue is the design.

**Next sub-project:** Plan E — Mass-delete guard E2E verification (`docs/superpowers/plans/2026-05-22-palm-sync-E-guard-e2e.md`).
