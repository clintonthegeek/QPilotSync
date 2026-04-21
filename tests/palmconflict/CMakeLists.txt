# Phase E.5 — PalmConflictHandler + PalmBackendConfig tests.
# Each test links WildPalmsPalmConflict (transitively WildPalmsPalmSync
# + Kalburator::Sync). Deliberately does NOT link WildPalmsCore or pisock.

function(add_palm_conflict_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsPalmConflict
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_conflict_test(tst_palmbackendconfig tst_palmbackendconfig.cpp)
