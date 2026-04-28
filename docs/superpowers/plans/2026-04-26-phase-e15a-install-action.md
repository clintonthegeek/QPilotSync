# Phase E.15a — Install Action Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the legacy `InstallConduit` (`IConduit`) as the first `IPluginAction`, sourced from a folder + cross-plugin blob backends; introduces `IPalmFileInstaller` so tests can drive the install flow without pisock; wires a generic `Tools → Actions` MainWindow submenu.

**Architecture:** `InstallActionPlugin::execute()` consumes a `params["files"]` list and installs each via `device->fileInstaller()`. Aggregation lives in `InstallSourceCollector` (folder scan + per-backend-plugin blob drain into a temp dir). `IPalmFileInstaller` has a real `PilotLinkPalmFileInstaller` (wraps `pi_file_install`) and a `MockPalmFileInstaller`. MainWindow auto-populates `Tools → Actions` from `PluginActionManager::actions()`; selection runs `QtConcurrent::run` against the action with collector-built params.

**Tech Stack:** C++20, Qt6 (Core, Concurrent, Widgets, Test), KF6::CoreAddons (`KPluginFactory`, `kcoreaddons_add_plugin`), libpisock (`pi_file_install` real path; mocks bypass it), `Kalburator::Sync` (`IBlobBackend`, `BackendRecord`).

**Spec:** `docs/superpowers/specs/2026-04-26-phase-e15a-install-action-design.md`

---

## File structure

```
src/palm/device/                                   (parent repo)
├── ipalmfileinstaller.h                           NEW (interface)
├── pilotlinkpalmfileinstaller.{h,cpp}             NEW (pisock impl)
└── CMakeLists.txt                                 modified — add new files

src/palm/sync/                                     (parent repo)
├── mockpalmfileinstaller.{h,cpp}                  NEW (test mock)
└── CMakeLists.txt                                 modified — add mock

src/palm/                                          (parent repo)
├── palmdeviceconnection.{h,cpp}                   modified — add fileInstaller() + ctor overload

src/runtime/                                       (parent repo)
├── installsourcecollector.{h,cpp}                 NEW (folder + blob aggregator)
└── CMakeLists.txt                                 modified — add collector

src/plugins/install/                               (parent repo, NOT a submodule)
├── CMakeLists.txt                                 modified — V2 toggle
├── install-conduit.json                           unchanged (legacy manifest)
├── install-action-plugin.json                     NEW (V2 manifest)
├── installactionplugin.{h,cpp}                    NEW (IPluginAction)
├── installconduit.{h,cpp}                         unchanged (legacy)
└── installview.{h,cpp}                            unchanged (legacy)

src/kf6/                                           (parent repo)
└── kf6mainwindow.{h,cpp}                          modified — Tools → Actions submenu

src/plugins/plucker/                               (submodule)
└── pluckerblobbackend.cpp                         modified — swap availableCollections() order

tests/plugins/install/                             (parent repo)  NEW
├── CMakeLists.txt                                 NEW
├── tst_palmfileinstaller.cpp                      NEW
├── tst_installsourcecollector.cpp                 NEW
├── tst_installactionplugin.cpp                    NEW
├── tst_install_v2_e2e.cpp                         NEW
└── fixtures/
    ├── dummy.prc                                   NEW (4 bytes)
    └── dummy.pdb                                   NEW (4 bytes)

tests/plugins/CMakeLists.txt                       modified — add_subdirectory(install)
tests/                                             modified — add a tst_palmdeviceconnection case (existing test file)

docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md   modified — flip E.15 row partial-✅ (E.15a only)

MEMORY.md + project_phase_e15a_install.md          (auto-memory)  NEW
```

---

## Task 1: `IPalmFileInstaller` + `MockPalmFileInstaller`

**Files:**
- Create: `src/palm/device/ipalmfileinstaller.h`
- Create: `src/palm/sync/mockpalmfileinstaller.h`
- Create: `src/palm/sync/mockpalmfileinstaller.cpp`
- Create: `tests/plugins/install/CMakeLists.txt`
- Create: `tests/plugins/install/tst_palmfileinstaller.cpp`
- Modify: `src/palm/sync/CMakeLists.txt` (add mock to SOURCES)
- Modify: `tests/plugins/CMakeLists.txt` (add subdirectory)

**Goal:** Pure abstract interface plus a recording mock. `PilotLinkPalmFileInstaller` lands in Task 2 to avoid mixing the test-only dependency tree with the pisock-linking real impl.

- [ ] **Step 1: Create the interface**

`src/palm/device/ipalmfileinstaller.h`:

```cpp
#ifndef WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H
#define WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H

#include <QString>

namespace WildPalms::PalmSync {

/**
 * @brief Installs whole-database files (.prc / .pdb) onto a Palm.
 *
 * Sibling abstraction to IPalmDatabaseAccess. Kept distinct because
 * record-shaped operations and whole-DB-from-disk install live at
 * different layers; mixing them blurs the contract for the four
 * backends already implementing IPalmDatabaseAccess.
 */
class IPalmFileInstaller
{
public:
    virtual ~IPalmFileInstaller() = default;

    /// Install `path` onto the connected device. Returns true on
    /// success. On failure, populates `errorMessage` (when non-null)
    /// with a human-readable diagnostic.
    virtual bool installFile(const QString &path,
                              QString *errorMessage = nullptr) = 0;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H
```

- [ ] **Step 2: Create the test file with the failing mock test**

`tests/plugins/install/tst_palmfileinstaller.cpp`:

```cpp
#include <QTest>

#include "palm/sync/mockpalmfileinstaller.h"

using namespace WildPalms::PalmSync;

class TestPalmFileInstaller : public QObject
{
    Q_OBJECT

private slots:
    void mock_recordsCallsByDefaultSucceeds()
    {
        MockPalmFileInstaller m;
        QString err;
        QVERIFY(m.installFile(QStringLiteral("/tmp/foo.prc"), &err));
        QVERIFY(err.isEmpty());
        QCOMPARE(m.installedPaths(),
                 (QStringList{QStringLiteral("/tmp/foo.prc")}));
    }

    void mock_setNextResult_appliesOnce()
    {
        MockPalmFileInstaller m;
        m.setNextResult(false, QStringLiteral("simulated"));

        QString err;
        QVERIFY(!m.installFile(QStringLiteral("/tmp/a.prc"), &err));
        QCOMPARE(err, QStringLiteral("simulated"));

        // Subsequent call falls back to default (success).
        QVERIFY(m.installFile(QStringLiteral("/tmp/b.prc"), &err));
    }

    void mock_setAllowAll_isSticky()
    {
        MockPalmFileInstaller m;
        m.setAllowAll(false);
        QVERIFY(!m.installFile(QStringLiteral("/tmp/a.prc")));
        QVERIFY(!m.installFile(QStringLiteral("/tmp/b.prc")));
        m.setAllowAll(true);
        QVERIFY(m.installFile(QStringLiteral("/tmp/c.prc")));
    }

    void mock_recordsAllPaths()
    {
        MockPalmFileInstaller m;
        m.installFile(QStringLiteral("/tmp/a.prc"));
        m.installFile(QStringLiteral("/tmp/b.prc"));
        m.installFile(QStringLiteral("/tmp/c.prc"));
        QCOMPARE(m.installedPaths().size(), 3);
        QCOMPARE(m.installedPaths()[1], QStringLiteral("/tmp/b.prc"));
    }
};

QTEST_MAIN(TestPalmFileInstaller)
#include "tst_palmfileinstaller.moc"
```

- [ ] **Step 3: Create `tests/plugins/install/CMakeLists.txt`**

```cmake
# Phase E.15a — Install action plugin tests.

set(INSTALL_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/install)
set(INSTALL_FIXTURE_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# --- Task 1: PalmFileInstaller ---
add_executable(tst_palmfileinstaller
    tst_palmfileinstaller.cpp
)
target_include_directories(tst_palmfileinstaller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_palmfileinstaller
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsPalmSync
)
add_test(NAME tst_palmfileinstaller COMMAND tst_palmfileinstaller)
```

- [ ] **Step 4: Append `add_subdirectory(install)` to `tests/plugins/CMakeLists.txt`**

```cmake

# Phase E.15a — Install action tests.
add_subdirectory(install)
```

- [ ] **Step 5: Build the test and watch it fail**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmfileinstaller 2>&1 | tail -10
```
Expected: build fails — `mockpalmfileinstaller.h` does not exist.

- [ ] **Step 6: Create `src/palm/sync/mockpalmfileinstaller.h`**

```cpp
#ifndef WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H
#define WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H

#include <QQueue>
#include <QPair>
#include <QString>
#include <QStringList>
#include <optional>

#include "palm/device/ipalmfileinstaller.h"

namespace WildPalms::PalmSync {

/**
 * @brief In-memory recording mock for IPalmFileInstaller.
 *
 * Default behaviour: every installFile() returns true and records the
 * path. Tests override per-call via setNextResult() (one-shot queue)
 * or setAllowAll(bool) (sticky blanket).
 */
class MockPalmFileInstaller : public IPalmFileInstaller
{
public:
    MockPalmFileInstaller() = default;

    bool installFile(const QString &path,
                      QString *errorMessage = nullptr) override;

    QStringList installedPaths() const { return m_paths; }
    void        clear()                { m_paths.clear(); }

    /// Push a one-shot result to the front of the response queue.
    /// Subsequent calls (after the queue empties) fall back to
    /// `setAllowAll`'s value, or to default-true if neither was set.
    void setNextResult(bool success, const QString &errorMsg = {});

    /// Sticky: every call returns `ok` (and an empty error). Overrides
    /// queued results once they're consumed. Call again to flip.
    void setAllowAll(bool ok);

private:
    QStringList                       m_paths;
    QQueue<QPair<bool, QString>>      m_queue;
    std::optional<bool>               m_blanket;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H
```

- [ ] **Step 7: Create `src/palm/sync/mockpalmfileinstaller.cpp`**

```cpp
#include "mockpalmfileinstaller.h"

namespace WildPalms::PalmSync {

bool MockPalmFileInstaller::installFile(const QString &path,
                                          QString       *errorMessage)
{
    m_paths.append(path);

    if (!m_queue.isEmpty()) {
        const auto next = m_queue.dequeue();
        if (errorMessage) *errorMessage = next.second;
        return next.first;
    }
    const bool ok = m_blanket.value_or(true);
    if (errorMessage) errorMessage->clear();
    return ok;
}

void MockPalmFileInstaller::setNextResult(bool success, const QString &errorMsg)
{
    m_queue.enqueue({success, errorMsg});
}

void MockPalmFileInstaller::setAllowAll(bool ok)
{
    m_blanket = ok;
}

} // namespace WildPalms::PalmSync
```

- [ ] **Step 8: Add the new files to `src/palm/sync/CMakeLists.txt`**

Append `mockpalmfileinstaller.cpp` and `mockpalmfileinstaller.h` to the existing `add_library(WildPalmsPalmSync STATIC ...)` SOURCES list, after the existing `mockpalmdatabaseaccess.{cpp,h}` entries.

- [ ] **Step 9: Build and run the test**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -3
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmfileinstaller
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_palmfileinstaller$' --output-on-failure
```
Expected: PASS (4 test functions).

- [ ] **Step 10: Commit**

```bash
cd /home/clinton/dev/WildPalms
git add src/palm/device/ipalmfileinstaller.h \
        src/palm/sync/mockpalmfileinstaller.h \
        src/palm/sync/mockpalmfileinstaller.cpp \
        src/palm/sync/CMakeLists.txt \
        tests/plugins/install \
        tests/plugins/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm): IPalmFileInstaller interface + MockPalmFileInstaller

Sibling abstraction to IPalmDatabaseAccess; isolates the
pi_file_install path from the four backends already implementing
record-shaped DB access. Mock recorder supports per-call queued
results and a sticky blanket flag for failure-path tests.

PilotLinkPalmFileInstaller (real impl) lands in Task 2.

Phase E.15a Task 1.
EOF
)"
```

---

## Task 2: `PilotLinkPalmFileInstaller` (real impl)

**Files:**
- Create: `src/palm/device/pilotlinkpalmfileinstaller.h`
- Create: `src/palm/device/pilotlinkpalmfileinstaller.cpp`
- Modify: `src/palm/device/CMakeLists.txt`

**Goal:** The pisock-linking real implementation. Verbatim port of the install logic in legacy `InstallConduit::installFile`. No new tests — this code is exercised in E.18's POSE64 integration; for E.15a we trust the legacy code's track record.

- [ ] **Step 1: Create `src/palm/device/pilotlinkpalmfileinstaller.h`**

```cpp
#ifndef WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H
#define WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H

#include "ipalmfileinstaller.h"

class KPilotLink;

namespace WildPalms::PalmSync {

/**
 * @brief pisock-backed IPalmFileInstaller.
 *
 * Borrows a non-owning KPilotLink* whose lifetime exceeds this
 * installer. installFile() opens the file via `pi_file_open`,
 * dispatches to `pi_file_install` against the link's socket,
 * and surfaces any non-zero return as an error.
 */
class PilotLinkPalmFileInstaller : public IPalmFileInstaller
{
public:
    explicit PilotLinkPalmFileInstaller(KPilotLink *link);
    ~PilotLinkPalmFileInstaller() override = default;

    bool installFile(const QString &path,
                      QString *errorMessage = nullptr) override;

private:
    KPilotLink *m_link = nullptr;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H
```

- [ ] **Step 2: Create `src/palm/device/pilotlinkpalmfileinstaller.cpp`**

```cpp
#include "pilotlinkpalmfileinstaller.h"

#include "palm/kpilotdevicelink.h"

extern "C" {
#include <pi-file.h>
}

namespace WildPalms::PalmSync {

PilotLinkPalmFileInstaller::PilotLinkPalmFileInstaller(KPilotLink *link)
    : m_link(link)
{
}

bool PilotLinkPalmFileInstaller::installFile(const QString &path,
                                                QString       *errorMessage)
{
    if (!m_link) {
        if (errorMessage) *errorMessage = QStringLiteral("no link");
        return false;
    }

    auto *deviceLink = dynamic_cast<KPilotDeviceLink *>(m_link);
    if (!deviceLink) {
        if (errorMessage) *errorMessage = QStringLiteral("link is not a real device");
        return false;
    }

    pi_file_t *pf = pi_file_open(path.toLocal8Bit().constData());
    if (!pf) {
        if (errorMessage) *errorMessage = QStringLiteral("pi_file_open failed for %1").arg(path);
        return false;
    }

    const int rc = pi_file_install(pf, deviceLink->socketDescriptor(), 0, nullptr);
    pi_file_close(pf);

    if (rc < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("pi_file_install rc=%1").arg(rc);
        return false;
    }
    return true;
}

} // namespace WildPalms::PalmSync
```

- [ ] **Step 3: Append the new files to `src/palm/device/CMakeLists.txt`**

Add `pilotlinkpalmfileinstaller.h` and `pilotlinkpalmfileinstaller.cpp` to the existing `add_library(WildPalmsPalmDevice STATIC ...)` SOURCES list. The library already links pisock transitively via `WildPalmsCore`.

- [ ] **Step 4: Build and verify**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -3
cmake --build /home/clinton/dev/WildPalms/build-dev --target WildPalmsPalmDevice 2>&1 | tail -10
```
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/palm/device/pilotlinkpalmfileinstaller.h \
        src/palm/device/pilotlinkpalmfileinstaller.cpp \
        src/palm/device/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(palm): PilotLinkPalmFileInstaller (pisock real impl)

Verbatim port of legacy InstallConduit::installFile, extracted into
the IPalmFileInstaller-shaped abstraction. Live-device coverage lands
in E.18 (POSE64); E.15a trusts the legacy code's track record.

Phase E.15a Task 2.
EOF
)"
```

---

## Task 3: `PalmDeviceConnection` extension

**Files:**
- Modify: `src/palm/palmdeviceconnection.h`
- Modify: `src/palm/palmdeviceconnection.cpp`
- Modify: `tests/test_palmdeviceconnection.cpp` (existing test file at `tests/`)

**Goal:** Add a `fileInstaller()` accessor + a constructor overload taking the installer. Existing single-arg constructor stays for backward compat; existing call sites compile unchanged.

- [ ] **Step 1: Add a failing test case to `tests/test_palmdeviceconnection.cpp`**

Find the existing test class. Add a new private slot:

```cpp
    void fileInstaller_returnsConfiguredInstance()
    {
        WildPalms::PalmSync::MockPalmDatabaseAccess db;
        WildPalms::PalmSync::MockPalmFileInstaller installer;
        PalmDeviceConnection conn(&db, &installer);
        QCOMPARE(conn.fileInstaller(),
                 static_cast<WildPalms::PalmSync::IPalmFileInstaller*>(&installer));
    }

    void fileInstaller_isNullByDefault()
    {
        WildPalms::PalmSync::MockPalmDatabaseAccess db;
        PalmDeviceConnection conn(&db);
        QVERIFY(conn.fileInstaller() == nullptr);
    }
```

Add the include near the top:
```cpp
#include "palm/sync/mockpalmfileinstaller.h"
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmdeviceconnection 2>&1 | tail -10
```
Expected: build fails — `fileInstaller()` does not exist.

- [ ] **Step 3: Update `src/palm/palmdeviceconnection.h`**

Replace the file content with:

```cpp
#ifndef WILDPALMS_PALM_PALMDEVICECONNECTION_H
#define WILDPALMS_PALM_PALMDEVICECONNECTION_H

#include <QObject>

namespace WildPalms::PalmSync {
class IPalmDatabaseAccess;
class IPalmFileInstaller;
class PalmBackend;
}

/**
 * @brief Aggregator passed to plugins via IBackendPlugin::createBackends
 *        and to actions via IPluginAction::execute.
 *
 * Owns a PalmBackend wrapping the caller-supplied IPalmDatabaseAccess.
 * Borrows the IPalmFileInstaller (Phase E.15a). Does NOT own the
 * IPalmDatabaseAccess or IPalmFileInstaller — the caller (application
 * runtime) keeps both alive for the connection's lifetime.
 *
 * Lives in the global namespace to match the forward declaration in
 * src/core/ibackendplugin.h.
 */
class PalmDeviceConnection : public QObject
{
    Q_OBJECT
public:
    explicit PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        QObject *parent = nullptr);

    /// Phase E.15a — overload that wires an installer for the install
    /// action.
    PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller,
        QObject *parent = nullptr);

    ~PalmDeviceConnection() override;

    WildPalms::PalmSync::IPalmDatabaseAccess *device() const;
    WildPalms::PalmSync::PalmBackend         *palmBackend() const;
    WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller() const;

signals:
    void connected();
    void disconnected();

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device        = nullptr;
    WildPalms::PalmSync::PalmBackend         *m_palmBackend   = nullptr;
    WildPalms::PalmSync::IPalmFileInstaller  *m_fileInstaller = nullptr;
};

#endif // WILDPALMS_PALM_PALMDEVICECONNECTION_H
```

- [ ] **Step 4: Update `src/palm/palmdeviceconnection.cpp`**

```cpp
#include "palmdeviceconnection.h"

#include "sync/palmbackend.h"

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device, QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
    , m_fileInstaller(nullptr)
{
}

PalmDeviceConnection::PalmDeviceConnection(
    WildPalms::PalmSync::IPalmDatabaseAccess *device,
    WildPalms::PalmSync::IPalmFileInstaller  *fileInstaller,
    QObject *parent)
    : QObject(parent)
    , m_device(device)
    , m_palmBackend(new WildPalms::PalmSync::PalmBackend(device, this))
    , m_fileInstaller(fileInstaller)
{
}

PalmDeviceConnection::~PalmDeviceConnection() = default;

WildPalms::PalmSync::IPalmDatabaseAccess *PalmDeviceConnection::device() const
{
    return m_device;
}

WildPalms::PalmSync::PalmBackend *PalmDeviceConnection::palmBackend() const
{
    return m_palmBackend;
}

WildPalms::PalmSync::IPalmFileInstaller *PalmDeviceConnection::fileInstaller() const
{
    return m_fileInstaller;
}
```

- [ ] **Step 5: Build and run the test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_palmdeviceconnection
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_palmdeviceconnection$' --output-on-failure
```
Expected: PASS — existing tests + 2 new test cases.

- [ ] **Step 6: Commit**

```bash
git add src/palm/palmdeviceconnection.h \
        src/palm/palmdeviceconnection.cpp \
        tests/test_palmdeviceconnection.cpp
git commit -m "$(cat <<'EOF'
feat(palm): PalmDeviceConnection gains fileInstaller() accessor

Adds a new constructor overload taking IPalmFileInstaller* alongside
the existing IPalmDatabaseAccess*. Existing single-arg constructor
preserved; fileInstaller() returns nullptr unless wired via the new
overload. Plugins/actions querying for the installer get a clean
"not configured" signal rather than a stale dangling pointer.

Phase E.15a Task 3.
EOF
)"
```

---

## Task 4: `InstallSourceCollector` — folder-scan side (TDD)

**Files:**
- Create: `src/runtime/installsourcecollector.h`
- Create: `src/runtime/installsourcecollector.cpp`
- Create: `tests/plugins/install/tst_installsourcecollector.cpp`
- Create: `tests/plugins/install/fixtures/dummy.prc` (4 bytes)
- Create: `tests/plugins/install/fixtures/dummy.pdb` (4 bytes)
- Modify: `src/runtime/CMakeLists.txt` (add collector to SOURCES)
- Modify: `tests/plugins/install/CMakeLists.txt` (add collector test)

**Goal:** Folder-scan and post-install-move logic only. Plugin-blob aggregation lands in Task 5.

- [ ] **Step 1: Create the fixture files**

Run from the project root:
```bash
printf 'PRCS' > /home/clinton/dev/WildPalms/tests/plugins/install/fixtures/dummy.prc
printf 'PDBS' > /home/clinton/dev/WildPalms/tests/plugins/install/fixtures/dummy.pdb
```

- [ ] **Step 2: Create `tests/plugins/install/tst_installsourcecollector.cpp`** (folder-side cases only)

```cpp
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "runtime/installsourcecollector.h"

using WildPalms::InstallSourceCollector;

class TestInstallSourceCollector : public QObject
{
    Q_OBJECT

private slots:
    void collect_emptyFolderAndNullManager_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QString(), nullptr);
        QVERIFY(r.files.isEmpty());
        QVERIFY(r.folderSourcedPaths.isEmpty());
    }

    void collect_folder_picksUpPrcAndPdb()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(folder).filePath(QStringLiteral("foo.prc")));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"),
                    QDir(folder).filePath(QStringLiteral("bar.pdb")));
        // A non-installable file that should be ignored.
        QFile junk(QDir(folder).filePath(QStringLiteral("junk.txt")));
        junk.open(QIODevice::WriteOnly);
        junk.write("xx");
        junk.close();

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);

        QCOMPARE(r.files.size(), 2);
        QCOMPARE(r.folderSourcedPaths.size(), 2);
        QStringList names;
        for (const auto &f : r.files) names << f.displayName;
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QStringList{QStringLiteral("bar.pdb"),
                                       QStringLiteral("foo.prc")}));
    }

    void collect_folder_caseInsensitive()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(tmp.path()).filePath(QStringLiteral("UPPER.PRC")));

        InstallSourceCollector c;
        const auto r = c.collect(tmp.path(), nullptr);
        QCOMPARE(r.files.size(), 1);
        QCOMPARE(r.files[0].displayName, QStringLiteral("UPPER.PRC"));
    }

    void collect_nonexistentFolder_returnsEmpty()
    {
        InstallSourceCollector c;
        const auto r = c.collect(QStringLiteral("/no/such/dir"), nullptr);
        QVERIFY(r.files.isEmpty());
    }

    void moveSucceededToInstalled_movesOnlyMatchingPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString folder = tmp.path();
        const QString a = QDir(folder).filePath(QStringLiteral("a.prc"));
        const QString b = QDir(folder).filePath(QStringLiteral("b.pdb"));
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"), a);
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.pdb"), b);

        InstallSourceCollector c;
        const auto r = c.collect(folder, nullptr);
        QCOMPARE(r.files.size(), 2);

        // Pretend only `a` succeeded.
        c.moveSucceededToInstalled(r, QStringList{a});
        QVERIFY(!QFile::exists(a));
        QVERIFY(QFile::exists(QDir(folder).filePath(
            QStringLiteral("installed/a.prc"))));
        QVERIFY(QFile::exists(b));   // not moved
    }
};

QTEST_MAIN(TestInstallSourceCollector)
#include "tst_installsourcecollector.moc"
```

- [ ] **Step 3: Append collector test to `tests/plugins/install/CMakeLists.txt`**

```cmake

# --- Task 4: InstallSourceCollector (folder side) ---
add_executable(tst_installsourcecollector
    tst_installsourcecollector.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/installsourcecollector.cpp
)
target_compile_definitions(tst_installsourcecollector
    PRIVATE
        INSTALL_FIXTURE_DIR="${INSTALL_FIXTURE_DIR}"
)
target_include_directories(tst_installsourcecollector
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_installsourcecollector BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_installsourcecollector
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsCore
        WildPalmsRuntime
        Kalburator::Sync
)
add_test(NAME tst_installsourcecollector COMMAND tst_installsourcecollector)
```

- [ ] **Step 4: Build the test and watch it fail**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -3
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_installsourcecollector 2>&1 | tail -10
```
Expected: build fails — `installsourcecollector.h` does not exist.

- [ ] **Step 5: Create `src/runtime/installsourcecollector.h`** (folder side only; plugin-blob hook is a stub for Task 5)

```cpp
#ifndef WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H
#define WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H

#include <QList>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace WildPalms {

class BackendPluginManager;

/**
 * @brief Aggregates installable files from a folder + cross-plugin
 *        blob backends into a flat list ready for InstallActionPlugin.
 *
 * Folder-sourced files are tracked separately so successful installs
 * can be moved into the legacy `installed/` subfolder. Plugin-blob
 * records are written to a fresh QTemporaryDir owned by the Result;
 * lifetime ends with the Result.
 */
class InstallSourceCollector
{
public:
    struct FileEntry {
        QString path;
        QString displayName;
    };

    struct Result {
        QList<FileEntry>                files;
        QSharedPointer<QTemporaryDir>   tempDir;        // shared with caller
        QStringList                     folderSourcedPaths;
    };

    InstallSourceCollector() = default;

    /// Aggregate sources. `folderPath` may be empty (skip folder
    /// scan). `manager` may be null (skip plugin scan).
    Result collect(const QString          &folderPath,
                   BackendPluginManager   *manager);

    /// Move folder-sourced files whose paths are in `succeededPaths`
    /// from `<folder>/X` to `<folder>/installed/X`. Creates the
    /// `installed/` subdir if absent.
    void moveSucceededToInstalled(const Result      &result,
                                   const QStringList &succeededPaths);

private:
    QList<FileEntry> scanFolder(const QString &folderPath,
                                  QStringList    *outFolderPaths);
    QList<FileEntry> drainPluginBlobs(BackendPluginManager *manager,
                                        QTemporaryDir       *dir);
    static bool      isInstallableType(const QString &type);
    static QString   inferExtension(const QString &type,
                                      const QString &displayName);
};

} // namespace WildPalms

#endif // WILDPALMS_RUNTIME_INSTALLSOURCECOLLECTOR_H
```

- [ ] **Step 6: Create `src/runtime/installsourcecollector.cpp`** (folder side; plugin side stub)

```cpp
#include "installsourcecollector.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace WildPalms {

InstallSourceCollector::Result
InstallSourceCollector::collect(const QString          &folderPath,
                                  BackendPluginManager   *manager)
{
    Result r;
    QStringList folderSourcedPaths;
    auto folderEntries = scanFolder(folderPath, &folderSourcedPaths);
    r.files               = folderEntries;
    r.folderSourcedPaths  = folderSourcedPaths;

    if (manager) {
        r.tempDir = QSharedPointer<QTemporaryDir>::create();
        if (r.tempDir->isValid()) {
            r.files += drainPluginBlobs(manager, r.tempDir.data());
        }
    }
    return r;
}

QList<InstallSourceCollector::FileEntry>
InstallSourceCollector::scanFolder(const QString &folderPath,
                                      QStringList    *outFolderPaths)
{
    QList<FileEntry> entries;
    if (folderPath.isEmpty()) return entries;

    QDir dir(folderPath);
    if (!dir.exists()) return entries;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };
    const auto files = dir.entryInfoList(filters,
                                           QDir::Files | QDir::Readable);
    for (const auto &fi : files) {
        FileEntry e;
        e.path        = fi.absoluteFilePath();
        e.displayName = fi.fileName();
        entries.append(e);
        if (outFolderPaths) outFolderPaths->append(e.path);
    }
    return entries;
}

QList<InstallSourceCollector::FileEntry>
InstallSourceCollector::drainPluginBlobs(BackendPluginManager * /*manager*/,
                                            QTemporaryDir       * /*dir*/)
{
    // Implemented in Task 5.
    return {};
}

bool InstallSourceCollector::isInstallableType(const QString &type)
{
    return type.endsWith(QStringLiteral("-prc"))
        || type.endsWith(QStringLiteral("-pdb"))
        || type.endsWith(QStringLiteral("-bootstrap"))
        || type.contains(QStringLiteral("-bootstrap"));
}

QString InstallSourceCollector::inferExtension(const QString &type,
                                                  const QString &displayName)
{
    if (type.endsWith(QStringLiteral("-prc"))) return QStringLiteral(".prc");
    if (type.endsWith(QStringLiteral("-pdb"))) return QStringLiteral(".pdb");
    if (type.contains(QStringLiteral("-bootstrap"))) {
        const QFileInfo fi(displayName);
        const QString ext = fi.suffix();
        if (!ext.isEmpty()) return QStringLiteral(".") + ext;
        return QStringLiteral(".prc");
    }
    return QStringLiteral(".pdb");
}

void InstallSourceCollector::moveSucceededToInstalled(
    const Result      &result,
    const QStringList &succeededPaths)
{
    const QSet<QString> succeeded(succeededPaths.begin(), succeededPaths.end());
    for (const QString &path : result.folderSourcedPaths) {
        if (!succeeded.contains(path)) continue;

        const QFileInfo fi(path);
        const QString folder = fi.absolutePath();
        const QString installedDir = QDir(folder).filePath(QStringLiteral("installed"));
        QDir().mkpath(installedDir);
        const QString dest = QDir(installedDir).filePath(fi.fileName());
        if (QFile::exists(dest)) QFile::remove(dest);
        QFile::rename(path, dest);
    }
}

} // namespace WildPalms
```

(Add `#include <QSet>` if not already pulled in transitively.)

- [ ] **Step 7: Add the collector to `src/runtime/CMakeLists.txt`**

Append to the existing `add_library(WildPalmsRuntime STATIC ...)` SOURCES list:
```cmake
    installsourcecollector.h
    installsourcecollector.cpp
```

- [ ] **Step 8: Build and run the test**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -3
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_installsourcecollector
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_installsourcecollector$' --output-on-failure
```
Expected: PASS (5 test functions). The plugin-blob hook returns empty per the stub.

- [ ] **Step 9: Commit**

```bash
git add src/runtime/installsourcecollector.h \
        src/runtime/installsourcecollector.cpp \
        src/runtime/CMakeLists.txt \
        tests/plugins/install/tst_installsourcecollector.cpp \
        tests/plugins/install/CMakeLists.txt \
        tests/plugins/install/fixtures
git commit -m "$(cat <<'EOF'
feat(runtime): InstallSourceCollector folder-scan side

Aggregates installable files from <folder>/*.{prc,pdb} (case-insensitive)
into a flat FileEntry list; tracks folder-sourced paths separately so
successful installs can be moved into the legacy installed/ subfolder
afterwards. Plugin-blob aggregation lands in Task 5.

Phase E.15a Task 4.
EOF
)"
```

---

## Task 5: `InstallSourceCollector` — plugin-blob side (TDD)

**Files:**
- Modify: `src/runtime/installsourcecollector.cpp` (fill in `drainPluginBlobs`)
- Modify: `tests/plugins/install/tst_installsourcecollector.cpp` (add plugin-blob cases)

**Goal:** Iterate `BackendPluginManager::plugins()`, call `createBackends(nullptr, nullptr)` per plugin to get a fresh blob backend, query its installable collections, write each `BackendRecord::data` to the temp dir.

- [ ] **Step 1: Add plugin-blob test cases to `tst_installsourcecollector.cpp`**

Add includes near the top:
```cpp
#include "core/ibackendplugin.h"
#include "runtime/backendpluginmanager.h"
#include <iblobbackend.h>
#include <QObject>
```

Add a fake backend + plugin near the top (before the test class):

```cpp
namespace {

using namespace Kalburator::Sync;

class FakeBlobBackend : public IBlobBackend
{
    Q_OBJECT
public:
    QString backendId() const override   { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    bool    isAvailable() const override { return true; }

    QList<CollectionInfo> availableCollections() override { return m_cols; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_cols) if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &) override { return {}; }

    QList<BackendRecord> loadRecords(const QString &id) override
    { return m_records.value(id); }
    std::optional<BackendRecord> loadRecord(const QString &) override { return std::nullopt; }
    QString createRecord(const QString &, const BackendRecord &) override { return {}; }
    bool    updateRecord(const BackendRecord &) override { return false; }
    bool    deleteRecord(const QString &)        override { return false; }
    QList<BackendRecord> modifiedSince(const QString &id, const QDateTime &) override
    { return loadRecords(id); }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }

    void setCollections(const QList<CollectionInfo> &c) { m_cols = c; }
    void setRecords(const QString &id, const QList<BackendRecord> &r) { m_records[id] = r; }

private:
    QList<CollectionInfo>                          m_cols;
    QHash<QString, QList<BackendRecord>>           m_records;
};

class FakeBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    QString pluginId()    const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("1.0"); }
    QIcon   icon()        const override { return {}; }
    QStringList claimedDatabases() const override { return {}; }

    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        auto *b = new FakeBlobBackend;
        b->setCollections({m_col});
        b->setRecords(m_col.id, m_records);
        return { b, nullptr };
    }

    void setCollection(const Kalburator::Sync::CollectionInfo &c) { m_col = c; }
    void setRecords(const QList<Kalburator::Sync::BackendRecord> &r) { m_records = r; }

private:
    Kalburator::Sync::CollectionInfo                   m_col;
    QList<Kalburator::Sync::BackendRecord>             m_records;
};

class FakePluginManager : public WildPalms::BackendPluginManager
{
public:
    FakePluginManager() : WildPalms::BackendPluginManager(nullptr, nullptr, nullptr, nullptr) {}
    bool injectPlugin(const QString &id, WildPalms::IBackendPlugin *p)
    {
        return registerInstanceForTest(id, p);
    }
};

} // namespace
```

Add new test cases inside `TestInstallSourceCollector`:

```cpp
    void collect_pluginBlobs_writesRecordsToTempDir()
    {
        auto *plugin = new FakeBackendPlugin;
        Kalburator::Sync::CollectionInfo col;
        col.id = QStringLiteral("plucker:bootstrap");
        col.name = QStringLiteral("Bootstrap");
        col.type = QStringLiteral("plucker");
        plugin->setCollection(col);

        Kalburator::Sync::BackendRecord r;
        r.id          = QStringLiteral("bootstrap:syszlib");
        r.type        = QStringLiteral("plucker-bootstrap");
        r.displayName = QStringLiteral("SysZLib.prc");
        r.data        = QByteArray("SZLB");
        plugin->setRecords({r});

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);

        QCOMPARE(result.files.size(), 1);
        QCOMPARE(result.files[0].displayName, QStringLiteral("SysZLib.prc"));
        QVERIFY(result.tempDir);
        QVERIFY(QFile::exists(result.files[0].path));
        QFile f(result.files[0].path);
        f.open(QIODevice::ReadOnly);
        QCOMPARE(f.readAll(), QByteArray("SZLB"));
    }

    void collect_pluginBlobs_skipsNonInstallableTypes()
    {
        auto *plugin = new FakeBackendPlugin;
        Kalburator::Sync::CollectionInfo col;
        col.id = QStringLiteral("memo:notes");
        col.type = QStringLiteral("memo");
        plugin->setCollection(col);

        Kalburator::Sync::BackendRecord r;
        r.id   = QStringLiteral("note:1");
        r.type = QStringLiteral("memo-text");
        r.data = QByteArray("# Hello");
        plugin->setRecords({r});

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);
        QVERIFY(result.files.isEmpty());
    }

    void collect_pluginBlobs_emptyCollectionEmitsNothing()
    {
        auto *plugin = new FakeBackendPlugin;
        Kalburator::Sync::CollectionInfo col;
        col.id   = QStringLiteral("plucker:channels");
        col.type = QStringLiteral("plucker");
        plugin->setCollection(col);
        plugin->setRecords({});  // no records

        FakePluginManager mgr;
        mgr.injectPlugin(QStringLiteral("fake"), plugin);

        InstallSourceCollector c;
        const auto result = c.collect(QString(), &mgr);
        QVERIFY(result.files.isEmpty());
    }
```

Add the moc include at the bottom:
```cpp
#include "tst_installsourcecollector.moc"
```
(if not already there).

- [ ] **Step 2: Run the test to verify the new cases fail**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_installsourcecollector
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_installsourcecollector$' --output-on-failure 2>&1 | tail -20
```
Expected: existing 5 cases still PASS; 3 new cases FAIL (`drainPluginBlobs` returns empty stub).

- [ ] **Step 3: Implement `drainPluginBlobs` in `src/runtime/installsourcecollector.cpp`**

Replace the stub body with:

```cpp
QList<InstallSourceCollector::FileEntry>
InstallSourceCollector::drainPluginBlobs(BackendPluginManager *manager,
                                            QTemporaryDir       *dir)
{
    QList<FileEntry> out;
    if (!manager || !dir) return out;

    for (auto *plugin : manager->plugins()) {
        if (!plugin) continue;
        auto backends = plugin->createBackends(nullptr, nullptr);
        auto *blob = backends.blob;
        if (!blob) continue;

        for (const auto &col : blob->availableCollections()) {
            const auto records = blob->loadRecords(col.id);
            for (const auto &rec : records) {
                if (!isInstallableType(rec.type)) continue;
                if (rec.data.isEmpty()) continue;

                const QString ext  = inferExtension(rec.type, rec.displayName);
                const QString stem = rec.displayName.isEmpty()
                                       ? rec.id.section(QChar(':'), -1)
                                       : QFileInfo(rec.displayName).completeBaseName();
                QString fileName = stem;
                if (!fileName.endsWith(ext, Qt::CaseInsensitive)) fileName += ext;
                const QString path = QDir(dir->path()).filePath(fileName);

                QFile f(path);
                if (!f.open(QIODevice::WriteOnly)) continue;
                f.write(rec.data);
                f.close();

                FileEntry e;
                e.path        = path;
                e.displayName = rec.displayName.isEmpty() ? fileName : rec.displayName;
                out.append(e);
            }
        }
        delete blob;   // we own the backend per IBackendPlugin contract
    }
    return out;
}
```

(`#include <QFileInfo>` if not already included.)

- [ ] **Step 4: Re-run the test**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_installsourcecollector
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_installsourcecollector$' --output-on-failure
```
Expected: all 8 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/installsourcecollector.cpp \
        tests/plugins/install/tst_installsourcecollector.cpp
git commit -m "$(cat <<'EOF'
feat(runtime): InstallSourceCollector plugin-blob aggregation

drainPluginBlobs iterates BackendPluginManager-loaded plugins,
constructs a fresh blob backend per plugin (per IBackendPlugin
contract), filters collections to records whose type ends with
-prc/-pdb/-bootstrap, writes each record's data to a fresh
QTemporaryDir-managed file. Caller owns the temp dir lifetime via
the returned Result.

Phase E.15a Task 5.
EOF
)"
```

---

## Task 6: `InstallActionPlugin` (TDD)

**Files:**
- Create: `src/plugins/install/installactionplugin.h`
- Create: `src/plugins/install/installactionplugin.cpp`
- Create: `src/plugins/install/install-action-plugin.json`
- Create: `tests/plugins/install/tst_installactionplugin.cpp`
- Modify: `src/plugins/install/CMakeLists.txt` (add toggle + V2 target)
- Modify: `tests/plugins/install/CMakeLists.txt` (add action plugin test)

**Goal:** The action itself. Stateless, consumes `params["files"]`, calls `device->fileInstaller()->installFile(path)` per entry, emits per-file signals + log/progress via `ActionContext`. Land the V2 manifest and CMake toggle in this task too so the plugin .so builds.

- [ ] **Step 1: Create `tests/plugins/install/tst_installactionplugin.cpp`**

```cpp
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/mockpalmfileinstaller.h"
#include "plugins/install/installactionplugin.h"
#include "runtime/simpleactioncontext.h"

using namespace WildPalms;
using namespace WildPalms::PalmSync;

namespace {
QJsonObject makeFile(const QString &path, const QString &name)
{
    QJsonObject o;
    o["path"]         = path;
    o["display_name"] = name;
    return o;
}
} // namespace

class TestInstallActionPlugin : public QObject
{
    Q_OBJECT

private slots:
    void execute_emptyList_returnsTrue()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonObject params;
        params["files"] = QJsonArray{};
        QVERIFY(action.execute(&ctx, &conn, params));
        QCOMPARE(inst.installedPaths().size(), 0);
    }

    void execute_installsAllAndReturnsTrue()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a.prc")));
        files.append(makeFile(QStringLiteral("/tmp/b.pdb"), QStringLiteral("b.pdb")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(action.execute(&ctx, &conn, params));
        QCOMPARE(inst.installedPaths(), (QStringList{
            QStringLiteral("/tmp/a.prc"), QStringLiteral("/tmp/b.pdb")}));
        QCOMPARE(ctx.total(), 2);
        QCOMPARE(ctx.current(), 2);
    }

    void execute_partialFailure_returnsFalseEmitsSignals()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        inst.setNextResult(true);                                 // a OK
        inst.setNextResult(false, QStringLiteral("simulated"));   // b fails
        inst.setNextResult(true);                                 // c OK

        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QSignalSpy spyOk    (&action, &InstallActionPlugin::fileInstalled);
        QSignalSpy spyFail  (&action, &InstallActionPlugin::fileFailed);

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        files.append(makeFile(QStringLiteral("/tmp/b.prc"), QStringLiteral("b")));
        files.append(makeFile(QStringLiteral("/tmp/c.prc"), QStringLiteral("c")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
        QCOMPARE(spyOk.count(),   2);
        QCOMPARE(spyFail.count(), 1);
        QCOMPARE(inst.installedPaths().size(), 3);
    }

    void execute_cancellation_stopsLoop()
    {
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        // Cancel after the first install.
        QObject::connect(&action, &InstallActionPlugin::fileInstalled,
                          [&ctx](const QString &) { ctx.cancel(); });

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        files.append(makeFile(QStringLiteral("/tmp/b.prc"), QStringLiteral("b")));
        files.append(makeFile(QStringLiteral("/tmp/c.prc"), QStringLiteral("c")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
        // Only the first file went through.
        QCOMPARE(inst.installedPaths().size(), 1);
    }

    void execute_noFileInstaller_returnsFalse()
    {
        MockPalmDatabaseAccess  db;
        PalmDeviceConnection    conn(&db);                  // no installer
        SimpleActionContext     ctx;
        InstallActionPlugin     action;

        QJsonArray files;
        files.append(makeFile(QStringLiteral("/tmp/a.prc"), QStringLiteral("a")));
        QJsonObject params;
        params["files"] = files;

        QVERIFY(!action.execute(&ctx, &conn, params));
    }

    void preconditions_requiresDevice()
    {
        InstallActionPlugin a;
        QCOMPARE(a.preconditions().requiresDeviceConnection, true);
        QVERIFY(a.preconditions().requiresFiles.isEmpty());
    }

    void identity_metadata()
    {
        InstallActionPlugin a;
        QCOMPARE(a.pluginId(),    QStringLiteral("install"));
        QCOMPARE(a.displayName(), QStringLiteral("Install Files"));
        QCOMPARE(a.version(),     QStringLiteral("2.0.0"));
    }
};

QTEST_MAIN(TestInstallActionPlugin)
#include "tst_installactionplugin.moc"
```

- [ ] **Step 2: Update `src/plugins/install/CMakeLists.txt` to add the V2 toggle**

Replace the entire file with:

```cmake
option(WILDPALMS_INSTALL_PLUGIN_V2
    "Build the new IPluginAction-based Install plugin" ON)

if (WILDPALMS_INSTALL_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_install_v2_action
        SOURCES
            installactionplugin.cpp
            installactionplugin.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_install_v2_action
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    target_include_directories(wildpalms_install_v2_action BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_install_v2_action
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            WildPalmsPalmSync
            KF6::CoreAddons
            Qt::Core
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_install
        SOURCES
            installconduit.cpp
            installconduit.h
            installview.cpp
            installview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_install
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 3: Create the V2 manifest `src/plugins/install/install-action-plugin.json`**

```json
{
    "KPlugin": {
        "Name": "Install Files",
        "Description": "Install .prc / .pdb files onto the connected Palm",
        "Icon": "document-import",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0.0",
        "Category": "Sync",
        "Id": "install"
    },
    "X-WildPalms-PluginType":   "action",
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder":     50
}
```

- [ ] **Step 4: Append action-plugin test target to `tests/plugins/install/CMakeLists.txt`**

```cmake

# --- Task 6: InstallActionPlugin ---
add_executable(tst_installactionplugin
    tst_installactionplugin.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/install/installactionplugin.cpp
)
target_include_directories(tst_installactionplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_installactionplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_installactionplugin
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmSync
        Kalburator::Sync
)
add_test(NAME tst_installactionplugin COMMAND tst_installactionplugin)
```

- [ ] **Step 5: Build the test and verify it fails**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build-dev 2>&1 | tail -3
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_installactionplugin 2>&1 | tail -10
```
Expected: build fails — `installactionplugin.h` does not exist.

- [ ] **Step 6: Create `src/plugins/install/installactionplugin.h`**

```cpp
#ifndef WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H
#define WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H

#include <QObject>

#include "core/ipluginaction.h"

namespace WildPalms {

class InstallActionPlugin : public QObject, public IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)

public:
    explicit InstallActionPlugin(QObject *parent = nullptr);

    // ===== IPlugin =====
    QString pluginId()    const override { return QStringLiteral("install"); }
    QString displayName() const override { return QStringLiteral("Install Files"); }
    QString description() const override
    { return QStringLiteral("Install .prc / .pdb files onto the connected Palm"); }
    QString version()     const override { return QStringLiteral("2.0.0"); }
    QIcon   icon()        const override;

    bool hasSettings() const override { return false; }

    // ===== IPluginAction =====
    bool execute(ActionContext       *ctx,
                  PalmDeviceConnection *device,
                  const QJsonObject   &parameters) override;

    Preconditions preconditions() const override
    {
        return { /*requiresDeviceConnection=*/ true,
                  /*requiresFiles=*/             {} };
    }

Q_SIGNALS:
    void fileInstalled(const QString &path);
    void fileFailed(const QString &path, const QString &errorMessage);
};

} // namespace WildPalms

#endif // WILDPALMS_INSTALL_INSTALLACTIONPLUGIN_H
```

- [ ] **Step 7: Create `src/plugins/install/installactionplugin.cpp`**

```cpp
#include "installactionplugin.h"

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>

#include <KPluginFactory>

#include "palm/palmdeviceconnection.h"
#include "palm/sync/ipalmfileinstaller.h" // unused header noted; keep include local
// Real declaration lives in src/palm/device/ipalmfileinstaller.h —
// transitively pulled via PalmDeviceConnection::fileInstaller().

namespace WildPalms {

InstallActionPlugin::InstallActionPlugin(QObject *parent)
    : QObject(parent)
{
}

QIcon InstallActionPlugin::icon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

bool InstallActionPlugin::execute(ActionContext       *ctx,
                                    PalmDeviceConnection *device,
                                    const QJsonObject   &parameters)
{
    auto *installer = device ? device->fileInstaller() : nullptr;
    if (!installer) {
        if (ctx) ctx->log(QStringLiteral("Install: no file installer available"));
        return false;
    }

    const QJsonArray files = parameters.value(QStringLiteral("files")).toArray();
    if (ctx) ctx->setTotal(files.size());

    int succeeded = 0;
    int failed    = 0;
    bool cancelled = false;

    for (int i = 0; i < files.size(); ++i) {
        if (ctx && ctx->isCancelled()) {
            cancelled = true;
            if (ctx) ctx->log(QStringLiteral("Install: cancelled at %1/%2")
                               .arg(i).arg(files.size()));
            break;
        }
        const QJsonObject f = files[i].toObject();
        const QString path  = f.value(QStringLiteral("path")).toString();
        const QString name  = f.value(QStringLiteral("display_name")).toString();

        QString err;
        const bool ok = installer->installFile(path, &err);
        if (ok) {
            ++succeeded;
            if (ctx) ctx->log(QStringLiteral("Installed: %1").arg(name));
            Q_EMIT fileInstalled(path);
        } else {
            ++failed;
            if (ctx) ctx->log(QStringLiteral("Failed: %1 (%2)").arg(name, err));
            Q_EMIT fileFailed(path, err);
        }
        if (ctx) ctx->setCurrent(i + 1);
    }

    if (ctx) ctx->log(QStringLiteral("Install: %1 succeeded, %2 failed%3")
                       .arg(succeeded)
                       .arg(failed)
                       .arg(cancelled ? QStringLiteral(" (cancelled)") : QString()));
    return failed == 0 && !cancelled;
}

} // namespace WildPalms

K_PLUGIN_FACTORY_WITH_JSON(
    InstallActionPluginFactory,
    "install-action-plugin.json",
    registerPlugin<WildPalms::InstallActionPlugin>();)

#include "installactionplugin.moc"
```

Note: the `#include "palm/sync/ipalmfileinstaller.h"` line above is incorrect — the file lives at `palm/device/ipalmfileinstaller.h`. Replace that include line with:
```cpp
// IPalmFileInstaller is reached via PalmDeviceConnection::fileInstaller() — header is pulled in transitively.
```
(No explicit include needed; we only call `installer->installFile()` and the implicit `IPalmFileInstaller*` type comes through the transitive forward declaration. If the build complains about incomplete type, add `#include "palm/device/ipalmfileinstaller.h"`.)

- [ ] **Step 8: Build and run the action plugin test + the V2 plugin .so**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target wildpalms_install_v2_action tst_installactionplugin 2>&1 | tail -10
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_installactionplugin$' --output-on-failure
```
Expected: clean build of `wildpalms_install_v2_action.so`; PASS for 7 test functions.

- [ ] **Step 9: Commit**

```bash
git add src/plugins/install/installactionplugin.h \
        src/plugins/install/installactionplugin.cpp \
        src/plugins/install/install-action-plugin.json \
        src/plugins/install/CMakeLists.txt \
        tests/plugins/install/tst_installactionplugin.cpp \
        tests/plugins/install/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(install): InstallActionPlugin (IPluginAction)

Stateless action consuming params["files"] = [{path, display_name},
...]. Installs each via device->fileInstaller(), emits fileInstalled
or fileFailed per entry, surfaces progress + log via ActionContext.
Cancellation respected between files. Returns true only on all-
success-no-cancel.

Behind WILDPALMS_INSTALL_PLUGIN_V2=ON; legacy InstallConduit
preserved under toggle OFF until E.16.

Phase E.15a Task 6.
EOF
)"
```

---

## Task 7: Plucker `availableCollections()` order swap

**Files:**
- Modify: `src/plugins/plucker/pluckerblobbackend.cpp`

**Goal:** Make Plucker's `availableCollections()` return `{bootstrap, channels}` so `InstallSourceCollector` drains bootstrap PRCs before channel `.pdb`s. Per Decision #3 in the spec, ordering is presentation-only (sync is atomic), so this is a one-line cosmetic improvement.

- [ ] **Step 1: Edit `src/plugins/plucker/pluckerblobbackend.cpp`**

Find `availableCollections()` and swap the return list. Replace:

```cpp
return {channels, boot};
```

with:

```cpp
return {boot, channels};
```

- [ ] **Step 2: Re-run plucker tests to confirm nothing regresses**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_plucker' --output-on-failure
```
Expected: existing 5 tests still PASS. The blob-backend test asserts `cols.size() == 2` and sorted IDs; no test pinned ordering, so no failures.

- [ ] **Step 3: Commit (submodule + parent pointer)**

Submodule:
```bash
cd src/plugins/plucker
git add pluckerblobbackend.cpp
git commit -m "$(cat <<'EOF'
fix(plucker): availableCollections() returns {bootstrap, channels}

Swap order so install-action drain logs SysZLib + viewer first,
matching install-time presentation expectations. Ordering is
presentation-only — sync is atomic on the device — but the log
reads cleaner.

Phase E.15a Task 7.
EOF
)"
```
Parent:
```bash
cd /home/clinton/dev/WildPalms
git add src/plugins/plucker
git commit -m "fix(plucker): bump submodule — collection order (E.15a Task 7)"
```

---

## Task 8: end-to-end test `tst_install_v2_e2e`

**Files:**
- Create: `tests/plugins/install/tst_install_v2_e2e.cpp`
- Modify: `tests/plugins/install/CMakeLists.txt`

**Goal:** Drive the full pipeline — `InstallSourceCollector` against folder + Plucker-mock backend → `InstallActionPlugin::execute` → `MockPalmFileInstaller` records both bootstrap PRCs and channel `.pdb`s in declared order.

- [ ] **Step 1: Create `tests/plugins/install/tst_install_v2_e2e.cpp`**

```cpp
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/mockpalmfileinstaller.h"
#include "plugins/install/installactionplugin.h"
#include "runtime/backendpluginmanager.h"
#include "runtime/installsourcecollector.h"
#include "runtime/simpleactioncontext.h"

#include <iblobbackend.h>

using namespace Kalburator::Sync;
using namespace WildPalms;
using namespace WildPalms::PalmSync;

namespace {

class StubBlobBackend : public IBlobBackend
{
    Q_OBJECT
public:
    QString backendId() const override   { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    bool    isAvailable() const override { return true; }
    QList<CollectionInfo> availableCollections() override { return m_cols; }
    CollectionInfo collectionInfo(const QString &id) override
    {
        for (const auto &c : m_cols) if (c.id == id) return c;
        return {};
    }
    QString createCollection(const CollectionInfo &) override { return {}; }
    QList<BackendRecord> loadRecords(const QString &id) override
    { return m_records.value(id); }
    std::optional<BackendRecord> loadRecord(const QString &) override { return std::nullopt; }
    QString createRecord(const QString &, const BackendRecord &) override { return {}; }
    bool    updateRecord(const BackendRecord &) override { return false; }
    bool    deleteRecord(const QString &)        override { return false; }
    QList<BackendRecord> modifiedSince(const QString &id, const QDateTime &) override
    { return loadRecords(id); }
    QStringList deletedSince(const QString &, const QDateTime &) override { return {}; }

    QList<CollectionInfo>                          m_cols;
    QHash<QString, QList<BackendRecord>>           m_records;
};

class StubBackendPlugin : public QObject, public IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    QString pluginId()    const override { return QStringLiteral("stubplucker"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QString description() const override { return {}; }
    QString version()     const override { return QStringLiteral("1.0"); }
    QIcon   icon()        const override { return {}; }
    QStringList claimedDatabases() const override { return {}; }

    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *,
                                     PalmDeviceConnection *) override
    {
        auto *b = new StubBlobBackend;
        // Bootstrap collection first (drained first).
        CollectionInfo boot;
        boot.id   = QStringLiteral("stub:bootstrap");
        boot.name = QStringLiteral("Bootstrap");
        boot.type = QStringLiteral("plucker");
        CollectionInfo channels;
        channels.id   = QStringLiteral("stub:channels");
        channels.name = QStringLiteral("Channels");
        channels.type = QStringLiteral("plucker");
        b->m_cols = {boot, channels};

        BackendRecord syszlib;
        syszlib.id          = QStringLiteral("bootstrap:syszlib");
        syszlib.type        = QStringLiteral("plucker-bootstrap");
        syszlib.displayName = QStringLiteral("SysZLib.prc");
        syszlib.data        = QByteArray("SZLB");
        BackendRecord viewer;
        viewer.id          = QStringLiteral("bootstrap:viewer");
        viewer.type        = QStringLiteral("plucker-bootstrap");
        viewer.displayName = QStringLiteral("viewer_en.prc");
        viewer.data        = QByteArray("VIEW");
        BackendRecord ch;
        ch.id          = QStringLiteral("channel:bbc");
        ch.type        = QStringLiteral("plucker-pdb");
        ch.displayName = QStringLiteral("BBC");
        ch.data        = QByteArray("PDB:BBC");

        b->m_records[boot.id]     = {syszlib, viewer};
        b->m_records[channels.id] = {ch};

        return { b, nullptr };
    }
};

class StubPluginManager : public BackendPluginManager
{
public:
    StubPluginManager() : BackendPluginManager(nullptr, nullptr, nullptr, nullptr) {}
    bool inject(const QString &id, IBackendPlugin *p)
    { return registerInstanceForTest(id, p); }
};

} // namespace

class TestInstallV2E2E : public QObject
{
    Q_OBJECT

private slots:
    void e2e_folderAndPluginBlobs_drainCorrectly()
    {
        // Folder fixture
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile::copy(QStringLiteral(INSTALL_FIXTURE_DIR "/dummy.prc"),
                    QDir(folder.path()).filePath(QStringLiteral("user.prc")));

        // Plugin fixture
        StubPluginManager mgr;
        mgr.inject(QStringLiteral("stubplucker"), new StubBackendPlugin);

        // Mock device + installer
        MockPalmDatabaseAccess  db;
        MockPalmFileInstaller   inst;
        PalmDeviceConnection    conn(&db, &inst);

        // Aggregate
        InstallSourceCollector collector;
        const auto result = collector.collect(folder.path(), &mgr);

        // Should have: 1 folder + 2 bootstrap + 1 channel = 4 entries.
        QCOMPARE(result.files.size(), 4);

        QJsonArray filesArr;
        for (const auto &f : result.files) {
            QJsonObject o;
            o[QStringLiteral("path")]         = f.path;
            o[QStringLiteral("display_name")] = f.displayName;
            filesArr.append(o);
        }
        QJsonObject params;
        params[QStringLiteral("files")] = filesArr;

        // Run action
        InstallActionPlugin action;
        SimpleActionContext ctx;
        QVERIFY(action.execute(&ctx, &conn, params));

        // Verify mock got every path; verify bootstrap → channels order
        // (order within a backend is preserved by collector).
        QCOMPARE(inst.installedPaths().size(), 4);

        QStringList names;
        for (const auto &p : inst.installedPaths()) names << QFileInfo(p).fileName();
        QVERIFY(names.contains(QStringLiteral("user.prc")));
        // Bootstrap entries appear before the channel entry from the same plugin.
        const int bootIdx    = names.indexOf(QStringLiteral("SysZLib.prc"));
        const int viewerIdx  = names.indexOf(QStringLiteral("viewer_en.prc"));
        const int channelIdx = -1 < names.indexOf(QStringLiteral("BBC.pdb"))
                                  ? names.indexOf(QStringLiteral("BBC.pdb"))
                                  : names.indexOf(QStringLiteral("BBC"));
        QVERIFY(bootIdx    >= 0);
        QVERIFY(viewerIdx  >= 0);
        QVERIFY(channelIdx >= 0);
        QVERIFY(bootIdx    < channelIdx);
        QVERIFY(viewerIdx  < channelIdx);

        // Folder-sourced "user.prc" should be moveable to installed/.
        InstallSourceCollector().moveSucceededToInstalled(
            result, inst.installedPaths());
        QVERIFY(QFile::exists(QDir(folder.path()).filePath(
            QStringLiteral("installed/user.prc"))));
    }
};

QTEST_MAIN(TestInstallV2E2E)
#include "tst_install_v2_e2e.moc"
```

- [ ] **Step 2: Append e2e test target to `tests/plugins/install/CMakeLists.txt`**

```cmake

# --- Task 8: end-to-end ---
add_executable(tst_install_v2_e2e
    tst_install_v2_e2e.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/installsourcecollector.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/install/installactionplugin.cpp
)
target_compile_definitions(tst_install_v2_e2e
    PRIVATE
        INSTALL_FIXTURE_DIR="${INSTALL_FIXTURE_DIR}"
)
target_include_directories(tst_install_v2_e2e
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_install_v2_e2e BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_install_v2_e2e
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmSync
        Kalburator::Sync
)
add_test(NAME tst_install_v2_e2e COMMAND tst_install_v2_e2e)
```

- [ ] **Step 3: Build and run**

```bash
cmake --build /home/clinton/dev/WildPalms/build-dev --target tst_install_v2_e2e
ctest --test-dir /home/clinton/dev/WildPalms/build-dev -R '^tst_install_v2_e2e$' --output-on-failure
```
Expected: PASS. The e2e exercises folder + cross-plugin drain + action execute + move-to-installed.

- [ ] **Step 4: Run the entire WP test suite to verify no regression**

```bash
ctest --test-dir /home/clinton/dev/WildPalms/build-dev --output-on-failure
```
Expected: 77/77 tests pass (72 pre-E.15a + 5 new install tests).

- [ ] **Step 5: Commit**

```bash
git add tests/plugins/install/tst_install_v2_e2e.cpp \
        tests/plugins/install/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(install): tst_install_v2_e2e end-to-end

Verifies folder + Plucker-shaped plugin-blob aggregation through
InstallSourceCollector → InstallActionPlugin::execute →
MockPalmFileInstaller. Confirms ordering (bootstrap before channel
within a plugin), that all 4 entries reach the installer, and that
folder-sourced files move to installed/ on success.

77/77 ctest passes.

Phase E.15a Task 8.
EOF
)"
```

---

## Task 9: Legacy build verification + parent-spec row + memory entry

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (annotate E.15 row partial-✅)
- Create: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e15a_install.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

**Goal:** Confirm the legacy `InstallConduit` still builds with toggle OFF, then mark E.15a done.

- [ ] **Step 1: Verify legacy build (toggle OFF)**

```bash
rm -rf /tmp/build-install-legacy
cmake -S /home/clinton/dev/WildPalms -B /tmp/build-install-legacy \
      -DCMAKE_BUILD_TYPE=Debug \
      -DWILDPALMS_INSTALL_PLUGIN_V2=OFF
cmake --build /tmp/build-install-legacy --target wildpalms_install 2>&1 | tail -5
rm -rf /tmp/build-install-legacy
```
Expected: clean build of the legacy `wildpalms_install` target.

- [ ] **Step 2: Edit the parent spec's E.15 row**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` line 593. Replace:

```
| **E.15** | Rewrite **Install** as `IPluginAction`. UI trigger surface in place (button/menu, not styled yet). | WP | E.14 | Action executes against mock device; progress signals fire. |
```

with:

```
| 🟡 **E.15a** | Install rewritten as `IPluginAction` (`InstallActionPlugin`). New `IPalmFileInstaller` abstraction (`PilotLinkPalmFileInstaller` real + `MockPalmFileInstaller`); `PalmDeviceConnection` gains `fileInstaller()`. New `InstallSourceCollector` at `src/runtime/` aggregates folder + cross-plugin blob backends (Plucker channels + bootstrap, deferred from E.14). Generic `Tools → Actions` MainWindow submenu auto-populated from `PluginActionManager`. Plucker `availableCollections()` order swapped to {bootstrap, channels}. CMake toggle `WILDPALMS_INSTALL_PLUGIN_V2=ON` (default ON); legacy `InstallConduit` remains buildable. Landed 2026-04-26. Plan: `docs/superpowers/plans/2026-04-26-phase-e15a-install-action.md`. | WP | E.14 | WP ctest passes (77/77); ~17 tests across mock/collector/action/e2e plus existing PalmDeviceConnection cases. |
| **E.15b** | `git mv src/fullsync/* src/runtime/`; fold `WildPalmsFullSync` static lib into `WildPalmsRuntime`; update `WildPalmsCore` link list. Mechanical relocation deferred from E.8. | WP | E.15a | WP ctest passes; libkalburator ctest passes; library graph still builds. |
```

- [ ] **Step 3: Create the memory file**

`/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e15a_install.md`:

```markdown
---
name: Phase E.15a — Install action plugin landed
description: E.15a landed 2026-04-26; Install is the first IPluginAction; cross-plugin blob drain via InstallSourceCollector; fullsync→runtime relocation split to E.15b
type: project
---

E.15a landed 2026-04-26. Install is the first `IPluginAction` plugin.
Architecture is two-tiered: `InstallSourceCollector` aggregates from
folder (`<sync>/install/*.{prc,pdb}`) + cross-plugin blob backends
(currently Plucker only; `*-prc`/`*-pdb`/`*-bootstrap` types match);
`InstallActionPlugin::execute()` consumes a `params["files"]` list
and calls `device->fileInstaller()->installFile(path)` per entry.

**Why two-tier:** action stays pure (install N paths → N pisock calls);
aggregation has separate concerns (folder I/O, plugin queries, temp-
file management, post-install moves) and is testable independently.

**How to apply:** When working on E.16 (legacy delete), the legacy
`InstallConduit` + `InstallView` go. The `installFile` logic in
legacy already moved to `PilotLinkPalmFileInstaller`; nothing
inherited from the legacy conduit needs preserving. The
`<sync>/install/installed/` move pattern stays — moved into
`InstallSourceCollector::moveSucceededToInstalled`.

**Splits:** the parent spec's E.15 row was decomposed into E.15a (this
sub-phase, install action) and E.15b (`fullsync`→`runtime`
relocation, separate spec/plan; no install dependency).

**Tests:** 5 new test executables (`tst_palmfileinstaller`,
`tst_installsourcecollector`, `tst_installactionplugin`,
`tst_install_v2_e2e`) + 2 new cases on `tst_palmdeviceconnection`.
77/77 WP ctest passes.

**Plucker collection-order swap:** `availableCollections()` now
returns `{bootstrap, channels}` (was `{channels, bootstrap}`). Per
spec Decision #3 this is presentation-only — sync is atomic on the
device; an interrupted HotSync leaving an orphan `.pdb` without its
viewer is cosmetic.

Toggle `WILDPALMS_INSTALL_PLUGIN_V2=ON` (default ON). Legacy
`InstallConduit` stays buildable until E.16.
```

- [ ] **Step 4: Append memory index entry**

Append after the existing E.14 line:

```
- [project_phase_e15a_install.md](project_phase_e15a_install.md) — E.15a landed 2026-04-26; Install is first IPluginAction; InstallSourceCollector folder + cross-plugin drain
```

- [ ] **Step 5: Commit the parent spec change**

```bash
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md
git commit -m "docs(phase-e): mark E.15a (Install action) landed; defer E.15b to its own row"
```

The memory updates persist locally; no commit needed there.

---

## Self-review checklist

- [ ] Every task lists exact file paths under "Files".
- [ ] Every code step shows the actual code, not a placeholder.
- [ ] Every test step shows the assertion / `QCOMPARE` calls.
- [ ] Type names used in later tasks match earlier ones (`IPalmFileInstaller`, `MockPalmFileInstaller`, `PilotLinkPalmFileInstaller`, `InstallSourceCollector`, `InstallActionPlugin`).
- [ ] Each task ends in a commit (or, for verification-only tasks, a justification why no commit).
- [ ] CMake toggle `WILDPALMS_INSTALL_PLUGIN_V2` is referenced consistently.
- [ ] Plugin manifest `X-WildPalms-PluginType: action` matches what `PluginActionManager` filters on.
- [ ] Predicate `isInstallableType` matches the `BackendRecord::type` strings Plucker emits (`plucker-bootstrap`, `plucker-pdb`).
- [ ] Tests cover spec requirements: action execute, cancellation, no-installer, partial failure; collector folder + plugin blobs + post-install move.

---

**Total tasks:** 9 (UI wiring deferred to E.17 — see spec Decision #4).
**Expected new test executables:** 4 (`tst_palmfileinstaller`, `tst_installsourcecollector`, `tst_installactionplugin`, `tst_install_v2_e2e`) + 2 new cases on existing `tst_palmdeviceconnection`.
**Expected pre-E.15a test count:** 72 → post-E.15a: 72 + 4 = 76 (each test executable counts as one ctest entry; the 2 new cases on tst_palmdeviceconnection don't add a new entry).

If the actual post-count differs, investigate before claiming success.
