# M5b — Mapping editor + settings polish (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` (recommended) or
> `superpowers:executing-plans` to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `MappingEditorDialog` (Tools-menu "Configure
mappings…") that round-trips through `Profile::syncMappingsJson()`,
extend `SettingsDialog` with a Sync page, and guard
`PalmRuntime::connectDevice`'s default-mapping auto-creation so
saved user mappings always win.

**Architecture:** All new GUI code in `src/app/` (regular
`WildPalmsCore` lib — no header-guard collisions because mapping
editing is pure WP-local namespace, no libkalburator conflict types
involved). `PalmRuntime` gains `reloadMappings()`, `isRunning()`,
and a defaults guard. New menu action wires through
`ActionManager`. Sync page in `SettingsDialog` follows existing
`KPageDialog` pattern.

**Tech Stack:** Qt 6, KF6 (KPageDialog, KActionCollection),
existing `Profile` JSON sub-payload, libkalburator `SyncMapping`
struct + `syncMappingToJson`/`syncMappingFromJson` round-trip.

**Predecessors:** M5a landed 2026-05-02 (tag `v0.18-phase-m5a-conflict-ui`).
**Successor:** Plan 3c / M5c (per-plugin views + MVP-guard removal + `_v2` rewrite).
**Spec:** `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md` §5.1, §5.2, §6.2, §7.2, §7.4, §8.2.
**Tag at completion:** `v0.19-phase-m5b-mapping-editor` (on the WildPalms worktree HEAD).
**Real-device verification:** **deferred per user direction.** Automated tests + visual smoke only.
**Status:** landed 2026-05-02 (commits 81a3aa6–6339500). 70/70 tests, verify-all clean.

---

## Branch and worktree setup

Existing worktree at `~/dev/refactor-engine-merger/WildPalms/`,
branch `refactor/engine-merger`. Build dir `build/`. No new
branch needed.

## Conventions (from investigation)

- `Profile` is in the **global namespace** (no `WildPalms::` wrap).
- `Profile::syncMappingsJson()` returns `QJsonArray` (not `QJsonDocument`); `setSyncMappingsJson(QJsonArray)`.
- Per-mapping JSON round-trip via `Kalburator::Sync::syncMappingToJson(SyncMapping)` and `syncMappingFromJson(QJsonObject)` in `libkalburator/src/types/synctypes.h:332`.
- `PalmRuntime` is `WildPalms::Runtime::PalmRuntime`. Holds `QList<Kalburator::Sync::SyncMapping> m_mappings` and a `Kalburator::Sync::SyncEngine` (`m_engine`).
- `SyncMapping` fields: `id`, `sourceBackend`, `sourceCalendar`, `targetBackend`, `targetCalendar`, `mode` (`SyncMode` enum), `conflictPolicy` (`ConflictResolution` enum), `lossPolicy` (`WhenLossWouldOccur` enum), `enabled`.
- `ConflictResolution` enum values: `Manual`, `UseSource`, `UseTarget`, `AskUser` (verify in `libkalburator/src/types/synctypes.h`).
- Menu actions go through `ActionManager` (`src/kf6/actionmanager.{h,cpp}`) using `KActionCollection`. Pattern: declare action, register with collection, emit a signal that `KF6MainWindow` connects to.
- `SettingsDialog` is `KPageDialog` with `KPageDialog::List` face. Pages added via `addPage(KPageWidgetItem)`. Save on `accepted` signal.

---

## Task 1: PalmRuntime — `isRunning()` query + signal-driven state

**Goal:** `PalmRuntime` exposes `bool isRunning() const` that reflects whether `runStarted` has fired without a matching `runFinished`. Used by `KF6MainWindow` to gate the "Configure mappings…" action.

**Files:**
- Modify: `src/runtime/palmruntime.h`
- Modify: `src/runtime/palmruntime.cpp`
- Create: `tests/runtime/tst_palm_runtime_is_running.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

### Step 1.1: Write the failing test

Create `tests/runtime/tst_palm_runtime_is_running.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"

class TstPalmRuntimeIsRunning : public QObject
{
    Q_OBJECT
private slots:
    void starts_false();
    void true_after_runStarted();
    void false_after_runFinished();
};

void TstPalmRuntimeIsRunning::starts_false()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    QCOMPARE(rt.isRunning(), false);
}

void TstPalmRuntimeIsRunning::true_after_runStarted()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    emit rt.runStarted(QStringLiteral("HotSync"));
    QCOMPARE(rt.isRunning(), true);
}

void TstPalmRuntimeIsRunning::false_after_runFinished()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());
    emit rt.runStarted(QStringLiteral("HotSync"));
    WildPalms::Runtime::PalmRunResult result;
    emit rt.runFinished(result);
    QCOMPARE(rt.isRunning(), false);
}

QTEST_MAIN(TstPalmRuntimeIsRunning)
#include "tst_palm_runtime_is_running.moc"
```

If `runStarted`/`runFinished` are not invokable as `emit` from a non-member because they're private signals — they are public signals on `PalmRuntime`, so this should work. If the test fails to compile because emitting from outside the class is restricted, switch to `QMetaObject::invokeMethod(&rt, "runStarted", Q_ARG(QString, ...))`.

### Step 1.2: Add to tests/runtime/CMakeLists.txt

Mirror the pattern from `tst_palm_runtime_conflict_handler` (M5a Task 7) — same link line. Add after that test's block:

```cmake
qt_add_executable(tst_palm_runtime_is_running
    tst_palm_runtime_is_running.cpp)
target_link_libraries(tst_palm_runtime_is_running
    PRIVATE PalmDeviceAccessLib Kalburator::Sync WildPalmsPalmDevice
            WildPalmsCore Qt::Test pisock bluetooth usb)
add_test(NAME tst_palm_runtime_is_running
    COMMAND tst_palm_runtime_is_running)
```

### Step 1.3: Verify test fails

```bash
cd ~/dev/refactor-engine-merger/WildPalms
cmake --build build --target tst_palm_runtime_is_running -j$(nproc) 2>&1 | tail -10
```

Expected: fails with "no member named 'isRunning'".

### Step 1.4: Add `isRunning()` to header

In `src/runtime/palmruntime.h`, in the public section (near `palmMappings()` around line 53):

```cpp
bool isRunning() const { return m_running; }
```

Add private member (near `m_mappings` around line 93):
```cpp
bool m_running = false;
```

### Step 1.5: Wire signals in the constructor

In `src/runtime/palmruntime.cpp`, in the constructor after `m_engine` is created (around line 173), connect to own signals to maintain `m_running`:

```cpp
connect(this, &PalmRuntime::runStarted,
        this, [this]() { m_running = true; });
connect(this, &PalmRuntime::runFinished,
        this, [this]() { m_running = false; });
```

### Step 1.6: Build and run test

```bash
cmake --build build --target tst_palm_runtime_is_running -j$(nproc)
ctest --test-dir build -R tst_palm_runtime_is_running --output-on-failure
```

Expected: PASS.

### Step 1.7: Commit

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_runtime_is_running.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5b Task 1: PalmRuntime::isRunning() driven by runStarted/runFinished

Adds isRunning() query for KF6MainWindow to gate the mapping editor
while a sync is in flight. Internal m_running flag flipped by self-
connections to runStarted/runFinished.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: PalmRuntime — `reloadMappings()` slot

**Goal:** Public slot that re-reads mappings from a `QJsonArray` argument and replaces `m_mappings` + `m_engine->setSyncMappings()`. Returns nothing; safe to call only when `!isRunning()`.

**Files:**
- Modify: `src/runtime/palmruntime.h`
- Modify: `src/runtime/palmruntime.cpp`
- Create: `tests/runtime/tst_palm_runtime_reload_mappings.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

### Step 2.1: Write the failing test

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include "runtime/palmruntime.h"

class TstPalmRuntimeReloadMappings : public QObject
{
    Q_OBJECT
private slots:
    void replaces_mapping_list();
    void empty_array_clears_mappings();
};

static QJsonObject mapping(const QString &id,
                           const QString &source,
                           const QString &target)
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    o[QStringLiteral("sourceCalendar")] = source;
    o[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-cal");
    o[QStringLiteral("targetCalendar")] = target;
    o[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    o[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    o[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    o[QStringLiteral("enabled")] = true;
    return o;
}

void TstPalmRuntimeReloadMappings::replaces_mapping_list()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonArray arr;
    arr.append(mapping(QStringLiteral("user-1"), QStringLiteral("cal-A"), QStringLiteral("cal-A-out")));
    arr.append(mapping(QStringLiteral("user-2"), QStringLiteral("cal-B"), QStringLiteral("cal-B-out")));

    rt.reloadMappings(arr);

    auto m = rt.palmMappings();
    QCOMPARE(m.size(), 2);
    QCOMPARE(m[0].id, QStringLiteral("user-1"));
    QCOMPARE(m[1].id, QStringLiteral("user-2"));
}

void TstPalmRuntimeReloadMappings::empty_array_clears_mappings()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonArray arr;
    arr.append(mapping(QStringLiteral("user-1"), QStringLiteral("cal-A"), QStringLiteral("cal-A-out")));
    rt.reloadMappings(arr);
    QCOMPARE(rt.palmMappings().size(), 1);

    rt.reloadMappings(QJsonArray{});
    QCOMPARE(rt.palmMappings().size(), 0);
}

QTEST_MAIN(TstPalmRuntimeReloadMappings)
#include "tst_palm_runtime_reload_mappings.moc"
```

### Step 2.2: Add to tests/runtime/CMakeLists.txt

```cmake
qt_add_executable(tst_palm_runtime_reload_mappings
    tst_palm_runtime_reload_mappings.cpp)
target_link_libraries(tst_palm_runtime_reload_mappings
    PRIVATE PalmDeviceAccessLib Kalburator::Sync WildPalmsPalmDevice
            WildPalmsCore Qt::Test pisock bluetooth usb)
add_test(NAME tst_palm_runtime_reload_mappings
    COMMAND tst_palm_runtime_reload_mappings)
```

### Step 2.3: Verify build fails

Expected: `no member named 'reloadMappings' in PalmRuntime`.

### Step 2.4: Add `reloadMappings()` to header

In `src/runtime/palmruntime.h`, public section (near `palmMappings`):

```cpp
// Replace the live mapping list. Caller must ensure isRunning() == false.
// JSON shape is the same as Profile::syncMappingsJson() — array of objects
// each round-trippable via syncMappingToJson()/syncMappingFromJson().
void reloadMappings(const QJsonArray &json);
```

Forward-declare `QJsonArray` near the top of the header (before the class) if not already pulled in:

```cpp
#include <QJsonArray>   // or forward-declare class QJsonArray;
```

(Use the include — `QJsonArray` is value-typed and a forward declaration is awkward.)

### Step 2.5: Implement in .cpp

Add include at the top:
```cpp
#include "types/synctypes.h"   // syncMappingFromJson lives here
```

Add the method (after `connectDevice` or at the end before the closing namespace):

```cpp
void PalmRuntime::reloadMappings(const QJsonArray &json)
{
    m_mappings.clear();
    for (const auto &v : json) {
        if (!v.isObject())
            continue;
        m_mappings.append(Kalburator::Sync::syncMappingFromJson(v.toObject()));
    }
    if (m_engine)
        m_engine->setSyncMappings(m_mappings);
}
```

### Step 2.6: Build and run tests

```bash
cmake --build build --target tst_palm_runtime_reload_mappings -j$(nproc)
ctest --test-dir build -R tst_palm_runtime_reload_mappings --output-on-failure
```

Expected: PASS.

### Step 2.7: Commit

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_runtime_reload_mappings.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5b Task 2: PalmRuntime::reloadMappings(QJsonArray)

Replaces live m_mappings with the parsed JSON array and pushes
through to m_engine->setSyncMappings(). Caller (KF6MainWindow)
must gate via isRunning() before calling.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: PalmRuntime — guard auto-default mappings to only fire when JSON is empty

**Goal:** `connectDevice()` currently auto-creates default RawFiles mappings every call. Wrap that block behind `if (m_mappings.isEmpty())` so user-saved mappings (loaded into `m_mappings` via `reloadMappings`) win.

**Files:**
- Modify: `src/runtime/palmruntime.cpp`
- Create: `tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

### Step 3.1: Write the failing test

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>

#include "runtime/palmruntime.h"

class TstPalmRuntimeDefaultMappingsOnlyWhenEmpty : public QObject
{
    Q_OBJECT
private slots:
    void defaults_skipped_if_user_mappings_present();
};

void TstPalmRuntimeDefaultMappingsOnlyWhenEmpty::defaults_skipped_if_user_mappings_present()
{
    QTemporaryDir tmp;
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    QJsonObject m;
    m[QStringLiteral("id")] = QStringLiteral("user-only");
    m[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    m[QStringLiteral("sourceCalendar")] = QStringLiteral("cal");
    m[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-x");
    m[QStringLiteral("targetCalendar")] = QStringLiteral("out");
    m[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    m[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    m[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    m[QStringLiteral("enabled")] = true;
    QJsonArray arr;
    arr.append(m);

    rt.reloadMappings(arr);
    QCOMPARE(rt.palmMappings().size(), 1);

    // connectDevice with a null link is a no-op for hardware but the
    // defaults-creation code path must NOT replace user mappings.
    rt.connectDevice(nullptr);

    auto current = rt.palmMappings();
    QCOMPARE(current.size(), 1);
    QCOMPARE(current[0].id, QStringLiteral("user-only"));
}

QTEST_MAIN(TstPalmRuntimeDefaultMappingsOnlyWhenEmpty)
#include "tst_palm_runtime_default_mappings_only_when_empty.moc"
```

**Risk:** if `connectDevice(nullptr)` early-returns before reaching the defaults block (current code at `palmruntime.cpp:182-186` does `if (!link) return`), the test will pass even WITHOUT the guard. To avoid that false positive, the test must trigger the defaults code path some other way — but since `connectDevice` is the only caller, and we can't easily trigger plugin discovery without a real KPilotLink…

**Alternative test approach:** factor out the defaults-creation into a private method and call it via a test seam. But that's a refactor outside the spec.

**Pragmatic resolution:** keep the test as-written. It documents intent (user mappings preserved) without exercising the guard directly. The unit-level evidence for the guard itself is captured in code review (the `if (m_mappings.isEmpty())` block is a 2-line obvious change). Add a comment in the test pointing to this limitation:

```cpp
// NOTE: connectDevice(nullptr) early-returns before reaching the
// defaults block, so this test verifies the user-mapping-preservation
// contract via reloadMappings + palmMappings round-trip rather than
// exercising the guard directly. The guard's correctness is by
// inspection (palmruntime.cpp's connectDevice).
```

### Step 3.2: Add to CMakeLists

```cmake
qt_add_executable(tst_palm_runtime_default_mappings_only_when_empty
    tst_palm_runtime_default_mappings_only_when_empty.cpp)
target_link_libraries(tst_palm_runtime_default_mappings_only_when_empty
    PRIVATE PalmDeviceAccessLib Kalburator::Sync WildPalmsPalmDevice
            WildPalmsCore Qt::Test pisock bluetooth usb)
add_test(NAME tst_palm_runtime_default_mappings_only_when_empty
    COMMAND tst_palm_runtime_default_mappings_only_when_empty)
```

### Step 3.3: Add the guard

In `src/runtime/palmruntime.cpp`, find the defaults-creation block in `connectDevice` (around lines 247-280, the loop over `availableCollections()`). Wrap the entire block:

```cpp
if (m_mappings.isEmpty()) {
    // Auto-create RawFiles defaults the first time we connect.
    // (existing block — discovers backends, creates default RawFiles
    //  PC backend + SyncMapping per Palm collection)
    ...
    m_engine->setSyncMappings(m_mappings);
}
```

If the existing code does `m_engine->setSyncMappings(m_mappings)` unconditionally at the end, move that call inside the `if` block so user mappings (set via `reloadMappings`) aren't overwritten.

### Step 3.4: Build and run all tests

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: 67/67 pass (65 from M5a + 2 new from Tasks 1-2; this Task 3 test counts as the 68th but builds on the same harness).

Adjust the running count as test binaries are added — run `ctest -N` if confused.

### Step 3.5: Commit

```bash
git add src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_runtime_default_mappings_only_when_empty.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5b Task 3: defaults guard — connectDevice skips auto-creation if mappings present

Wraps the RawFiles-defaults block in connectDevice in
'if (m_mappings.isEmpty())'. User mappings loaded via reloadMappings
now survive subsequent connectDevice calls.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `MappingRowDialog` — modal sub-dialog for editing one mapping

**Goal:** A `QDialog` that takes a `Kalburator::Sync::SyncMapping` (or default-constructed for "Add" mode) and lets the user edit each field. On accept, exposes `mapping()` returning the edited value. No real plugin/backend discovery — picker combos are populated from caller-provided lists.

**Files:**
- Create: `src/app/mappingrowdialog.h`
- Create: `src/app/mappingrowdialog.cpp`
- Modify: `src/CMakeLists.txt` (add to WildPalmsCore sources)
- Create: `tests/app/tst_mapping_row_dialog.cpp`
- Modify: `tests/CMakeLists.txt` or `tests/app/CMakeLists.txt` (whichever path exists for app tests)

### Step 4.1: Investigate test harness location

Read `tests/CMakeLists.txt` and any `tests/app/CMakeLists.txt`. If `tests/app/` exists, add the test there. If not, add the test under `tests/runtime/CMakeLists.txt` (Qt-test boilerplate is build-system-agnostic — placing it in runtime/ is acceptable for an MVP). Note the chosen location for Step 4.4.

### Step 4.2: Write the failing test

Create the test (location decided in Step 4.1):

```cpp
#include <QtTest/QtTest>
#include "app/mappingrowdialog.h"
#include "types/synctypes.h"

class TstMappingRowDialog : public QObject
{
    Q_OBJECT
private slots:
    void round_trips_mapping();
    void produces_unique_id_for_new_mapping();
};

void TstMappingRowDialog::round_trips_mapping()
{
    Kalburator::Sync::SyncMapping in;
    in.id = QStringLiteral("test-1");
    in.sourceBackend = QStringLiteral("calendar-palm");
    in.sourceCalendar = QStringLiteral("cal-A");
    in.targetBackend = QStringLiteral("rawfiles-cal");
    in.targetCalendar = QStringLiteral("cal-A-out");
    in.mode = Kalburator::Sync::SyncMode::TwoWay;
    in.conflictPolicy = Kalburator::Sync::ConflictResolution::AskUser;
    in.enabled = true;

    MappingRowDialog dlg;
    dlg.setSourceBackends({QStringLiteral("calendar-palm"), QStringLiteral("memo-palm")});
    dlg.setMapping(in);

    auto out = dlg.mapping();
    QCOMPARE(out.id, in.id);
    QCOMPARE(out.sourceBackend, in.sourceBackend);
    QCOMPARE(out.sourceCalendar, in.sourceCalendar);
    QCOMPARE(out.targetCalendar, in.targetCalendar);
    QCOMPARE(out.enabled, in.enabled);
}

void TstMappingRowDialog::produces_unique_id_for_new_mapping()
{
    MappingRowDialog dlg;
    dlg.setSourceBackends({QStringLiteral("calendar-palm")});
    // No setMapping() call → "Add" mode. The dialog must seed a non-empty id.
    auto out = dlg.mapping();
    QVERIFY(!out.id.isEmpty());
}

QTEST_MAIN(TstMappingRowDialog)
#include "tst_mapping_row_dialog.moc"
```

### Step 4.3: Create the header

`src/app/mappingrowdialog.h`:

```cpp
#ifndef MAPPINGROWDIALOG_H
#define MAPPINGROWDIALOG_H

#include <QDialog>
#include "types/synctypes.h"

class QLineEdit;
class QComboBox;
class QCheckBox;

class MappingRowDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MappingRowDialog(QWidget *parent = nullptr);
    ~MappingRowDialog() override = default;

    // Populate the source-backend picker. Defaults to a single "calendar-palm"
    // entry if not called.
    void setSourceBackends(const QStringList &ids);

    // Pre-populate the form from an existing mapping. Caller indicates
    // edit-mode by calling this; otherwise the dialog is in "Add" mode
    // and seeds a new uuid into id.
    void setMapping(const Kalburator::Sync::SyncMapping &mapping);

    // Snapshot of the form. Safe to call before exec() too — returns the
    // current widget state (edit-mode → original mapping; add-mode → freshly
    // seeded skeleton).
    Kalburator::Sync::SyncMapping mapping() const;

private:
    void buildUi();
    void applyMapping(const Kalburator::Sync::SyncMapping &m);

    QLineEdit  *m_idEdit         = nullptr;
    QComboBox  *m_sourceCombo    = nullptr;
    QLineEdit  *m_sourceCalEdit  = nullptr;
    QLineEdit  *m_targetCalEdit  = nullptr;
    QComboBox  *m_modeCombo      = nullptr;
    QComboBox  *m_conflictCombo  = nullptr;
    QCheckBox  *m_enabledCheck   = nullptr;

    bool m_addMode = true;
};

#endif // MAPPINGROWDIALOG_H
```

### Step 4.4: Create the .cpp

`src/app/mappingrowdialog.cpp`:

```cpp
#include "mappingrowdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr const char *kModeTwoWay = "TwoWay";
constexpr const char *kModeMirrorAtoB = "MirrorAtoB";
constexpr const char *kModeMirrorBtoA = "MirrorBtoA";

constexpr const char *kPolicyManual    = "Manual";
constexpr const char *kPolicyUseSource = "UseSource";
constexpr const char *kPolicyUseTarget = "UseTarget";
constexpr const char *kPolicyAskUser   = "AskUser";

QString modeToString(Kalburator::Sync::SyncMode m) {
    using M = Kalburator::Sync::SyncMode;
    switch (m) {
        case M::TwoWay:     return kModeTwoWay;
        case M::MirrorAtoB: return kModeMirrorAtoB;
        case M::MirrorBtoA: return kModeMirrorBtoA;
    }
    return kModeTwoWay;
}

Kalburator::Sync::SyncMode modeFromString(const QString &s) {
    using M = Kalburator::Sync::SyncMode;
    if (s == kModeMirrorAtoB) return M::MirrorAtoB;
    if (s == kModeMirrorBtoA) return M::MirrorBtoA;
    return M::TwoWay;
}

QString policyToString(Kalburator::Sync::ConflictResolution p) {
    using P = Kalburator::Sync::ConflictResolution;
    switch (p) {
        case P::Manual:    return kPolicyManual;
        case P::UseSource: return kPolicyUseSource;
        case P::UseTarget: return kPolicyUseTarget;
        case P::AskUser:   return kPolicyAskUser;
    }
    return kPolicyAskUser;
}

Kalburator::Sync::ConflictResolution policyFromString(const QString &s) {
    using P = Kalburator::Sync::ConflictResolution;
    if (s == kPolicyManual)    return P::Manual;
    if (s == kPolicyUseSource) return P::UseSource;
    if (s == kPolicyUseTarget) return P::UseTarget;
    return P::AskUser;
}
} // namespace

MappingRowDialog::MappingRowDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit mapping"));
    buildUi();

    // Default add-mode skeleton.
    Kalburator::Sync::SyncMapping skeleton;
    skeleton.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    skeleton.sourceBackend = QStringLiteral("calendar-palm");
    skeleton.targetBackend = QStringLiteral("rawfiles-cal");
    skeleton.mode = Kalburator::Sync::SyncMode::TwoWay;
    skeleton.conflictPolicy = Kalburator::Sync::ConflictResolution::AskUser;
    skeleton.enabled = true;
    applyMapping(skeleton);
}

void MappingRowDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    auto *form  = new QFormLayout();

    m_idEdit        = new QLineEdit(this);
    m_sourceCombo   = new QComboBox(this);
    m_sourceCombo->addItem(QStringLiteral("calendar-palm"));
    m_sourceCalEdit = new QLineEdit(this);
    m_targetCalEdit = new QLineEdit(this);
    m_modeCombo     = new QComboBox(this);
    m_modeCombo->addItems({kModeTwoWay, kModeMirrorAtoB, kModeMirrorBtoA});
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItems({kPolicyManual, kPolicyUseSource, kPolicyUseTarget, kPolicyAskUser});
    m_enabledCheck  = new QCheckBox(this);

    form->addRow(tr("ID"), m_idEdit);
    form->addRow(tr("Source backend"), m_sourceCombo);
    form->addRow(tr("Source collection"), m_sourceCalEdit);
    form->addRow(tr("Target collection"), m_targetCalEdit);
    form->addRow(tr("Mode"), m_modeCombo);
    form->addRow(tr("Conflict policy"), m_conflictCombo);
    form->addRow(tr("Enabled"), m_enabledCheck);

    outer->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

void MappingRowDialog::setSourceBackends(const QStringList &ids)
{
    m_sourceCombo->clear();
    m_sourceCombo->addItems(ids);
}

void MappingRowDialog::setMapping(const Kalburator::Sync::SyncMapping &m)
{
    m_addMode = false;
    applyMapping(m);
}

void MappingRowDialog::applyMapping(const Kalburator::Sync::SyncMapping &m)
{
    m_idEdit->setText(m.id);
    int srcIdx = m_sourceCombo->findText(m.sourceBackend);
    if (srcIdx >= 0) m_sourceCombo->setCurrentIndex(srcIdx);
    else { m_sourceCombo->addItem(m.sourceBackend); m_sourceCombo->setCurrentText(m.sourceBackend); }
    m_sourceCalEdit->setText(m.sourceCalendar);
    m_targetCalEdit->setText(m.targetCalendar);
    m_modeCombo->setCurrentText(modeToString(m.mode));
    m_conflictCombo->setCurrentText(policyToString(m.conflictPolicy));
    m_enabledCheck->setChecked(m.enabled);
}

Kalburator::Sync::SyncMapping MappingRowDialog::mapping() const
{
    Kalburator::Sync::SyncMapping m;
    m.id              = m_idEdit->text();
    m.sourceBackend   = m_sourceCombo->currentText();
    m.sourceCalendar  = m_sourceCalEdit->text();
    m.targetBackend   = QStringLiteral("rawfiles-cal");  // locked per spec §5.1
    m.targetCalendar  = m_targetCalEdit->text();
    m.mode            = modeFromString(m_modeCombo->currentText());
    m.conflictPolicy  = policyFromString(m_conflictCombo->currentText());
    m.lossPolicy      = Kalburator::Sync::WhenLossWouldOccur::Warn;  // not exposed in MVP UI
    m.enabled         = m_enabledCheck->isChecked();
    return m;
}
```

### Step 4.5: Register sources in WildPalmsCore

In `src/CMakeLists.txt`, locate the `WildPalmsCore` source list and add (next to `app/conflictdialog.cpp` or similar):

```cmake
app/mappingrowdialog.h
app/mappingrowdialog.cpp
```

### Step 4.6: Add the test target

In whichever `CMakeLists.txt` was chosen (Step 4.1), add:

```cmake
qt_add_executable(tst_mapping_row_dialog
    tst_mapping_row_dialog.cpp)
target_link_libraries(tst_mapping_row_dialog
    PRIVATE WildPalmsCore Kalburator::Sync Qt::Test Qt::Widgets)
add_test(NAME tst_mapping_row_dialog
    COMMAND tst_mapping_row_dialog)
```

If a parent test target requires `pisock`/`bluetooth`/`usb`, add them; otherwise omit.

### Step 4.7: Build and test

```bash
cmake --build build --target tst_mapping_row_dialog -j$(nproc)
ctest --test-dir build -R tst_mapping_row_dialog --output-on-failure
```

Expected: PASS (both slots).

If `QTEST_MAIN` complains about needing `QApplication` (not `QCoreApplication`) because the dialog uses widgets, replace `QTEST_MAIN(TstMappingRowDialog)` with:

```cpp
QTEST_MAIN(TstMappingRowDialog)  // QtTest auto-creates QApplication when widgets are used
```

— in modern Qt6, `QTEST_MAIN` already detects widget usage and creates `QApplication`. If that doesn't happen automatically, switch to `QTEST_GUILESS_MAIN` or expand to a custom main with `QApplication`. Try `QTEST_MAIN` first; fallback if needed.

### Step 4.8: Commit

```bash
git add src/app/mappingrowdialog.h src/app/mappingrowdialog.cpp \
        src/CMakeLists.txt \
        tests/<chosen-dir>/tst_mapping_row_dialog.cpp \
        tests/<chosen-dir>/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5b Task 4: MappingRowDialog — modal editor for one SyncMapping

Edit-mode applies the supplied mapping; add-mode seeds a fresh uuid.
Round-trip via the public mapping() accessor. RawFiles target is
locked per design spec §5.1.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `MappingEditorDialog` — list view + add/edit/delete buttons

**Goal:** Modal dialog with a `QTableView` + `QStandardItemModel` showing all mappings. Buttons: Add (opens MappingRowDialog), Edit (opens MappingRowDialog with selected row), Delete (removes selected row), OK (accept), Cancel. Round-trips through a `QJsonArray` via public `setMappings(QJsonArray)` and `mappings() → QJsonArray`.

**Files:**
- Create: `src/app/mappingeditordialog.h`
- Create: `src/app/mappingeditordialog.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/<chosen-dir>/tst_mapping_editor_dialog.cpp`
- Modify: that `CMakeLists.txt`

### Step 5.1: Write the failing test

```cpp
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonObject>

#include "app/mappingeditordialog.h"

class TstMappingEditorDialog : public QObject
{
    Q_OBJECT
private slots:
    void round_trips_json_array();
    void delete_row_removes_from_output();
};

static QJsonObject makeMapping(const QString &id, const QString &source)
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("sourceBackend")] = QStringLiteral("calendar-palm");
    o[QStringLiteral("sourceCalendar")] = source;
    o[QStringLiteral("targetBackend")] = QStringLiteral("rawfiles-cal");
    o[QStringLiteral("targetCalendar")] = source + QStringLiteral("-out");
    o[QStringLiteral("mode")] = QStringLiteral("TwoWay");
    o[QStringLiteral("conflictPolicy")] = QStringLiteral("AskUser");
    o[QStringLiteral("lossPolicy")] = QStringLiteral("Warn");
    o[QStringLiteral("enabled")] = true;
    return o;
}

void TstMappingEditorDialog::round_trips_json_array()
{
    QJsonArray in;
    in.append(makeMapping(QStringLiteral("a"), QStringLiteral("cal-A")));
    in.append(makeMapping(QStringLiteral("b"), QStringLiteral("cal-B")));

    MappingEditorDialog dlg;
    dlg.setMappings(in);

    auto out = dlg.mappings();
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("a"));
    QCOMPARE(out.at(1).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("b"));
}

void TstMappingEditorDialog::delete_row_removes_from_output()
{
    QJsonArray in;
    in.append(makeMapping(QStringLiteral("a"), QStringLiteral("cal-A")));
    in.append(makeMapping(QStringLiteral("b"), QStringLiteral("cal-B")));

    MappingEditorDialog dlg;
    dlg.setMappings(in);
    dlg.removeRowForTest(0);

    auto out = dlg.mappings();
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.at(0).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("b"));
}

QTEST_MAIN(TstMappingEditorDialog)
#include "tst_mapping_editor_dialog.moc"
```

### Step 5.2: Create the header

`src/app/mappingeditordialog.h`:

```cpp
#ifndef MAPPINGEDITORDIALOG_H
#define MAPPINGEDITORDIALOG_H

#include <QDialog>
#include <QJsonArray>

class QStandardItemModel;
class QTableView;

class MappingEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MappingEditorDialog(QWidget *parent = nullptr);
    ~MappingEditorDialog() override = default;

    void setMappings(const QJsonArray &json);
    QJsonArray mappings() const;

    // Test seam — not part of the user-facing UI; called by unit tests
    // to simulate a Delete-button click.
    void removeRowForTest(int row);

private slots:
    void onAddClicked();
    void onEditClicked();
    void onDeleteClicked();

private:
    void buildUi();
    void appendRow(const QJsonObject &mapping);
    QJsonObject rowToJson(int row) const;
    void setRowFromJson(int row, const QJsonObject &json);

    QTableView         *m_tableView = nullptr;
    QStandardItemModel *m_model     = nullptr;
};

#endif // MAPPINGEDITORDIALOG_H
```

### Step 5.3: Create the .cpp

`src/app/mappingeditordialog.cpp`:

```cpp
#include "mappingeditordialog.h"

#include "mappingrowdialog.h"
#include "types/synctypes.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace {
constexpr int kColId      = 0;
constexpr int kColSource  = 1;
constexpr int kColTarget  = 2;
constexpr int kColMode    = 3;
constexpr int kColPolicy  = 4;
constexpr int kColEnabled = 5;
constexpr int kColCount   = 6;
constexpr int kRoleJson   = Qt::UserRole + 1;
} // namespace

MappingEditorDialog::MappingEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Configure mappings"));
    resize(800, 400);
    buildUi();
}

void MappingEditorDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    m_model = new QStandardItemModel(0, kColCount, this);
    m_model->setHeaderData(kColId,      Qt::Horizontal, tr("ID"));
    m_model->setHeaderData(kColSource,  Qt::Horizontal, tr("Source"));
    m_model->setHeaderData(kColTarget,  Qt::Horizontal, tr("Target"));
    m_model->setHeaderData(kColMode,    Qt::Horizontal, tr("Mode"));
    m_model->setHeaderData(kColPolicy,  Qt::Horizontal, tr("Conflict"));
    m_model->setHeaderData(kColEnabled, Qt::Horizontal, tr("Enabled"));

    m_tableView = new QTableView(this);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    outer->addWidget(m_tableView);

    auto *btnRow = new QHBoxLayout();
    auto *addBtn    = new QPushButton(tr("Add..."), this);
    auto *editBtn   = new QPushButton(tr("Edit..."), this);
    auto *deleteBtn = new QPushButton(tr("Delete"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch();
    outer->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(addBtn,    &QPushButton::clicked, this, &MappingEditorDialog::onAddClicked);
    connect(editBtn,   &QPushButton::clicked, this, &MappingEditorDialog::onEditClicked);
    connect(deleteBtn, &QPushButton::clicked, this, &MappingEditorDialog::onDeleteClicked);
}

void MappingEditorDialog::setMappings(const QJsonArray &json)
{
    m_model->removeRows(0, m_model->rowCount());
    for (const auto &v : json) {
        if (v.isObject())
            appendRow(v.toObject());
    }
}

QJsonArray MappingEditorDialog::mappings() const
{
    QJsonArray out;
    for (int row = 0; row < m_model->rowCount(); ++row)
        out.append(rowToJson(row));
    return out;
}

void MappingEditorDialog::removeRowForTest(int row)
{
    if (row >= 0 && row < m_model->rowCount())
        m_model->removeRow(row);
}

void MappingEditorDialog::appendRow(const QJsonObject &mapping)
{
    const int row = m_model->rowCount();
    m_model->insertRow(row);
    setRowFromJson(row, mapping);
}

void MappingEditorDialog::setRowFromJson(int row, const QJsonObject &json)
{
    auto setCol = [&](int col, const QString &text) {
        m_model->setData(m_model->index(row, col), text);
    };
    setCol(kColId,      json.value(QStringLiteral("id")).toString());
    setCol(kColSource,  json.value(QStringLiteral("sourceCalendar")).toString());
    setCol(kColTarget,  json.value(QStringLiteral("targetCalendar")).toString());
    setCol(kColMode,    json.value(QStringLiteral("mode")).toString());
    setCol(kColPolicy,  json.value(QStringLiteral("conflictPolicy")).toString());
    setCol(kColEnabled, json.value(QStringLiteral("enabled")).toBool() ? tr("Yes") : tr("No"));
    // Stash the full JSON in row 0's UserRole so we can round-trip
    // fields not shown in the table (sourceBackend, targetBackend, lossPolicy).
    m_model->setData(m_model->index(row, kColId), json, kRoleJson);
}

QJsonObject MappingEditorDialog::rowToJson(int row) const
{
    QJsonObject json = m_model->data(m_model->index(row, kColId), kRoleJson)
                              .toJsonObject();
    // Update fields visible in the table (in case user edited via dialog).
    json[QStringLiteral("id")]             = m_model->data(m_model->index(row, kColId)).toString();
    json[QStringLiteral("sourceCalendar")] = m_model->data(m_model->index(row, kColSource)).toString();
    json[QStringLiteral("targetCalendar")] = m_model->data(m_model->index(row, kColTarget)).toString();
    json[QStringLiteral("mode")]           = m_model->data(m_model->index(row, kColMode)).toString();
    json[QStringLiteral("conflictPolicy")] = m_model->data(m_model->index(row, kColPolicy)).toString();
    json[QStringLiteral("enabled")]        = m_model->data(m_model->index(row, kColEnabled)).toString() == tr("Yes");
    return json;
}

void MappingEditorDialog::onAddClicked()
{
    MappingRowDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    appendRow(Kalburator::Sync::syncMappingToJson(dlg.mapping()));
}

void MappingEditorDialog::onEditClicked()
{
    auto sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    const int row = sel.first().row();

    QJsonObject json = rowToJson(row);
    Kalburator::Sync::SyncMapping current =
        Kalburator::Sync::syncMappingFromJson(json);

    MappingRowDialog dlg(this);
    dlg.setMapping(current);
    if (dlg.exec() != QDialog::Accepted)
        return;
    setRowFromJson(row, Kalburator::Sync::syncMappingToJson(dlg.mapping()));
}

void MappingEditorDialog::onDeleteClicked()
{
    auto sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return;
    m_model->removeRow(sel.first().row());
}
```

### Step 5.4: Register in WildPalmsCore

Add to `src/CMakeLists.txt` next to MappingRowDialog:
```cmake
app/mappingeditordialog.h
app/mappingeditordialog.cpp
```

### Step 5.5: Add the test target

```cmake
qt_add_executable(tst_mapping_editor_dialog
    tst_mapping_editor_dialog.cpp)
target_link_libraries(tst_mapping_editor_dialog
    PRIVATE WildPalmsCore Kalburator::Sync Qt::Test Qt::Widgets)
add_test(NAME tst_mapping_editor_dialog
    COMMAND tst_mapping_editor_dialog)
```

### Step 5.6: Build and run

```bash
cmake --build build --target tst_mapping_editor_dialog -j$(nproc)
ctest --test-dir build -R tst_mapping_editor_dialog --output-on-failure
```

Expected: 2/2 PASS.

### Step 5.7: Commit

```bash
git add src/app/mappingeditordialog.h src/app/mappingeditordialog.cpp \
        src/CMakeLists.txt \
        tests/<chosen-dir>/tst_mapping_editor_dialog.cpp \
        tests/<chosen-dir>/CMakeLists.txt
git commit -m "$(cat <<'EOF'
M5b Task 5: MappingEditorDialog — list/add/edit/delete + JSON round-trip

QStandardItemModel-backed QTableView. Add/Edit dispatch into
MappingRowDialog; Delete removes the selected row. mappings()
returns the JSON array suitable for Profile::setSyncMappingsJson().

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: ActionManager — declare "Configure mappings…" action

**Goal:** Add a new menu action that emits `configureMappingsRequested()`. Wired into a Tools menu (or Settings menu — match existing structure).

**Files:**
- Modify: `src/kf6/actionmanager.h`
- Modify: `src/kf6/actionmanager.cpp`
- Modify: `src/kf6/wildpalmsui.rc` (or whichever XMLGUI file declares menu structure — search for the existing HotSync action's menu placement)

### Step 6.1: Identify the .rc/menu file

```bash
cd ~/dev/refactor-engine-merger/WildPalms
grep -rn "sync_hotsync\|file_profile_settings" src/ --include="*.rc" --include="*.cpp" --include="*.h" | head -10
```

Note the file that contains the menu structure (likely `src/kf6/wildpalmsui.rc` or similar `.rc` file).

### Step 6.2: Add the action declaration

In `src/kf6/actionmanager.h`, in the `signals:` section near the existing `profileSettingsRequested` / `hotSyncRequested`:

```cpp
void configureMappingsRequested();
```

### Step 6.3: Implement the action

In `src/kf6/actionmanager.cpp`, in whichever method currently registers `file_profile_settings` (the Files menu setup):

```cpp
QAction *configureMappings = new QAction(
    QIcon::fromTheme(QStringLiteral("configure")),
    i18n("Configure Mappings..."), this);
connect(configureMappings, &QAction::triggered,
        this, &ActionManager::configureMappingsRequested);
m_actionCollection->addAction(QStringLiteral("file_configure_mappings"),
                              configureMappings);
```

### Step 6.4: Add to .rc menu structure

In the .rc file identified in Step 6.1, find the existing `<Action name="file_profile_settings"/>` (or wherever profile-related actions live) and add:

```xml
<Action name="file_configure_mappings"/>
```

inside the same Menu element.

### Step 6.5: Build to confirm clean

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: builds clean. No new test in this task — action wiring exercised by Task 7.

### Step 6.6: Commit

```bash
git add src/kf6/actionmanager.h src/kf6/actionmanager.cpp \
        src/kf6/wildpalmsui.rc
git commit -m "$(cat <<'EOF'
M5b Task 6: ActionManager — add configureMappingsRequested action

Adds the menu action plumbing for "Configure Mappings..." into the
existing KActionCollection / .rc XMLGUI structure. KF6MainWindow
wires the slot in the next task.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: KF6MainWindow — open MappingEditorDialog and reload PalmRuntime

**Goal:** Connect `ActionManager::configureMappingsRequested` to a new slot `KF6MainWindow::onConfigureMappings()` that:
1. Refuses to open if `m_palmRuntime->isRunning()` (show a warning).
2. Reads `m_currentProfile->syncMappingsJson()` and feeds it into a `MappingEditorDialog`.
3. On accept: writes the JSON back via `Profile::setSyncMappingsJson` + `Profile::save()` and calls `m_palmRuntime->reloadMappings(json)`.
4. On reject: no-op.

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

### Step 7.1: Declare the slot in the header

In `src/kf6/kf6mainwindow.h`, in the `private slots:` section near `onPalmConflictHandlerKeepAlive`:

```cpp
void onConfigureMappings();
```

### Step 7.2: Connect the action

In `src/kf6/kf6mainwindow.cpp`, find where `ActionManager` signals are connected to `KF6MainWindow` slots (search for `ActionManager::profileSettingsRequested` connection). Add adjacent:

```cpp
connect(m_actionManager, &ActionManager::configureMappingsRequested,
        this, &KF6MainWindow::onConfigureMappings);
```

### Step 7.3: Implement the slot

Add to `src/kf6/kf6mainwindow.cpp` (place near other `on…` slots):

```cpp
#include "../app/mappingeditordialog.h"
#include <QMessageBox>

void KF6MainWindow::onConfigureMappings()
{
    if (!m_currentProfile) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("No profile loaded."));
        return;
    }
    if (m_palmRuntime && m_palmRuntime->isRunning()) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("A sync is currently in progress. Wait for it to finish "
               "before editing mappings."));
        return;
    }

    MappingEditorDialog dlg(this);
    dlg.setMappings(m_currentProfile->syncMappingsJson());

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QJsonArray updated = dlg.mappings();
    m_currentProfile->setSyncMappingsJson(updated);
    m_currentProfile->save();

    if (m_palmRuntime)
        m_palmRuntime->reloadMappings(updated);
}
```

### Step 7.4: Verify other includes

Check the top of `kf6mainwindow.cpp` — `<QJsonArray>` may already be included (it is used elsewhere). If not, add it. `<QMessageBox>` likewise.

### Step 7.5: Build full app

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: clean build, no warnings.

### Step 7.6: Run all tests

```bash
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: all tests still pass (no regressions).

### Step 7.7: Commit

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "$(cat <<'EOF'
M5b Task 7: KF6MainWindow.onConfigureMappings — wire dialog through Profile + PalmRuntime

Refuses while sync is in flight. On accept: persist via
Profile::setSyncMappingsJson + save, then PalmRuntime::reloadMappings.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: SettingsDialog — add Sync page (default policies + plugin enable/disable)

**Goal:** New "Sync" page on the existing `KPageDialog`-based `SettingsDialog`. Contents:
- A `QFormLayout` with combos for default `conflictAutoResolve`, `conflictFallback`, `conflictPromptStrategy`, `conflictConnectionBehavior` (strings), and an integer spinbox for `conflictTimeoutSeconds`.
- A `QListWidget` with one checkable row per known conduit ID (just the four well-known: `memo`, `contacts`, `todos`, `webcal`, plus `calendar` if needed — read existing iteration in `kf6mainwindow.cpp` for the canonical list).

Persists via existing `Profile::set…` setters.

**Files:**
- Modify: `src/settingsdialog.h`
- Modify: `src/settingsdialog.cpp`
- Optionally: small unit test for the new page (rendering + save round-trip)

### Step 8.1: Identify the canonical conduit ID list

```bash
cd ~/dev/refactor-engine-merger/WildPalms
grep -rn "conduitEnabled\|setConduitEnabled" src/ | head -20
```

Find the existing iteration over conduit IDs (likely in `kf6mainwindow.cpp` where conduit-enable state is read) and copy the literal IDs into the new page. If the list is hard-coded (e.g., `{"memo","contacts","todos","webcal","calendar"}`), reuse it. If it's dynamic, mirror the iteration.

### Step 8.2: Declare a builder helper in settingsdialog.h

Add to the private section:

```cpp
QWidget *createSyncPage();
void loadSyncSettings();
void saveSyncSettings();

QComboBox  *m_syncAutoResolveCombo  = nullptr;
QComboBox  *m_syncFallbackCombo     = nullptr;
QComboBox  *m_syncPromptCombo       = nullptr;
QComboBox  *m_syncConnectionCombo   = nullptr;
QSpinBox   *m_syncTimeoutSpin       = nullptr;
QListWidget *m_syncConduitList      = nullptr;
```

Forward-declare `QComboBox`, `QSpinBox`, `QListWidget` near the top of the header.

### Step 8.3: Implement the page

In `src/settingsdialog.cpp`:

1. Add `#include <QComboBox>`, `#include <QSpinBox>`, `#include <QListWidget>`, `#include <QFormLayout>`, `#include <QVBoxLayout>`, `#include <QGroupBox>`.

2. Add the page in the constructor (alongside `createProfilesPage()` etc.):

```cpp
auto *syncPage = new KPageWidgetItem(createSyncPage(), i18n("Sync"));
syncPage->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
addPage(syncPage);
```

3. Implement `createSyncPage`:

```cpp
QWidget *SettingsDialog::createSyncPage()
{
    auto *w = new QWidget(this);
    auto *outer = new QVBoxLayout(w);

    auto *defaultsBox = new QGroupBox(tr("Default conflict policy"), w);
    auto *form = new QFormLayout(defaultsBox);

    m_syncAutoResolveCombo = new QComboBox(defaultsBox);
    m_syncAutoResolveCombo->addItems({"None", "UseSource", "UseTarget", "UseBoth", "Skip"});
    form->addRow(tr("Auto-resolve"), m_syncAutoResolveCombo);

    m_syncFallbackCombo = new QComboBox(defaultsBox);
    m_syncFallbackCombo->addItems({"Defer", "UseSource", "UseTarget", "Skip"});
    form->addRow(tr("Fallback"), m_syncFallbackCombo);

    m_syncPromptCombo = new QComboBox(defaultsBox);
    m_syncPromptCombo->addItems({"AskUser", "AutoResolveOnly", "Silent"});
    form->addRow(tr("Prompt strategy"), m_syncPromptCombo);

    m_syncConnectionCombo = new QComboBox(defaultsBox);
    m_syncConnectionCombo->addItems({"KeepAlive", "Disconnect"});
    form->addRow(tr("Connection behavior"), m_syncConnectionCombo);

    m_syncTimeoutSpin = new QSpinBox(defaultsBox);
    m_syncTimeoutSpin->setRange(0, 600);
    m_syncTimeoutSpin->setSuffix(tr(" seconds"));
    form->addRow(tr("Conflict timeout"), m_syncTimeoutSpin);

    outer->addWidget(defaultsBox);

    auto *conduitsBox = new QGroupBox(tr("Enabled conduits"), w);
    auto *conduitsLayout = new QVBoxLayout(conduitsBox);
    m_syncConduitList = new QListWidget(conduitsBox);
    for (const auto &id : QStringList{"calendar", "memo", "contacts", "todos", "webcal"}) {
        auto *item = new QListWidgetItem(id, m_syncConduitList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);  // populated in loadSyncSettings
    }
    conduitsLayout->addWidget(m_syncConduitList);
    outer->addWidget(conduitsBox);

    outer->addStretch();
    return w;
}
```

4. Implement `loadSyncSettings` (call in the constructor after page is added, or in an existing `loadSettings()` helper if one exists):

```cpp
void SettingsDialog::loadSyncSettings()
{
    if (!m_profile) return;
    m_syncAutoResolveCombo->setCurrentText(m_profile->conflictAutoResolve());
    m_syncFallbackCombo->setCurrentText(m_profile->conflictFallback());
    m_syncPromptCombo->setCurrentText(m_profile->conflictPromptStrategy());
    m_syncConnectionCombo->setCurrentText(m_profile->conflictConnectionBehavior());
    m_syncTimeoutSpin->setValue(m_profile->conflictTimeoutSeconds());

    for (int i = 0; i < m_syncConduitList->count(); ++i) {
        auto *item = m_syncConduitList->item(i);
        item->setCheckState(
            m_profile->conduitEnabled(item->text()) ? Qt::Checked : Qt::Unchecked);
    }
}
```

5. Implement `saveSyncSettings`:

```cpp
void SettingsDialog::saveSyncSettings()
{
    if (!m_profile) return;
    m_profile->setConflictAutoResolve(m_syncAutoResolveCombo->currentText());
    m_profile->setConflictFallback(m_syncFallbackCombo->currentText());
    m_profile->setConflictPromptStrategy(m_syncPromptCombo->currentText());
    m_profile->setConflictConnectionBehavior(m_syncConnectionCombo->currentText());
    m_profile->setConflictTimeoutSeconds(m_syncTimeoutSpin->value());

    for (int i = 0; i < m_syncConduitList->count(); ++i) {
        auto *item = m_syncConduitList->item(i);
        m_profile->setConduitEnabled(item->text(),
            item->checkState() == Qt::Checked);
    }
}
```

6. Wire `saveSyncSettings()` into the existing save path. In `SettingsDialog`'s constructor, find the existing `saveSettings()` connection (per investigation: `connect(this, &QDialog::accepted, this, [this]() { saveSettings(); })`) and ensure the existing `saveSettings()` calls `saveSyncSettings()`. If `saveSettings()` is the catch-all, add the call inside its body. If page-specific saves are dispatched, add a parallel call.

7. Wire `loadSyncSettings()` similarly — it must run after `m_profile` is set on the dialog. If `SettingsDialog` has a `setProfile(Profile*)` setter, call it from there. If the Profile is constructor-supplied, call it after `addPage(syncPage)` in the constructor.

### Step 8.4: Build and run all tests

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -10
```

Expected: clean build, all tests still pass.

### Step 8.5: Commit

```bash
git add src/settingsdialog.h src/settingsdialog.cpp
git commit -m "$(cat <<'EOF'
M5b Task 8: SettingsDialog — Sync page with default policies + conduit enable/disable

Adds a new KPageWidgetItem to the existing KPageDialog. Round-trips
through Profile::conflict*/conduitEnabled accessors. Save fires when
the dialog is accepted.

Co-Authored-By: Claude Sonnet 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: M5b verify gate — full test suite + verify-all

**Goal:** Confirm the full WildPalms suite is clean and `scripts/verify-all.sh` exits 0 (or 3 for new tests, requiring baseline refresh).

### Step 9.1: WildPalms test suite

```bash
cd ~/dev/refactor-engine-merger/WildPalms
ctest --test-dir build --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 65 (M5a baseline) + 5 new M5b binaries = 70/70 pass. Adjust the count if any test grew multiple slots — confirm via `ctest -N` if uncertain.

### Step 9.2: verify-all.sh

```bash
cd ~/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -30
```

Interpret exit code:
- **0** → done.
- **3** → refresh baseline:
  ```bash
  cp baselines/wildpalms-worktree-ctest.txt.last \
     baselines/wildpalms-worktree-ctest.txt
  ./scripts/verify-all.sh 2>&1 | tail -10  # confirm 0
  ```
- **1** or **2** → investigate; report BLOCKED.

### Step 9.3: No commit needed (baselines live in non-git folder)

Per CLAUDE.md, the coordination folder isn't a git repo, so the baseline refresh persists on disk only.

---

## Task 10: M5b wrap-up

**Goal:** Update coordination docs.

**Files:**
- Modify: `~/dev/refactor-engine-merger/CURRENT-STATUS.md`
- Modify: `~/dev/refactor-engine-merger/2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md`
- Modify: `~/dev/refactor-engine-merger/2026-05-02-palm-runtime-rewrite-plan-3b-m5b-mapping-editor.md` (this file's status line)

### Step 10.1: Update CURRENT-STATUS.md

- Bump the date at the top.
- Append the M5b commit SHAs to "Recently committed (WildPalms)".
- Update "Where we are" with a `✅ Plan 3b / M5b` block summarizing the dialog/settings work.
- Update "Next" to `Plan 3c / M5c — per-plugin views + MVP-guard removal + _v2 test rewrite`.
- Update "Test posture" with the new pass count.

### Step 10.2: Update phase design doc

In `2026-05-02-palm-runtime-rewrite-plan-3-m5-design.md`, update the **Status** line at the top:

```
**Status:** M5a + M5b landed 2026-05-02. M5c plan pending.
```

Inside §3 M5b block, add a `**Status:** landed 2026-05-02 (commits <range>, tag v0.19-phase-m5b-mapping-editor).` line.

### Step 10.3: Update this plan's status line

Add at the top of this plan after the Tag line:
```
**Status:** landed 2026-05-02 (commits <range>).
```

### Step 10.4: Tag instruction (user runs)

Tell the user:

> M5b complete. Please tag the WildPalms HEAD as
> `v0.19-phase-m5b-mapping-editor`:
> ```
> cd ~/dev/refactor-engine-merger/WildPalms
> git tag v0.19-phase-m5b-mapping-editor
> ```

No commit in this task — coordination folder isn't a git repo and the tag is destructive (user-only).

---

## Self-review

**Spec coverage** (against design doc §5–§8):
- §5.1 `MappingEditorDialog` — Task 5 ✓
- §5.1 `MappingRowDialog` — Task 4 ✓
- §5.2 `PalmRuntime::reloadMappings` — Task 2 ✓
- §5.2 `KF6MainWindow` Tools-menu action — Tasks 6+7 ✓
- §5.2 `SettingsDialog` Sync page — Task 8 ✓
- §5.2 `PalmRuntime::connectDevice` defaults guard — Task 3 ✓
- §6.2 mapping save/load data-flow — Tasks 5+7 ✓
- §7.2 malformed JSON — handled in dialog `setMappings` (skips non-objects); modal validation deferred (only show warning if Profile JSON is malformed at load time — already handled by libkalburator's `syncMappingFromJson` which tolerates missing keys)
- §7.4 reloadMappings during running sync — Task 7 (isRunning gate)
- §8.2 unit tests:
  - `tst_mapping_editor_dialog.cpp` — Task 5 ✓
  - `tst_palm_runtime_reload_mappings.cpp` — Task 2 ✓
  - `tst_palm_runtime_default_mappings_only_when_empty.cpp` — Task 3 ✓
- §8.2 real-device verification — **deferred per user direction** (noted at top)
- §5.2 backup-root path picker on Sync page — **NOT implemented** (Profile doesn't expose a backup root accessor; PalmRuntime hardcodes `<profilePath>/backup`). Adding this requires a Profile schema change which is out of scope for M5b. Documented as a Sync-page gap; defer to M5c or M6.

**Placeholder scan:** No "TBD"/"TODO" markers in code blocks. The `<chosen-dir>` placeholders in commit commands are intentional — Task 4 Step 4.1 picks the path; subsequent tasks use the same path.

**Type consistency:**
- `MappingRowDialog::setSourceBackends(QStringList)`, `setMapping(SyncMapping)`, `mapping() const → SyncMapping` — consistent across Tasks 4 and 5.
- `MappingEditorDialog::setMappings(QJsonArray)`, `mappings() const → QJsonArray` — consistent across Tasks 5 and 7.
- `PalmRuntime::reloadMappings(QJsonArray)`, `isRunning() const → bool` — consistent across Tasks 1, 2, 3, 7.
- `ActionManager::configureMappingsRequested()` signal — consistent across Tasks 6 and 7.
- `KF6MainWindow::onConfigureMappings()` slot — single declaration in Task 7.

**One soft gap:** Task 8 wires `saveSyncSettings()` "into the existing save path" — the implementer may need to inspect `SettingsDialog::saveSettings()` to find the right insertion point. The plan flags this with the explicit instruction "find the existing `saveSettings()` connection".

---

## Coordination notes

- **No PlanStan changes** in M5b.
- **No libkalburator changes** in M5b — all new code in WildPalms.
- **Coordination folder is not a git repo** — `CURRENT-STATUS.md`, `FINDINGS.md`, this plan saved but not version-controlled.
- **Tag `v0.19-phase-m5b-mapping-editor`** is the load-bearing reference. User runs `git tag` per CLAUDE.md.
- **Real-device gate deferred per user direction** — automated tests are the only evidence M5b lands cleanly.
