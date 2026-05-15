# Phase E.15a — Install action plugin tests.

set(INSTALL_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/install)
set(INSTALL_FIXTURE_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# --- Task 1: PalmFileInstaller ---
add_executable(tst_palmfileinstaller
    tst_palmfileinstaller.cpp
)
target_include_directories(tst_palmfileinstaller
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_palmfileinstaller
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsPalmSync
)
add_test(NAME tst_palmfileinstaller COMMAND tst_palmfileinstaller)
set_tests_properties(tst_palmfileinstaller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# K.8b T13: tst_installsourcecollector deleted along with BackendPluginManager
# (the test inherited from BackendPluginManager to fake the plugin-blob drain
# path; folder-side scanning is now covered by tst_install_v2_e2e's removal
# leaves the InstallSourceCollector unit coverage to be re-added in T18+).

# --- Task 6: InstallActionPlugin ---
add_executable(tst_installactionplugin
    tst_installactionplugin.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/install/installactionplugin.cpp
)
target_include_directories(tst_installactionplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_installactionplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_installactionplugin
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsCore
        WildPalmsRuntime
        WildPalmsPalmSync
        KF6::CoreAddons
        Kalburator::Sync
)
add_test(NAME tst_installactionplugin COMMAND tst_installactionplugin)
set_tests_properties(tst_installactionplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# K.8b T13: tst_install_v2_e2e deleted along with IBackendPluginV2 +
# BackendPluginManager (the test inherited from BackendPluginManager).
