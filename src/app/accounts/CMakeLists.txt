# WildPalmsAppAccounts — Phase Ic AccountsPage + AddAccountDialog +
# MappingPromptDialog.
#
# Lives in a separate static lib (not compiled into WildPalmsCore directly)
# for the same reason as WildPalmsAppMapping/WildPalmsAppConflict: avoids
# having WildPalmsCore's PUBLIC src/core/ on the include path so that
# libkalburator headers (e.g. <synctypes.h>, <iprovider.h>) resolve to the
# Kalburator::Sync namespace, not the WP-local Sync:: namespace.

add_library(WildPalmsAppAccounts STATIC
    addaccountdialog.cpp
    addaccountdialog.h
    accountspage.cpp
    accountspage.h
    mappingpromptdialog.cpp
    mappingpromptdialog.h
)

target_include_directories(WildPalmsAppAccounts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppAccounts
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        # Phase Ic Task 9 — AccountsPage references AccountController and
        # PalmRuntime (both live in PalmDeviceAccessLib per Task 3).
        PalmDeviceAccessLib
)

set_target_properties(WildPalmsAppAccounts PROPERTIES POSITION_INDEPENDENT_CODE ON)
