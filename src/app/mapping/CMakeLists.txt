# WildPalmsAppMapping — Mapping editor dialogs (M5b).
#
# Lives in a separate static lib (not compiled into WildPalmsCore directly)
# because WildPalmsCore PUBLIC-exposes src/core/ on the include path, where
# there is a *different* synctypes.h (WP-local Sync:: namespace). The
# dialogs here use Kalburator::Sync types from libkalburator's synctypes.h,
# so they need a TU whose include path does NOT have src/core/ on it.
# Same pattern as WildPalmsAppConflict.

add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
)

target_include_directories(WildPalmsAppMapping
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppMapping
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
)

set_target_properties(WildPalmsAppMapping PROPERTIES POSITION_INDEPENDENT_CODE ON)
