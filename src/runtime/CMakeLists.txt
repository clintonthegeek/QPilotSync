# WildPalmsRuntime — managers for the new WP plugin ABI (Phase E.8 of the
# libkalburator integration). Houses BackendPluginManager and
# PluginActionManager plus the shared metadata-reading helpers. Keeps
# KPluginFactory / KPluginMetaData wiring out of WildPalmsCore and
# confines the Kalburator::Sync dependency to a single static lib.
#
# Deliberately does NOT absorb src/fullsync/ yet — that relocation is
# deferred to E.15 per docs/superpowers/plans/2026-04-23-phase-e8-plugin-abi.md.

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
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

# Phase E.15a — installsourcecollector.cpp uses Kalburator::Sync's
# IBlobBackend / BackendRecord. Include the headers PRIVATE so we
# don't leak the Kalburator surface to consumers.
target_include_directories(WildPalmsRuntime BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        Qt::Widgets   # QIcon, QWidget in core headers (ibackendplugin.h)
        KF6::CoreAddons
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
