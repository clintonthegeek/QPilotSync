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
    # Phase E.15a — pisock IPalmFileInstaller real impl.
    pilotlinkpalmfileinstaller.h
    pilotlinkpalmfileinstaller.cpp
)

# Phase E.16 — none of this lib's classes carry Q_OBJECT, so disable
# AUTOMOC. This breaks the false AUTOMOC cycle that closes via Core's
# headers when WildPalmsCore links WildPalmsPalmDevice for pilot-link
# wrapper construction in kf6mainwindow.
set_target_properties(WildPalmsPalmDevice PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_include_directories(WildPalmsPalmDevice
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmDevice
    PUBLIC
        Qt::Core
        WildPalmsPalmSync
)

# Phase E.16 — get Core's *include path* (KPilotLink, PilotRecord)
# without LINKING to Core. As a STATIC archive, this lib's
# unresolved Core symbols get resolved when consumers link both this
# and Core into a SHARED lib or executable. This sidesteps the
# Core <-> PalmDevice CMake cycle (CMake doesn't allow cycles
# through SHARED targets).
target_include_directories(WildPalmsPalmDevice PRIVATE
    $<TARGET_PROPERTY:WildPalmsCore,INTERFACE_INCLUDE_DIRECTORIES>
)

set_target_properties(WildPalmsPalmDevice PROPERTIES POSITION_INDEPENDENT_CODE ON)
