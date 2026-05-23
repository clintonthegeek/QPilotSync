# WildPalmsAppWizard — F.1c.1 NewProfileWizard
#
# Lives in a separate static lib (matching WildPalmsAppAccounts etc.) so
# WildPalmsCore's src/core/synctypes.h doesn't shadow libkalburator's.

add_library(WildPalmsAppWizard STATIC
    wizardstate.h
)

set_target_properties(WildPalmsAppWizard PROPERTIES
    LINKER_LANGUAGE CXX
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(WildPalmsAppWizard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppWizard
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppAccounts
        PalmDeviceAccessLib
)
