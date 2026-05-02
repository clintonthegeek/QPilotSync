# Phase E.8 — WildPalmsRuntime tests (BackendPluginManager,
# PluginActionManager, plugin metadata helpers).

function(add_wildpalms_runtime_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            KF6::CoreAddons
            WildPalmsCore
            WildPalmsRuntime
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_wildpalms_runtime_test(tst_pluginmetadatahelpers tst_pluginmetadatahelpers.cpp)
add_wildpalms_runtime_test(tst_backendpluginmanager tst_backendpluginmanager.cpp)
add_wildpalms_runtime_test(tst_simpleactioncontext tst_simpleactioncontext.cpp)
add_wildpalms_runtime_test(tst_pluginactionmanager tst_pluginactionmanager.cpp)
add_wildpalms_runtime_test(tst_plugin_factory_roundtrip tst_plugin_factory_roundtrip.cpp)
# tst_syncrunner uses SyncRunner_wp which is broken by Plan 1 (F1 facade
# deleted). Excluded from WILDPALMS_CALENDAR_MVP_ONLY builds until M6.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_wildpalms_runtime_test(tst_syncrunner tst_syncrunner.cpp)
    target_link_libraries(tst_syncrunner PRIVATE Kalburator::Sync)
endif()
# tst_palm_device_access links PalmDeviceAccessLib directly (not the full
# WildPalmsRuntime) because syncrunner_wp.cpp is intentionally broken by
# Plan 1 until Task 12 rewrites it. PalmDeviceAccessLib has the same include
# paths and Qt::Core linkage that this test needs.
add_executable(tst_palm_device_access tst_palm_device_access.cpp)
target_link_libraries(tst_palm_device_access
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        # palmruntime.cpp instantiates PilotLinkPalmDatabaseAccess; pull in
        # WildPalmsPalmDevice and its transitive pisock deps to resolve it.
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_device_access COMMAND tst_palm_device_access)
set_tests_properties(tst_palm_device_access PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
# Force the dummy .so files to be built before running this test.
add_dependencies(tst_plugin_factory_roundtrip
    dummy_backend
    dummy_action
)
add_executable(tst_palm_runtime_hotsync tst_palm_runtime_hotsync.cpp)
target_link_libraries(tst_palm_runtime_hotsync
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        Kalburator::Sync
        # palmruntime.cpp directly instantiates PilotLinkPalmDatabaseAccess
        # (WildPalmsPalmDevice) on connectDevice(); link in the device lib and
        # its transitive pisock deps so the symbol resolves at link time.
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_hotsync COMMAND tst_palm_runtime_hotsync)
set_tests_properties(tst_palm_runtime_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
# M5a Task 2: KalburatorInteractiveConflictHandler unit test
add_executable(tst_kalburator_interactive_conflict_handler
    tst_kalburator_interactive_conflict_handler.cpp
)
target_link_libraries(tst_kalburator_interactive_conflict_handler
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Concurrent
        WildPalmsAppConflict
        Kalburator::Sync
        # WildPalmsCore provides ConflictDialog (referenced by conflictdialogbridge.cpp).
        # The bridge's exec() is never called in tests (the hook short-circuits first),
        # but the linker still requires the symbol to be resolved.
        WildPalmsCore
)
add_test(NAME tst_kalburator_interactive_conflict_handler
    COMMAND tst_kalburator_interactive_conflict_handler
)
set_tests_properties(tst_kalburator_interactive_conflict_handler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5a Tasks 7-8: PalmRuntime::setConflictHandler
add_executable(tst_palm_runtime_conflict_handler
    tst_palm_runtime_conflict_handler.cpp)
target_link_libraries(tst_palm_runtime_conflict_handler
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_conflict_handler
    COMMAND tst_palm_runtime_conflict_handler)
set_tests_properties(tst_palm_runtime_conflict_handler PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 1: PalmRuntime::isRunning()
add_executable(tst_palm_runtime_is_running tst_palm_runtime_is_running.cpp)
target_link_libraries(tst_palm_runtime_is_running
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_is_running
    COMMAND tst_palm_runtime_is_running)
set_tests_properties(tst_palm_runtime_is_running PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M3 Tasks 14-17: fullSync / copyPalmToPC / copyPCToPalm / backup / restore
add_executable(tst_palm_runtime_modes tst_palm_runtime_modes.cpp)
target_link_libraries(tst_palm_runtime_modes
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        Kalburator::Sync
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_runtime_modes COMMAND tst_palm_runtime_modes)
set_tests_properties(tst_palm_runtime_modes PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
