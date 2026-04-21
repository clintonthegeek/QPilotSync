# WildPalmsFullSync — Full Sync Mode host-interface implementations
# consumed by libkalburator. Distinct from WildPalmsCore so that Client
# Mode builds and tests remain free of the Kalburator::Sync link.
# See docs/plans/2026-04-20-libkalburator-integration.md (Phase D).

add_library(WildPalmsFullSync STATIC
    calendarcollection_wp.cpp
    calendarcollection_wp.h
    syncconfigstore_wp.cpp
    syncconfigstore_wp.h
    conflictresolver_wp.cpp
    conflictresolver_wp.h
    conflictpresenter_wp.cpp
    conflictpresenter_wp.h
    synchost_wp.cpp
    synchost_wp.h
)

target_include_directories(WildPalmsFullSync
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsFullSync
    PUBLIC
        Qt::Core
        Kalburator::Sync
)

set_target_properties(WildPalmsFullSync PROPERTIES POSITION_INDEPENDENT_CODE ON)
