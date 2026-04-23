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
)

target_include_directories(WildPalmsRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)

target_link_libraries(WildPalmsRuntime
    PUBLIC
        Qt::Core
        KF6::CoreAddons
        WildPalmsCore
)

set_target_properties(WildPalmsRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)
