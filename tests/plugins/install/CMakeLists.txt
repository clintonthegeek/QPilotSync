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
