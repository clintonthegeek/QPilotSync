# WildPalmsPalmDevice — pilot-link adapter bridging WildPalmsCore's
# PilotRecord / KPilotLink abstractions onto the Kalburator-side
# IPalmDatabaseAccess interface (WildPalmsPalmSync).
#
# Phase E.4 of the libkalburator integration. First library-graph
# convergence point between WildPalmsCore (pisock-linked) and
# WildPalmsPalmSync (Kalburator-linked). Transitively pulls both in.

add_library(WildPalmsPalmDevice STATIC
    palmrecord_bridge.h
    palmrecord_bridge.cpp
    pilotlinkpalmdatabaseaccess.h
    pilotlinkpalmdatabaseaccess.cpp
)

target_include_directories(WildPalmsPalmDevice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmDevice
    PUBLIC
        Qt::Core
        WildPalmsCore
        WildPalmsPalmSync
)

set_target_properties(WildPalmsPalmDevice PROPERTIES POSITION_INDEPENDENT_CODE ON)
