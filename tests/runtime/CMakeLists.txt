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
# tst_palm_device_access links PalmDeviceAccessLib directly (kept after
# the M5c deletion of SyncRunner_wp because PalmDeviceAccessLib has the
# same include paths and Qt::Core linkage that this test needs).
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
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        # Without WHOLE_ARCHIVE the linker drops the registrars as
        # unreferenced TUs and dispatchSync errors with "no edge path".
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
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

# M5b Task 2: PalmRuntime::reloadMappings(QJsonArray)
add_executable(tst_palm_runtime_reload_mappings
    tst_palm_runtime_reload_mappings.cpp)
target_link_libraries(tst_palm_runtime_reload_mappings
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
add_test(NAME tst_palm_runtime_reload_mappings
    COMMAND tst_palm_runtime_reload_mappings)
set_tests_properties(tst_palm_runtime_reload_mappings PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 3: defaults guard — connectDevice skips defaults if mappings present
add_executable(tst_palm_runtime_default_mappings_only_when_empty
    tst_palm_runtime_default_mappings_only_when_empty.cpp)
target_link_libraries(tst_palm_runtime_default_mappings_only_when_empty
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
add_test(NAME tst_palm_runtime_default_mappings_only_when_empty
    COMMAND tst_palm_runtime_default_mappings_only_when_empty)
set_tests_properties(tst_palm_runtime_default_mappings_only_when_empty PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ic Task 3: AccountController smoke + integration tests.
add_executable(tst_account_controller tst_account_controller.cpp)
target_link_libraries(tst_account_controller
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_account_controller COMMAND tst_account_controller)
set_tests_properties(tst_account_controller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase Ic Task 11: AccountsPage widget tests.
add_executable(tst_accounts_page tst_accounts_page.cpp)
target_link_libraries(tst_accounts_page
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        WildPalmsCore
        WildPalmsAppAccounts
        pisock
        bluetooth
        usb
)
add_test(NAME tst_accounts_page COMMAND tst_accounts_page)
set_tests_properties(tst_accounts_page PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 4: MappingRowDialog — modal editor for one SyncMapping
add_executable(tst_mapping_row_dialog tst_mapping_row_dialog.cpp)
target_link_libraries(tst_mapping_row_dialog
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppMapping
)
add_test(NAME tst_mapping_row_dialog COMMAND tst_mapping_row_dialog)
set_tests_properties(tst_mapping_row_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5b Task 5: MappingEditorDialog — list/add/edit/delete + JSON round-trip
add_executable(tst_mapping_editor_dialog tst_mapping_editor_dialog.cpp)
target_link_libraries(tst_mapping_editor_dialog
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppMapping
)
add_test(NAME tst_mapping_editor_dialog COMMAND tst_mapping_editor_dialog)
set_tests_properties(tst_mapping_editor_dialog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M5c Task 2: smoke test — KF6MainWindow per-plugin KPageWidget wiring.
# Loads real WildPalms plugins via BackendPluginManager (same path
# KF6MainWindow uses) and asserts every plugin claiming hasMainView()=true
# returns a non-null QWidget from createMainView() — the contract that
# onBackendPluginLoaded depends on.
add_executable(tst_main_window_plugin_pages_populated
    tst_main_window_plugin_pages_populated.cpp)
target_compile_definitions(tst_main_window_plugin_pages_populated
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_link_libraries(tst_main_window_plugin_pages_populated
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::CoreAddons
        WildPalmsCore
        WildPalmsRuntime
)
add_test(NAME tst_main_window_plugin_pages_populated
    COMMAND tst_main_window_plugin_pages_populated)
set_tests_properties(tst_main_window_plugin_pages_populated PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# M3 Tasks 14-17: fullSync / copyPalmToPC / copyPCToPalm / backup / restore
add_executable(tst_palm_runtime_modes tst_palm_runtime_modes.cpp)
target_link_libraries(tst_palm_runtime_modes
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
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

# M6b Task 3: PalmDeviceAccess connect lifecycle test
# Uses MockKPilotDeviceLink (subclass) injected via
# PalmDeviceAccess::setLinkFactoryForTest to drive
# onConnectionEstablished/onConnectionFailed without real hardware.
add_executable(tst_palm_device_access_connect
    tst_palm_device_access_connect.cpp
    mockkpilotdevicelink.cpp
)
target_link_libraries(tst_palm_device_access_connect
    PRIVATE
        Qt::Core
        Qt::Test
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_palm_device_access_connect COMMAND tst_palm_device_access_connect)
set_tests_properties(tst_palm_device_access_connect PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
