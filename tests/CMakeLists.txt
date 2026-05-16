# Wild Palms Test Suite
# Uses Qt Test framework for unit and integration testing

# Helper function to create a test executable
function(add_wildpalms_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})

    add_executable(${TEST_NAME} ${TEST_SOURCES})

    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Qt::Widgets
            KF6::CalendarCore
            KF6::ConfigCore
            Kalburator::Sync
            WildPalmsCore
            pisock
            bluetooth
            usb
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/palm
        ${CMAKE_SOURCE_DIR}/src/sync
    )

    # Register with CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # Set test properties for better output
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

# ============================================================
# Unit Tests - Data Mappers (now in conduit plugins)
# ============================================================
# Mappers have been migrated from WildPalmsCore to their respective
# conduit plugins (MODULE libraries). Since MODULE libraries cannot
# be linked against at compile time, tests compile the mapper
# sources directly from the plugin directories.

# ============================================================
# Unit Tests - Palm Data Structures
# ============================================================

add_wildpalms_test(test_categoryinfo
    test_categoryinfo.cpp
)

# ============================================================
# Unit Tests - Sync Infrastructure
# ============================================================

add_wildpalms_test(test_syncstate
    test_syncstate.cpp
)

add_wildpalms_test(test_conflict
    test_conflict.cpp
)

# K.8b T13: test_localfilebackend / test_syncengine deleted along with
# LocalFileBackend + SyncEngine. test_multidatabase (further down) likewise.

add_wildpalms_test(test_profile
    test_profile.cpp
)

# K.8b T9/T10: Profile accounts subgroup + sidecar migration.
# Needs Kalburator::Sync and KF6::ConfigCore directly because profile.h
# includes backendconfiguration.h and the include path / KConfig dep is
# not propagated through WildPalmsCore (which links both PRIVATELY).
add_executable(test_profile_accounts test_profile_accounts.cpp)
target_link_libraries(test_profile_accounts
    PRIVATE
        Qt::Core
        Qt::Test
        Kalburator::Sync
        KF6::ConfigCore
        WildPalmsCore
)
add_test(NAME test_profile_accounts COMMAND test_profile_accounts)
set_tests_properties(test_profile_accounts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(test_profile_sidecar_migration test_profile_sidecar_migration.cpp)
target_link_libraries(test_profile_sidecar_migration
    PRIVATE
        Qt::Core
        Qt::Test
        Kalburator::Sync
        KF6::ConfigCore
        WildPalmsCore
)
add_test(NAME test_profile_sidecar_migration COMMAND test_profile_sidecar_migration)
set_tests_properties(test_profile_sidecar_migration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase L Task 0.A: AutoSyncOrchestrator unregistered-device confirmation path
add_wildpalms_test(test_autosyncorchestrator_unregistered
    test_autosyncorchestrator_unregistered.cpp
)
set_tests_properties(test_autosyncorchestrator_unregistered PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${CMAKE_BINARY_DIR}/test-home-autosync;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-xdg-autosync"
)

# K.8b T13: test_multidatabase deleted along with SyncEngine + SyncConduitBase.

# ============================================================
# Helper for runtime-only tests (Phase D originally; consolidated under
# WildPalmsRuntime in E.15b). Links WildPalmsRuntime + Kalburator::Sync,
# stays out of the WildPalmsCore/pisock link line.
# ============================================================

function(add_runtime_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsRuntime
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_runtime_test(test_fullsync_syncconfigstore    test_fullsync_syncconfigstore.cpp)
add_runtime_test(test_fullsync_synchost           test_fullsync_synchost.cpp)

# ============================================================
# Phase E.3 — PalmBackend + mock device access
# ============================================================

add_subdirectory(palmsync)

# ============================================================
# Phase E.4 — Pilot-link adapter for PalmBackend
# ============================================================

add_subdirectory(palmdevice)

# ============================================================
# Phase E.5 — Palm-aware ConflictHandler + BackendConfig
# ============================================================

add_subdirectory(palmconflict)

# ============================================================
# Phase E.6 — PalmCalendarBackend + DatebookCodec
# ============================================================

add_subdirectory(palmcalendar)

# ============================================================
# Phase E.7 — Codec + Adapter Tests for Contact/Memo/Todo
# ============================================================

add_subdirectory(palmadapters)

# K.8b T18: Smoke test deleted — libkalburator owns its own contract testing.
# Consumer smoke tests are anti-patterns.

# ============================================================
# Test Data Directory
# ============================================================

# Copy test data files to build directory
file(COPY testdata DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Phase E.8 — new-ABI plugin manager tests + dummy plugins.
add_subdirectory(runtime)
add_subdirectory(plugins)

# Phase E.9 — core plugin-ABI unit tests (IBackendPlugin defaults).
add_subdirectory(core)

# Phase E.9 — concrete PalmDeviceConnection aggregator.
add_subdirectory(palm)
