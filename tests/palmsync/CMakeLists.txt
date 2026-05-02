# Phase E.3 — PalmBackend scaffold tests.
# Each test links WildPalmsPalmSync + Kalburator::Sync. Deliberately
# does NOT link WildPalmsCore or pisock.

function(add_palm_sync_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            WildPalmsPalmSync
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_sync_test(tst_mockpalmdatabaseaccess tst_mockpalmdatabaseaccess.cpp)
add_palm_sync_test(tst_palmbackend              tst_palmbackend.cpp)
# tst_palmbackend_roundtrip uses runBlobTwoWay / BlobSyncResult which were
# removed by Plan 1 (F1 facade deletion). Excluded until M6 rewrites it.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_palm_sync_test(tst_palmbackend_roundtrip    tst_palmbackend_roundtrip.cpp)
endif()
