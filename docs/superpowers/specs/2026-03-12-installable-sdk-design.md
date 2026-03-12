# Installable WildPalms SDK

Design spec for making WildPalms installable as a CMake package so that external projects (like ShadowStan) can build conduit plugins without living inside the WildPalms source tree.

## Context

WildPalms conduit plugins currently build inside the source tree, linking directly against the `WildPalmsCore` shared library target. External plugin development is blocked because there is no installed CMake config-file package — `find_package(WildPalms)` does not work.

The ShadowStan conduit adaptation design (`docs/superpowers/specs/2026-03-11-shadowstan-conduit-adaptation-design.md`) lists this as a dependency. ShadowStan's build system needs:

```cmake
find_package(WildPalms REQUIRED)
target_link_libraries(wildpalms_shadowplan WildPalms::Core KF6::CoreAddons)
```

## Approach

Single-target SDK. Add standard CMake install rules to the existing `WildPalmsCore` shared library. Install public headers, bundle pilot-link headers, and generate a CMake config-file package. No library splitting or source restructuring.

## Prerequisite Code Changes

These changes are required before the export/install machinery will work:

### 1. Refactor `SyncContext::deviceLink` type

`SyncContext::deviceLink` in `src/sync/conduit.h` is currently typed as `KPilotDeviceLink*` (the concrete implementation). It must be changed to `KPilotLink*` (the abstract interface). Without this, the public header `conduit.h` requires `kpilotdevicelink.h` which is an application internal.

`conduit.cpp` calls three methods on `deviceLink` that exist only on `KPilotDeviceLink`, not on the abstract `KPilotLink`:
- `isConnected()` — connection state check
- `cleanUpDatabase(int dbHandle)` — post-sync database cleanup
- `resetSyncFlags(int dbHandle)` — reset dirty flags after sync

These are legitimate device operations that any link implementation should support. Promote them to pure virtual methods on `KPilotLink`.

**Files:**
- `src/palm/kpilotlink.h` — add `isConnected()`, `cleanUpDatabase()`, `resetSyncFlags()` as pure virtuals
- `src/sync/conduit.h` — change forward declaration and member type from `KPilotDeviceLink*` to `KPilotLink*`
- `src/sync/syncengine.cpp` — update SyncContext construction (already has a `KPilotDeviceLink*`, assign to `KPilotLink*` — implicit upcast)
- `src/sync/conduit.cpp` — no changes needed once methods are on the abstract interface

### 2. Change `pisock` linkage to PRIVATE

`pisock` is an `IMPORTED STATIC` target linked `PUBLIC` to `WildPalmsCore`. CMake cannot export an imported target from the same project. Since `libpisock.a` is statically linked into `libWildPalmsCore.so` (its symbols are embedded), the linkage should be `PRIVATE`.

**File:** `src/CMakeLists.txt` — move `pisock` from PUBLIC to PRIVATE link libraries

### 3. Split PUBLIC/PRIVATE link dependencies

Most KF6 dependencies on `WildPalmsCore` are application UI concerns that plugins never use. Leaving them PUBLIC means the config file must `find_dependency()` for all of them, imposing unnecessary requirements on plugin developers. Refactor to:

**PUBLIC** (plugins need these):
- `Qt::Core`, `Qt::Widgets` — fundamental types in the API
- `KF6::I18n` — plugins may use i18n

**PRIVATE** (application internals):
- `Qt::Network` — used by device communication, not plugin API
- `KF6::CalendarCore` — only used by calendar/todo conduits internally
- `KF6::CoreAddons` — plugins find this themselves for `kcoreaddons_add_plugin()`
- `KF6::XmlGui` — `IConduit::createGUIClient()` returns nullptr by default; plugins needing XML GUI integration must `find_package(KF6XmlGui)` themselves (same pattern as KF6CoreAddons)
- `KF6::WidgetsAddons` — application dialogs
- `KF6::ConfigCore`, `KF6::ConfigWidgets` — application configuration
- `KF6::Notifications`, `KF6::StatusNotifierItem` — application UI
- `pisock` — symbols embedded in .so
- `${UDEV_LIBRARIES}` — device monitoring

Note: `iconduit.h` forward-declares `KXMLGUIClient` and declares `createGUIClient()` returning `KXMLGUIClient*`. Since it's a forward declaration and the default returns nullptr, plugins that don't override this method don't need KF6::XmlGui. Plugins that do must find it themselves.

Similarly, conduits that work with calendar types (KCalendarCore::Event, etc.) must `find_package(KF6CalendarCore)` themselves.

**File:** `src/CMakeLists.txt` — split `target_link_libraries` into PUBLIC and PRIVATE sections

## What Gets Installed

### 1. Shared library

`libWildPalmsCore.so` installed to `lib/` with proper soname symlinks. Needs `EXPORT`, `VERSION`, and `SOVERSION` properties.

### 2. Public headers

Installed under `include/wildpalms/` with subdirectory structure preserved:

**Core interfaces:**
- `core/iconduit.h` — minimal conduit interface
- `core/isyncconduit.h` — sync conduit interface with record conversion methods
- `core/itoolconduit.h` — tool conduit interface (for non-sync conduits like Plucker)

**Sync infrastructure:**
- `sync/conduit.h` — `SyncConduitBase` (the base class plugins extend) and `SyncContext`
- `sync/synctypes.h` — `SyncMode`, `SyncResult`, `SyncStats`, `IDMapping`, `BackendRecord`, etc.
- `sync/syncstate.h` — `SyncState` (ID mappings, baseline tracking)
- `sync/syncbackend.h` — `SyncBackend` abstract interface

**QSyncCore shared components:**
- `sync/qsynccore/synccommon.h` — `RecordId`, `IdMapping`
- `sync/qsynccore/idmappingstore.h` — bidirectional ID mapping store
- `sync/qsynccore/baselinestore.h` — content hash tracking
- `sync/qsynccore/conflictrecord.h` — `ConflictType`, `RecordSnapshot`, `ConflictRecord`
- `sync/qsynccore/conflictstore.h` — pending conflict management
- `sync/qsynccore/conflictpolicy.h` — `ConflictPolicy`, `ConflictHandler`

**Palm device abstractions:**
- `palm/pilotrecord.h` — `PilotRecord` (Qt wrapper for Palm records)
- `palm/categoryinfo.h` — `CategoryInfo` (Palm category parsing)
- `palm/kpilotlink.h` — `KPilotLink` (abstract device communication interface)

**Bundled pilot-link headers:**
- `pilot-link/*.h` — all `pi-*.h` headers from the pilot-link build, installed under `include/wildpalms/pilot-link/`

### 3. CMake package files

Installed to `lib/cmake/WildPalms/`:
- `WildPalmsConfig.cmake` — finds transitive dependencies (Qt6 Core/Widgets, KF6I18n), includes targets
- `WildPalmsConfigVersion.cmake` — version compatibility check (`SameMajorVersion`)
- `WildPalmsTargets.cmake` — exported `WildPalms::Core` target

## What Is NOT Installed

These are WildPalms application internals, not part of the plugin API:

- `app/` — conflict dialog, log widget, interactive conflict handler
- `kf6/` — conduit manager, main window, settings, auto-sync orchestrator
- `widgets/` — dashboard, sidebar, application dialogs
- `palm/kpilotdevicelink.h`, `palm/deviceworker.h`, `palm/devicesession.h`, `palm/tickleworker.h` — device communication internals (plugins use the abstract `KPilotLink` interface via `SyncContext::deviceLink`)
- `sync/syncengine.h` — engine orchestration
- `sync/localfilebackend.h` — built-in backend implementation
- `profile.h`, `settingsdialog.h` — application configuration

## CMake Changes

### `cmake/WildPalmsConfig.cmake.in` (new file)

Template for the installed config file. Responsibilities:
- Call `find_dependency(Qt6 6.2 COMPONENTS Core Widgets)` for transitive Qt deps
- Call `find_dependency(KF6I18n)` for i18n support
- Include `WildPalmsTargets.cmake` to define `WildPalms::Core`

Only PUBLIC dependencies need `find_dependency()` calls. After the prerequisite refactoring (step 3), this is just Qt6 Core/Widgets and KF6I18n. Plugin authors still need `find_package(KF6CoreAddons)` separately for `kcoreaddons_add_plugin()`, since that's a CMake macro, not a link dependency of WildPalmsCore.

### `lib/CMakeLists.txt` changes

Add install rule for pilot-link headers:
```cmake
install(DIRECTORY ${PILOT_LINK_INSTALL_DIR}/include/
    DESTINATION include/wildpalms/pilot-link)
```

### `src/CMakeLists.txt` changes

1. **Split include dirs into PUBLIC and PRIVATE** — PUBLIC for plugin-facing headers (with build/install generator expressions), PRIVATE for application internals:
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

2. **Set library version properties:**
```cmake
set_target_properties(WildPalmsCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
```

3. **Export target on install:**
```cmake
install(TARGETS WildPalmsCore
    EXPORT WildPalmsTargets
    LIBRARY DESTINATION lib
)
```

3. **Install public headers** — one `install(FILES ...)` block per subdirectory, installing only the public headers listed above.

### `CMakeLists.txt` (top-level) changes

Add RPATH policy and package config generation after the existing install rules:

```cmake
# RPATH for installed binaries/libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

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

## Plugin Developer Usage

After installing WildPalms (`cmake --install build --prefix /usr/local`), a plugin project's CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.19)
project(ShadowPlanConduit VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)

find_package(KF6CoreAddons REQUIRED)
find_package(WildPalms 0.1 REQUIRED)

kcoreaddons_add_plugin(wildpalms_shadowplan
    SOURCES shadowplanconduit.cpp shadowplanconduit.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_shadowplan
    WildPalms::Core
    KF6::CoreAddons
    Qt::Widgets
)
```

## Files Changed

| File | Change |
|---|---|
| `cmake/WildPalmsConfig.cmake.in` | **New** — config template |
| `CMakeLists.txt` | Add RPATH policy, package config generation, export install |
| `src/CMakeLists.txt` | Split PUBLIC/PRIVATE link deps and include dirs, add SOVERSION, export on install, header install rules |
| `src/palm/kpilotlink.h` | Promote `isConnected()`, `cleanUpDatabase()`, `resetSyncFlags()` to abstract interface |
| `src/sync/conduit.h` | Change `SyncContext::deviceLink` from `KPilotDeviceLink*` to `KPilotLink*` |
| `src/sync/syncengine.cpp` | Update SyncContext construction for new deviceLink type |
| `lib/CMakeLists.txt` | Install pilot-link headers |

## Testing

After implementation, verify by:
1. `cmake --install build --prefix /tmp/wildpalms-sdk`
2. Confirm header tree under `/tmp/wildpalms-sdk/include/wildpalms/` — verify public headers are present and application-internal headers are NOT installed
3. Confirm soname symlinks: `libWildPalmsCore.so` → `libWildPalmsCore.so.0` → `libWildPalmsCore.so.0.1.1`
4. Confirm `lib/cmake/WildPalms/WildPalmsConfig.cmake` exists and contains correct `find_dependency()` calls
5. Build a minimal test plugin against the installed SDK using `find_package(WildPalms)` — verify it compiles, links, and the resulting `.so` loads at runtime
6. Confirm pilot-link headers resolve correctly — `#include <pi-appinfo.h>` in `categoryinfo.h` must work from the installed include path
