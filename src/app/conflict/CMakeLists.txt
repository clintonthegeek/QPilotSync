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
    # Bridge TU — includes conflictdialog.h (WP-local namespace) in isolation
    # from the libkalburator headers in the handler TU.  The two TUs must NOT
    # be merged, because both sets of headers share the same include-guard names
    # (QSYNCCORE_CONFLICTRECORD_H, QSYNCCORE_CONFLICTPOLICY_H) and a single TU
    # cannot include both without type-guard collisions.
    conflictdialogbridge.h
    conflictdialogbridge.cpp
)

target_include_directories(WildPalmsAppConflict
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # conflictdialogbridge.cpp includes "../conflictdialog.h" which in turn
        # includes "sync/qsynccore/conflictrecord.h" etc.  Those are relative to
        # WildPalmsCore's public include root (src/).
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
