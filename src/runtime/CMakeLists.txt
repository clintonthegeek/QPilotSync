# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8) plus
# Full-Sync host-interface implementations (relocated from src/fullsync/
# in E.15b). Houses BackendPluginManager + PluginActionManager + shared
# metadata helpers + InstallSourceCollector + the SyncEngine-facing
# WP host implementations consumed by libkalburator.

find_package(KF6 REQUIRED COMPONENTS CoreAddons)
find_package(Qt6 REQUIRED COMPONENTS Concurrent)

add_library(WildPalmsRuntime STATIC
    pluginmetadatahelpers.h
    pluginmetadatahelpers.cpp
    backendpluginmanager.h
    backendpluginmanager.cpp
    pluginactionmanager.h
    pluginactionmanager.cpp
    simpleactioncontext.h
    simpleactioncontext.cpp
    # Phase E.15a — folder + cross-plugin blob aggregator for the
    # install action.
    installsourcecollector.h
    installsourcecollector.cpp
    # Phase E.15b — Full-Sync host-interface implementations relocated
    # from src/fullsync/. WildPalmsFullSync no longer exists.
    calendarcollection_wp.h
    calendarcollection_wp.cpp
    conflictpresenter_wp.h
    conflictpresenter_wp.cpp
    conflictresolver_wp.h
    conflictresolver_wp.cpp
    syncconfigstore_wp.h
    syncconfigstore_wp.cpp
    synchost_wp.h
    synchost_wp.cpp
    # Phase E.16 — pilot-link factory header (forward-decls only;
    # impl compiled into WildPalmsCore where the device-side
    # libraries can be linked without an AUTOMOC cycle).
    pilotlinkconnectionfactory.h
)

# Phase E.16 — SyncRunner_wp is broken by Plan 1 (F1 facade deleted).
# Exclude it when building in MVP-only mode; M6 deletes the source entirely.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    target_sources(WildPalmsRuntime PRIVATE
        syncrunner_wp.h
        syncrunner_wp.cpp
    )
endif()

target_sources(WildPalmsRuntime PRIVATE
    # G.7 — HotSyncCoordinator replaces SyncRunner_wp (Task 55 deletes the old files).
    hotsynccoordinator.h
    hotsynccoordinator.cpp
    # M2 Task 2/3 — PalmDeviceAccess: IPalmDatabaseAccess wrapper that
    # marshals all calls to a dedicated link thread via BlockingQueuedConnection.
    # NOTE: compiled into PalmDeviceAccessLib (below) while syncrunner_wp.cpp
    # is broken by Plan 1 (Task 12 re-merges these into WildPalmsRuntime once
    # the syncrunner is rewritten).
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        Qt::Widgets       # QIcon, QWidget in core headers (ibackendplugin.h)
        KF6::CoreAddons
        # Phase E.15b — fullsync host impls implement Kalburator::Sync
        # interfaces with virtual methods; vtable emission requires an
        # actual link, not just an include path.
        Kalburator::Sync
    # WildPalmsCore is exposed only on the INTERFACE link line — consumers
    # that pull in WildPalmsRuntime get the shared lib — but WildPalmsRuntime
    # itself does NOT link WildPalmsCore. This breaks the Phase-E.9 cycle
    # where KF6MainWindow (inside WildPalmsCore) needs BackendPluginManager.
    # WildPalmsCore is the SHARED consumer; linking Runtime PRIVATE there
    # embeds these static objects, and the Core-side symbols they need
    # (ipluginaction.cpp / ibackendplugin.cpp) are already compiled into
    # the same .so.
    INTERFACE
        WildPalmsCore
)

set_target_properties(WildPalmsRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

# M2 Task 2/3 — Isolated build unit for PalmDeviceAccess so the test target
# compiles while syncrunner_wp.cpp is still broken (Plan 1 breakage; Task 12
# merges this back into WildPalmsRuntime once the syncrunner is rewritten).
# M2 Task 5 — PalmRuntime skeleton + PalmRunResult type compiled here for the
# same reason (WildPalmsRuntime is still broken by syncrunner_wp.cpp).
add_library(PalmDeviceAccessLib STATIC
    palmdeviceaccess.h
    palmdeviceaccess.cpp
    palmrunresult.h
    palmruntime.h
    palmruntime.cpp
)
target_include_directories(PalmDeviceAccessLib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(PalmDeviceAccessLib
    PUBLIC
        Qt::Core
        Qt::Concurrent  # M3 — QtConcurrent::run used by backup/restore
        # M2 Task 5 — PalmRuntime's unique_ptr members (BackendRegistry,
        # SyncEngine) require complete types at destruction; add Kalburator::Sync
        # so the destructor compiles. Task 12 merges this lib back into
        # WildPalmsRuntime which already links Kalburator::Sync.
        Kalburator::Sync
)
set_target_properties(PalmDeviceAccessLib PROPERTIES POSITION_INDEPENDENT_CODE ON)
