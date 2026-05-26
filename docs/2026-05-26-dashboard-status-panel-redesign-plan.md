# Dashboard Status Panel Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the passive `DashboardWidget` with a live device + sync narrator driven by a testable `SyncStatusModel`, so users never need to read the log for normal operation.

**Architecture:** A UI-free `SyncStatusModel` (QObject) owns all panel state (connection state machine, per-conduit chips, progress, conflict count, last-sync digest) and is fed by existing signals. `DashboardWidget` becomes a pure view that re-renders on the model's `changed()` signal and forwards button clicks back through the model. `PalmRuntime` gains per-mapping progress signals by forwarding libkalburator `SyncEngine` signals it currently ignores. `KF6MainWindow` owns one model and wires sources into it per profile load.

**Tech Stack:** C++17, Qt6, KDE Frameworks 6, libkalburator (FetchContent), QtTest.

**Spec:** `docs/2026-05-26-dashboard-status-panel-redesign-design.md`

---

## Key type references (verified against source)

- `WildPalms::Runtime::PalmRunResult` — `src/runtime/palmrunresult.h:11`. Fields used: `bool success`, `QString errorMessage`, `qint64 durationMs() const`.
- `PalmRuntime` members: `m_engine` is `std::unique_ptr<Kalburator::Engine::SyncEngine>` (`palmruntime.h:224`); plugins are `std::vector<std::unique_ptr<Kalburator::Plugin>> m_palmPlugins` (`:231`), exposed via `palmPlugins()` (`:110`). Engine constructed at `palmruntime.cpp:129`; existing engine connection at `:137`; `setSyncMappings` at `:410`.
- Engine signals (reference via `Kalburator::Sync::SyncEngine::` to match existing code at `palmruntime.cpp:137`): `syncStarted(const QString&)`, `progressUpdated(int,int,const QString&)`, `phaseChanged(SyncPhase)`, `fetchProgress(const QString&,int,int)`, `writeProgress(const QString&,int,int)`. `SyncPhase` enum: `Idle, FetchingSource, FetchingTarget, Processing, Complete` (`syncengine.h:368`).
- `Kalburator::Sync::SyncResult` (`synctypes.h:111`): `bool success`, `bool cancelled`, `bool skipped`, `SyncStats sourceStats`, `SyncStats targetStats`. `SyncStats`: `int created, updated, deleted, unchanged, conflicts, errors`.
- `Kalburator::Sync::SyncMapping` (`synctypes.h:224`): `QString id, sourceBackend, sourceCalendar, targetBackend, targetCalendar; bool enabled`.
- `Kalburator::Plugin` base exposes identity via the WP plugin types: `pluginId()`, `displayName()`, `icon()`. Palm plugin ids: calendar=`"calendar"`, contacts=`"contacts"`, memo=`"memo"`, todo=`"todo"`, plucker=`"plucker"`. (Mapping correlation key is `mapping.sourceBackend == pluginId()`.)
- `Profile` (`profile.h`): `name()`, `lastSyncTime()`, `deviceFingerprint()`, `autoSyncOnConnect()`, `defaultSyncType()`.
- `DeviceFingerprint` (`profile.h:43`): `isValid()`, `hasExtendedInfo()`, `displayString()`, `palmOSVersionString()`, `quint64 ramFree`, static `formatMemorySize(quint64)`.
- HotSync entry: `m_palmRuntime->hotSync()` returns `QFuture<PalmRunResult>` (`kf6mainwindow.cpp:1877`). Cancel: `PalmRuntime::cancelSync()` (`palmruntime.h:83`).
- KF6MainWindow members: `DashboardWidget *m_dashboardWidget`, `std::unique_ptr<...::PalmRuntime> m_palmRuntime`, `PalmDeviceMonitor *m_deviceMonitor`, `AutoSyncOrchestrator *m_autoSync`, `std::unique_ptr<Profile> m_currentProfile`.
- Device monitor signals: `PalmDeviceMonitor::palmDetected(QStringList,QString)`, `palmDisconnected(QString)`. PalmRuntime connection signals: `connectionStarted()`, `connectionComplete(bool,QString)`, `deviceDisconnected()`, `runStarted(QString)`, `runProgress(int,int,QString)`, `runFinished(PalmRunResult)`.

---

## File structure

- **Create** `src/widgets/dashboard/syncstatusmodel.h` / `.cpp` — UI-free state model.
- **Create** `tests/widgets/CMakeLists.txt`, `tests/widgets/tst_syncstatusmodel.cpp` — model unit tests.
- **Modify** `tests/CMakeLists.txt` — `add_subdirectory(widgets)`.
- **Rewrite** `src/widgets/dashboard/dashboardwidget.h` / `.cpp` — pure view over the model.
- **Modify** `src/runtime/palmruntime.h` / `.cpp` — new per-mapping signals, engine-signal forwarding, `conduitDescriptors()`.
- **Modify** `src/kf6/kf6mainwindow.h` / `.cpp` — own the model, wire sources, delete orphaned `onDeviceStatusChanged`.
- **Modify** `src/widgets/dashboard/CMakeLists.txt` (or the CMake list that compiles dashboard sources) — add `syncstatusmodel.cpp`.

---

## Task 1: SyncStatusModel — connection state machine

**Files:**
- Create: `src/widgets/dashboard/syncstatusmodel.h`
- Create: `src/widgets/dashboard/syncstatusmodel.cpp`
- Create: `tests/widgets/tst_syncstatusmodel.cpp`
- Create: `tests/widgets/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the model header**

Create `src/widgets/dashboard/syncstatusmodel.h`:

```cpp
#ifndef SYNCSTATUSMODEL_H
#define SYNCSTATUSMODEL_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>

#include "../../runtime/palmrunresult.h"  // WildPalms::Runtime::PalmRunResult

/**
 * UI-free state model for the dashboard status panel.
 *
 * Fed by device-monitor, connection, and sync-run signals (translated to plain
 * values by KF6MainWindow). Emits changed() whenever any rendered state moves;
 * the view re-renders wholesale. Action requests originate here so KF6MainWindow
 * has a single wiring surface.
 */
class SyncStatusModel : public QObject
{
    Q_OBJECT
public:
    enum class LinkState { Listening, Detected, Handshaking, Connected, Syncing, Disconnected };
    Q_ENUM(LinkState)

    enum class ChipState { Pending, Active, Done, Error, Interrupted };
    Q_ENUM(ChipState)

    struct ConduitSeed {
        QString mappingId;
        QString label;
        QString iconName;
    };

    struct Conduit {
        QString mappingId;
        QString label;
        QString iconName;
        ChipState state = ChipState::Pending;
        int current = 0;
        int total = 0;
        int created = 0;
        int modified = 0;
        int deleted = 0;
    };

    struct Digest {
        bool valid = false;
        QString modeLabel;
        int totalChanges = 0;
        int conflicts = 0;
        qint64 durationMs = 0;
        bool success = true;
    };

    explicit SyncStatusModel(QObject *parent = nullptr);

    // --- rendered state getters ---
    LinkState linkState() const { return m_linkState; }
    const QVector<Conduit> &conduits() const { return m_conduits; }
    int progressCurrent() const { return m_progressCurrent; }
    int progressTotal() const { return m_progressTotal; }
    QString progressMessage() const { return m_progressMessage; }
    int conflictCount() const { return m_conflictCount; }
    Digest lastDigest() const { return m_digest; }
    QString errorText() const { return m_errorText; }

    QString deviceName() const { return m_deviceName; }
    QString deviceDetails() const { return m_deviceDetails; }
    QString profileName() const { return m_profileName; }
    QDateTime lastSyncTime() const { return m_lastSyncTime; }
    QString autoSyncPlan() const { return m_autoSyncPlan; }

    // --- derived view helpers ---
    QString headline() const;
    QString primaryActionLabel() const;   // "Sync Now" / "Cancel" / "" (hidden)
    bool primaryActionVisible() const { return !primaryActionLabel().isEmpty(); }

public slots:
    // device + profile snapshot (plain values — keeps the model Profile-free)
    void setDeviceInfo(const QString &name, const QString &details);
    void setProfileInfo(const QString &profileName, const QDateTime &lastSync,
                        const QString &autoSyncPlan);
    void seedConduits(const QVector<ConduitSeed> &conduits);

    // connection lifecycle
    void onDeviceDetected();
    void onDeviceLost();                                   // udev unplug OR link teardown
    void onConnectionStarted();
    void onConnectionComplete(bool success, const QString &error);

    // sync lifecycle
    void onRunStarted(const QString &modeLabel);
    void onRunProgress(int current, int total, const QString &message);
    void onRunFinished(const WildPalms::Runtime::PalmRunResult &result);
    void onMappingSyncStarted(const QString &mappingId, const QString &label,
                              const QString &iconName);
    void onMappingSyncProgress(const QString &mappingId, int phase, int current, int total);
    void onMappingSyncFinished(const QString &mappingId, int created, int modified,
                               int deleted, bool ok);

    void onConflictCountChanged(int count);

    // action requests (called by the view; the model picks the right signal)
    void triggerPrimaryAction();
    void triggerResolveConflicts();

signals:
    void changed();
    void syncRequested();
    void cancelRequested();
    void resolveConflictsRequested();

private:
    Conduit *findConduit(const QString &mappingId);     // nullptr if absent
    void setState(LinkState s);

    LinkState m_linkState = LinkState::Listening;
    QVector<Conduit> m_conduits;
    int m_progressCurrent = 0;
    int m_progressTotal = 0;
    QString m_progressMessage;
    int m_conflictCount = 0;
    Digest m_digest;
    QString m_errorText;
    QString m_currentRunLabel;
    int m_runChanges = 0;                                 // accumulated this run

    QString m_deviceName;
    QString m_deviceDetails;
    QString m_profileName;
    QDateTime m_lastSyncTime;
    QString m_autoSyncPlan;
};

#endif // SYNCSTATUSMODEL_H
```

- [ ] **Step 2: Write the failing connection-state test**

Create `tests/widgets/tst_syncstatusmodel.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "syncstatusmodel.h"

class TestSyncStatusModel : public QObject
{
    Q_OBJECT
private slots:
    void initialStateIsListening();
    void detectThenHandshakeThenConnected();
    void connectionFailureReturnsToListening();
    void unplugFromConnectedGoesDisconnected();
    void primaryActionLabelTracksState();
    void changedSignalFiresOnTransition();
};

void TestSyncStatusModel::initialStateIsListening()
{
    SyncStatusModel m;
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Listening);
    QVERIFY(!m.primaryActionVisible());
}

void TestSyncStatusModel::detectThenHandshakeThenConnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Detected);
    m.onConnectionStarted();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Handshaking);
    m.onConnectionComplete(true, QString());
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Connected);
    QVERIFY(m.errorText().isEmpty());
}

void TestSyncStatusModel::connectionFailureReturnsToListening()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(false, QStringLiteral("port busy"));
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Listening);
    QCOMPARE(m.errorText(), QStringLiteral("port busy"));
}

void TestSyncStatusModel::unplugFromConnectedGoesDisconnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onDeviceLost();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
}

void TestSyncStatusModel::primaryActionLabelTracksState()
{
    SyncStatusModel m;
    QVERIFY(m.primaryActionLabel().isEmpty());          // Listening
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    QCOMPARE(m.primaryActionLabel(), QStringLiteral("Sync Now"));   // Connected
    m.onRunStarted(QStringLiteral("HotSync"));
    QCOMPARE(m.primaryActionLabel(), QStringLiteral("Cancel"));     // Syncing
}

void TestSyncStatusModel::changedSignalFiresOnTransition()
{
    SyncStatusModel m;
    QSignalSpy spy(&m, &SyncStatusModel::changed);
    m.onDeviceDetected();
    QVERIFY(spy.count() >= 1);
}

QTEST_GUILESS_MAIN(TestSyncStatusModel)
#include "tst_syncstatusmodel.moc"
```

- [ ] **Step 3: Write the test CMake and register it**

Create `tests/widgets/CMakeLists.txt`:

```cmake
add_executable(tst_syncstatusmodel
    tst_syncstatusmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/syncstatusmodel.cpp
)
target_include_directories(tst_syncstatusmodel
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/widgets/dashboard
)
target_link_libraries(tst_syncstatusmodel
    PRIVATE
        Qt::Core
        Qt::Test
)
add_test(NAME tst_syncstatusmodel COMMAND tst_syncstatusmodel)
set_tests_properties(tst_syncstatusmodel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

In `tests/CMakeLists.txt`, add alongside the other `add_subdirectory` lines:

```cmake
add_subdirectory(widgets)
```

- [ ] **Step 4: Implement the model (connection state machine portion)**

Create `src/widgets/dashboard/syncstatusmodel.cpp`:

```cpp
#include "syncstatusmodel.h"

SyncStatusModel::SyncStatusModel(QObject *parent)
    : QObject(parent)
{
}

void SyncStatusModel::setState(LinkState s)
{
    if (m_linkState == s)
        return;
    m_linkState = s;
    Q_EMIT changed();
}

SyncStatusModel::Conduit *SyncStatusModel::findConduit(const QString &mappingId)
{
    for (auto &c : m_conduits)
        if (c.mappingId == mappingId)
            return &c;
    return nullptr;
}

void SyncStatusModel::setDeviceInfo(const QString &name, const QString &details)
{
    m_deviceName = name;
    m_deviceDetails = details;
    Q_EMIT changed();
}

void SyncStatusModel::setProfileInfo(const QString &profileName,
                                     const QDateTime &lastSync,
                                     const QString &autoSyncPlan)
{
    m_profileName = profileName;
    m_lastSyncTime = lastSync;
    m_autoSyncPlan = autoSyncPlan;
    Q_EMIT changed();
}

void SyncStatusModel::seedConduits(const QVector<ConduitSeed> &conduits)
{
    m_conduits.clear();
    for (const auto &s : conduits) {
        Conduit c;
        c.mappingId = s.mappingId;
        c.label = s.label;
        c.iconName = s.iconName;
        c.state = ChipState::Pending;
        m_conduits.append(c);
    }
    Q_EMIT changed();
}

void SyncStatusModel::onDeviceDetected()
{
    if (m_linkState == LinkState::Listening || m_linkState == LinkState::Disconnected)
        setState(LinkState::Detected);
}

void SyncStatusModel::onDeviceLost()
{
    if (m_linkState == LinkState::Syncing) {
        for (auto &c : m_conduits)
            if (c.state == ChipState::Active)
                c.state = ChipState::Interrupted;
    }
    setState(LinkState::Disconnected);
}

void SyncStatusModel::onConnectionStarted()
{
    setState(LinkState::Handshaking);
}

void SyncStatusModel::onConnectionComplete(bool success, const QString &error)
{
    if (success) {
        m_errorText.clear();
        setState(LinkState::Connected);
    } else {
        m_errorText = error;
        setState(LinkState::Listening);
    }
}

void SyncStatusModel::onConflictCountChanged(int count)
{
    if (m_conflictCount == count)
        return;
    m_conflictCount = count;
    Q_EMIT changed();
}

void SyncStatusModel::triggerPrimaryAction()
{
    if (m_linkState == LinkState::Connected)
        Q_EMIT syncRequested();
    else if (m_linkState == LinkState::Syncing)
        Q_EMIT cancelRequested();
}

void SyncStatusModel::triggerResolveConflicts()
{
    Q_EMIT resolveConflictsRequested();
}

QString SyncStatusModel::primaryActionLabel() const
{
    switch (m_linkState) {
    case LinkState::Connected: return QStringLiteral("Sync Now");
    case LinkState::Syncing:   return QStringLiteral("Cancel");
    default:                   return QString();
    }
}

QString SyncStatusModel::headline() const
{
    switch (m_linkState) {
    case LinkState::Listening:
        return m_errorText.isEmpty()
            ? QStringLiteral("Listening for Palm devices…\nPress HotSync on your Palm.")
            : m_errorText;
    case LinkState::Detected:    return QStringLiteral("Palm device detected…");
    case LinkState::Handshaking: return QStringLiteral("Connecting…");
    case LinkState::Connected:
        if (m_digest.valid)
            return m_digest.success
                ? QStringLiteral("%1 complete").arg(m_digest.modeLabel)
                : QStringLiteral("%1 finished with errors").arg(m_digest.modeLabel);
        return QStringLiteral("Ready to sync");
    case LinkState::Syncing:
        return m_progressMessage.isEmpty()
            ? QStringLiteral("Syncing…") : m_progressMessage;
    case LinkState::Disconnected:
        return m_errorText.isEmpty()
            ? QStringLiteral("Disconnected")
            : QStringLiteral("Disconnected — %1").arg(m_errorText);
    }
    return QString();
}

// --- sync lifecycle (implemented in Task 2/3) ---
void SyncStatusModel::onRunStarted(const QString &) {}
void SyncStatusModel::onRunProgress(int, int, const QString &) {}
void SyncStatusModel::onRunFinished(const WildPalms::Runtime::PalmRunResult &) {}
void SyncStatusModel::onMappingSyncStarted(const QString &, const QString &, const QString &) {}
void SyncStatusModel::onMappingSyncProgress(const QString &, int, int, int) {}
void SyncStatusModel::onMappingSyncFinished(const QString &, int, int, int, bool) {}
```

> Note: the `primaryActionLabelTracksState` test exercises `onRunStarted` → must reach `Syncing`. Implement the minimal `onRunStarted` body now so this test passes: set `m_currentRunLabel`, `setState(LinkState::Syncing)`. Replace the empty stub:
> ```cpp
> void SyncStatusModel::onRunStarted(const QString &modeLabel) {
>     m_currentRunLabel = modeLabel;
>     m_errorText.clear();
>     m_runChanges = 0;
>     m_progressCurrent = m_progressTotal = 0;
>     m_progressMessage.clear();
>     for (auto &c : m_conduits) {
>         c.state = ChipState::Pending;
>         c.current = c.total = c.created = c.modified = c.deleted = 0;
>     }
>     m_digest.valid = false;
>     setState(LinkState::Syncing);
> }
> ```

- [ ] **Step 5: Run the test to verify it fails, then passes**

Run: `cmake --build build --target tst_syncstatusmodel -j"$(nproc)" && ctest --test-dir build -R tst_syncstatusmodel --output-on-failure`
Expected: builds, all 6 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/widgets/dashboard/syncstatusmodel.h src/widgets/dashboard/syncstatusmodel.cpp \
        tests/widgets/CMakeLists.txt tests/widgets/tst_syncstatusmodel.cpp tests/CMakeLists.txt
git commit -m "feat(dashboard): SyncStatusModel connection state machine + tests"
```

---

## Task 2: SyncStatusModel — per-conduit tracking

**Files:**
- Modify: `src/widgets/dashboard/syncstatusmodel.cpp`
- Modify: `tests/widgets/tst_syncstatusmodel.cpp`

- [ ] **Step 1: Add failing conduit tests**

Add these methods to the `private slots:` block and implement them in `tst_syncstatusmodel.cpp`:

```cpp
    void seedingCreatesPendingChips();
    void mappingStartedMarksActiveAndPreviousDone();
    void mappingProgressUpdatesCounts();
    void mappingFinishedFillsCountsAndState();
    void unplugMidSyncInterruptsActiveChip();
```

```cpp
static QVector<SyncStatusModel::ConduitSeed> twoSeeds()
{
    return {
        { QStringLiteral("m-cal"), QStringLiteral("Calendar"),  QStringLiteral("office-calendar") },
        { QStringLiteral("m-con"), QStringLiteral("Contacts"),  QStringLiteral("contact-new") },
    };
}

void TestSyncStatusModel::seedingCreatesPendingChips()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    QCOMPARE(m.conduits().size(), 2);
    QCOMPARE(m.conduits()[0].label, QStringLiteral("Calendar"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Pending);
}

void TestSyncStatusModel::mappingStartedMarksActiveAndPreviousDone()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Active);
    m.onMappingSyncStarted(QStringLiteral("m-con"), QStringLiteral("Contacts"), QStringLiteral("contact-new"));
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Done);   // previous auto-done
    QCOMPARE(m.conduits()[1].state, SyncStatusModel::ChipState::Active);
}

void TestSyncStatusModel::mappingProgressUpdatesCounts()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onMappingSyncProgress(QStringLiteral("m-cal"), 0, 12, 45);
    QCOMPARE(m.conduits()[0].current, 12);
    QCOMPARE(m.conduits()[0].total, 45);
}

void TestSyncStatusModel::mappingFinishedFillsCountsAndState()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onMappingSyncFinished(QStringLiteral("m-cal"), 3, 2, 1, true);
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Done);
    QCOMPARE(m.conduits()[0].created, 3);
    QCOMPARE(m.conduits()[0].modified, 2);
    QCOMPARE(m.conduits()[0].deleted, 1);
    SyncStatusModel m2;
    m2.seedConduits(twoSeeds());
    m2.onRunStarted(QStringLiteral("HotSync"));
    m2.onMappingSyncFinished(QStringLiteral("m-con"), 0, 0, 0, false);
    QCOMPARE(m2.conduits()[1].state, SyncStatusModel::ChipState::Error);
}

void TestSyncStatusModel::unplugMidSyncInterruptsActiveChip()
{
    SyncStatusModel m;
    m.seedConduits(twoSeeds());
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    m.onDeviceLost();
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
    QCOMPARE(m.conduits()[0].state, SyncStatusModel::ChipState::Interrupted);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target tst_syncstatusmodel -j"$(nproc)" && ctest --test-dir build -R tst_syncstatusmodel --output-on-failure`
Expected: the 5 new tests FAIL (stubs do nothing).

- [ ] **Step 3: Replace the conduit stubs with real implementations**

In `syncstatusmodel.cpp` replace the empty `onMappingSyncStarted/Progress/Finished` stubs:

```cpp
void SyncStatusModel::onMappingSyncStarted(const QString &mappingId,
                                           const QString &label,
                                           const QString &iconName)
{
    // Auto-complete any still-active conduit (engine runs sequentially).
    for (auto &c : m_conduits)
        if (c.state == ChipState::Active && c.mappingId != mappingId)
            c.state = ChipState::Done;

    Conduit *c = findConduit(mappingId);
    if (!c) {
        Conduit nc;
        nc.mappingId = mappingId;
        nc.label = label;
        nc.iconName = iconName;
        m_conduits.append(nc);
        c = &m_conduits.last();
    }
    c->state = ChipState::Active;
    if (!label.isEmpty())   c->label = label;
    if (!iconName.isEmpty()) c->iconName = iconName;
    Q_EMIT changed();
}

void SyncStatusModel::onMappingSyncProgress(const QString &mappingId, int /*phase*/,
                                            int current, int total)
{
    if (Conduit *c = findConduit(mappingId)) {
        c->current = current;
        c->total = total;
        Q_EMIT changed();
    }
}

void SyncStatusModel::onMappingSyncFinished(const QString &mappingId, int created,
                                            int modified, int deleted, bool ok)
{
    if (Conduit *c = findConduit(mappingId)) {
        c->created = created;
        c->modified = modified;
        c->deleted = deleted;
        c->state = ok ? ChipState::Done : ChipState::Error;
        m_runChanges += created + modified + deleted;
        Q_EMIT changed();
    }
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target tst_syncstatusmodel -j"$(nproc)" && ctest --test-dir build -R tst_syncstatusmodel --output-on-failure`
Expected: all 11 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/widgets/dashboard/syncstatusmodel.cpp tests/widgets/tst_syncstatusmodel.cpp
git commit -m "feat(dashboard): per-conduit chip tracking in SyncStatusModel"
```

---

## Task 3: SyncStatusModel — progress, digest, and run completion

**Files:**
- Modify: `src/widgets/dashboard/syncstatusmodel.cpp`
- Modify: `tests/widgets/tst_syncstatusmodel.cpp`

- [ ] **Step 1: Add failing tests**

Add to `private slots:` and implement:

```cpp
    void runProgressUpdatesProgressFields();
    void runFinishedBuildsDigestAndReturnsToConnected();
    void runFinishedAfterUnplugStaysDisconnected();
```

```cpp
void TestSyncStatusModel::runProgressUpdatesProgressFields()
{
    SyncStatusModel m;
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onRunProgress(2, 6, QStringLiteral("Syncing Contacts"));
    QCOMPARE(m.progressCurrent(), 2);
    QCOMPARE(m.progressTotal(), 6);
    QCOMPARE(m.progressMessage(), QStringLiteral("Syncing Contacts"));
    QCOMPARE(m.headline(), QStringLiteral("Syncing Contacts"));
}

void TestSyncStatusModel::runFinishedBuildsDigestAndReturnsToConnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.seedConduits(twoSeeds());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onMappingSyncFinished(QStringLiteral("m-cal"), 3, 2, 1, true);
    m.onMappingSyncFinished(QStringLiteral("m-con"), 1, 0, 0, true);

    WildPalms::Runtime::PalmRunResult r;
    r.success = true;
    r.startTime = QDateTime::currentDateTime().addSecs(-8);
    r.endTime = QDateTime::currentDateTime();
    m.onRunFinished(r);

    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Connected);
    QVERIFY(m.lastDigest().valid);
    QCOMPARE(m.lastDigest().totalChanges, 7);          // 3+2+1 + 1
    QVERIFY(m.lastDigest().success);
}

void TestSyncStatusModel::runFinishedAfterUnplugStaysDisconnected()
{
    SyncStatusModel m;
    m.onDeviceDetected();
    m.onConnectionStarted();
    m.onConnectionComplete(true, QString());
    m.onRunStarted(QStringLiteral("HotSync"));
    m.onDeviceLost();                                   // unplugged mid-sync
    WildPalms::Runtime::PalmRunResult r;
    r.success = false;
    r.errorMessage = QStringLiteral("link lost");
    m.onRunFinished(r);
    QCOMPARE(m.linkState(), SyncStatusModel::LinkState::Disconnected);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target tst_syncstatusmodel -j"$(nproc)" && ctest --test-dir build -R tst_syncstatusmodel --output-on-failure`
Expected: 3 new tests FAIL.

- [ ] **Step 3: Implement onRunProgress and onRunFinished**

Replace the `onRunProgress` and `onRunFinished` stubs in `syncstatusmodel.cpp`:

```cpp
void SyncStatusModel::onRunProgress(int current, int total, const QString &message)
{
    m_progressCurrent = current;
    m_progressTotal = total;
    m_progressMessage = message;
    Q_EMIT changed();
}

void SyncStatusModel::onRunFinished(const WildPalms::Runtime::PalmRunResult &result)
{
    // Any conduit left Active completes now.
    for (auto &c : m_conduits)
        if (c.state == ChipState::Active)
            c.state = result.success ? ChipState::Done : ChipState::Error;

    m_digest.valid = true;
    m_digest.modeLabel = m_currentRunLabel.isEmpty()
        ? QStringLiteral("Sync") : m_currentRunLabel;
    m_digest.totalChanges = m_runChanges;
    m_digest.conflicts = m_conflictCount;
    m_digest.durationMs = result.durationMs();
    m_digest.success = result.success;

    if (!result.success && m_errorText.isEmpty())
        m_errorText = result.errorMessage;

    // Only return to Connected if we did not lose the link mid-sync.
    if (m_linkState == LinkState::Syncing)
        setState(LinkState::Connected);
    else
        Q_EMIT changed();
}
```

- [ ] **Step 4: Run to verify pass**

Run: `cmake --build build --target tst_syncstatusmodel -j"$(nproc)" && ctest --test-dir build -R tst_syncstatusmodel --output-on-failure`
Expected: all 14 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/widgets/dashboard/syncstatusmodel.cpp tests/widgets/tst_syncstatusmodel.cpp
git commit -m "feat(dashboard): run progress + last-sync digest in SyncStatusModel"
```

---

## Task 4: PalmRuntime — conduit descriptors + per-mapping signals (declarations)

**Files:**
- Modify: `src/runtime/palmruntime.h`

- [ ] **Step 1: Add the descriptor type, accessor, and signals to the header**

In `src/runtime/palmruntime.h`, inside the `PalmRuntime` class public section, add:

```cpp
    struct ConduitDescriptor {
        QString mappingId;
        QString label;
        QString iconName;
    };
    /// Identity (id/label/icon) for each enabled mapping, resolved via the
    /// loaded plugins. Used to seed the dashboard conduit row before a sync.
    QVector<ConduitDescriptor> conduitDescriptors() const;
```

In the `signals:` block, alongside `runProgress`, add:

```cpp
    void mappingSyncStarted(const QString &mappingId, const QString &label,
                            const QString &iconName);
    void mappingSyncProgress(const QString &mappingId, int phase,
                             int current, int total);
    void mappingSyncFinished(const QString &mappingId, int created,
                             int modified, int deleted, bool ok);
```

Add a private helper declaration:

```cpp
private:
    /// Resolve a mapping's display label + theme icon name from m_palmPlugins
    /// (matches plugin->pluginId() against mapping.sourceBackend).
    void resolveMappingIdentity(const QString &mappingId,
                                QString &outLabel, QString &outIconName) const;
    QString m_activeMappingId;   // mapping currently emitting fetch/write progress
```

Add `#include <QVector>` if not already present.

- [ ] **Step 2: Commit (declaration only — implemented next task)**

```bash
git add src/runtime/palmruntime.h
git commit -m "feat(runtime): declare per-mapping progress signals + conduitDescriptors"
```

---

## Task 5: PalmRuntime — engine-signal forwarding implementation

**Files:**
- Modify: `src/runtime/palmruntime.cpp`

- [ ] **Step 1: Implement resolveMappingIdentity and conduitDescriptors**

Add to `src/runtime/palmruntime.cpp` (near the other accessor implementations). The plugin identity calls (`pluginId()`, `displayName()`, `icon()`) are on `Kalburator::Plugin`; the theme icon name is derived from the plugin id with a stable fallback:

```cpp
void PalmRuntime::resolveMappingIdentity(const QString &mappingId,
                                         QString &outLabel,
                                         QString &outIconName) const
{
    outLabel = mappingId;
    outIconName = QStringLiteral("view-list-details");
    // Find the mapping, then the plugin whose id == mapping.sourceBackend.
    for (const auto &m : m_mappings) {
        if (m.id != mappingId)
            continue;
        for (const auto &plugin : m_palmPlugins) {
            if (plugin->pluginId() == m.sourceBackend) {
                outLabel = plugin->displayName();
                // Per-plugin theme icon names (match the page icons).
                static const QHash<QString, QString> kIcons = {
                    { QStringLiteral("calendar"), QStringLiteral("office-calendar") },
                    { QStringLiteral("contacts"), QStringLiteral("x-office-address-book") },
                    { QStringLiteral("memo"),     QStringLiteral("text-x-generic") },
                    { QStringLiteral("todo"),     QStringLiteral("view-task") },
                    { QStringLiteral("plucker"),  QStringLiteral("text-html") },
                };
                outIconName = kIcons.value(m.sourceBackend, QStringLiteral("view-list-details"));
                return;
            }
        }
        return;
    }
}

QVector<PalmRuntime::ConduitDescriptor> PalmRuntime::conduitDescriptors() const
{
    QVector<ConduitDescriptor> out;
    for (const auto &m : m_mappings) {
        if (!m.enabled)
            continue;
        ConduitDescriptor d;
        d.mappingId = m.id;
        resolveMappingIdentity(m.id, d.label, d.iconName);
        out.append(d);
    }
    return out;
}
```

- [ ] **Step 2: Connect the engine signals where the engine is constructed**

In `src/runtime/palmruntime.cpp`, immediately after the existing `conflictDetected` connection (around `:137-139`), add:

```cpp
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::syncStarted,
                     this, [this](const QString &mappingId) {
        m_activeMappingId = mappingId;
        QString label, icon;
        resolveMappingIdentity(mappingId, label, icon);
        Q_EMIT mappingSyncStarted(mappingId, label, icon);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::progressUpdated,
                     this, [this](int current, int total, const QString &message) {
        Q_EMIT runProgress(current, total, message);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::fetchProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/0, current, total);
    });
    QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::writeProgress,
                     this, [this](const QString &, int current, int total) {
        if (!m_activeMappingId.isEmpty())
            Q_EMIT mappingSyncProgress(m_activeMappingId, /*phase=*/1, current, total);
    });
```

> These engine signals are emitted on the engine worker thread; the default
> (auto) connection delivers them queued onto PalmRuntime's thread. Safe.

- [ ] **Step 3: Emit per-mapping finished from the run result**

In `runAllMappings()` (`palmruntime.cpp:583`), in the `.then(...)` continuation that walks `results` to aggregate stats, emit a per-mapping finished signal. The `ids` list (enabled mapping ids, in order) aligns with `results`. Add inside the continuation, alongside the existing aggregation loop:

```cpp
        // Per-mapping finished — chips fill their counts here (run-end only;
        // the engine has no per-mapping completion signal).
        for (int i = 0; i < results.size() && i < ids.size(); ++i) {
            const auto &sr = results[i];
            const auto &ts = sr.targetStats;
            QMetaObject::invokeMethod(this, [this, id = ids[i], ts, sr]() {
                Q_EMIT mappingSyncFinished(id, ts.created, ts.updated, ts.deleted,
                                           sr.success && !sr.cancelled);
            });
        }
        m_activeMappingId.clear();
```

> Use `QMetaObject::invokeMethod` (queued to the main thread) to match how
> `runFinished` is already emitted from this continuation.

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build build -j"$(nproc)" 2>&1 | tail -20`
Expected: builds clean. (No unit test — engine forwarding is integration glue, exercised by the device/manual verification at the end. The model logic it feeds is covered by Tasks 1–3.)

- [ ] **Step 5: Commit**

```bash
git add src/runtime/palmruntime.cpp
git commit -m "feat(runtime): forward SyncEngine signals as per-mapping progress"
```

---

## Task 6: DashboardWidget — rewrite as a pure view

**Files:**
- Rewrite: `src/widgets/dashboard/dashboardwidget.h`
- Rewrite: `src/widgets/dashboard/dashboardwidget.cpp`
- Modify: the CMake list compiling dashboard sources — add `syncstatusmodel.cpp`.

- [ ] **Step 1: Add syncstatusmodel.cpp to the app build**

Find the CMake target that compiles `dashboardwidget.cpp` (grep: `grep -rn dashboardwidget src/**/CMakeLists.txt src/CMakeLists.txt`). Add `syncstatusmodel.cpp` next to it, e.g.:

```cmake
    widgets/dashboard/dashboardwidget.cpp
    widgets/dashboard/syncstatusmodel.cpp
```

- [ ] **Step 2: Rewrite the widget header**

Replace `src/widgets/dashboard/dashboardwidget.h`:

```cpp
#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>

class QLabel;
class QPushButton;
class QProgressBar;
class QHBoxLayout;
class QTimer;
class SyncStatusModel;

/**
 * Two-tier status strip rendered entirely from a SyncStatusModel.
 * Top row: device | profile | "now" zone (headline/progress) + primary button.
 * Bottom row: per-conduit chips.
 */
class DashboardWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override = default;

    /// Bind to the model (borrowed). Re-bindable per profile load.
    void setModel(SyncStatusModel *model);

private slots:
    void render();              // full re-render from the model

private:
    void setupUI();
    void renderConduits();

    SyncStatusModel *m_model = nullptr;

    QLabel *m_deviceIconLabel = nullptr;
    QLabel *m_deviceNameLabel = nullptr;
    QLabel *m_deviceStatusLabel = nullptr;
    QLabel *m_deviceDetailsLabel = nullptr;
    QLabel *m_profileNameLabel = nullptr;
    QLabel *m_lastSyncLabel = nullptr;
    QLabel *m_autoSyncLabel = nullptr;
    QLabel *m_headlineLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_primaryButton = nullptr;
    QPushButton *m_conflictButton = nullptr;
    QHBoxLayout *m_conduitRow = nullptr;
    QTimer *m_relativeTimer = nullptr;   // refreshes "synced N ago"
};

#endif // DASHBOARDWIDGET_H
```

- [ ] **Step 3: Rewrite the widget implementation**

Replace `src/widgets/dashboard/dashboardwidget.cpp`:

```cpp
#include "dashboardwidget.h"
#include "syncstatusmodel.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QLocale>
#include <QIcon>

#include <KLocalizedString>

DashboardWidget::DashboardWidget(QWidget *parent) : QWidget(parent)
{
    setupUI();
}

void DashboardWidget::setupUI()
{
    setFixedHeight(140);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    root->setSpacing(6);

    // ---- top tier ----
    auto *top = new QHBoxLayout;
    top->setSpacing(16);

    m_deviceIconLabel = new QLabel;
    m_deviceIconLabel->setFixedSize(48, 48);
    top->addWidget(m_deviceIconLabel);

    auto *deviceCol = new QVBoxLayout;
    deviceCol->setSpacing(2);
    m_deviceNameLabel = new QLabel(i18n("No device"));
    m_deviceNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_deviceStatusLabel = new QLabel(i18n("Disconnected"));
    m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_deviceDetailsLabel = new QLabel;
    m_deviceDetailsLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    deviceCol->addWidget(m_deviceNameLabel);
    deviceCol->addWidget(m_deviceStatusLabel);
    deviceCol->addWidget(m_deviceDetailsLabel);
    deviceCol->addStretch();
    top->addLayout(deviceCol);

    auto *sep1 = new QFrame; sep1->setFrameShape(QFrame::VLine); sep1->setFrameShadow(QFrame::Sunken);
    top->addWidget(sep1);

    auto *profileCol = new QVBoxLayout;
    profileCol->setSpacing(2);
    m_profileNameLabel = new QLabel(i18n("No profile"));
    m_profileNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_lastSyncLabel = new QLabel(i18n("Last sync: Never"));
    m_lastSyncLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_autoSyncLabel = new QLabel;
    m_autoSyncLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    profileCol->addWidget(m_profileNameLabel);
    profileCol->addWidget(m_lastSyncLabel);
    profileCol->addWidget(m_autoSyncLabel);
    profileCol->addStretch();
    top->addLayout(profileCol);

    auto *sep2 = new QFrame; sep2->setFrameShape(QFrame::VLine); sep2->setFrameShadow(QFrame::Sunken);
    top->addWidget(sep2);

    auto *nowCol = new QVBoxLayout;
    nowCol->setSpacing(2);
    m_headlineLabel = new QLabel;
    m_headlineLabel->setAlignment(Qt::AlignCenter);
    m_headlineLabel->setWordWrap(true);
    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(true);
    m_progressBar->hide();
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_primaryButton = new QPushButton;
    m_primaryButton->hide();
    m_conflictButton = new QPushButton;
    m_conflictButton->setFlat(true);
    m_conflictButton->setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
    m_conflictButton->hide();
    btnRow->addWidget(m_primaryButton);
    btnRow->addWidget(m_conflictButton);
    btnRow->addStretch();
    nowCol->addWidget(m_headlineLabel);
    nowCol->addWidget(m_progressBar);
    nowCol->addLayout(btnRow);
    nowCol->addStretch();
    top->addLayout(nowCol, 1);

    root->addLayout(top);

    auto *sep3 = new QFrame; sep3->setFrameShape(QFrame::HLine); sep3->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep3);

    // ---- bottom tier: conduit chips ----
    m_conduitRow = new QHBoxLayout;
    m_conduitRow->setSpacing(14);
    m_conduitRow->addStretch();
    root->addLayout(m_conduitRow);

    m_relativeTimer = new QTimer(this);
    m_relativeTimer->setInterval(60 * 1000);
    connect(m_relativeTimer, &QTimer::timeout, this, &DashboardWidget::render);
    m_relativeTimer->start();
}

void DashboardWidget::setModel(SyncStatusModel *model)
{
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &SyncStatusModel::changed, this, &DashboardWidget::render);
        connect(m_primaryButton, &QPushButton::clicked,
                m_model, &SyncStatusModel::triggerPrimaryAction);
        connect(m_conflictButton, &QPushButton::clicked,
                m_model, &SyncStatusModel::triggerResolveConflicts);
    }
    render();
}

static QString relativeTime(const QDateTime &t)
{
    if (!t.isValid())
        return i18n("Last sync: Never");
    const qint64 secs = t.secsTo(QDateTime::currentDateTime());
    if (secs < 60)        return i18n("Synced just now");
    if (secs < 3600)      return i18n("Synced %1 min ago", secs / 60);
    if (secs < 86400)     return i18n("Synced %1 h ago", secs / 3600);
    return i18n("Last sync: %1", QLocale().toString(t, QLocale::ShortFormat));
}

void DashboardWidget::render()
{
    if (!m_model)
        return;
    using LS = SyncStatusModel::LinkState;
    const LS state = m_model->linkState();
    const bool connected = (state == LS::Connected || state == LS::Syncing);

    // device
    m_deviceNameLabel->setText(m_model->deviceName().isEmpty()
        ? i18n("No device") : m_model->deviceName());
    m_deviceDetailsLabel->setText(m_model->deviceDetails());
    m_deviceDetailsLabel->setVisible(!m_model->deviceDetails().isEmpty());
    if (connected) {
        m_deviceStatusLabel->setText(i18n("Connected"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        m_deviceIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("network-connect")).pixmap(48, 48));
    } else {
        m_deviceStatusLabel->setText(state == LS::Disconnected ? i18n("Disconnected") : i18n("Listening"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_deviceIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("phone")).pixmap(48, 48));
    }

    // profile
    m_profileNameLabel->setText(m_model->profileName().isEmpty()
        ? i18n("No profile") : m_model->profileName());
    m_lastSyncLabel->setText(relativeTime(m_model->lastSyncTime()));
    m_autoSyncLabel->setText(m_model->autoSyncPlan());
    m_autoSyncLabel->setVisible(!m_model->autoSyncPlan().isEmpty());

    // now zone
    m_headlineLabel->setText(m_model->headline());
    if (state == LS::Syncing && m_model->progressTotal() > 0) {
        m_progressBar->setRange(0, m_model->progressTotal());
        m_progressBar->setValue(m_model->progressCurrent());
        m_progressBar->show();
    } else {
        m_progressBar->hide();
    }
    const QString action = m_model->primaryActionLabel();
    m_primaryButton->setText(action);
    m_primaryButton->setVisible(!action.isEmpty());

    const int conflicts = m_model->conflictCount();
    m_conflictButton->setText(i18n("%1 conflicts", conflicts));
    m_conflictButton->setVisible(conflicts > 0);

    renderConduits();
}

void DashboardWidget::renderConduits()
{
    // Clear existing chip widgets (keep the trailing stretch).
    while (m_conduitRow->count() > 0) {
        QLayoutItem *item = m_conduitRow->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    using CS = SyncStatusModel::ChipState;
    for (const auto &c : m_model->conduits()) {
        QString glyph;
        QString color = QStringLiteral("gray");
        switch (c.state) {
        case CS::Pending:     glyph = QStringLiteral("·"); break;
        case CS::Active:      glyph = QStringLiteral("⟳ %1/%2").arg(c.current).arg(c.total); color = QStringLiteral("#1d6fb8"); break;
        case CS::Done:        glyph = QStringLiteral("✓ +%1 ~%2 −%3").arg(c.created).arg(c.modified).arg(c.deleted); color = QStringLiteral("green"); break;
        case CS::Error:       glyph = QStringLiteral("✗"); color = QStringLiteral("#c0392b"); break;
        case CS::Interrupted: glyph = QStringLiteral("⚠"); color = QStringLiteral("#c0392b"); break;
        }
        auto *chip = new QLabel(QStringLiteral("%1 %2").arg(c.label, glyph));
        chip->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color));
        m_conduitRow->addWidget(chip);
    }
    m_conduitRow->addStretch();
}
```

- [ ] **Step 4: Build and add a smoke test**

Add to `tests/widgets/CMakeLists.txt` a second target that constructs the widget, binds a model, drives a sync, and verifies no crash:

```cmake
add_executable(tst_dashboardwidget
    tst_dashboardwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/dashboardwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/syncstatusmodel.cpp
)
target_include_directories(tst_dashboardwidget PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/widgets/dashboard)
target_link_libraries(tst_dashboardwidget PRIVATE
    Qt::Widgets Qt::Test KF6::I18n)
add_test(NAME tst_dashboardwidget COMMAND tst_dashboardwidget)
set_tests_properties(tst_dashboardwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

Create `tests/widgets/tst_dashboardwidget.cpp`:

```cpp
#include <QtTest/QtTest>
#include "dashboardwidget.h"
#include "syncstatusmodel.h"

class TestDashboardWidget : public QObject
{
    Q_OBJECT
private slots:
    void bindsAndRendersThroughSyncWithoutCrashing();
    void primaryButtonForwardsToModel();
};

void TestDashboardWidget::bindsAndRendersThroughSyncWithoutCrashing()
{
    SyncStatusModel model;
    DashboardWidget w;
    w.setModel(&model);
    model.seedConduits({{ QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar") }});
    model.onDeviceDetected();
    model.onConnectionStarted();
    model.onConnectionComplete(true, QString());
    model.onRunStarted(QStringLiteral("HotSync"));
    model.onMappingSyncStarted(QStringLiteral("m-cal"), QStringLiteral("Calendar"), QStringLiteral("office-calendar"));
    model.onMappingSyncProgress(QStringLiteral("m-cal"), 0, 3, 9);
    model.onMappingSyncFinished(QStringLiteral("m-cal"), 1, 1, 0, true);
    WildPalms::Runtime::PalmRunResult r; r.success = true;
    model.onRunFinished(r);
    QVERIFY(true);   // reaching here without crashing is the assertion
}

void TestDashboardWidget::primaryButtonForwardsToModel()
{
    SyncStatusModel model;
    DashboardWidget w;
    w.setModel(&model);
    QSignalSpy spy(&model, &SyncStatusModel::syncRequested);
    model.onDeviceDetected();
    model.onConnectionStarted();
    model.onConnectionComplete(true, QString());      // Connected → "Sync Now"
    model.triggerPrimaryAction();
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestDashboardWidget)
#include "tst_dashboardwidget.moc"
```

Run: `cmake --build build -j"$(nproc)" && ctest --test-dir build -R "tst_dashboardwidget|tst_syncstatusmodel" --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/widgets/dashboard/dashboardwidget.h src/widgets/dashboard/dashboardwidget.cpp \
        tests/widgets/CMakeLists.txt tests/widgets/tst_dashboardwidget.cpp
git add src/CMakeLists.txt   # or whichever CMake gained syncstatusmodel.cpp
git commit -m "feat(dashboard): rewrite DashboardWidget as a view over SyncStatusModel"
```

---

## Task 7: KF6MainWindow — own the model and wire sources

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Add the model member and helper**

In `src/kf6/kf6mainwindow.h`, add an include `#include "../widgets/dashboard/syncstatusmodel.h"` and a member near `m_dashboardWidget`:

```cpp
    SyncStatusModel *m_syncStatusModel = nullptr;
```

Add a private helper declaration:

```cpp
    void wireSyncStatusModel();   // (re)connect sources to the model for current profile
    void pushProfileInfoToStatusModel();
```

Remove the orphaned slot declaration `void onDeviceStatusChanged(int status);` (`kf6mainwindow.h:140`).

- [ ] **Step 2: Create the model and wire device-monitor + connection sources**

In the constructor, after `m_dashboardWidget` is created (`kf6mainwindow.cpp:274`), add:

```cpp
    m_syncStatusModel = new SyncStatusModel(this);
    m_dashboardWidget->setModel(m_syncStatusModel);

    // Live udev presence (independent of any profile/runtime).
    connect(m_deviceMonitor, &PalmDeviceMonitor::palmDetected,
            this, [this](const QStringList &, const QString &) {
                m_syncStatusModel->onDeviceDetected();
            });
    connect(m_deviceMonitor, &PalmDeviceMonitor::palmDisconnected,
            this, [this](const QString &) {
                m_syncStatusModel->onDeviceLost();
            });

    // Panel action requests.
    connect(m_syncStatusModel, &SyncStatusModel::syncRequested,
            this, &KF6MainWindow::onHotSync);
    connect(m_syncStatusModel, &SyncStatusModel::cancelRequested,
            this, [this]() { if (m_palmRuntime) m_palmRuntime->cancelSync(); });
    connect(m_syncStatusModel, &SyncStatusModel::resolveConflictsRequested,
            this, &KF6MainWindow::onConflictBadgeClicked);
```

> `m_deviceMonitor` is created at `:137`, before the dashboard at `:274`, so it
> exists here. If ordering ever changes, move these connects after both exist.

- [ ] **Step 3: Wire per-profile PalmRuntime signals into the model**

In `loadProfile()`, where `PalmRuntime` is (re)created and `runStarted/runFinished`
are connected (`kf6mainwindow.cpp:553-556`), add the model wiring:

```cpp
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::connectionStarted,
            m_syncStatusModel, &SyncStatusModel::onConnectionStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::connectionComplete,
            m_syncStatusModel, &SyncStatusModel::onConnectionComplete);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::deviceDisconnected,
            m_syncStatusModel, &SyncStatusModel::onDeviceLost);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runStarted,
            m_syncStatusModel, &SyncStatusModel::onRunStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runProgress,
            m_syncStatusModel, &SyncStatusModel::onRunProgress);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::runFinished,
            m_syncStatusModel, &SyncStatusModel::onRunFinished);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncStarted,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncStarted);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncProgress,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncProgress);
    connect(m_palmRuntime.get(), &WildPalms::Runtime::PalmRuntime::mappingSyncFinished,
            m_syncStatusModel, &SyncStatusModel::onMappingSyncFinished);
```

- [ ] **Step 4: Push profile + device info and seed conduits on load**

Replace the existing `m_dashboardWidget->updateStatus(m_currentProfile.get(), connected);`
call (`kf6mainwindow.cpp:649`) with a call to a new helper, and define it. The helper
extracts plain values from the profile (keeping the model Profile-free):

```cpp
void KF6MainWindow::pushProfileInfoToStatusModel()
{
    if (!m_syncStatusModel) return;
    if (m_currentProfile) {
        const DeviceFingerprint fp = m_currentProfile->deviceFingerprint();
        QString name = fp.isValid() ? fp.displayString() : i18n("No device registered");
        QString details;
        if (fp.isValid() && fp.hasExtendedInfo()) {
            QStringList parts;
            const QString os = fp.palmOSVersionString();
            if (!os.isEmpty()) parts << i18n("Palm OS %1", os);
            if (fp.ramFree != 0) parts << i18n("%1 free", DeviceFingerprint::formatMemorySize(fp.ramFree));
            details = parts.join(QStringLiteral(" · "));
        }
        m_syncStatusModel->setDeviceInfo(name, details);

        QString plan;
        if (m_currentProfile->autoSyncOnConnect()) {
            plan = (m_currentProfile->defaultSyncType() == QStringLiteral("fullsync"))
                ? i18n("Auto-sync (FullSync) on connect")
                : i18n("Auto-sync (HotSync) on connect");
        }
        m_syncStatusModel->setProfileInfo(m_currentProfile->name(),
                                          m_currentProfile->lastSyncTime(), plan);
    } else {
        m_syncStatusModel->setDeviceInfo(QString(), QString());
        m_syncStatusModel->setProfileInfo(QString(), QDateTime(), QString());
    }

    // Seed the conduit chip row from the runtime's enabled mappings.
    if (m_palmRuntime) {
        const auto descs = m_palmRuntime->conduitDescriptors();
        QVector<SyncStatusModel::ConduitSeed> seeds;
        for (const auto &d : descs)
            seeds.append({ d.mappingId, d.label, d.iconName });
        m_syncStatusModel->seedConduits(seeds);
    }
}
```

At `:649`, replace `m_dashboardWidget->updateStatus(...)` with:

```cpp
    pushProfileInfoToStatusModel();
    if (connected)
        m_syncStatusModel->onConnectionComplete(true, QString());
```

- [ ] **Step 5: Remove remaining `updateStatus` calls and the orphaned slot body**

- Delete the `m_dashboardWidget->updateStatus(m_currentProfile.get(), true);` at the
  end of `onConnectionComplete()` (`:1037`) — the model now receives
  `connectionComplete` directly via the per-profile wiring, and
  `pushProfileInfoToStatusModel()` keeps device/profile fields fresh. Add a call to
  `pushProfileInfoToStatusModel();` there instead (device fingerprint may have just
  been learned/merged).
- Delete the `onDeviceStatusChanged(int)` method body (`:1239-1268`).
- Update conflict-count plumbing: in `refreshConflictBadge()` (`:2097`), after
  computing visibility, also call
  `if (m_syncStatusModel) m_syncStatusModel->onConflictCountChanged(m_pendingConflictCount);`

- [ ] **Step 6: Build**

Run: `cmake --build build -j"$(nproc)" 2>&1 | tail -20`
Expected: builds clean, no references to removed `updateStatus`/`onDeviceStatusChanged`.

- [ ] **Step 7: Run the full test suite (regression gate)**

Run: `ctest --test-dir build --output-on-failure 2>&1 | tail -25`
Expected: all tests pass (matches the pre-change baseline count plus the two new dashboard tests).

- [ ] **Step 8: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "feat(dashboard): wire SyncStatusModel into KF6MainWindow; drop dead slots"
```

---

## Task 8: Polish — spinner + listening pulse (optional, ship-if-time)

**Files:**
- Modify: `src/widgets/dashboard/dashboardwidget.h` / `.cpp`

- [ ] **Step 1: Add a rotation timer for the active headline glyph**

In `DashboardWidget`, add `QTimer *m_spinTimer = nullptr;` and an `int m_spinPhase = 0;`.
In `setupUI()`:

```cpp
    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(120);
    connect(m_spinTimer, &QTimer::timeout, this, [this]() {
        m_spinPhase = (m_spinPhase + 1) % 4;
        render();
    });
```

In `render()`, start/stop based on state:

```cpp
    if (state == LS::Syncing) m_spinTimer->start();
    else                      m_spinTimer->stop();
```

And in `renderConduits()`, for `CS::Active`, pick the spinner frame from
`{"⟳","⟲","⟳","⟲"}[m_spinPhase]` (purely cosmetic).

- [ ] **Step 2: Build + visual check, then commit**

Run: `cmake --build build -j"$(nproc)"`
```bash
git add src/widgets/dashboard/dashboardwidget.h src/widgets/dashboard/dashboardwidget.cpp
git commit -m "feat(dashboard): spinner animation on active sync"
```

---

## Final verification (device-backed)

These cannot be unit-tested; perform with a real Palm (per `reference_palm_ttyusb_connection`):

- [ ] Plug in the Palm with no profile → panel shows **Detected**.
- [ ] Connect → **Connected**, device name/OS/RAM populated, **Sync Now** appears.
- [ ] Press **Sync Now** → conduit chips animate (active/progress), headline tracks
      "Syncing <conduit>", progress bar moves.
- [ ] Sync completes → chips show ✓ with counts, headline shows digest
      "<mode> complete · N changes · Ns", **Sync Now** returns.
- [ ] **Unplug mid-sync** → panel flips to **Disconnected** immediately, active chip
      shows ⚠ Interrupted (this is the originally-reported bug).
- [ ] Unplug while idle-connected → **Disconnected** immediately (no longer stuck green).
- [ ] Trigger a conflict → in-panel ⚠ count appears and statusbar badge still works;
      clicking the panel count opens the conflict review dialog.

---

## Self-review notes

- **Spec coverage:** state machine (Task 1), per-conduit chips (Tasks 2,5,6),
  progress bar (Tasks 3,6), digest (Task 3), interactive button (Tasks 1,6,7),
  in-panel conflicts kept-alongside-statusbar (Tasks 6,7), two-tier ~140px layout
  (Task 6), polish (Task 8), unplug-reflected-immediately (Tasks 1,7 + device check).
- **Type consistency:** `onMappingSyncFinished(id, created, modified, deleted, ok)`
  used identically in model (Task 2), PalmRuntime emit (Task 5), and KF6 wiring
  (Task 7). `ConduitSeed`/`ConduitDescriptor` translated explicitly in Task 7 Step 4.
- **Out of scope (phase 2, per spec):** live per-conduit counts (needs libkalburator
  per-mapping completion signal — handoff doc), structured LogWidget ticker, persisted
  digest across restarts.
