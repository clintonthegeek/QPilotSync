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

## What Gets Installed

### 1. Shared library

`libWildPalmsCore.so` installed to `lib/`. Already partially in place — needs `EXPORT` added.

### 2. Public headers

Installed under `include/wildpalms/` with subdirectory structure preserved:

**Core interfaces:**
- `core/iconduit.h` — minimal conduit interface
- `core/isyncconduit.h` — sync conduit interface with record conversion methods

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
- `WildPalmsConfig.cmake` — finds transitive dependencies (Qt6, KF6CoreAddons), includes targets
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
- Call `find_dependency(KF6CoreAddons)` for the plugin macro
- Include `WildPalmsTargets.cmake` to define `WildPalms::Core`

Plugin authors only need `find_package(WildPalms)` — transitive dependencies resolve automatically. Plugin authors still need `find_package(KF6CoreAddons)` separately for `kcoreaddons_add_plugin()`, since that's a CMake macro, not a link dependency.

### `lib/CMakeLists.txt` changes

Add install rule for pilot-link headers:
```cmake
install(DIRECTORY ${PILOT_LINK_INSTALL_DIR}/include/
    DESTINATION include/wildpalms/pilot-link)
```

### `src/CMakeLists.txt` changes

1. **Generator expressions for include dirs** — replace flat `PUBLIC` include paths with build/install split:
```cmake
target_include_directories(WildPalmsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sync>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/palm>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kf6>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/app>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/widgets>
    $<BUILD_INTERFACE:${UDEV_INCLUDE_DIRS}>
    $<INSTALL_INTERFACE:include/wildpalms>
    $<INSTALL_INTERFACE:include/wildpalms/core>
    $<INSTALL_INTERFACE:include/wildpalms/sync>
    $<INSTALL_INTERFACE:include/wildpalms/palm>
    $<INSTALL_INTERFACE:include/wildpalms/pilot-link>
)
```

2. **Export target on install:**
```cmake
install(TARGETS WildPalmsCore
    EXPORT WildPalmsTargets
    LIBRARY DESTINATION lib
    INCLUDES DESTINATION include/wildpalms
)
```

3. **Install public headers** — one `install(FILES ...)` block per subdirectory, installing only the public headers listed above.

### `CMakeLists.txt` (top-level) changes

Add at the end, after the existing install rules:

```cmake
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
| `CMakeLists.txt` | Add package config generation and export install |
| `src/CMakeLists.txt` | Generator expressions for include dirs, export on install, header install rules |
| `lib/CMakeLists.txt` | Install pilot-link headers |

## Testing

After implementation, verify by:
1. `cmake --install build --prefix /tmp/wildpalms-sdk`
2. Confirm header tree under `/tmp/wildpalms-sdk/include/wildpalms/`
3. Confirm `lib/cmake/WildPalms/WildPalmsConfig.cmake` exists and contains correct paths
4. Build a minimal test plugin against the installed SDK using `find_package(WildPalms)`
