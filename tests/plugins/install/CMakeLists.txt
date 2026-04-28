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

# --- Task 4: InstallSourceCollector (folder side) ---
add_executable(tst_installsourcecollector
    tst_installsourcecollector.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/installsourcecollector.cpp
)
target_compile_definitions(tst_installsourcecollector
    PRIVATE
        INSTALL_FIXTURE_DIR="${INSTALL_FIXTURE_DIR}"
)
target_include_directories(tst_installsourcecollector
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_installsourcecollector BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_installsourcecollector
    PRIVATE
        Qt::Test
        Qt::Core
        WildPalmsCore
        WildPalmsRuntime
        Kalburator::Sync
)
add_test(NAME tst_installsourcecollector COMMAND tst_installsourcecollector)

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
