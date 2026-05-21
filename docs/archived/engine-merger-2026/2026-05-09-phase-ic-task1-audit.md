# Phase Ic Task 1 — Pre-flight Audit

Date: 2026-05-09
Working tree: `~/dev/refactor-engine-merger/WildPalms/`
Status: read-only audit, no code changes.

This document confirms the six design assumptions from the Phase Ic
plan against current `refactor/engine-merger` HEAD in WildPalms.

## Step 1 — `SettingsDialog` callers

`grep -rn "new SettingsDialog\|SettingsDialog(" WildPalms/src/`
returns three matches; only one is a production caller, and the
other two are the constructor's own declaration / definition:

- `src/settingsdialog.h:33` — constructor declaration.
- `src/settingsdialog.cpp:26` — constructor definition.
- `src/kf6/kf6mainwindow.cpp:2024` — sole production caller, in
  `KF6MainWindow::onSettings()`:

```cpp
void KF6MainWindow::onSettings()
{
    SettingsDialog dialog(this, m_currentProfile);
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}
```

There is no test caller and no second production caller. The plan's
assumption holds. Only the `kf6mainwindow.cpp:2024` site needs to
be updated when `SettingsDialog`'s constructor signature is
extended (e.g. to receive an `AccountController`).

Note: actual file path is `src/kf6/kf6mainwindow.cpp` (a `kf6/`
subdirectory), not `src/kf6mainwindow.cpp` as the plan implies.

## Step 2 — `Profile::syncMappingsJson()` round-trip shape

`Profile::syncMappingsJson()` returns a `QJsonArray`. Storage is
the member `m_syncMappingsJson` (`profile.cpp:339-353`):

- Load (`profile.cpp:337-346`): reads `syncMappings/json` from
  the `.wildpalms.conf` `QSettings` group, parses with
  `QJsonDocument::fromJson`, accepts only `doc.isArray()`.
- Getter (`profile.cpp:351`): returns the array directly.
- Setter (`profile.cpp:353`): replaces wholesale.
- Save (`profile.cpp:452-453`): serialises via `QJsonDocument(arr)`.

Each array element is a `QJsonObject` round-trippable via
`Kalburator::Sync::syncMappingFromJson()` /
`syncMappingToJson()` — the `SyncMapping` shape with `id`,
`sourceBackend`, `sourceCalendar`, `targetBackend`,
`targetCalendar`, `mode`, `conflictPolicy`, `enabled`.

**Surprise**: `WildPalmsSyncMappingHelper::parseMappings()` is
referenced ONLY in a docstring at `profile.h:257` — there is no
class of that name. Actual parsing is done inline by callers
using `Kalburator::Sync::syncMappingFromJson()` directly, e.g.
`runtime/syncconfigstore_wp.cpp:61`,
`runtime/palmruntime.cpp:357`, and
`app/mapping/mappingeditordialog.cpp:165`. The plan should not
rely on that helper class existing.

## Step 3 — `PalmRuntime::reloadMappings` semantics

`runtime/palmruntime.h:74-79` declares:

```cpp
bool isRunning() const { return m_running; }

// Replace the live mapping list. Caller must ensure isRunning() == false.
// JSON shape is the same as Profile::syncMappingsJson() — array of objects
// each round-trippable via syncMappingToJson()/syncMappingFromJson().
void reloadMappings(const QJsonArray &json);
```

Implementation (`runtime/palmruntime.cpp:351-361`):

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

`m_running` is toggled by lambdas at `palmruntime.cpp:202` (true on
`runStarted`) and `:204` (false on `runFinished`).

The `!isRunning()` precondition is **documented but unenforced** —
no `Q_ASSERT`, no early return. Caller is responsible. The plan's
described semantics ("replaces `m_mappings` from supplied JSON
when `!isRunning()`") match — caller must gate.

## Step 4 — `m_running` interlock signal

`runtime/palmruntime.h` (lines 74, 110, 113):

```cpp
bool isRunning() const { return m_running; }
...
void runStarted(QString modeLabel);
void runFinished(PalmRunResult);
```

Both signals are present with the expected signatures. There is
also an additional `runProgress(int, int, QString)` and
`runLog(QString)` not listed in the plan but harmless for the
account-controller use case.

`AccountsPage` (or `AccountController`) can subscribe to
`runStarted` (disable Add/Remove/Edit affordances) and
`runFinished` (re-enable, refresh status). For the synchronous
gate (e.g. inside an `addProvider()` slot), it calls
`palmRuntime->isRunning()` directly. Plan assumption holds.

## Step 5 — Profile-switch teardown order

The plan's line numbers `:713-870` and `:871-885` are exact —
`loadProfile` lives at `kf6mainwindow.cpp:713-869` and
`closeProfile` at `:871-885`.

`loadProfile()` (relevant excerpt):

```cpp
713: void KF6MainWindow::loadProfile(const QString &path)
714: {
715:     if (m_currentProfile) {
716:         m_currentProfile->save();
717:         delete m_currentProfile;        // <-- old profile destroyed
718:         m_currentProfile = nullptr;
719:     }
...
754:     m_palmRuntime = std::make_unique<...PalmRuntime>(  // <-- new runtime
755:         m_currentProfile->stateDirectoryPath(), this);
```

`closeProfile()`:

```cpp
871: void KF6MainWindow::closeProfile()
872: {
873:     if (m_currentProfile) {
874:         m_currentProfile->save();
875:         delete m_currentProfile;
876:         m_currentProfile = nullptr;
877:     }
...
```

**Surprise / risk**: there is no current `m_palmRuntime.reset()`
before `delete m_currentProfile` in either method. `PalmRuntime`
is replaced wholesale at line 754 in `loadProfile`, but in
`closeProfile()` it is left dangling — `m_palmRuntime` survives
the profile delete.

For Phase Ic the design rule is: **`m_accountController` must be
reset BEFORE `delete m_currentProfile` at `loadProfile():717`,
and as the FIRST line of `closeProfile()` (before `:874`)** — AC
borrows `Profile*` and must not outlive it. This is consistent
with the plan; record the exact target lines.

## Step 6 — `rawfiles-cal` use in fixtures

`grep -B1 -A2 "rawfiles-cal" tests/runtime/*.cpp` finds three
files; in every match the literal is **seeded as input only**
(left-hand assignment to `targetBackend`):

- `tst_mapping_row_dialog.cpp:20` — `in.targetBackend = "rawfiles-cal";`
- `tst_mapping_editor_dialog.cpp` — sets `o["targetBackend"] = "rawfiles-cal"`.
- `tst_palm_runtime_reload_mappings.cpp` — sets `o["targetBackend"] = "rawfiles-cal"`.

A targeted check of QCOMPARE/QVERIFY on `targetBackend` in those
three files returns no hits, i.e. **no assertion-style use**:

```
$ grep -n "QCOMPARE.*targetBackend\|QVERIFY.*targetBackend" \
      tests/runtime/tst_mapping_*.cpp tests/runtime/tst_palm_runtime_reload_mappings.cpp
(no output)
```

`tst_mapping_row_dialog.cpp:33-40` round-trips the mapping
through the dialog and `QCOMPARE`s `id`, `sourceBackend`,
`sourceCalendar`, `targetCalendar`, `enabled`, `mode`,
`conflictPolicy` — `targetBackend` is deliberately omitted.

`tst_palm_runtime_modes.cpp` parameterises `tgtBackend` and never
uses the literal `"rawfiles-cal"`.

Conclusion: the existing fixtures will continue to pass when
`MappingRowDialog`'s target combo is generalised beyond
`rawfiles-cal`.

## Library target names

`tests/runtime/CMakeLists.txt:185` and `:241`:

```cmake
target_link_libraries(tst_mapping_row_dialog
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        Kalburator::Sync
        WildPalmsAppMapping)
...
target_link_libraries(tst_palm_runtime_modes
    PRIVATE
        Qt::Core Qt::Test
        PalmDeviceAccessLib
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsPalmDevice
        WildPalmsCore
        pisock bluetooth usb)
```

Confirmed library targets (replacing the plan's placeholders):

- `WildPalmsCore` — exists, used by `tst_palm_runtime_modes` and
  `tst_main_window_plugin_pages_populated` (plan's placeholder
  name was correct).
- `WildPalmsRuntime` — exists, used by
  `tst_main_window_plugin_pages_populated:231` (plan's placeholder
  was correct).
- `WildPalmsAppMapping` — additional target used by the
  mapping-dialog tests. Tests for `MappingRowDialog`'s extended
  target combo will need to link against this (and probably also
  `WildPalmsCore` for backend-registry access through
  `PalmRuntime`).
- `WildPalmsPalmDevice`, `PalmDeviceAccessLib` — palm-side libs,
  not needed for AccountController/AccountsPage tests.

So the plan's placeholders are essentially correct; new tests for
`MappingRowDialog` extension should link `WildPalmsAppMapping +
WildPalmsCore`, and tests for AccountController/AccountsPage will
likely link `WildPalmsCore + WildPalmsRuntime` (and Qt::Widgets
for dialog tests).

## Surprises

Three deviations from the plan's framing — none block proceeding,
but the plan should account for them:

1. **`WildPalmsSyncMappingHelper` does not exist as a class.** It
   appears only as a docstring reference in `profile.h:257`.
   Phase Ic's AccountController should not assume that helper —
   it should use `Kalburator::Sync::syncMappingFromJson` /
   `syncMappingToJson` directly, matching what
   `syncconfigstore_wp.cpp`, `palmruntime.cpp`, and
   `mappingeditordialog.cpp` already do.

2. **`reloadMappings`'s `!isRunning()` precondition is unenforced.**
   The header comment is documentation only; the implementation
   has no `Q_ASSERT` or early return. AccountController callers
   must gate explicitly via `palmRuntime->isRunning()` before
   pushing changes — no existing safety net.

3. **`closeProfile()` currently leaves `m_palmRuntime` alive
   across the profile delete.** This is an existing bug-ish
   pattern (PalmRuntime borrows nothing from Profile after
   construction beyond paths captured at construction, so it
   technically works), but Phase Ic adds AccountController which
   genuinely borrows `Profile*` — the plan's "reset AC FIRST in
   closeProfile" is critical and must not be elided.

Plus a path nit: **`SettingsDialog` is constructed at
`src/kf6/kf6mainwindow.cpp:2024`** (subdirectory `kf6/`), and
`onSettings` is the function name. The plan's prose "production
caller in `kf6mainwindow.cpp`" is accurate enough.

No surprise contradicts a load-bearing design assumption. The
implementation can proceed with Tasks 2+ as-planned, with the
above three notes folded into the AccountController interface
and the KF6MainWindow integration task.
