# WildPalms SDK Plugin Developer Guide

## Overview

The WildPalms SDK allows external projects to build conduit plugins that sync Palm OS databases with PC-side storage. Install the SDK, use `find_package(WildPalms)`, link against `WildPalms::Core`, and subclass `Sync::SyncConduitBase`.

## CMakeLists.txt Template

```cmake
cmake_minimum_required(VERSION 3.19)
project(MyConduit VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(BUILD_SHARED_LIBS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
find_package(ECM 6.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs6)
include(KDECMakeSettings)

find_package(KF6CoreAddons REQUIRED)
find_package(WildPalms 0.1 REQUIRED)

kcoreaddons_add_plugin(wildpalms_myconduit
    SOURCES myconduit.cpp myconduit.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_myconduit
    WildPalms::Core
    KF6::CoreAddons
    Qt::Widgets
)
```

**Required boilerplate notes:**
- `BUILD_SHARED_LIBS ON` is required — `kcoreaddons_add_plugin` defaults to STATIC without it.
- `ECM 6.0` — a version is required; without it, `KDECMakeSettings` skips output directory setup and the plugin macro fails.
- `include(KDECMakeSettings)` — required by `kcoreaddons_add_plugin` for `CMAKE_LIBRARY_OUTPUT_DIRECTORY`.

## Wildcard Database Names

Conduits can claim dynamic sets of Palm databases using glob patterns in `palmDatabaseNames()`:

```cpp
QStringList palmDatabaseNames() const override {
    return {"ShadTags", "ShadViews", "ShadP-*"};
}
```

The sync engine calls `expandDatabaseName()` which:
- **Literal names** (no `*` or `?`): checked for exact membership in the device database list.
- **Glob patterns**: converted to regex via `QRegularExpression::wildcardToRegularExpression()` and matched against all databases on the device.
- **Missing databases**: skipped with a log message — not an error.

The engine calls `sync(context)` once per matched database. Inside your `sync()` override, use `context->palmDatabase` to determine which specific database is being synced:

```cpp
SyncResult MyConduit::sync(SyncContext *context) {
    if (context->palmDatabase.startsWith("ShadP-")) {
        return syncListDatabase(context);
    }
    return SyncConduitBase::sync(context);  // default per-record
}
```

## Overriding the Sync Algorithm

`SyncConduitBase::sync()` is virtual. The default implementation runs per-record sync (open database, iterate modified records, diff, merge). Override it entirely for databases that need different semantics:

```cpp
SyncResult MyConduit::sync(SyncContext *context) {
    // Whole-file sync: treat entire database as one unit
    // ... read all records, hash, compare baseline, resolve conflicts ...
    return result;
}
```

The protected methods `hotSync()`, `fullSync()`, `firstSync()`, etc. are also virtual and can be overridden individually for finer control.

## Version Compatibility

The SDK uses `SameMinorVersion` compatibility policy. For a `find_package(WildPalms 0.1)` call:
- SDK 0.1.x satisfies it (patch-level compatible).
- SDK 0.2.0 does **not** (minor version bump may break ABI).
- SDK 1.0.0 does **not** (major version mismatch).

## Installed Headers

Public headers are under `include/wildpalms/`:

| Directory | Headers | Purpose |
|---|---|---|
| `core/` | `iconduit.h`, `isyncconduit.h`, `itoolconduit.h` | Plugin interfaces |
| `sync/` | `conduit.h`, `synctypes.h`, `syncstate.h`, `syncbackend.h` | Sync framework |
| `sync/qsynccore/` | `synccommon.h`, `idmappingstore.h`, `baselinestore.h`, `conflictrecord.h`, `conflictstore.h`, `conflictpolicy.h` | Conflict resolution |
| `palm/` | `pilotrecord.h`, `categoryinfo.h`, `kpilotlink.h` | Palm device abstraction |
| `pilot-link/` | `pi-*.h` | pilot-link C headers |

Include paths work naturally: `#include "sync/conduit.h"`, `#include "palm/kpilotlink.h"`, etc.

## Transitive Dependencies

`WildPalms::Core` transitively provides:
- `Qt6::Core`, `Qt6::Widgets`
- `KF6::I18n`
- pilot-link symbols (libusb, libbluetooth resolved internally)

You do **not** need to link `usb`, `bluetooth`, or `pisock` — these are resolved within `libWildPalmsCore.so`.
