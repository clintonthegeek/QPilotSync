# WildPalmsAppMapping — Sync Mappings graph view (F.3).
#
# Lives in a separate static lib (not compiled into WildPalmsCore directly)
# because WildPalmsCore PUBLIC-exposes src/core/ on the include path, where
# there is a *different* synctypes.h (WP-local Sync:: namespace). The code
# here uses Kalburator::Sync types from libkalburator's synctypes.h, so it
# needs a TU whose include path does NOT have src/core/ on it.
# Same pattern as WildPalmsAppConflict.

add_library(WildPalmsAppMapping STATIC
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
    mappingedge.h
    mappingedge.cpp
    syncmappingsgraphview.h
    syncmappingsgraphview.cpp
    mappinginspectorpanel.h
    mappinginspectorpanel.cpp
    syncmappingspage.h
    syncmappingspage.cpp
)

target_include_directories(WildPalmsAppMapping
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # SyncMappingsPage includes profile.h + runtime/accountcontroller.h
        # + runtime/palmruntime.h. These live in WildPalmsCore /
        # WildPalmsRuntime. We need src/ on the include path PRIVATELY
        # so this lib sees them without exposing WP-local synctypes.h to
        # consumers.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsAppMapping
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
    PRIVATE
        # F.3: SyncMappingsPage uses AccountController + PalmRuntime,
        # both of which live in PalmDeviceAccessLib (per
        # WildPalmsAppAccounts pattern). Profile lives in WildPalmsCore
        # but is forward-declared in syncmappingspage.h; the
        # implementation include of "profile.h" is resolved via the
        # added src/ include path above.
        PalmDeviceAccessLib
)

set_target_properties(WildPalmsAppMapping PROPERTIES POSITION_INDEPENDENT_CODE ON)
