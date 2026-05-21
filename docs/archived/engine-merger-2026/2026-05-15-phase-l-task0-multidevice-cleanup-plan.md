# Phase L Task 0 — Multi-Device Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land three small WildPalms-side cleanups before the Akonadi work in `2026-05-15-phase-l-akonadi-plan.md`: (A) replace `AutoSyncOrchestrator`'s silent auto-create-profile path with a confirmation dialog, (B) consolidate two parallel device→profile registries into the serial-keyed one with a one-time migration, and (C) delete the aspirational "Multiple Device Support" section from `WildPalms/docs/ROADMAP.md`.

**Architecture:** Three independent commits on `refactor/engine-merger` in the WildPalms worktree. No libkalburator-side code changes; no engine changes. The `DeviceFingerprint` struct itself stays (it backs the mismatch-warning dialog and dashboard panels). All work happens in `WildPalms/src/kf6/`, `WildPalms/src/`, and `WildPalms/tests/`.

**Tech Stack:** C++20, Qt6, KF6 (`KConfig`, `KSharedConfig`, `KConfigGroup`), KF6 i18n macros, Qt Test, `QMessageBox::question` (matching the existing pattern at `kf6mainwindow.cpp:870`).

**Pair docs:**
- Design: `2026-05-15-phase-l-multidevice-cleanup-design.md` (alongside this plan)
- Parent plan: `2026-05-15-phase-l-akonadi-plan.md` (Tasks 1+ proceed after Task 0 lands)

---

## Pre-flight (run once before Task 0.A)

Verify clean state. If any of these fail, stop and resolve before beginning.

- [ ] **Step 0.0.1: Confirm working directory**

```bash
cd /home/clinton/dev/refactor-engine-merger
git -C WildPalms status
git -C WildPalms branch --show-current
```

Expected output: branch is `refactor/engine-merger`; only the libkalburator submodule pointer (already in `m libkalburator`) is dirty, no other working-tree changes in WildPalms.

- [ ] **Step 0.0.2: Confirm baseline is green**

```bash
./scripts/verify-all.sh
```

Expected: exit code 0; "all green, no flips". WildPalms test count = 71, libkalburator 92/92, PlanStan 82/105. If anything is red, do NOT proceed — the K.9 follow-up #3 work should leave a green tree.

- [ ] **Step 0.0.3: Confirm the doomed methods are still present**

```bash
grep -n "registerDevice\b\|findProfileForDevice\b\|deviceRegistry\b\|clearDeviceRegistry\b\|registryKey\b\|fromRegistryKey\b" \
    WildPalms/src/kf6/kf6settings.h \
    WildPalms/src/kf6/kf6settings.cpp \
    WildPalms/src/profile.h | wc -l
```

Expected: ≥ 14 hits (5 method declarations in `kf6settings.h`, 5 definitions in `kf6settings.cpp`, 2 helpers in `profile.h`; plus duplicates from header guards / forward decls). Non-zero confirms the cleanup target exists.

---

## Task 0.A: Confirmation-dialog rework for new-device path

**Files:**
- Modify: `WildPalms/src/kf6/autosyncorchestrator.h`
- Modify: `WildPalms/src/kf6/autosyncorchestrator.cpp`
- Modify: `WildPalms/src/kf6/kf6mainwindow.h`
- Modify: `WildPalms/src/kf6/kf6mainwindow.cpp`
- Create: `WildPalms/tests/test_autosyncorchestrator_unregistered.cpp`
- Modify: `WildPalms/tests/CMakeLists.txt` (register new test)

**Behavior change:** `findOrCreateProfile(serial, name, id)` today does steps 1–5 (resolve path, init Profile, save, register, log) atomically with no user consent. Split into:

- `onPalmDetected(...)` (existing slot) — on serial-miss, emit new signal `unregisteredDeviceDetected(usbSerial, userName, userId)`, do not create anything.
- `createProfileForDevice(usbSerial, userName, userId)` (new public method) — the body of today's steps 1–5; called from the dialog accept path.
- `findOrCreateProfile(...)` (existing) — thin wrapper that calls `createProfileForDevice(...)` directly. Retained because test fixtures and any non-UI bootstrap paths use it.

`KF6MainWindow` gains a slot `onUnregisteredDeviceDetected(serial, userName, userId)` that shows a `QMessageBox::question` modal; on Yes, calls `m_autoSyncOrchestrator->createProfileForDevice(serial, userName, userId)`.

### Steps

- [ ] **Step 0.A.1: Write the failing test**

Create `WildPalms/tests/test_autosyncorchestrator_unregistered.cpp`:

```cpp
/**
 * @file test_autosyncorchestrator_unregistered.cpp
 * @brief Tests for AutoSyncOrchestrator's new unregistered-device confirmation path
 *
 * Verifies that detecting an unrecognised Palm device does NOT silently
 * create a profile; instead it emits unregisteredDeviceDetected so that
 * the UI can prompt the user.
 */

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include "kf6/autosyncorchestrator.h"
#include "profile.h"

class TestAutoSyncOrchestratorUnregistered : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void testUnregisteredEmitsSignalAndDoesNotCreate();
    void testCreateProfileForDeviceCreatesProfile();

private:
    QTemporaryDir *m_tempDir = nullptr;
};

void TestAutoSyncOrchestratorUnregistered::initTestCase()
{
    // Isolate config and home so we don't pollute the real config.
    // The test executable's CMakeLists sets XDG_CONFIG_HOME + HOME.
    qDebug() << "HOME=" << qgetenv("HOME");
}

void TestAutoSyncOrchestratorUnregistered::cleanup()
{
    delete m_tempDir;
    m_tempDir = nullptr;
}

void TestAutoSyncOrchestratorUnregistered::testUnregisteredEmitsSignalAndDoesNotCreate()
{
    AutoSyncOrchestrator orch;

    QSignalSpy spy(&orch, &AutoSyncOrchestrator::unregisteredDeviceDetected);
    QVERIFY(spy.isValid());

    // Drive the slot directly (no PalmDeviceMonitor needed for this assertion).
    QMetaObject::invokeMethod(&orch, "onPalmDetected", Qt::DirectConnection,
                              Q_ARG(QStringList, QStringList{ "/dev/ttyUSB0" }),
                              Q_ARG(QString, QStringLiteral("UNKNOWN-SN-12345")));

    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("UNKNOWN-SN-12345"));

    // And: no PalmSync/* subdir was created in $HOME.
    QDir palmSyncDir(QDir::homePath() + QStringLiteral("/PalmSync"));
    if (palmSyncDir.exists()) {
        QStringList entries = palmSyncDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY2(entries.isEmpty(),
                 qPrintable(QStringLiteral("Unexpected profile dirs created: ") + entries.join(", ")));
    }
}

void TestAutoSyncOrchestratorUnregistered::testCreateProfileForDeviceCreatesProfile()
{
    AutoSyncOrchestrator orch;

    QSignalSpy created(&orch, &AutoSyncOrchestrator::profileCreated);
    QVERIFY(created.isValid());

    Profile *p = orch.createProfileForDevice(
        QStringLiteral("CONFIRMED-SN-99999"),
        QStringLiteral("TestUser"),
        42u);

    QVERIFY(p != nullptr);
    QCOMPARE(created.count(), 1);
    QVERIFY(QDir(p->syncFolderPath()).exists());
    delete p;
}

QTEST_MAIN(TestAutoSyncOrchestratorUnregistered)
#include "test_autosyncorchestrator_unregistered.moc"
```

- [ ] **Step 0.A.2: Register the test in CMake (use isolated HOME)**

Edit `WildPalms/tests/CMakeLists.txt`. After the `test_profile_sidecar_migration` block (~line 100), append:

```cmake
# Phase L Task 0.A: AutoSyncOrchestrator unregistered-device confirmation path
add_wildpalms_test(test_autosyncorchestrator_unregistered
    test_autosyncorchestrator_unregistered.cpp
)
set_tests_properties(test_autosyncorchestrator_unregistered PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${CMAKE_BINARY_DIR}/test-home-autosync;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-xdg-autosync"
)
```

The `HOME` override is what isolates the `~/PalmSync/` directory the orchestrator writes to.

- [ ] **Step 0.A.3: Configure + build, watch the test fail**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -20
```

Expected: compile error on `&AutoSyncOrchestrator::unregisteredDeviceDetected` (signal does not exist yet) and on `orch.createProfileForDevice(...)` (method does not exist yet). This is the "failing test" of TDD step 1 — the failure is at compile time.

- [ ] **Step 0.A.4: Add the new signal + public method declaration**

Edit `WildPalms/src/kf6/autosyncorchestrator.h`. Replace the existing `Q_SIGNALS:` block with:

```cpp
Q_SIGNALS:
    /** Emitted when a device is detected and profile resolved (profile may be nullptr for unknown devices) */
    void deviceDetected(Profile *profile, const QStringList &ports);

    /** Emitted when a new profile is auto-created */
    void profileCreated(const QString &profilePath, const QString &userName);

    /**
     * @brief Emitted when an unrecognised Palm device is detected.
     *
     * Replaces the previous silent auto-create-profile behavior. The UI is
     * expected to connect to this signal and prompt the user; on consent,
     * it calls createProfileForDevice() to actually create the profile.
     */
    void unregisteredDeviceDetected(const QString &usbSerial,
                                    const QString &userName,
                                    quint32 userId);

    void error(const QString &message);
    void statusChanged(const QString &status);
```

In the same header, just after the `findOrCreateProfile(...)` declaration, add:

```cpp
    /**
     * @brief Create + register a profile for a confirmed-new device.
     *
     * Called by the UI after the user confirms creation in the
     * unregistered-device dialog. Does NOT prompt itself.
     * @return the newly-created Profile (caller takes ownership), or
     *         nullptr if profile init failed.
     */
    Profile* createProfileForDevice(const QString &usbSerial,
                                    const QString &userName,
                                    quint32 userId);
```

- [ ] **Step 0.A.5: Extract createProfileForDevice from findOrCreateProfile**

Edit `WildPalms/src/kf6/autosyncorchestrator.cpp`. After the existing `findOrCreateProfile(...)` body (line 218), append:

```cpp
Profile* AutoSyncOrchestrator::createProfileForDevice(const QString &usbSerial,
                                                       const QString &userName,
                                                       quint32 userId)
{
    KF6Settings &settings = KF6Settings::instance();

    DeviceFingerprint fingerprint;
    fingerprint.userId = userId;
    fingerprint.userName = userName;
    fingerprint.usbSerialNumber = usbSerial;

    QString safeName = userName.isEmpty() ? QStringLiteral("PalmUser") : userName;
    safeName.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                     QStringLiteral("_"));

    QString basePath = QDir::homePath() + QStringLiteral("/PalmSync/") + safeName;
    QString finalPath = basePath;
    int suffix = 1;
    while (QDir(finalPath).exists()) {
        Profile existingProfile(finalPath);
        if (existingProfile.exists()) {
            existingProfile.load();
            DeviceFingerprint existingFp = existingProfile.deviceFingerprint();
            if (existingFp.isEmpty() || existingFp.matches(fingerprint)) {
                break;
            }
        } else {
            break;
        }
        finalPath = basePath + QStringLiteral("_%1").arg(suffix++);
    }

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Creating new profile at: %1").arg(finalPath));
    }

    auto *profile = new Profile(finalPath);
    profile->setName(safeName);
    profile->setDeviceFingerprint(fingerprint);

    if (!profile->initialize()) {
        if (m_logWidget) {
            m_logWidget->logError(
                QStringLiteral("Failed to initialize profile at: %1").arg(finalPath));
        }
        delete profile;
        return nullptr;
    }

    profile->save();

    // Register only by USB serial (Task 0.B removes the fingerprint-keyed registry).
    if (!usbSerial.isEmpty()) {
        settings.registerDeviceBySerial(usbSerial, finalPath);
    }
    settings.addRecentProfile(finalPath);
    settings.setDefaultProfilePath(finalPath);
    settings.sync();

    Q_EMIT profileCreated(finalPath, userName);

    return profile;
}
```

Note: the body intentionally uses **only** `registerDeviceBySerial` — the fingerprint-keyed `registerDevice` call is dropped here as forward-prep for Task 0.B. Until Task 0.B lands, `findOrCreateProfile` continues to call both (preserving current behavior for the wrapper).

- [ ] **Step 0.A.6: Modify findOrCreateProfile to delegate to createProfileForDevice**

In the same file, replace the auto-create block of `findOrCreateProfile(...)` (lines 158–217 — from `// 3. No profile found -- auto-create one` through the closing `return profile;`) with:

```cpp
    // 3. No profile found -- auto-create one via the now-shared path.
    return createProfileForDevice(usbSerial, userName, userId);
}
```

Keep the two registry-lookup branches at the top of the function unchanged (steps 1 and 2). Result: `findOrCreateProfile` is now ~30 LOC instead of ~100, and it shares the create code with `createProfileForDevice`.

- [ ] **Step 0.A.7: Modify onPalmDetected to emit signal instead of probing fingerprint registry**

In the same file, replace the body of `onPalmDetected(...)` (lines 43–109) with:

```cpp
void AutoSyncOrchestrator::onPalmDetected(const QStringList &ports, const QString &usbSerial)
{
    if (m_busy) {
        if (m_logWidget) {
            m_logWidget->logWarning(
                QStringLiteral("Palm detected but already busy - ignoring"));
        }
        return;
    }

    m_busy = true;
    m_currentUsbSerial = usbSerial;

    Q_EMIT statusChanged(tr("Palm device detected..."));

    if (m_logWidget) {
        m_logWidget->logInfo(
            QStringLiteral("Palm detected (S/N: %1) on %2 port(s)")
                .arg(usbSerial.isEmpty() ? QStringLiteral("unknown") : usbSerial)
                .arg(ports.size()));
    }

    // Resolve profile by USB serial only (fingerprint-keyed registry
    // removed in Task 0.B; the serial registry is the sole lookup).
    Profile *profile = nullptr;
    KF6Settings &settings = KF6Settings::instance();

    if (!usbSerial.isEmpty()) {
        QString profilePath = settings.findProfileBySerial(usbSerial);
        if (!profilePath.isEmpty() && QDir(profilePath).exists()) {
            auto *p = new Profile(profilePath);
            if (p->exists()) {
                p->load();
                profile = p;
                if (m_logWidget) {
                    m_logWidget->logInfo(
                        QStringLiteral("Found profile by serial: %1").arg(profilePath));
                }
            } else {
                delete p;
            }
        }
    }

    if (profile) {
        Q_EMIT deviceDetected(profile, ports);
    } else {
        // Unrecognised device: do NOT silently auto-create. UI prompts.
        if (m_logWidget) {
            m_logWidget->logInfo(
                QStringLiteral("Unrecognised Palm (S/N: %1) — prompting user").arg(usbSerial));
        }
        Q_EMIT unregisteredDeviceDetected(usbSerial, QString(), 0u);
        Q_EMIT deviceDetected(nullptr, ports);
    }

    m_busy = false;
}
```

Note: `userName`/`userId` aren't known yet at USB-detect time (those come from the handshake). The signal carries empty strings and 0 for them; the UI shows the dialog with serial only, and on accept passes through to `createProfileForDevice` which uses defaults.

- [ ] **Step 0.A.8: Run the test, verify it passes**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -10
ctest --test-dir build -R test_autosyncorchestrator_unregistered --output-on-failure
```

Expected: 1 test executable, 2 sub-cases, both PASS.

- [ ] **Step 0.A.9: Wire the UI slot in KF6MainWindow**

Edit `WildPalms/src/kf6/kf6mainwindow.h`. In the `private Q_SLOTS:` section, add:

```cpp
    void onUnregisteredDeviceDetected(const QString &usbSerial,
                                       const QString &userName,
                                       quint32 userId);
```

Edit `WildPalms/src/kf6/kf6mainwindow.cpp`. Find the existing wiring of the orchestrator (search for `m_autoSyncOrchestrator->`, the `connect(` calls around the constructor — likely near a `deviceDetected` wiring). Add a matching connect:

```cpp
    connect(m_autoSyncOrchestrator, &AutoSyncOrchestrator::unregisteredDeviceDetected,
            this, &KF6MainWindow::onUnregisteredDeviceDetected);
```

Then add the slot implementation (place it near `onDeviceReady`, line 1176, for code-locality):

```cpp
void KF6MainWindow::onUnregisteredDeviceDetected(const QString &usbSerial,
                                                  const QString &userName,
                                                  quint32 userId)
{
    Q_UNUSED(userName)
    Q_UNUSED(userId)

    QString prompt = i18n("An unrecognised Palm device was detected.\n\n"
                          "USB serial: %1\n\n"
                          "Create a new profile for this device?",
                          usbSerial.isEmpty() ? i18n("(unknown)") : usbSerial);

    int ret = QMessageBox::question(this, i18n("New Palm Device"), prompt,
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::Yes);
    if (ret != QMessageBox::Yes) {
        if (m_logWidget) {
            m_logWidget->logInfo(i18n("User declined to create profile for new device."));
        }
        return;
    }

    Profile *p = m_autoSyncOrchestrator->createProfileForDevice(
        usbSerial, userName, userId);
    if (!p) {
        QMessageBox::warning(this, i18n("Profile Creation Failed"),
                             i18n("Could not create a profile for this device."));
        return;
    }
    // Hand off to the normal "device detected with profile" path.
    Q_EMIT m_autoSyncOrchestrator->deviceDetected(p, QStringList{ m_autoSyncOrchestrator->m_currentUsbSerial });
}
```

Wait — the last line accesses a private member. Cleaner: have the slot just emit our own internal signal, or add a public accessor `currentUsbSerial()` to `AutoSyncOrchestrator`. Adopt the public-accessor option:

In `WildPalms/src/kf6/autosyncorchestrator.h`, in the public section:

```cpp
    QString currentUsbSerial() const { return m_currentUsbSerial; }
```

Then in the slot above, change the last line to:

```cpp
    Q_EMIT m_autoSyncOrchestrator->deviceDetected(p, QStringList{ m_autoSyncOrchestrator->currentUsbSerial() });
```

(The friend-emit is unusual but matches the pattern used elsewhere in this file — `emit otherObject->signal()` works because emit is just function-call syntax for signals.)

- [ ] **Step 0.A.10: Build everything, confirm no regressions**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -10
ctest --test-dir build --output-on-failure 2>&1 | tail -20
```

Expected: build clean, 72 tests pass (was 71; +1 for the new test executable). If anything else flips red, halt and investigate.

- [ ] **Step 0.A.11: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/kf6/autosyncorchestrator.h src/kf6/autosyncorchestrator.cpp \
        src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp \
        tests/test_autosyncorchestrator_unregistered.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Phase L Task 0.A: confirm before creating profile for unknown device

AutoSyncOrchestrator used to silently create ~/PalmSync/<userName>/
the first time it saw an unrecognised USB serial — no dialog, no
consent. Splits findOrCreateProfile into two callables:

- onPalmDetected emits new unregisteredDeviceDetected(serial,name,id)
  signal on miss; no profile is created.
- createProfileForDevice (new public method) does the actual creation
  + registration; called from the UI's confirmation slot.

KF6MainWindow gains an onUnregisteredDeviceDetected slot showing a
QMessageBox::question with the device serial. On Yes, calls
createProfileForDevice; on No, nothing happens.

findOrCreateProfile is retained as a thin wrapper that calls
createProfileForDevice directly (preserves the non-UI bootstrap path
used by test fixtures).

The serial-keyed registry is the only one written here; the
fingerprint-keyed KF6Settings::registerDevice call is dropped from
createProfileForDevice as forward-prep for Task 0.B. findOrCreateProfile
keeps both registrations until Task 0.B lands.

Tests: new test_autosyncorchestrator_unregistered (+2 subcases) with
isolated HOME so it can't pollute the real ~/PalmSync. WildPalms test
count 71 → 72.
EOF
)"
```

---

## Task 0.B: Consolidate device→profile registries

**Files:**
- Modify: `WildPalms/src/kf6/kf6settings.h` (delete 5 method decls + 1 group helper)
- Modify: `WildPalms/src/kf6/kf6settings.cpp` (delete 5 method bodies + 1 group helper; add migration)
- Modify: `WildPalms/src/profile.h` (delete `registryKey()` + `fromRegistryKey()`)
- Modify: `WildPalms/src/kf6/autosyncorchestrator.cpp` (drop the now-dead fingerprint fallback already touched in 0.A's `onPalmDetected` rewrite; also drop it from `findOrCreateProfile`)
- Modify: `WildPalms/src/settingsdialog.cpp:413-433` (flip Registered Devices page to read serial-keyed group)
- Modify: `WildPalms/tests/test_profile.cpp` (delete two tests that exercise the deleted helpers)
- Create: `WildPalms/tests/test_kf6settings_registry_migration.cpp`
- Modify: `WildPalms/tests/CMakeLists.txt`

### Steps

- [ ] **Step 0.B.1: Write the failing migration test**

Create `WildPalms/tests/test_kf6settings_registry_migration.cpp`:

```cpp
/**
 * @file test_kf6settings_registry_migration.cpp
 * @brief Tests for the one-time DeviceRegistry → DeviceSerials migration
 *
 * Verifies that legacy fingerprint-keyed entries (written by pre-Phase-L
 * versions of WildPalms) are copied into the serial-keyed group at next
 * KF6Settings instantiation, and that the legacy group is then cleared.
 */

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <KSharedConfig>
#include <KConfigGroup>
#include "kf6/kf6settings.h"

class TestKF6SettingsMigration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testMigrationCopiesSerialEntries();
    void testMigrationIsIdempotent();
    void testEmptyLegacyGroupIsNoOp();
};

void TestKF6SettingsMigration::initTestCase()
{
    // CMakeLists isolates XDG_CONFIG_HOME per test execution.
    qDebug() << "XDG_CONFIG_HOME=" << qgetenv("XDG_CONFIG_HOME");
}

void TestKF6SettingsMigration::testMigrationCopiesSerialEntries()
{
    // Arrange: write two legacy DeviceRegistry entries directly via
    // KSharedConfig, BEFORE constructing KF6Settings.
    {
        KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
        KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
        // Key format per DeviceFingerprint::registryKey(): "userId:userName:serial"
        legacy.writeEntry(QStringLiteral("42:Alice:SN-AAA-001"), QStringLiteral("/tmp/profileA"));
        legacy.writeEntry(QStringLiteral("99:Bob:SN-BBB-002"), QStringLiteral("/tmp/profileB"));
        cfg->sync();
    }

    // Act: construct (triggers migration in ctor).
    KF6Settings &s = KF6Settings::instance();

    // Assert: DeviceSerials has both entries with the parsed serial as key.
    QCOMPARE(s.findProfileBySerial(QStringLiteral("SN-AAA-001")),
             QStringLiteral("/tmp/profileA"));
    QCOMPARE(s.findProfileBySerial(QStringLiteral("SN-BBB-002")),
             QStringLiteral("/tmp/profileB"));

    // Assert: legacy DeviceRegistry group is now empty.
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
    QVERIFY2(legacy.keyList().isEmpty(),
             qPrintable(QStringLiteral("Legacy group not cleared: ") + legacy.keyList().join(",")));
}

void TestKF6SettingsMigration::testMigrationIsIdempotent()
{
    // KF6Settings is a singleton, so this re-uses the already-constructed
    // instance from the prior test — that's exactly the in-process
    // "second access after migration ran" scenario we want to verify:
    // no crash, no undo, the migrated state is still present.
    KF6Settings &s = KF6Settings::instance();
    QCOMPARE(s.findProfileBySerial(QStringLiteral("SN-AAA-001")),
             QStringLiteral("/tmp/profileA"));
}

void TestKF6SettingsMigration::testEmptyLegacyGroupIsNoOp()
{
    // Already covered by testMigrationIsIdempotent's second run, but
    // make the intent explicit: no crash, no spurious writes.
    KSharedConfig::Ptr cfg = KSharedConfig::openConfig(QStringLiteral("wildpalmsrc"));
    KConfigGroup legacy(cfg, QStringLiteral("DeviceRegistry"));
    QVERIFY(legacy.keyList().isEmpty());
}

QTEST_MAIN(TestKF6SettingsMigration)
#include "test_kf6settings_registry_migration.moc"
```

- [ ] **Step 0.B.2: Register the test (isolated XDG)**

Edit `WildPalms/tests/CMakeLists.txt`. Append after the Task 0.A test block:

```cmake
# Phase L Task 0.B: KF6Settings DeviceRegistry → DeviceSerials migration
add_wildpalms_test(test_kf6settings_registry_migration
    test_kf6settings_registry_migration.cpp
)
set_tests_properties(test_kf6settings_registry_migration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${CMAKE_BINARY_DIR}/test-home-kf6mig;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-xdg-kf6mig"
)
```

- [ ] **Step 0.B.3: Build, see the test fail**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -10
ctest --test-dir build -R test_kf6settings_registry_migration --output-on-failure 2>&1 | tail -30
```

Expected: builds (the test only uses public KF6Settings + raw KConfigGroup), but FAILS — `findProfileBySerial` returns empty for `SN-AAA-001` because no migration runs yet.

- [ ] **Step 0.B.4: Add the migration to the KF6Settings ctor**

Edit `WildPalms/src/kf6/kf6settings.cpp`. Replace the existing constructor (lines 13–16):

```cpp
KF6Settings::KF6Settings()
    : m_config(KSharedConfig::openConfig(QStringLiteral("wildpalmsrc")))
{
}
```

with:

```cpp
KF6Settings::KF6Settings()
    : m_config(KSharedConfig::openConfig(QStringLiteral("wildpalmsrc")))
{
    migrateLegacyDeviceRegistry();
}

void KF6Settings::migrateLegacyDeviceRegistry()
{
    // Phase L Task 0.B: legacy "DeviceRegistry" group keyed on
    // DeviceFingerprint::registryKey() ("userId:userName:serial") is
    // collapsed into the simpler serial-keyed "DeviceSerials" group.
    KConfigGroup legacy(m_config, QStringLiteral("DeviceRegistry"));
    QStringList legacyKeys = legacy.keyList();
    if (legacyKeys.isEmpty()) {
        return;
    }

    KConfigGroup serials = deviceSerialsGroup();
    for (const QString &key : legacyKeys) {
        // Parse the serial out of the trailing segment after the second ':'.
        int firstColon = key.indexOf(QLatin1Char(':'));
        if (firstColon < 0) continue;
        int secondColon = key.indexOf(QLatin1Char(':'), firstColon + 1);
        if (secondColon < 0) continue;
        QString serial = key.mid(secondColon + 1);
        if (serial.isEmpty()) continue;

        QString profilePath = legacy.readEntry(key, QString());
        if (profilePath.isEmpty()) continue;

        // Don't clobber an existing newer serial entry.
        if (serials.readEntry(serial, QString()).isEmpty()) {
            serials.writeEntry(serial, profilePath);
        }
    }

    // Wipe legacy group entirely so subsequent ctors are no-ops.
    legacy.deleteGroup();
    m_config->sync();
}
```

Add the private declaration in `WildPalms/src/kf6/kf6settings.h`. In the `private:` section (after the existing helper-method declarations, before the `};` closing the class):

```cpp
    void migrateLegacyDeviceRegistry();
```

- [ ] **Step 0.B.5: Run the migration test, verify it passes**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -5
ctest --test-dir build -R test_kf6settings_registry_migration --output-on-failure
```

Expected: PASS.

- [ ] **Step 0.B.6: Delete the doomed KF6Settings methods**

Edit `WildPalms/src/kf6/kf6settings.h`. Remove the entire `// ========== Device Registry ==========` block (lines 44–61 in the original — the five method declarations from `registerDevice(...)` through `clearDeviceRegistry()`). Also remove the corresponding private helper line:

```cpp
    KConfigGroup deviceRegistryGroup() const;
```

(line 125 in the original).

Edit `WildPalms/src/kf6/kf6settings.cpp`. Delete:
- `KConfigGroup KF6Settings::deviceRegistryGroup() const { ... }` (lines 25–28)
- The entire `// ========== Device Registry ==========` block (lines 112–177 inclusive — five method bodies)

Note: do NOT delete `deviceSerialsGroup()` or the three `registerDeviceBySerial / findProfileBySerial / unregisterDeviceBySerial` methods — those stay.

- [ ] **Step 0.B.7: Delete DeviceFingerprint::registryKey + fromRegistryKey**

Edit `WildPalms/src/profile.h`. Delete the two methods at the bottom of the `DeviceFingerprint` struct (lines 127–146):

```cpp
    // Create a unique key for registry lookups (format: userId:userName:serial)
    QString registryKey() const {
        return QString("%1:%2:%3").arg(userId).arg(userName, usbSerialNumber);
    }

    static DeviceFingerprint fromRegistryKey(const QString &key) {
        ...
    }
```

- [ ] **Step 0.B.8: Update callers — AutoSyncOrchestrator**

Edit `WildPalms/src/kf6/autosyncorchestrator.cpp`. Search for remaining calls to deleted methods (post-Task-0.A, there should be none left in `onPalmDetected`, but `findOrCreateProfile` may still call `registerDevice` and have a fingerprint fallback):

```bash
grep -n "registerDevice\b\|findProfileForDevice\b\|DeviceFingerprint::fromRegistryKey\|\.registryKey()" \
    src/kf6/autosyncorchestrator.cpp
```

For each remaining hit:
- Calls to `settings.registerDevice(fingerprint, finalPath)` — delete (the serial registration above it stays).
- Calls to `settings.findProfileForDevice(fp)` — delete the entire fallback branch (it's now unreachable given the serial-first lookup above it; if you still find such a branch, remove it).

The `findOrCreateProfile` body should now only:
1. Look up by serial via `findProfileBySerial`.
2. If miss, call `createProfileForDevice(...)`.

That's it. Same shape as `createProfileForDevice` from Task 0.A but with the lookup-first step retained.

- [ ] **Step 0.B.9: Update callers — SettingsDialog "Registered Devices" page**

Edit `WildPalms/src/settingsdialog.cpp`. Replace the block at lines 413–433 (the `// Device registry` section through the close of its `if (registry.isEmpty())`) with:

```cpp
    // Registered devices (by USB serial — Phase L Task 0.B consolidated
    // the previous fingerprint-keyed DeviceRegistry into DeviceSerials).
    m_deviceRegistryList->clear();
    KConfigGroup serials = s.deviceSerialsGroup();
    QStringList serialKeys = serials.keyList();
    for (const QString &serial : serialKeys) {
        QString profilePath = serials.readEntry(serial, QString());
        if (profilePath.isEmpty()) continue;
        QFileInfo profileInfo(profilePath);

        auto *item = new QListWidgetItem(
            QStringLiteral("%1 → %2").arg(serial, profileInfo.fileName()));
        item->setToolTip(i18n("USB Serial: %1\nProfile: %2", serial, profilePath));
        m_deviceRegistryList->addItem(item);
    }

    if (serialKeys.isEmpty()) {
        auto *item = new QListWidgetItem(i18n("(No devices registered yet)"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(Qt::gray);
        m_deviceRegistryList->addItem(item);
    }
```

This requires `deviceSerialsGroup()` to be accessible from outside KF6Settings. Today it's a private helper. Promote it to public:

In `WildPalms/src/kf6/kf6settings.h`, move the `KConfigGroup deviceSerialsGroup() const;` line out of the `private:` block into the public `// ========== Device Registry by USB Serial ==========` section, just below the existing serial methods. The implementation in `.cpp` is unchanged.

If `settingsdialog.cpp` doesn't already `#include <KConfigGroup>`, add it.

- [ ] **Step 0.B.10: Update callers — test_profile.cpp**

Edit `WildPalms/tests/test_profile.cpp`. Delete the two tests that exercise removed helpers:

In the `private slots:` section, remove these two declarations:
```cpp
    void testFingerprintRegistryKey();
    void testFingerprintFromRegistryKey();
```

In the implementation section, delete the two function bodies (lines 172–194 — `testFingerprintRegistryKey` and `testFingerprintFromRegistryKey`).

- [ ] **Step 0.B.11: Build and run full test suite**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake --build build -j 10 2>&1 | tail -10
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: build clean. Tests pass at count 73 (was 71 at start: +1 from Task 0.A's `test_autosyncorchestrator_unregistered`, +1 from Task 0.B's `test_kf6settings_registry_migration`). `test_profile` runs `−2` subcases (the two deleted) but is still 1 executable, still passes.

- [ ] **Step 0.B.12: Sanity grep — no dead references remain**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
grep -rn "\.registerDevice(\|\.unregisterDevice(\|\.findProfileForDevice(\|\.deviceRegistry(\|\.clearDeviceRegistry(\|\.registryKey()\|DeviceFingerprint::fromRegistryKey" src/ tests/ | grep -v "registerDeviceBySerial\|unregisterDeviceBySerial\|findProfileBySerial"
```

Expected: zero hits in `src/`. Hits in `tests/` are limited to the migration test's deliberate raw-KConfig writes (which don't match these patterns anyway since they use `KConfigGroup::writeEntry`). If anything else surfaces, fix and rebuild.

- [ ] **Step 0.B.13: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/kf6/kf6settings.h src/kf6/kf6settings.cpp src/profile.h \
        src/kf6/autosyncorchestrator.cpp src/settingsdialog.cpp \
        tests/test_profile.cpp tests/test_kf6settings_registry_migration.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Phase L Task 0.B: collapse DeviceRegistry into DeviceSerials

Two parallel device→profile registries coexisted in KF6Settings:
the original "DeviceRegistry" keyed on a userId:userName:serial
composite key, and the "DeviceSerials" group keyed on USB serial
alone (added later as "more reliable"). Both were written on every
device-register; lookup checked both.

Deletes:
- KF6Settings::registerDevice / unregisterDevice / findProfileForDevice
  / deviceRegistry / clearDeviceRegistry (5 methods)
- KF6Settings::deviceRegistryGroup() private helper
- DeviceFingerprint::registryKey() / fromRegistryKey() static helpers
- AutoSyncOrchestrator's fingerprint-fallback branch in findOrCreateProfile
- test_profile.cpp testFingerprintRegistryKey / testFingerprintFromRegistryKey

Adds:
- KF6Settings::migrateLegacyDeviceRegistry() — called from the ctor
  once; parses serial out of each legacy key, copies into DeviceSerials
  if not already present, then deleteGroup()s the old group. Idempotent.
- Public access to deviceSerialsGroup() so SettingsDialog can enumerate
  the serial registry for its "Registered Devices" page (data source
  flipped; columns become Serial + Profile filename instead of full
  fingerprint breakdown).
- test_kf6settings_registry_migration (+3 subcases) covering the
  migration logic and idempotency.

WildPalms test count: 72 → 73.
EOF
)"
```

---

## Task 0.C: Delete WildPalms ROADMAP §5.5 multi-device aspiration

**Files:**
- Modify: `WildPalms/docs/ROADMAP.md`

### Steps

- [ ] **Step 0.C.1: Verify the section is at the expected lines**

```bash
sed -n '347,355p' /home/clinton/dev/refactor-engine-merger/WildPalms/docs/ROADMAP.md
```

Expected output: shows `#### 5.5 Multiple Device Support` and the four bullets. If line numbers have shifted, locate the section via `grep -n "5.5 Multiple Device Support" WildPalms/docs/ROADMAP.md` and use those lines.

- [ ] **Step 0.C.2: Delete the section**

Edit `WildPalms/docs/ROADMAP.md`. Locate the block:

```markdown
#### 5.5 Multiple Device Support
- [ ] Device profiles
- [ ] Switch between devices
- [ ] Separate backups per device
- [ ] Separate ID mappings per device

```

Delete it. The blank line above (the `- [ ] Polish visual design` line ends at line 347) and below (subsection `#### 5.6 Utilities` begins after the deleted block) connect naturally. After deletion, the file should jump from `#### 5.4 Enhanced UI`'s closing bullet straight to `#### 5.6 Utilities`.

(Optional cosmetic: renumber `5.6` → `5.5` and onward; defer that to a separate cleanup task — Phase L Task 0 doesn't need to touch the rest of the file.)

- [ ] **Step 0.C.3: Verify the deletion**

```bash
grep -c "Multiple Device Support\|Separate backups per device\|Separate ID mappings per device" \
    /home/clinton/dev/refactor-engine-merger/WildPalms/docs/ROADMAP.md
```

Expected: `0`.

- [ ] **Step 0.C.4: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add docs/ROADMAP.md
git commit -m "$(cat <<'EOF'
Phase L Task 0.C: drop multi-device-per-profile aspiration from ROADMAP

Section §5.5 "Multiple Device Support" advertised four feature
bullets (device profiles, switch between devices, separate backups
per device, separate ID mappings per device) that were never
implemented and contradict the actual 1:1 device↔profile model.
Tasks 0.A + 0.B made the 1:1 model explicit in code; this commit
makes it explicit in the roadmap.
EOF
)"
```

---

## Task 0.D: Refresh baseline + update coordination docs

**Files:**
- Modify: `baselines/wildpalms-worktree-ctest.txt` (refresh)
- Modify: `libkalburator/docs/phase0/04w-deferred-work.md` (new entry under §D)
- Modify: `CURRENT-STATUS.md`
- Optional: `FINDINGS.md` (only if non-obvious surfaced)

### Steps

- [ ] **Step 0.D.1: Run verify-all, capture state**

```bash
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -30
```

Expected: exit code 3 (test count increased, pass→fail). New WildPalms count = 73; libkalburator unchanged at 92; PlanStan unchanged at 82/105.

- [ ] **Step 0.D.2: Refresh the WildPalms baseline**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
ctest --test-dir build --output-on-failure -j 10 > /tmp/wildpalms-new-baseline.txt 2>&1 || true
cp /tmp/wildpalms-new-baseline.txt /home/clinton/dev/refactor-engine-merger/baselines/wildpalms-worktree-ctest.txt
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh 2>&1 | tail -10
```

Expected: second `verify-all` run exits 0 (baseline matches new posture).

- [ ] **Step 0.D.3: Update 04w-deferred-work.md**

Edit `libkalburator/docs/phase0/04w-deferred-work.md`. Find section `## D. Consumer UX (Phase Ic)`. After the existing D.4 entry, append:

```markdown
### D.5 WildPalms multi-device cleanup (Phase L Task 0)

**Status:** ✅ landed 2026-05-15 (Phase L Task 0, commits A/B/C on
`refactor/engine-merger` in WildPalms).

**Source:** `2026-05-15-phase-l-multidevice-cleanup-design.md` +
`2026-05-15-phase-l-task0-multidevice-cleanup-plan.md` (coordination
folder root).

**Outcome:** WildPalms is now explicitly 1:1 device↔profile in code
*and* docs. Changes:

- `AutoSyncOrchestrator::onPalmDetected` no longer silently creates
  profile dirs for unrecognised devices; it emits
  `unregisteredDeviceDetected` and `KF6MainWindow` shows a
  confirmation `QMessageBox`. New public method
  `createProfileForDevice(serial,name,id)` does the actual creation
  on user consent.
- `KF6Settings::DeviceRegistry` group (fingerprint-keyed) deleted
  along with its 5 methods. `DeviceSerials` (USB-serial-keyed) is
  now the sole device→profile lookup. Migration in the
  `KF6Settings` ctor copies legacy entries into `DeviceSerials`
  once and `deleteGroup()`s the old group. Idempotent.
- `DeviceFingerprint::registryKey()` + `fromRegistryKey(...)` deleted
  (only existed to serve the deleted group).
- `WildPalms/docs/ROADMAP.md §5.5 "Multiple Device Support"` deleted.
- `SettingsDialog` "Registered Devices" page now enumerates the
  serial-keyed group instead of the fingerprint-keyed one; columns
  are Serial + Profile filename instead of the full fingerprint
  breakdown.

**Retained (deliberately, per design):** `DeviceFingerprint` struct,
`Profile::deviceFingerprint()` persistence,
`KF6MainWindow::handleDeviceFingerprint` mismatch dialog, dashboard
+ sidebar device-info display panels. These are the per-profile
"this profile is for *this* device" warning surface and are useful
even (especially) in a 1:1 world.

**Net code change:** ~150 LOC deleted across `kf6settings.{h,cpp}`,
`profile.h`, `autosyncorchestrator.cpp`, `settingsdialog.cpp`; ~80
LOC added (migration + new test fixtures). WildPalms test count
71 → 73.
```

- [ ] **Step 0.D.4: Update CURRENT-STATUS.md**

Edit `CURRENT-STATUS.md`. Bump the date at the top. Update "What to do RIGHT NOW" to reflect:
- Phase L Task 0 (multi-device cleanup) ✅ landed
- Phase L Task 1+ (Akonadi provider) ⬜ next
- K closing tag still pending Layer B silent-success-on-disconnect; explicitly deferred until Phase L lands.

Add a "Recently committed (WildPalms — Phase L Task 0, 2026-05-15)" section quoting the three commit subjects (A/B/C). Keep the file ≤ 200 lines; trim the older "Recently committed" sections if needed.

- [ ] **Step 0.D.5: Optionally append to FINDINGS.md**

Only if a non-obvious gotcha surfaced during 0.A/0.B (e.g., `KConfigGroup::deleteGroup()` semantic, `QSignalSpy` quirk, friend-emit pattern in `KF6MainWindow`). If nothing was surprising, skip.

- [ ] **Step 0.D.6: Commit coordination updates**

```bash
cd /home/clinton/dev/refactor-engine-merger
git -C libkalburator add docs/phase0/04w-deferred-work.md
git -C libkalburator commit -m "$(cat <<'EOF'
docs: 04w D.5 entry — WildPalms multi-device cleanup landed (Phase L Task 0)

Records the resolution of the multi-device-per-profile cleanup that
preceded the Akonadi provider work. Cross-references the design +
plan in the coordination folder. The WildPalms-side commits live on
the WildPalms worktree's refactor/engine-merger branch.
EOF
)"

# CURRENT-STATUS.md + (optional) FINDINGS.md live in the coordination
# root which is NOT a git repo; no commit needed for those.

# Baseline file lives in coordination root too — same: no commit.
```

- [ ] **Step 0.D.7: Final verify**

```bash
cd /home/clinton/dev/refactor-engine-merger
./scripts/verify-all.sh
```

Expected: exit code 0. Phase L Task 0 is complete. Proceed to Phase L Task 1 (`AkonadiProvider` skeleton) per `2026-05-15-phase-l-akonadi-plan.md`.

---

## Self-review checklist (for the executor, before claiming done)

- [ ] Three commits exist in WildPalms on `refactor/engine-merger`, subjects begin "Phase L Task 0.A:", "Phase L Task 0.B:", "Phase L Task 0.C:".
- [ ] One commit in libkalburator on `refactor/engine-merger`, subject begins "docs: 04w D.5 entry".
- [ ] `verify-all.sh` exits 0.
- [ ] `grep -rn "registerDevice\b\|deviceRegistry\b\|findProfileForDevice\b\|registryKey\b\|fromRegistryKey\b" WildPalms/src/` returns zero hits (only the new `migrateLegacyDeviceRegistry` method matches, but that's distinct from `registerDevice`).
- [ ] WildPalms test count is 73; libkalburator 92; PlanStan 82/105.
- [ ] `WildPalms/docs/ROADMAP.md` contains no string "Multiple Device Support".
- [ ] `CURRENT-STATUS.md` date bumped; "Recently committed" section reflects Phase L Task 0.
- [ ] `04w-deferred-work.md` has the new D.5 entry.

If all checked, Task 0 is done and the Phase L plan's Task 1 (AkonadiProvider skeleton) is unblocked.
