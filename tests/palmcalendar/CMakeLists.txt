# Phase E.6 — PalmCalendarBackend + DatebookCodec + CategoryMappingStore tests.
# Each test links WildPalmsPalmCalendar (transitively WildPalmsPalmSync +
# Kalburator::Sync + KF6::CalendarCore + pisock). Deliberately does NOT
# link WildPalmsCore.

function(add_palm_calendar_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Kalburator::Sync
            KF6::CalendarCore
            WildPalmsPalmCalendar
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_calendar_test(tst_categorymappingstore tst_categorymappingstore.cpp)
