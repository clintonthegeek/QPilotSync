# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8) plus
# Full-Sync host-interface implementations (relocated from src/fullsync/
# in E.15b). Houses BackendPluginManager + PluginActionManager + shared
# metadata helpers + InstallSourceCollector + the SyncCoordinator-facing
# WP host implementations consumed by libkalburator.

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

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
    # Phase E.16 — single orchestrator for the new ABI; replaces
    # legacy Sync::SyncEngine driving IConduit derivatives.
    syncrunner_wp.h
    syncrunner_wp.cpp
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
