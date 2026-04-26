# Phase E.13 — WebCalendar plugin tests.
# Tasks 3-6 build test binaries directly against the source files in
# the webcalendar submodule.

set(WEBCAL_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/webcalendar)

# --- Task 3: WebcalFeed ---
add_executable(tst_webcalfeed
    tst_webcalfeed.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_include_directories(tst_webcalfeed
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_webcalfeed
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_webcalfeed COMMAND tst_webcalfeed)

# --- Task 4: WebcalBlobBackend ---
set(WEBCAL_FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_executable(tst_webcalblobbackend
    tst_webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_compile_definitions(tst_webcalblobbackend
    PRIVATE
        WEBCAL_FIXTURE_DIR="${WEBCAL_FIXTURE_DIR}"
)
target_include_directories(tst_webcalblobbackend
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_webcalblobbackend
    PRIVATE
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        Kalburator::Sync
)
add_test(NAME tst_webcalblobbackend COMMAND tst_webcalblobbackend)
