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
add_wildpalms_runtime_test(tst_syncrunner tst_syncrunner.cpp)
target_link_libraries(tst_syncrunner PRIVATE Kalburator::Sync)
# Force the dummy .so files to be built before running this test.
add_dependencies(tst_plugin_factory_roundtrip
    dummy_backend
    dummy_action
)
