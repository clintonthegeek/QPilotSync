# F.1d — Device-binding Lifecycle Integrity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the three overlapping defects identified in the F.1d spec (Forget leaks, connectedDevice missing serial, two parallel registries) by unifying device-binding under `ProfileEntry.usbSerial`, populating fingerprints completely, and rendering the mismatch dialog from like-for-like data.

**Architecture:** `ProfileEntry` gains a `usbSerial` field; `ProfileRegistry` gains `findBySerial` + `bindSerial`. `KF6Settings`'s serial-registry surface is deleted (clean break — no migration). `DeviceFingerprint::compare()` returns a tri-state result so the mismatch dialog only fires on known contradiction.

**Tech Stack:** Qt6 + KF6 (KConfig, KSharedConfig), libkalburator types (BackendConfiguration), QtTest.

**Dependencies:** F.1a ✅, F.1b ✅, F.1c ✅.

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/runtime/profileregistry.h` | Modify | Add `usbSerial` to `ProfileEntry`; add `findBySerial` / `bindSerial`. |
| `src/runtime/profileregistry.cpp` | Modify | Persist + load `usbSerial`; implement new methods. |
| `src/profile.h` | Modify | Add `enum MatchResult`; add `compare()`; keep `matches()` as wrapper. Add `static QList<ComparisonRow> comparisonRows(a, b)`. |
| `src/palm/kpilotdevicelink.h` / `.cpp` | Modify | Expose `handshakeUsbSerial()` if not present (survey first). |
| `src/kf6/kf6mainwindow.h` / `.cpp` | Modify | (a) `onConnectionComplete` populates `connectedDevice.usbSerialNumber`; (b) `handleDeviceFingerprint` uses `compare()` and renders structured dialog on `MismatchKnown` only; (c) `onForgetProfile` — no change needed (unregister cascades the serial); (d) callsites that read `KF6Settings::findProfileBySerial` redirected to `ProfileRegistry::findBySerial`; (e) callsites that write via `KF6Settings::registerDeviceBySerial` redirected to `ProfileRegistry::bindSerial`. |
| `src/kf6/autosyncorchestrator.h` / `.cpp` | Modify | (a) Borrow `ProfileRegistry*`; (b) `handleUsbDevice` uses `ProfileRegistry::findBySerial`; (c) `createProfileForDevice` no longer seeds `Profile::deviceFingerprint`; binds via `ProfileRegistry::bindSerial`. |
| `src/kf6/kf6settings.h` / `.cpp` | Modify | Delete `registerDeviceBySerial` / `findProfileBySerial` / `unregisterDeviceBySerial` / `deviceSerialsGroup`. Optional: leave `migrateLegacyDeviceRegistry` (covers older configs). |
| `tests/runtime/tst_profileregistry_serial_binding.cpp` | Create | Round-trip + find + move-on-conflict + cascade-with-unregister. |
| `tests/runtime/tst_devicefingerprint_compare.cpp` | Create | Three-way `compare()` priority ladder + symmetric. |
| `tests/runtime/tst_kf6mainwindow_mismatch_dialog.cpp` | Create | Indeterminate doesn't open dialog; structured table for `MismatchKnown`. |
| `tests/runtime/tst_kf6mainwindow_forget_profile.cpp` | Modify | Add assertion: Forget removes the serial binding. |
| `tests/runtime/CMakeLists.txt` | Modify | Register the three new test executables. |
| `docs/plans/2026-04-20-libkalburator-integration.md` | Modify | Mark F.1d ✅ landed. |

---

## Task 1: `ProfileEntry.usbSerial` field + persistence (red → green)

**Files:**
- Create test: `tests/runtime/tst_profileregistry_serial_binding.cpp`
- Modify: `src/runtime/profileregistry.h`, `src/runtime/profileregistry.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test for round-trip persistence**

```cpp
// tests/runtime/tst_profileregistry_serial_binding.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"
#include "runtime/profileregistry.h"

#include <KSharedConfig>

using WildPalms::Runtime::ProfileRegistry;
using WildPalms::Runtime::ProfileEntry;

class TstProfileRegistrySerialBinding : public QObject {
    Q_OBJECT
private slots:
    void serialFieldDefaultsEmpty();
    void bindSerialPersists();
    void findBySerialReturnsBoundEntry();
    void bindSerialOnNewProfileWithoutSerial();
    void unregisterCascadesSerialBinding();
    void bindSerialMovesAcrossEntries();
};

namespace {
std::unique_ptr<ProfileRegistry> makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wprc"));
    auto r = std::make_unique<ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/root"));
    return r;
}
} // namespace

void TstProfileRegistrySerialBinding::serialFieldDefaultsEmpty()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("A"));
    QVERIFY(e.isValid());
    QCOMPARE(e.usbSerial, QString());
}

void TstProfileRegistrySerialBinding::bindSerialPersists()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto cfgPath = d.path() + QStringLiteral("/wprc");
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        ProfileRegistry reg(cfg);
        reg.setDefaultRoot(d.path() + QStringLiteral("/root"));
        const auto e = reg.registerNew(QStringLiteral("Palm m505"));
        QVERIFY(reg.bindSerial(e.id, QStringLiteral("L0JG14I11398")));
    }
    // Reopen and verify persistence.
    auto cfg = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg(cfg);
    const auto entries = reg.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().usbSerial, QStringLiteral("L0JG14I11398"));
}

void TstProfileRegistrySerialBinding::findBySerialReturnsBoundEntry()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("Palm"));
    QVERIFY(reg->bindSerial(e.id, QStringLiteral("SN-1")));
    const auto found = reg->findBySerial(QStringLiteral("SN-1"));
    QVERIFY(found.isValid());
    QCOMPARE(found.id, e.id);
}

void TstProfileRegistrySerialBinding::bindSerialOnNewProfileWithoutSerial()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    QVERIFY(!reg->findBySerial(QStringLiteral("missing")).isValid());
    QVERIFY(!reg->findBySerial(QString()).isValid());
}

void TstProfileRegistrySerialBinding::unregisterCascadesSerialBinding()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto e = reg->registerNew(QStringLiteral("A"));
    QVERIFY(reg->bindSerial(e.id, QStringLiteral("SN-A")));
    QVERIFY(reg->unregister(e.id));
    QVERIFY(!reg->findBySerial(QStringLiteral("SN-A")).isValid());
}

void TstProfileRegistrySerialBinding::bindSerialMovesAcrossEntries()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    const auto a = reg->registerNew(QStringLiteral("A"));
    const auto b = reg->registerNew(QStringLiteral("B"));
    QVERIFY(reg->bindSerial(a.id, QStringLiteral("SN-X")));
    QCOMPARE(reg->findBySerial(QStringLiteral("SN-X")).id, a.id);

    // Bind same serial to b: should move off a.
    QVERIFY(reg->bindSerial(b.id, QStringLiteral("SN-X")));
    QCOMPARE(reg->findBySerial(QStringLiteral("SN-X")).id, b.id);
    QCOMPARE(reg->entry(a.id).usbSerial, QString());
}

WILDPALMS_QTEST_MAIN(TstProfileRegistrySerialBinding)
#include "tst_profileregistry_serial_binding.moc"
```

- [ ] **Step 2: Register the test executable in `tests/runtime/CMakeLists.txt`**

```cmake
# F.1d T1 — ProfileRegistry serial binding
add_executable(tst_profileregistry_serial_binding tst_profileregistry_serial_binding.cpp)
target_link_libraries(tst_profileregistry_serial_binding
    PRIVATE
        Qt::Core
        Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_profileregistry_serial_binding COMMAND tst_profileregistry_serial_binding)
set_tests_properties(tst_profileregistry_serial_binding PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Run the test to confirm it fails (red)**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev --target tst_profileregistry_serial_binding 2>&1 | tail -10
```
Expected: compile failure — `ProfileEntry` has no `usbSerial`; `ProfileRegistry` has no `findBySerial`/`bindSerial`.

- [ ] **Step 4: Add `usbSerial` to `ProfileEntry` and implement `findBySerial`/`bindSerial`**

In `src/runtime/profileregistry.h`, add `QString usbSerial;` to the `ProfileEntry` struct after `lastOpened`. Add the two new method declarations:

```cpp
ProfileEntry findBySerial(const QString &usbSerial) const;
bool         bindSerial(const QString &id, const QString &usbSerial);
```

In `src/runtime/profileregistry.cpp`:
1. In `load()`, read `usbSerial` from each `[profile-<id>]` group:
   ```cpp
   e.usbSerial = grp.readEntry("usbSerial", QString());
   ```
2. In `save()`, write `usbSerial` for each entry:
   ```cpp
   if (!e.usbSerial.isEmpty())
       grp.writeEntry("usbSerial", e.usbSerial);
   else
       grp.deleteEntry("usbSerial");
   ```
3. Implement `findBySerial`:
   ```cpp
   ProfileEntry ProfileRegistry::findBySerial(const QString &usbSerial) const
   {
       if (usbSerial.isEmpty()) return {};
       for (const auto &e : m_cache)
           if (e.usbSerial == usbSerial) return e;
       return {};
   }
   ```
4. Implement `bindSerial`:
   ```cpp
   bool ProfileRegistry::bindSerial(const QString &id, const QString &usbSerial)
   {
       if (id.isEmpty()) return false;
       int targetIdx = -1;
       for (int i = 0; i < m_cache.size(); ++i)
           if (m_cache[i].id == id) { targetIdx = i; break; }
       if (targetIdx < 0) return false;

       // If another entry already owns this serial, clear it there first.
       if (!usbSerial.isEmpty()) {
           for (int i = 0; i < m_cache.size(); ++i) {
               if (i == targetIdx) continue;
               if (m_cache[i].usbSerial == usbSerial) {
                   m_cache[i].usbSerial.clear();
                   emit entryUpdated(m_cache[i].id);
               }
           }
       }
       if (m_cache[targetIdx].usbSerial == usbSerial) {
           // No-op (already bound).
           save();
           return true;
       }
       m_cache[targetIdx].usbSerial = usbSerial;
       save();
       emit entryUpdated(id);
       return true;
   }
   ```

- [ ] **Step 5: Run the test again (green)**

```bash
cmake --build build-dev --target tst_profileregistry_serial_binding 2>&1 | tail -5
cd build-dev && ctest -R tst_profileregistry_serial_binding --output-on-failure && cd ..
```
Expected: all 6 cases pass.

- [ ] **Step 6: Full suite still green**

```bash
cmake --build build-dev 2>&1 | tail -3
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```
Expected: 92 (pre-F.1d) + new test = 93 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/profileregistry.h src/runtime/profileregistry.cpp tests/runtime/tst_profileregistry_serial_binding.cpp tests/runtime/CMakeLists.txt
git commit -m "feat: ProfileEntry.usbSerial + findBySerial/bindSerial (F.1d T1)

Adds usbSerial field to ProfileEntry persisted in wildpalmsrc.
findBySerial(serial) returns the bound entry or invalid. bindSerial
sets or moves the binding (one serial cannot bind to two entries).
unregister cascades the binding implicitly (entry deletion removes
the field).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: `DeviceFingerprint::compare()` tri-state + `matches()` wrapper

**Files:**
- Create: `tests/runtime/tst_devicefingerprint_compare.cpp`
- Modify: `src/profile.h`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/runtime/tst_devicefingerprint_compare.cpp
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"
#include "profile.h"

using MR = DeviceFingerprint::MatchResult;

class TstDeviceFingerprintCompare : public QObject {
    Q_OBJECT
private slots:
    void serialPriorityMatch();
    void serialPriorityMismatch();
    void userIdFallbackWhenSerialEmptyOnOneSide();
    void userNameFallbackWhenIdEmptyOnOneSide();
    void indeterminateWhenNoOverlap();
    void symmetric();
    void matchesWrapperPreserved();
};

namespace {
DeviceFingerprint makeFp(const QString &serial = {},
                         quint32 userId = 0,
                         const QString &userName = {}) {
    DeviceFingerprint fp;
    fp.usbSerialNumber = serial;
    fp.userId = userId;
    fp.userName = userName;
    return fp;
}
} // namespace

void TstDeviceFingerprintCompare::serialPriorityMatch()
{
    auto a = makeFp(QStringLiteral("S1"), 1, QStringLiteral("X"));
    auto b = makeFp(QStringLiteral("S1"), 2, QStringLiteral("Y"));
    QCOMPARE(a.compare(b), MR::Match);   // serial wins
}

void TstDeviceFingerprintCompare::serialPriorityMismatch()
{
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S2"));
    QCOMPARE(a.compare(b), MR::MismatchKnown);
}

void TstDeviceFingerprintCompare::userIdFallbackWhenSerialEmptyOnOneSide()
{
    auto a = makeFp(QString(), 42);
    auto b = makeFp(QStringLiteral("S2"), 42);
    QCOMPARE(a.compare(b), MR::Match);   // serial check skipped; userId match
}

void TstDeviceFingerprintCompare::userNameFallbackWhenIdEmptyOnOneSide()
{
    auto a = makeFp(QString(), 0, QStringLiteral("clinton"));
    auto b = makeFp(QString(), 42, QStringLiteral("clinton"));
    QCOMPARE(a.compare(b), MR::Match);
}

void TstDeviceFingerprintCompare::indeterminateWhenNoOverlap()
{
    auto a = makeFp(QStringLiteral("S1"));   // serial only
    auto b = makeFp(QString(), 0, QStringLiteral("X"));   // username only
    QCOMPARE(a.compare(b), MR::Indeterminate);
}

void TstDeviceFingerprintCompare::symmetric()
{
    auto a = makeFp(QStringLiteral("S1"), 42);
    auto b = makeFp(QStringLiteral("S2"), 42);
    QCOMPARE(a.compare(b), b.compare(a));
}

void TstDeviceFingerprintCompare::matchesWrapperPreserved()
{
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S1"));
    QVERIFY(a.matches(b));
    auto c = makeFp(QStringLiteral("S2"));
    QVERIFY(!a.matches(c));
}

WILDPALMS_QTEST_MAIN(TstDeviceFingerprintCompare)
#include "tst_devicefingerprint_compare.moc"
```

- [ ] **Step 2: Register test executable**

```cmake
# F.1d T2 — DeviceFingerprint::compare()
add_executable(tst_devicefingerprint_compare tst_devicefingerprint_compare.cpp)
target_link_libraries(tst_devicefingerprint_compare
    PRIVATE
        Qt::Core Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmDevice
        KF6::I18n
        pisock bluetooth usb
)
add_test(NAME tst_devicefingerprint_compare COMMAND tst_devicefingerprint_compare)
set_tests_properties(tst_devicefingerprint_compare PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Confirm red**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev --target tst_devicefingerprint_compare 2>&1 | tail -10
```
Expected: no `compare()` / no `MatchResult` enum.

- [ ] **Step 4: Add `compare()` and `MatchResult` to `DeviceFingerprint`**

In `src/profile.h`, inside the `DeviceFingerprint` struct after `isEmpty()`:

```cpp
enum class MatchResult { Match, MismatchKnown, Indeterminate };

MatchResult compare(const DeviceFingerprint &other) const {
    if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty())
        return usbSerialNumber == other.usbSerialNumber
            ? MatchResult::Match : MatchResult::MismatchKnown;
    if (userId != 0 && other.userId != 0)
        return userId == other.userId
            ? MatchResult::Match : MatchResult::MismatchKnown;
    if (!userName.isEmpty() && !other.userName.isEmpty())
        return userName == other.userName
            ? MatchResult::Match : MatchResult::MismatchKnown;
    return MatchResult::Indeterminate;
}
```

`matches()` stays as-is (or rewrite as `return compare(other) == MatchResult::Match;` — both work; the spec preserves the wrapper).

- [ ] **Step 5: Verify green + full suite**

```bash
cmake --build build-dev --target tst_devicefingerprint_compare 2>&1 | tail -3
cd build-dev && ctest -R tst_devicefingerprint_compare && ctest 2>&1 | tail -5 && cd ..
```

- [ ] **Step 6: Commit**

```bash
git add src/profile.h tests/runtime/tst_devicefingerprint_compare.cpp tests/runtime/CMakeLists.txt
git commit -m "feat: DeviceFingerprint::compare() tri-state result (F.1d T2)

Returns Match / MismatchKnown / Indeterminate so the connection-time
mismatch dialog can distinguish 'these two fingerprints contradict'
from 'we don't have enough overlapping fields to say'. Legacy
matches() preserved as a Match-only wrapper for existing callers.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: `onConnectionComplete` populates `connectedDevice.usbSerialNumber`

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp`
- (Maybe) Modify: `src/palm/kpilotdevicelink.h` / `.cpp` (only if `handshakeUsbSerial()` doesn't exist).

- [ ] **Step 1: Survey: does `KPilotDeviceLink` expose a usbSerial getter?**

```bash
grep -nE "usbSerial|handshakeUsbSerial|deviceSerial|getUsbSerial" /home/clinton/dev/WildPalms/src/palm/kpilotdevicelink.h /home/clinton/dev/WildPalms/src/palm/kpilotdevicelink.cpp 2>/dev/null | head -10
```

If it does: use it directly. If not: skip to Step 2 to add a thin getter that exposes whatever the link already stores (which `AutoSyncOrchestrator` already discovers via `currentUsbSerial()` before connection). Worst-case fallback: read from `m_autoSync->currentUsbSerial()` only.

- [ ] **Step 2: Update `onConnectionComplete` to populate the serial**

Find the block (~`kf6mainwindow.cpp:928`):

```cpp
DeviceFingerprint connectedDevice;
connectedDevice.userId = userId;
connectedDevice.userName = userName;
if (deviceLink->handshakeSysInfoValid()) { ... }
```

Add immediately after `connectedDevice.userName = userName;`:

```cpp
// F.1d: always populate usbSerial so handleDeviceFingerprint can
// match against the registry entry's binding. Prefer the link's
// own value; fall back to the orchestrator's pre-connection scan.
if (deviceLink && !deviceLink->handshakeUsbSerial().isEmpty()) {
    connectedDevice.usbSerialNumber = deviceLink->handshakeUsbSerial();
} else if (m_autoSync && !m_autoSync->currentUsbSerial().isEmpty()) {
    connectedDevice.usbSerialNumber = m_autoSync->currentUsbSerial();
}
```

If Step 1 found no usbSerial getter on the link: skip the first branch; use only the orchestrator fallback. The link can grow a real getter later without changing the F.1d behavior.

- [ ] **Step 3: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -3
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```
Expected: all pass; no regressions.

- [ ] **Step 4: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp
git commit -m "fix: connectedDevice always carries usbSerialNumber (F.1d T3)

onConnectionComplete builds connectedDevice from the handshake but
never populated usbSerialNumber, even though the serial is known to
the system upstream (AutoSyncOrchestrator detects it during USB
scan). The omission was the proximate cause of the reported 'Wrong
Device' false-positive: expected fingerprint had serial-only,
connected had userName-only, DeviceFingerprint::matches saw no
overlap → false.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: Switch consumers from `KF6Settings::findProfileBySerial` to `ProfileRegistry::findBySerial`

**Files:**
- Modify: `src/kf6/autosyncorchestrator.h` / `.cpp`
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `src/kf6/kf6mainwindow.h` (pass ProfileRegistry* into orchestrator)

- [ ] **Step 1: Make `AutoSyncOrchestrator` borrow `ProfileRegistry*`**

In `src/kf6/autosyncorchestrator.h`, add a setter:

```cpp
void setProfileRegistry(WildPalms::Runtime::ProfileRegistry *registry);
```

And a private member `m_profileRegistry`.

In `src/kf6/autosyncorchestrator.cpp`, implement and use the borrowed registry in `handleUsbDevice` instead of `KF6Settings::findProfileBySerial`:

```cpp
if (!usbSerial.isEmpty() && m_profileRegistry) {
    const auto entry = m_profileRegistry->findBySerial(usbSerial);
    if (entry.isValid() && QDir(entry.path).exists()) {
        auto *p = new Profile(entry.path);
        if (p->exists()) { p->load(); profile = p; }
        else { delete p; }
    }
}
```

`createProfileForDevice` (around line 184-188) is updated in T5 (bind via ProfileRegistry there).

- [ ] **Step 2: Wire from `KF6MainWindow`**

In `kf6mainwindow.cpp` ctor (around line 138-156), after `m_autoSync = new AutoSyncOrchestrator(this);` and after `m_profileRegistry` is constructed in F.1a (it's currently constructed at line 98), add:

```cpp
m_autoSync->setProfileRegistry(m_profileRegistry.get());
```

(Verify construction order: ProfileRegistry created at line 98, AutoSync at line 139 — order is fine, hook after both.)

- [ ] **Step 3: Update `onConnectionComplete` no-profile branch (line ~955)**

```cpp
// Was: KF6Settings::instance().findProfileBySerial(...)
// Now:
const auto entry = m_profileRegistry->findBySerial(connectedDevice.usbSerialNumber);
QString knownProfile = entry.isValid() ? entry.path : QString();
```

- [ ] **Step 4: Update `handleDeviceFingerprint` Switch-Profile button (line ~1122)**

```cpp
const auto entry = m_profileRegistry->findBySerial(connectedDevice.usbSerialNumber);
QString profilePath = entry.isValid() ? entry.path : QString();
```

- [ ] **Step 5: Build + full suite (still green)**

```bash
cmake --build build-dev 2>&1 | tail -3
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```

- [ ] **Step 6: Commit**

```bash
git add src/kf6/autosyncorchestrator.h src/kf6/autosyncorchestrator.cpp src/kf6/kf6mainwindow.cpp src/kf6/kf6mainwindow.h
git commit -m "refactor: redirect serial→profile lookups to ProfileRegistry (F.1d T4)

Three consumers (AutoSyncOrchestrator::handleUsbDevice,
KF6MainWindow::onConnectionComplete no-profile branch,
KF6MainWindow::handleDeviceFingerprint Switch-Profile button) now
use ProfileRegistry::findBySerial instead of KF6Settings's serial
group. KF6Settings reads remain in place; deletion in T6.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: Switch writers from `KF6Settings::registerDeviceBySerial` to `ProfileRegistry::bindSerial`

**Files:**
- Modify: `src/kf6/autosyncorchestrator.cpp` (`createProfileForDevice`)
- Modify: `src/kf6/kf6mainwindow.cpp` (`registerDeviceWithCurrentProfile`)

- [ ] **Step 1: Update `createProfileForDevice`**

In `autosyncorchestrator.cpp` around line 138-141, remove the partial-fingerprint seed:

```cpp
// REMOVED:
// DeviceFingerprint fingerprint;
// fingerprint.userId = userId;
// fingerprint.userName = userName;
// fingerprint.usbSerialNumber = usbSerial;
// ...
// profile->setDeviceFingerprint(fingerprint);
```

The profile is constructed without a fingerprint section in `profile.conf [device]`. The fingerprint is written later by `registerDeviceWithCurrentProfile` after the first complete handshake.

Around line 184-188 (replace `KF6Settings::registerDeviceBySerial` call):

```cpp
// Was: settings.registerDeviceBySerial(usbSerial, finalPath);
// Now:
if (!usbSerial.isEmpty() && m_profileRegistry) {
    // The profile was just registered via constructor; look up its id.
    const auto entries = m_profileRegistry->entries();
    QString id;
    for (const auto &e : entries) {
        if (e.path == finalPath) { id = e.id; break; }
    }
    if (!id.isEmpty()) m_profileRegistry->bindSerial(id, usbSerial);
}
```

Note: `createProfileForDevice` currently writes to disk via `Profile::initialize()` then `Profile::save()` without going through `ProfileRegistry::registerNew`. **This needs to change** so `ProfileRegistry` knows about the new profile. The cleanest approach: refactor to `m_profileRegistry->registerNew(safeName)` (which now creates the dir + profile.conf for us per F.1a), then `bindSerial`, then load and set fingerprint after handshake. Plan T5 sub-step.

Plan T5 sub-step: replace the `Profile *profile = new Profile(finalPath); profile->setName(safeName); profile->initialize(); profile->save();` block with a call to `m_profileRegistry->registerNew(safeName)`, then load the returned entry's profile and return it from `createProfileForDevice` (existing return contract).

- [ ] **Step 2: Update `registerDeviceWithCurrentProfile` in `kf6mainwindow.cpp` (~line 1141)**

```cpp
void KF6MainWindow::registerDeviceWithCurrentProfile(const DeviceFingerprint &fingerprint)
{
    if (!m_currentProfile) return;

    m_currentProfile->setDeviceFingerprint(fingerprint);
    m_currentProfile->save();

    // F.1d: bind the serial onto ProfileRegistry's entry (replaces
    // KF6Settings::registerDeviceBySerial).
    if (!fingerprint.usbSerialNumber.isEmpty() && m_profileRegistry) {
        m_profileRegistry->bindSerial(
            m_currentProfile->id(), fingerprint.usbSerialNumber);
    }

    m_logWidget->logInfo(i18n("Device registered: %1", fingerprint.displayString()));
}
```

- [ ] **Step 3: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -3
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```

- [ ] **Step 4: Commit**

```bash
git add src/kf6/autosyncorchestrator.cpp src/kf6/kf6mainwindow.cpp
git commit -m "refactor: write serial bindings to ProfileRegistry (F.1d T5)

createProfileForDevice no longer seeds Profile::deviceFingerprint
with a partial (serial-only) fingerprint — full fingerprint is
written by registerDeviceWithCurrentProfile after the first
complete handshake. Both writer call sites now go through
ProfileRegistry::bindSerial. Combined with T3 (connectedDevice
always carries serial) this closes the false-positive mismatch path
in the F.1d spec §1 scenario.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6: Delete dead `KF6Settings` serial API

**Files:**
- Modify: `src/kf6/kf6settings.h` / `.cpp`

- [ ] **Step 1: Delete the four functions**

In `kf6settings.h`, delete declarations for:
- `void registerDeviceBySerial(const QString &usbSerial, const QString &profilePath);`
- `QString findProfileBySerial(const QString &usbSerial) const;`
- `void unregisterDeviceBySerial(const QString &usbSerial);`
- `KConfigGroup deviceSerialsGroup() const;` (if present)

In `kf6settings.cpp`, delete the corresponding implementations.

If `migrateLegacyDeviceRegistry()` exists (from Phase L Task 0.B), keep it — it covers older configs and is idempotent. Its target is now an unused config group, which is fine.

- [ ] **Step 2: Survey any leftover references**

```bash
grep -rn "registerDeviceBySerial\|findProfileBySerial\|unregisterDeviceBySerial\|deviceSerialsGroup" /home/clinton/dev/WildPalms/src /home/clinton/dev/WildPalms/tests 2>/dev/null
```
Expected: zero matches (T4 + T5 removed all callers). If anything remains, update it before compiling.

- [ ] **Step 3: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -10
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```
Expected: builds cleanly; all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/kf6/kf6settings.h src/kf6/kf6settings.cpp
git commit -m "refactor: delete KF6Settings serial-registry API (F.1d T6)

Clean break per spec §5 — no migration. The four functions
(registerDeviceBySerial / findProfileBySerial /
unregisterDeviceBySerial / deviceSerialsGroup) are gone; the
on-disk [DeviceSerials] group remains as dead data in existing
users' wildpalms.kf6settings.conf (harmless; can be hand-deleted).
migrateLegacyDeviceRegistry stays in place for older configs.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 7: Mismatch dialog renders structured comparison + only fires on `MismatchKnown`

**Files:**
- Create: `tests/runtime/tst_kf6mainwindow_mismatch_dialog.cpp`
- Modify: `src/kf6/kf6mainwindow.h` / `.cpp` (add test seam + rework dialog)
- Modify: `src/profile.h` (add `DeviceFingerprint::comparisonRows` helper)
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/runtime/tst_kf6mainwindow_mismatch_dialog.cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "../wildpalms_qtest_main.h"
#include "../../src/kf6/kf6mainwindow.h"
#include "profile.h"

class TstKf6MainWindowMismatchDialog : public QObject {
    Q_OBJECT
private slots:
    void indeterminateDoesNotOpenDialog();
    void matchDoesNotOpenDialog();
    void mismatchKnownPopulatesStructuredMessage();
};

namespace {
DeviceFingerprint makeFp(const QString &serial,
                         quint32 userId = 0,
                         const QString &userName = {},
                         const QString &modelName = {}) {
    DeviceFingerprint fp;
    fp.usbSerialNumber = serial;
    fp.userId = userId;
    fp.userName = userName;
    fp.modelName = modelName;
    return fp;
}

// Capturing override on KF6MainWindow that returns the dialog payload
// without exec'ing.
struct CapturingMainWindow : public KF6MainWindow {
    QString lastMessage;
    int     dialogOpens = 0;
protected:
    QString renderMismatchMessageForTest(const DeviceFingerprint &a,
                                          const DeviceFingerprint &b) override {
        ++dialogOpens;
        lastMessage = KF6MainWindow::renderMismatchMessageForTest(a, b);
        return lastMessage;
    }
};
} // namespace

void TstKf6MainWindowMismatchDialog::matchDoesNotOpenDialog()
{
    CapturingMainWindow win;
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S1"));
    QCOMPARE(win.runMismatchCheckForTest(a, b), true);   // proceed
    QCOMPARE(win.dialogOpens, 0);
}

void TstKf6MainWindowMismatchDialog::indeterminateDoesNotOpenDialog()
{
    CapturingMainWindow win;
    auto a = makeFp(QStringLiteral("S1"));   // serial only
    auto b = makeFp(QString(), 0, QStringLiteral("user"));   // username only
    QCOMPARE(win.runMismatchCheckForTest(a, b), true);   // indeterminate → proceed
    QCOMPARE(win.dialogOpens, 0);
}

void TstKf6MainWindowMismatchDialog::mismatchKnownPopulatesStructuredMessage()
{
    auto a = makeFp(QStringLiteral("S1"), 0, QStringLiteral("alice"));
    auto b = makeFp(QStringLiteral("S2"), 0, QStringLiteral("bob"));
    const QString msg = KF6MainWindow::renderMismatchMessageForTest(a, b);
    // Structured table-ish output.
    QVERIFY(msg.contains(QStringLiteral("Serial"), Qt::CaseInsensitive));
    QVERIFY(msg.contains(QStringLiteral("User"),   Qt::CaseInsensitive));
    QVERIFY(msg.contains(QStringLiteral("S1")));
    QVERIFY(msg.contains(QStringLiteral("S2")));
    QVERIFY(msg.contains(QStringLiteral("alice")));
    QVERIFY(msg.contains(QStringLiteral("bob")));
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowMismatchDialog)
#include "tst_kf6mainwindow_mismatch_dialog.moc"
```

- [ ] **Step 2: Register test executable**

```cmake
# F.1d T7 — mismatch dialog
add_executable(tst_kf6mainwindow_mismatch_dialog tst_kf6mainwindow_mismatch_dialog.cpp)
target_link_libraries(tst_kf6mainwindow_mismatch_dialog
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore KF6::XmlGui
        pisock bluetooth usb
)
add_test(NAME tst_kf6mainwindow_mismatch_dialog COMMAND tst_kf6mainwindow_mismatch_dialog)
set_tests_properties(tst_kf6mainwindow_mismatch_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Confirm red**

Build fails: `KF6MainWindow` has no `renderMismatchMessageForTest`, no `runMismatchCheckForTest`.

- [ ] **Step 4: Add the `comparisonRows` helper to `DeviceFingerprint`**

In `src/profile.h`, after `displayString()`:

```cpp
struct ComparisonRow { QString label; QString lhs; QString rhs; };

static QList<ComparisonRow>
comparisonRows(const DeviceFingerprint &expected,
               const DeviceFingerprint &connected) {
    auto orDash = [](const QString &s) {
        return s.isEmpty() ? QStringLiteral("—") : s;
    };
    auto idStr = [](quint32 id) {
        return id == 0 ? QString() : QString::number(id);
    };
    QList<ComparisonRow> out;
    out.append({ QObject::tr("Serial"),
                 orDash(expected.usbSerialNumber),
                 orDash(connected.usbSerialNumber) });
    out.append({ QObject::tr("User"),
                 orDash(expected.userName),
                 orDash(connected.userName) });
    out.append({ QObject::tr("User ID"),
                 orDash(idStr(expected.userId)),
                 orDash(idStr(connected.userId)) });
    out.append({ QObject::tr("Model"),
                 orDash(expected.modelName),
                 orDash(connected.modelName) });
    return out;
}
```

- [ ] **Step 5: Add test seams to `KF6MainWindow`**

In `kf6mainwindow.h`:

```cpp
public:
    // F.1d test seam — render the mismatch dialog message without exec'ing.
    static QString renderMismatchMessageForTest(
        const DeviceFingerprint &expected,
        const DeviceFingerprint &connected);

    // F.1d test seam — run mismatch check; returns proceed/reject.
    // Match + Indeterminate → returns true with no UI. MismatchKnown →
    // calls the (virtual) renderMismatchMessageForTest path but does not
    // actually open a QMessageBox in test mode.
    bool runMismatchCheckForTest(
        const DeviceFingerprint &expected,
        const DeviceFingerprint &connected);

protected:
    virtual QString renderMismatchMessageForTest(
        const DeviceFingerprint &expected,
        const DeviceFingerprint &connected) const;
```

In `kf6mainwindow.cpp`, implement:

```cpp
QString KF6MainWindow::renderMismatchMessageForTest(
    const DeviceFingerprint &expected,
    const DeviceFingerprint &connected) const
{
    QString msg;
    msg += i18n("This profile is associated with a different Palm device.\n\n");
    msg += QStringLiteral("<table>");
    msg += QStringLiteral("<tr><th></th><th>%1</th><th>%2</th></tr>")
        .arg(i18n("Expected"), i18n("Connected"));
    const auto rows = DeviceFingerprint::comparisonRows(expected, connected);
    for (const auto &r : rows) {
        msg += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>%3</td></tr>")
            .arg(r.label.toHtmlEscaped(),
                 r.lhs.toHtmlEscaped(),
                 r.rhs.toHtmlEscaped());
    }
    msg += QStringLiteral("</table>");
    return msg;
}

bool KF6MainWindow::runMismatchCheckForTest(
    const DeviceFingerprint &expected,
    const DeviceFingerprint &connected)
{
    using MR = DeviceFingerprint::MatchResult;
    const auto r = expected.compare(connected);
    if (r == MR::Match || r == MR::Indeterminate) return true;
    // MismatchKnown: render message (test seam captures); test version
    // does NOT exec the QMessageBox.
    (void)renderMismatchMessageForTest(expected, connected);
    return false;
}
```

- [ ] **Step 6: Rewrite `handleDeviceFingerprint`**

Around line 1042-1112, replace the body's middle section:

```cpp
const auto result = expectedDevice.compare(connectedDevice);
if (result == DeviceFingerprint::MatchResult::Match) {
    m_logWidget->logInfo(i18n("Device fingerprint verified"));
    // ... existing merge-extended-info loop ...
    return true;
}
if (result == DeviceFingerprint::MatchResult::Indeterminate) {
    m_logWidget->logInfo(
        i18n("Device fingerprint indeterminate — assuming match"));
    registerDeviceWithCurrentProfile(connectedDevice);
    return true;
}
// MismatchKnown: open the structured dialog.
QString message = renderMismatchMessageForTest(expectedDevice, connectedDevice);

QMessageBox msgBox(this);
msgBox.setWindowTitle(i18n("Wrong Device"));
msgBox.setTextFormat(Qt::RichText);
msgBox.setText(message);
msgBox.setIcon(QMessageBox::Warning);
// ... existing three buttons ...
```

- [ ] **Step 7: Build + run**

```bash
cmake --build build-dev --target tst_kf6mainwindow_mismatch_dialog 2>&1 | tail -10
cd build-dev && ctest -R tst_kf6mainwindow_mismatch_dialog --output-on-failure 2>&1 | tail -10 && cd ..
```
Expected: 3 tests pass.

- [ ] **Step 8: Full suite**

```bash
cmake --build build-dev 2>&1 | tail -3
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```

- [ ] **Step 9: Commit**

```bash
git add src/profile.h src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp tests/runtime/tst_kf6mainwindow_mismatch_dialog.cpp tests/runtime/CMakeLists.txt
git commit -m "feat: mismatch dialog renders like-for-like comparison (F.1d T7)

handleDeviceFingerprint uses DeviceFingerprint::compare; Match and
Indeterminate proceed silently (the latter accepts insufficient
evidence as 'good enough' until the next handshake fills in
fields). Only MismatchKnown opens the dialog, which now renders a
fixed-shape comparison (Serial / User / User ID / Model rows with
'—' placeholders) instead of two free-form display strings of
different shapes.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 8: Extend `tst_kf6mainwindow_forget_profile` to assert serial-binding removal

**Files:**
- Modify: `tests/runtime/tst_kf6mainwindow_forget_profile.cpp`

- [ ] **Step 1: Add the assertion**

Find the existing forget-with-delete test method. After `onForgetProfile` is called and asserted (or whatever pattern the file uses), add:

```cpp
// F.1d: forget cascades the serial binding.
// Prior step bound a serial to the profile being forgotten.
QVERIFY(!reg->findBySerial(boundSerial).isValid());
```

If the existing test doesn't bind a serial before forgetting, also add a step earlier to bind one (so the assertion is meaningful):

```cpp
QVERIFY(reg->bindSerial(targetEntry.id, QStringLiteral("F1D-SN-TEST")));
// ... existing forget logic ...
QVERIFY(!reg->findBySerial(QStringLiteral("F1D-SN-TEST")).isValid());
```

- [ ] **Step 2: Build + run**

```bash
cmake --build build-dev --target tst_kf6mainwindow_forget_profile 2>&1 | tail -5
cd build-dev && ctest -R tst_kf6mainwindow_forget_profile && cd ..
```

- [ ] **Step 3: Commit**

```bash
git add tests/runtime/tst_kf6mainwindow_forget_profile.cpp
git commit -m "test: forget cascades serial binding (F.1d T8)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 9: Push + integration plan update + memory

**Files:**
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`
- Modify: `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + new memory file

- [ ] **Step 1: Push everything**

```bash
git push 2>&1 | tail -3
```

- [ ] **Step 2: Update the integration plan**

In `docs/plans/2026-04-20-libkalburator-integration.md`, find the F.1c reference. Add an F.1d line directly after:

```markdown
- F.1d (device-binding lifecycle integrity) ✅ landed 2026-05-23.
  Fixes the latent gap from F.1a where `KF6Settings DeviceSerials`
  shadow registry was left in place; folds device-binding into
  `ProfileEntry.usbSerial`, drops the legacy registry, and reworks
  `DeviceFingerprint::compare()` to surface insufficient-evidence
  vs known-contradiction so the mismatch dialog only fires when a
  contradiction is provable. Spec:
  `docs/superpowers/specs/2026-05-23-f1d-device-binding-lifecycle-design.md`.
  Plan: `docs/superpowers/plans/2026-05-23-f1d-device-binding.md`.
```

Update the Phase F status header:

```markdown
**In progress.** F.1a ✅ + F.1b ✅ + F.2 ✅ done 2026-05-22; F.1c ✅ done 2026-05-23; F.1d ✅ done 2026-05-23. F.3 / F.4 pending.
```

- [ ] **Step 3: Add memory record**

Create `~/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_f1d_device_binding.md` with type=project, body recording: ProfileEntry now carries usbSerial; KF6Settings serial API removed; DeviceFingerprint::compare returns tri-state; mismatch dialog only fires on contradiction. Append to `MEMORY.md` index.

- [ ] **Step 4: Commit + push**

```bash
git add -f docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "docs: integration plan — F.1d ✅ landed 2026-05-23"
git push 2>&1 | tail -3
```

---

## Self-Review Notes

- **Spec coverage:** Every section/requirement in the F.1d spec is reachable from a task. §2.1 in-scope items map to T1 (usbSerial field + findBySerial/bindSerial), T3 (connectedDevice fix), T5 (createProfileForDevice discipline + writer redirect), T6 (drop KF6Settings API), T2 (compare tri-state), T7 (dialog data fix + indeterminate suppression). Forget atomicity (§2.1 "Forget atomically clears the binding") is automatic because §4.2 puts usbSerial *on* the ProfileEntry; T8 just adds the test for it.
- **Placeholder scan:** No "TBD"/"TODO". Step 4 of Task 5 has a sub-decision ("if `createProfileForDevice` should use registerNew") — the plan picks the approach inline. T3 has a survey step for `handshakeUsbSerial` with a defined fallback.
- **Type consistency:** `MatchResult` enum names (T2) reused in T7. `ComparisonRow` fields (label/lhs/rhs) consistent between profile.h (T7 Step 4) and kf6mainwindow.cpp (T7 Step 5). `ProfileEntry.usbSerial` (T1) used identically by T4 (read), T5 (write), T8 (test).
