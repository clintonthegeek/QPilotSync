# WildPalmsPalmConflict — Palm-aware ConflictHandler + BackendConfig.
#
# Phase E.5 of the libkalburator integration. Implements
# Kalburator::Sync::QSyncCore::ConflictHandler with Palm-specific
# overlays (archive-bit safety, secret-flag protection, category
# tie-break) and registers under backend id "palm".
#
# Links WildPalmsPalmSync so the handler can decode Palm record IDs
# and query IPalmDatabaseAccess during resolution. Transitively pulls
# Kalburator::Sync for the ConflictHandler base interface.

add_library(WildPalmsPalmConflict STATIC
    palmbackendconfig.h
    palmbackendconfig.cpp
    palmconflicthandler.h
    palmconflicthandler.cpp
)

target_include_directories(WildPalmsPalmConflict
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmConflict
    PUBLIC
        Qt::Core
        WildPalmsPalmSync
)

set_target_properties(WildPalmsPalmConflict PROPERTIES POSITION_INDEPENDENT_CODE ON)
