# WildPalmsAppPatchbay — Sync Patchbay (three-tier graph centerpiece).
# Separate static lib for the same reason as WildPalmsAppMapping: code here
# uses Kalburator::Sync types from libkalburator's synctypes.h, so it needs
# a TU whose include path does NOT have src/core/ on it.
#
# Source files are added as the plan's tasks create them (Task 5 → 15).
add_library(WildPalmsAppPatchbay STATIC
    patchbaytypes.h
    patchbaymodel.h
    patchbaymodel.cpp
    patchnodeitem.h
    patchnodeitem.cpp
    signalpathwire.h
    signalpathwire.cpp
    syncpatchbayview.h
    syncpatchbayview.cpp
    # patchbayinspector.h patchbayinspector.cpp # Task 14
    # patchbaypage.h    patchbaypage.cpp      # Task 14
)

target_include_directories(WildPalmsAppPatchbay
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # PatchbayModel/Page include runtime/routemapping.h, profile.h,
        # runtime/accountcontroller.h, runtime/palmruntime.h via src/.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsAppPatchbay
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        Graffodil::Core
    PRIVATE
        PalmDeviceAccessLib
)

set_target_properties(WildPalmsAppPatchbay PROPERTIES POSITION_INDEPENDENT_CODE ON)
