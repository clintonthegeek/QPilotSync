# WildPalmsAppConflict — libkalburator-side interactive ConflictHandler.
#
# M5a: KalburatorInteractiveConflictHandler wraps the legacy
# InteractiveConflictHandler UI surface but implements the
# Kalburator::Sync::QSyncCore::ConflictHandler interface so that
# PalmRuntime's embedded SyncEngine can use it directly.
#
# Lives in a separate static lib (not compiled into WildPalmsCore directly)
# to avoid the header-guard collision between libkalburator's
# conflict/conflictpolicy.h and WildPalms' local
# sync/qsynccore/conflictpolicy.h, which share the include guard name
# QSYNCCORE_CONFLICTPOLICY_H. Both must not appear in the same TU.

add_library(WildPalmsAppConflict STATIC
    kalburatorinteractiveconflicthandler.h
    kalburatorinteractiveconflicthandler.cpp
)

target_include_directories(WildPalmsAppConflict
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppConflict
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
)

set_target_properties(WildPalmsAppConflict PROPERTIES POSITION_INDEPENDENT_CODE ON)
