# Phase E.4 — PilotLink-backed PalmDatabaseAccess adapter tests.
# Link WildPalmsPalmDevice (transitively WildPalmsCore + pisock +
# WildPalmsPalmSync + Kalburator::Sync).

function(add_palm_device_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            WildPalmsPalmDevice
            # Phase E.16 — PalmDevice no longer links Core
            # transitively; add it here explicitly.
            WildPalmsCore
            pisock
            bluetooth
            usb
    )
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/palm
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_device_test(tst_palmrecord_bridge tst_palmrecord_bridge.cpp)

add_palm_device_test(tst_mockkpilotlink
    tst_mockkpilotlink.cpp
    mockkpilotlink.cpp
)

add_palm_device_test(tst_pilotlinkpalmdatabaseaccess
    tst_pilotlinkpalmdatabaseaccess.cpp
    mockkpilotlink.cpp
)

add_palm_device_test(tst_palmdevice_roundtrip
    tst_palmdevice_roundtrip.cpp
    mockkpilotlink.cpp
)
