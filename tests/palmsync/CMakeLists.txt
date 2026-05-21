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
# Phase Ia Task 11 — wire-bytes round-trip for the palm↔vcard4
# transformation stages.
add_palm_sync_test(tst_palmrecord_wirebytes     tst_palmrecord_wirebytes.cpp)
# tst_palmbackend_roundtrip was deleted in M5c — its source used the
# F1-facade APIs (BlobSyncResult / runBlobTwoWay) deleted in Plan 1.
