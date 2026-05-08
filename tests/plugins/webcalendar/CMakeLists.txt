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
set_tests_properties(tst_webcalfeed PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
set_tests_properties(tst_webcalblobbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 5: WebcalBackendPlugin ---
add_executable(tst_webcalbackendplugin
    tst_webcalbackendplugin.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalbackendplugin.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_include_directories(tst_webcalbackendplugin
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_webcalbackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_webcalbackendplugin
    PRIVATE
        WildPalmsCore
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        Kalburator::Sync
)
add_test(NAME tst_webcalbackendplugin COMMAND tst_webcalbackendplugin)
set_tests_properties(tst_webcalbackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M5c Task 6: end-to-end mirror (feed → target) via PalmRuntime
# (replaces the runBlobMirror call this file used pre-M5c).
add_executable(tst_webcal_v2_e2e
    tst_webcal_v2_e2e.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalblobbackend.cpp
    ${WEBCAL_PLUGIN_SRC_DIR}/webcalfeed.cpp
)
target_compile_definitions(tst_webcal_v2_e2e
    PRIVATE
        WEBCAL_FIXTURE_DIR="${WEBCAL_FIXTURE_DIR}"
)
target_include_directories(tst_webcal_v2_e2e
    PRIVATE
        ${WEBCAL_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_webcal_v2_e2e BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_webcal_v2_e2e
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        PalmDeviceAccessLib
        Qt::Test
        Qt::Core
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        pisock
        bluetooth
        usb
)
add_test(NAME tst_webcal_v2_e2e COMMAND tst_webcal_v2_e2e)
set_tests_properties(tst_webcal_v2_e2e PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
