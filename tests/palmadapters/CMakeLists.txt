# Phase E.7 — codec + adapter tests for Contact/Memo/Todo domains.
# Each test links WildPalmsPalmCodecs (codec tests) plus
# WildPalmsPalmAdapters (adapter tests, added in later tasks).
# Deliberately does NOT link WildPalmsCore.

function(add_palm_adapter_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            WildPalmsPalmCodecs
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_adapter_test(tst_memocodec tst_memocodec.cpp)
add_palm_adapter_test(tst_contactcodec tst_contactcodec.cpp)
add_palm_adapter_test(tst_todocodec tst_todocodec.cpp)
add_palm_adapter_test(tst_kde_pim_convert tst_kde_pim_convert.cpp)
# Adapter tests are appended in later tasks.
