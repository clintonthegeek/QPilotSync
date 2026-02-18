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

add_wildpalms_test(test_calendarmapper
    test_calendarmapper.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/calendar/calendarmapper.cpp
)
target_include_directories(test_calendarmapper PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/calendar
)

add_wildpalms_test(test_contactmapper
    test_contactmapper.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/contacts/contactmapper.cpp
)
target_include_directories(test_contactmapper PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/contacts
)

add_wildpalms_test(test_memomapper
    test_memomapper.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/memo/memomapper.cpp
)
target_include_directories(test_memomapper PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/memo
)

add_wildpalms_test(test_todomapper
    test_todomapper.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/todos/todomapper.cpp
)
target_include_directories(test_todomapper PRIVATE
    ${CMAKE_SOURCE_DIR}/src/plugins/todos
)

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

add_wildpalms_test(test_localfilebackend
    test_localfilebackend.cpp
)

add_wildpalms_test(test_syncengine
    test_syncengine.cpp
)

add_wildpalms_test(test_profile
    test_profile.cpp
)

# ============================================================
# Unit Tests - Plucker Config
# ============================================================

add_executable(test_pluckerconfig
    test_pluckerconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/plugins/plucker/pluckerconfig.cpp
)
target_link_libraries(test_pluckerconfig PRIVATE Qt::Core Qt::Test)
target_include_directories(test_pluckerconfig PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_pluckerconfig COMMAND test_pluckerconfig)
set_tests_properties(test_pluckerconfig PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ============================================================
# Test Data Directory
# ============================================================

# Copy test data files to build directory
file(COPY testdata DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
