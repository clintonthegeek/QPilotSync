# WildPalmsAppConflict — libkalburator-side interactive ConflictHandler.
#
# Wraps the legacy InteractiveConflictHandler UI surface in a
# Kalburator::Conflict::ConflictHandler implementation so that PalmRuntime's
# embedded SyncEngine can use it directly.
#
# Note (2026-05-21): the conflictdialogbridge + palmruntimebridgeinstall TUs
# were collapsed when src/sync/qsynccore/ was deleted. The include-guard
# collision that required the bridge no longer exists; the handler TU includes
# conflictdialog.h directly. This library now contains only the handler.

add_library(WildPalmsAppConflict STATIC
    kalburatorinteractiveconflicthandler.h
    kalburatorinteractiveconflicthandler.cpp
)

target_include_directories(WildPalmsAppConflict
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # The handler TU now includes "../conflictdialog.h" directly.
        # CMAKE_CURRENT_SOURCE_DIR is src/app/conflict; ../../ = src/
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsAppConflict
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
    # WildPalmsCore is intentionally NOT listed here: WildPalmsCore already
    # PRIVATE-links WildPalmsAppConflict (see src/CMakeLists.txt), so adding
    # the reverse link would create a circular dependency between two static
    # libs.  ConflictDialog's symbols (which the bridge TU references) are
    # resolved at the final executable link stage when both archives are
    # present.
)

set_target_properties(WildPalmsAppConflict PROPERTIES POSITION_INDEPENDENT_CODE ON)
