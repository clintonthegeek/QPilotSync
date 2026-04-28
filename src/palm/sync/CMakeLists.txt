# WildPalmsPalmSync — PalmBackend + device-access abstraction.
# Phase E.3 of the libkalburator integration. PUBLIC-links
# Kalburator::Sync so PalmBackend can implement IBlobBackend; does
# NOT link pisock (the pilot-link adapter lands in E.4 and lives
# elsewhere).
#
# Kept deliberately separate from WildPalmsCore so the Phase-D
# quarantine boundary is preserved at the library-graph level through
# Phase E — E.3 is the first Kalburator-linked Palm-side code and
# sits on its own.

add_library(WildPalmsPalmSync STATIC
    palmrecord.h
    ipalmdatabaseaccess.h
    mockpalmdatabaseaccess.cpp
    mockpalmdatabaseaccess.h
    palmbackend.cpp
    palmbackend.h
    # Phase E.15a — IPalmFileInstaller mock recorder.
    mockpalmfileinstaller.cpp
    mockpalmfileinstaller.h
    # Phase E.9 — concrete PalmDeviceConnection aggregator, lives in
    # src/palm/ so the forward declaration in src/core/ibackendplugin.h
    # (class PalmDeviceConnection in the global namespace) resolves
    # against the same physical file plugins will eventually include.
    ../palmdeviceconnection.cpp
    ../palmdeviceconnection.h
)

target_include_directories(WildPalmsPalmSync
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsPalmSync
    PUBLIC
        Qt::Core
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmSync PROPERTIES POSITION_INDEPENDENT_CODE ON)
