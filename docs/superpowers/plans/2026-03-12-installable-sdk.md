# Installable WildPalms SDK — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make WildPalms installable as a CMake config-file package so external projects can build conduit plugins with `find_package(WildPalms)`.

**Architecture:** Add standard CMake install rules to WildPalmsCore, split PUBLIC/PRIVATE dependencies, promote three KPilotDeviceLink methods to the abstract KPilotLink interface, and generate a CMake config-file package with version checking.

**Tech Stack:** CMake 3.19+, Qt6, KDE Frameworks 6, C++20

**Spec:** `docs/superpowers/specs/2026-03-12-installable-sdk-design.md`

---

## Chunk 1: Prerequisite Code Changes

### Task 1: Promote device methods to KPilotLink abstract interface

`conduit.cpp` calls `isConnected()`, `cleanUpDatabase()`, and `resetSyncFlags()` on `SyncContext::deviceLink`. These exist only on `KPilotDeviceLink` (concrete), not `KPilotLink` (abstract). Promote them so `deviceLink` can be typed as `KPilotLink*`.

**Files:**
- Modify: `src/palm/kpilotlink.h:55-75` (add three pure virtuals)
- Modify: `src/sync/conduit.h:24,37` (change forward decl + member type)

- [ ] **Step 1: Add pure virtual methods to KPilotLink**

In `src/palm/kpilotlink.h`, add these three methods to the public section, after the existing `endSync()` declaration (line 75):

```cpp
    // Post-sync operations
    virtual bool isConnected() const = 0;
    virtual bool cleanUpDatabase(int dbHandle) = 0;
    virtual bool resetSyncFlags(int dbHandle) = 0;
```

- [ ] **Step 2: Change SyncContext::deviceLink type**

In `src/sync/conduit.h`:

Change the forward declaration (line 24) from:
```cpp
class KPilotDeviceLink;
```
to:
```cpp
class KPilotLink;
```

Change the member (line 37) from:
```cpp
    KPilotDeviceLink *deviceLink = nullptr;  ///< Connection to Palm device
```
to:
```cpp
    KPilotLink *deviceLink = nullptr;  ///< Connection to Palm device
```

- [ ] **Step 3: Replace concrete include in conduit.cpp**

In `src/sync/conduit.cpp`, change line 2 from:
```cpp
#include "../palm/kpilotdevicelink.h"
```
to:
```cpp
#include "../palm/kpilotlink.h"
```

`conduit.h` only forward-declares `KPilotLink` — `conduit.cpp` needs the full class definition to call methods like `isConnected()`, `openDatabase()`, etc.

- [ ] **Step 4: Add override to KPilotDeviceLink**

In `src/palm/kpilotdevicelink.h`, add `override` to the three promoted methods to match project style:

Line 97 — change:
```cpp
    bool isConnected() const { return m_isConnected; }
```
to:
```cpp
    bool isConnected() const override { return m_isConnected; }
```

Line 151 — change:
```cpp
    bool cleanUpDatabase(int dbHandle);
```
to:
```cpp
    bool cleanUpDatabase(int dbHandle) override;
```

Line 159 — change:
```cpp
    bool resetSyncFlags(int dbHandle);
```
to:
```cpp
    bool resetSyncFlags(int dbHandle) override;
```

- [ ] **Step 5: Fix in-tree plugins that downcast deviceLink**

Two plugins access `KPilotDeviceLink`-only methods through `context->deviceLink`. Since the type is now `KPilotLink*`, they need explicit downcasts.

In `src/plugins/plucker/pluckerconduit.cpp`, find the line that assigns `context->deviceLink` to a `KPilotDeviceLink*` (around line 222) and add a `dynamic_cast`:

```cpp
auto *link = dynamic_cast<KPilotDeviceLink*>(context->deviceLink);
if (!link) {
    emit errorOccurred("Plucker requires a real device connection");
    return result;
}
```

In `src/plugins/install/installconduit.cpp`, find the `socketDescriptor()` call (around line 123) and add a `dynamic_cast`:

```cpp
auto *devLink = dynamic_cast<KPilotDeviceLink*>(context->deviceLink);
if (!devLink) {
    emit errorOccurred("Install conduit requires a real device connection");
    return result;
}
int socket = devLink->socketDescriptor();
```

Both plugins must `#include "../palm/kpilotdevicelink.h"` if they don't already.

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build 2>&1 | tail -30`
Then: `cd build && ctest --output-on-failure 2>&1 | tail -40`

Expected: All compiles. All tests pass. `KPilotDeviceLink` already implements these methods — it now satisfies the abstract interface. The in-tree plugins compile with their explicit downcasts.

- [ ] **Step 7: Commit**

```bash
git add src/palm/kpilotlink.h src/palm/kpilotdevicelink.h src/sync/conduit.h src/sync/conduit.cpp src/plugins/plucker/pluckerconduit.cpp src/plugins/install/installconduit.cpp
git commit -m "refactor: promote isConnected/cleanUpDatabase/resetSyncFlags to KPilotLink interface

Prerequisite for SDK: SyncContext::deviceLink must be the abstract
KPilotLink* type so conduit.h doesn't depend on KPilotDeviceLink.
Plugins that need the concrete type use dynamic_cast."
```

### Task 2: Split PUBLIC/PRIVATE link dependencies and include dirs

Currently all dependencies on WildPalmsCore are PUBLIC. Most are application internals. Split them so the exported target only requires Qt6 Core/Widgets and KF6::I18n.

**Files:**
- Modify: `src/CMakeLists.txt:105-131`

- [ ] **Step 1: Refactor target_link_libraries**

In `src/CMakeLists.txt`, replace the existing `target_link_libraries` block (lines 105-121) with:

```cmake
target_link_libraries(WildPalmsCore
    PUBLIC
        Qt::Core
        Qt::Widgets
        KF6::I18n
    PRIVATE
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Notifications
        KF6::StatusNotifierItem
        pisock
        ${UDEV_LIBRARIES}
)
```

- [ ] **Step 2: Split target_include_directories**

Replace the existing `target_include_directories` block (lines 123-131) with:

```cmake
target_include_directories(WildPalmsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sync>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/palm>
    $<INSTALL_INTERFACE:include/wildpalms>
    $<INSTALL_INTERFACE:include/wildpalms/core>
    $<INSTALL_INTERFACE:include/wildpalms/sync>
    $<INSTALL_INTERFACE:include/wildpalms/palm>
    $<INSTALL_INTERFACE:include/wildpalms/pilot-link>
)

target_include_directories(WildPalmsCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/kf6
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${UDEV_INCLUDE_DIRS}
)
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build 2>&1 | tail -30`
Then: `cd build && ctest --output-on-failure 2>&1 | tail -40`

Expected: All compiles. All tests pass. The in-tree plugins already find headers through their own include directories and link against WildPalmsCore which contains all symbols regardless of PUBLIC/PRIVATE visibility.

If compilation fails because some source file in WildPalmsCore itself can't find a header that was previously PUBLIC, the fix is to add the missing `#include` explicitly — the PUBLIC include dirs were masking missing includes.

- [ ] **Step 4: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "refactor: split WildPalmsCore PUBLIC/PRIVATE deps and include dirs

Only Qt::Core, Qt::Widgets, and KF6::I18n are PUBLIC. All other
dependencies are application internals moved to PRIVATE. Include dirs
use generator expressions for build/install split."
```

## Chunk 2: CMake Install Infrastructure

### Task 3: Add SOVERSION and RPATH

**Files:**
- Modify: `src/CMakeLists.txt` (add set_target_properties)
- Modify: `CMakeLists.txt` (add RPATH policy)

- [ ] **Step 1: Add SOVERSION to WildPalmsCore**

In `src/CMakeLists.txt`, add after the `target_include_directories` blocks (before the `if(NOT WILDPALMS_INSTALLED)` block):

```cmake
set_target_properties(WildPalmsCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
```

- [ ] **Step 2: Add RPATH policy to top-level CMakeLists.txt**

In `CMakeLists.txt`, add after line 26 (`include(KDEInstallDirs6)`):

```cmake
# RPATH for installed binaries/libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build 2>&1 | tail -20`

Expected: Compiles. Check that the soname is set:
```bash
readelf -d build/lib/libWildPalmsCore.so | grep SONAME
```
Expected output contains: `(SONAME)  Library soname: [libWildPalmsCore.so.0]`

- [ ] **Step 4: Commit**

```bash
git add src/CMakeLists.txt CMakeLists.txt
git commit -m "build: add SOVERSION and RPATH policy to WildPalmsCore

Sets VERSION=0.1.1, SOVERSION=0 for proper symlink chain.
RPATH policy ensures installed .so is findable at runtime."
```

### Task 4: Add export and header install rules

**Files:**
- Modify: `src/CMakeLists.txt:143-146` (update install target)
- Modify: `lib/CMakeLists.txt` (add pilot-link header install)

- [ ] **Step 1: Update WildPalmsCore install with EXPORT**

In `src/CMakeLists.txt`, replace the existing install block (lines 143-146):

```cmake
# Install the shared library
install(TARGETS WildPalmsCore
    LIBRARY DESTINATION lib
)
```

with:

```cmake
# Install the shared library with export
install(TARGETS WildPalmsCore
    EXPORT WildPalmsTargets
    LIBRARY DESTINATION lib
)

# Install public SDK headers
install(FILES
    core/iconduit.h
    core/isyncconduit.h
    core/itoolconduit.h
    DESTINATION include/wildpalms/core
)

install(FILES
    sync/conduit.h
    sync/synctypes.h
    sync/syncstate.h
    sync/syncbackend.h
    DESTINATION include/wildpalms/sync
)

install(FILES
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.h
    sync/qsynccore/conflictrecord.h
    sync/qsynccore/conflictstore.h
    sync/qsynccore/conflictpolicy.h
    DESTINATION include/wildpalms/sync/qsynccore
)

install(FILES
    palm/pilotrecord.h
    palm/categoryinfo.h
    palm/kpilotlink.h
    DESTINATION include/wildpalms/palm
)
```

- [ ] **Step 2: Install pilot-link headers**

In `lib/CMakeLists.txt`, add at the end:

```cmake
# Install pilot-link headers for SDK consumers
install(DIRECTORY ${PILOT_LINK_INSTALL_DIR}/include/
    DESTINATION include/wildpalms/pilot-link
)
```

- [ ] **Step 3: Build and test install**

```bash
cmake --build build
cmake --install build --prefix /tmp/wildpalms-sdk
```

Verify the header tree:
```bash
find /tmp/wildpalms-sdk/include/wildpalms -type f | sort
```

Expected output includes:
```
/tmp/wildpalms-sdk/include/wildpalms/core/iconduit.h
/tmp/wildpalms-sdk/include/wildpalms/core/isyncconduit.h
/tmp/wildpalms-sdk/include/wildpalms/core/itoolconduit.h
/tmp/wildpalms-sdk/include/wildpalms/palm/categoryinfo.h
/tmp/wildpalms-sdk/include/wildpalms/palm/kpilotlink.h
/tmp/wildpalms-sdk/include/wildpalms/palm/pilotrecord.h
/tmp/wildpalms-sdk/include/wildpalms/pilot-link/pi-appinfo.h
... (all pi-*.h files)
/tmp/wildpalms-sdk/include/wildpalms/sync/conduit.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/baselinestore.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/conflictpolicy.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/conflictrecord.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/conflictstore.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/idmappingstore.h
/tmp/wildpalms-sdk/include/wildpalms/sync/qsynccore/synccommon.h
/tmp/wildpalms-sdk/include/wildpalms/sync/syncbackend.h
/tmp/wildpalms-sdk/include/wildpalms/sync/syncstate.h
/tmp/wildpalms-sdk/include/wildpalms/sync/synctypes.h
```

Verify NO application-internal headers are installed:
```bash
find /tmp/wildpalms-sdk/include/wildpalms -name "kpilotdevicelink.h" -o -name "syncengine.h" -o -name "localfilebackend.h" -o -name "conduitmanager.h" | wc -l
```
Expected: `0`

- [ ] **Step 4: Verify soname symlinks**

```bash
ls -la /tmp/wildpalms-sdk/lib/libWildPalmsCore*
```

Expected: three entries — `libWildPalmsCore.so` → `libWildPalmsCore.so.0` → `libWildPalmsCore.so.0.1.1`

- [ ] **Step 5: Commit**

```bash
git add src/CMakeLists.txt lib/CMakeLists.txt
git commit -m "build: add SDK header install rules and export target

Installs public headers under include/wildpalms/ with subdirectory
structure. Bundles pilot-link headers under include/wildpalms/pilot-link/.
Adds EXPORT to WildPalmsCore install for CMake package generation."
```

### Task 5: Create CMake config-file package

**Files:**
- Create: `cmake/WildPalmsConfig.cmake.in`
- Modify: `CMakeLists.txt` (add package config generation)

- [ ] **Step 1: Create the config template**

Create `cmake/WildPalmsConfig.cmake.in`:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)

# Public transitive dependencies
find_dependency(Qt6 6.2 COMPONENTS Core Widgets)
find_dependency(KF6I18n)

# Import WildPalms targets
include("${CMAKE_CURRENT_LIST_DIR}/WildPalmsTargets.cmake")

check_required_components(WildPalms)
```

- [ ] **Step 2: Add package config generation to top-level CMakeLists.txt**

In `CMakeLists.txt`, add at the end (after the existing `install(FILES data/wildpalmsui.rc ...)` block):

```cmake
# === SDK: CMake config-file package ===

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    cmake/WildPalmsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    INSTALL_DESTINATION lib/cmake/WildPalms
)

install(EXPORT WildPalmsTargets
    FILE WildPalmsTargets.cmake
    NAMESPACE WildPalms::
    DESTINATION lib/cmake/WildPalms
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    DESTINATION lib/cmake/WildPalms
)
```

- [ ] **Step 3: Reconfigure, build, and install**

```bash
cmake -B build
cmake --build build
rm -rf /tmp/wildpalms-sdk && cmake --install build --prefix /tmp/wildpalms-sdk
```

- [ ] **Step 4: Verify CMake package files**

```bash
ls /tmp/wildpalms-sdk/lib/cmake/WildPalms/
```

Expected (4 files — CMake generates a per-configuration file alongside the targets):
```
WildPalmsConfig.cmake
WildPalmsConfigVersion.cmake
WildPalmsTargets.cmake
WildPalmsTargets-noconfig.cmake
```

Verify the config file references Qt6 and KF6I18n:
```bash
grep find_dependency /tmp/wildpalms-sdk/lib/cmake/WildPalms/WildPalmsConfig.cmake
```

Expected output:
```
find_dependency(Qt6 6.2 COMPONENTS Core Widgets)
find_dependency(KF6I18n)
```

- [ ] **Step 5: Commit**

```bash
git add cmake/WildPalmsConfig.cmake.in CMakeLists.txt
git commit -m "build: add CMake config-file package for WildPalms SDK

External projects can now use find_package(WildPalms) to get the
WildPalms::Core target with public headers and transitive deps.
Version compatibility uses SameMajorVersion policy."
```

## Chunk 3: Integration Test

### Task 6: Build a test plugin against the installed SDK

Create a minimal external plugin project that uses `find_package(WildPalms)` to verify the full SDK works end-to-end. This is a throwaway test, not committed — just run to validate.

**Files:**
- Create: `/tmp/test-plugin/CMakeLists.txt` (temporary)
- Create: `/tmp/test-plugin/testconduit.h` (temporary)
- Create: `/tmp/test-plugin/testconduit.cpp` (temporary)
- Create: `/tmp/test-plugin/testconduit.json` (temporary)

- [ ] **Step 1: Create the test plugin project**

```bash
mkdir -p /tmp/test-plugin
```

Write `/tmp/test-plugin/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.19)
project(TestConduit VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)

find_package(KF6CoreAddons REQUIRED)
find_package(WildPalms 0.1 REQUIRED)

kcoreaddons_add_plugin(wildpalms_test
    SOURCES testconduit.cpp testconduit.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_test
    WildPalms::Core
    KF6::CoreAddons
    Qt::Widgets
)
```

Write `/tmp/test-plugin/testconduit.json`:

```json
{
    "KPlugin": {
        "Name": "Test Conduit",
        "Description": "Minimal SDK test conduit",
        "Version": "1.0"
    },
    "X-WildPalms-ConduitId": "test",
    "X-WildPalms-PalmDatabases": ["TestDB"]
}
```

Write `/tmp/test-plugin/testconduit.h`:

```cpp
#ifndef TESTCONDUIT_H
#define TESTCONDUIT_H

#include "sync/conduit.h"

class TestConduit : public Sync::SyncConduitBase
{
    Q_OBJECT
public:
    explicit TestConduit(QObject *parent = nullptr);

    QString conduitId() const override { return "test"; }
    QString displayName() const override { return "Test"; }
    QStringList palmDatabaseNames() const override { return {"TestDB"}; }
    QString fileExtension() const override { return ".txt"; }

    BackendRecord *palmToBackend(PilotRecord *, Sync::SyncContext *) override { return nullptr; }
    PilotRecord *backendToPalm(BackendRecord *, Sync::SyncContext *) override { return nullptr; }
    bool recordsEqual(PilotRecord *, BackendRecord *, const Sync::SyncContext *) const override { return true; }
    QString palmRecordDescription(PilotRecord *, const Sync::SyncContext *) const override { return {}; }
    void enrichConflictSnapshot(QSyncCore::RecordSnapshot &, bool) const override {}
    QString formatConflictRecordHtml(const QSyncCore::RecordSnapshot &) const override { return {}; }
    QString categoryNameForIndex(int) const override { return {}; }
    bool writeModifiedCategories(Sync::SyncContext *) override { return true; }
};

#endif
```

Write `/tmp/test-plugin/testconduit.cpp`:

```cpp
#include "testconduit.h"
#include <KPluginFactory>

TestConduit::TestConduit(QObject *parent)
    : Sync::SyncConduitBase(parent)
{
}

K_PLUGIN_FACTORY_WITH_JSON(TestConduitFactory, "testconduit.json",
                           registerPlugin<TestConduit>();)

#include "testconduit.moc"
```

- [ ] **Step 2: Build the test plugin against the installed SDK**

```bash
cd /tmp/test-plugin
cmake -B build -DCMAKE_PREFIX_PATH="/tmp/wildpalms-sdk"
cmake --build build
```

Expected: Configures successfully (finds WildPalms 0.1.1), compiles, and produces a `wildpalms_test.so` somewhere in the build tree.

- [ ] **Step 3: Verify the plugin .so exists and links correctly**

```bash
find /tmp/test-plugin/build -name "wildpalms_test.so"
ldd $(find /tmp/test-plugin/build -name "wildpalms_test.so") | grep WildPalms
```

Expected: File found. `ldd` output shows `libWildPalmsCore.so.0 => /tmp/wildpalms-sdk/lib/libWildPalmsCore.so.0`.

- [ ] **Step 4: Clean up**

```bash
rm -rf /tmp/test-plugin /tmp/wildpalms-sdk
```

- [ ] **Step 5: Commit (no code changes — just verify existing tests still pass)**

```bash
cd /path/to/WildPalms/build && ctest --output-on-failure
```

Expected: All existing tests pass.
