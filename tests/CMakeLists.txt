# QPilotSync Test Suite
# Uses Qt Test framework for unit and integration testing

# Helper function to create a test executable
function(add_qpilotsync_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})

    add_executable(${TEST_NAME} ${TEST_SOURCES})

    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            Qt::Widgets
            KF6::CalendarCore
            QPilotCore
            pisock
            bluetooth
            usb
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/mappers
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
# Unit Tests - Data Mappers (Pure logic, no I/O dependencies)
# ============================================================

add_qpilotsync_test(test_calendarmapper
    test_calendarmapper.cpp
)

add_qpilotsync_test(test_contactmapper
    test_contactmapper.cpp
)

add_qpilotsync_test(test_memomapper
    test_memomapper.cpp
)

add_qpilotsync_test(test_todomapper
    test_todomapper.cpp
)

# ============================================================
# Unit Tests - Palm Data Structures
# ============================================================

add_qpilotsync_test(test_categoryinfo
    test_categoryinfo.cpp
)

# ============================================================
# Unit Tests - Sync Infrastructure
# ============================================================

add_qpilotsync_test(test_syncstate
    test_syncstate.cpp
)

add_qpilotsync_test(test_localfilebackend
    test_localfilebackend.cpp
)

add_qpilotsync_test(test_syncengine
    test_syncengine.cpp
)

add_qpilotsync_test(test_profile
    test_profile.cpp
)

# ============================================================
# Test Data Directory
# ============================================================

# Copy test data files to build directory
file(COPY testdata DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
