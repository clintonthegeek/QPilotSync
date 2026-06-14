# End-to-end HotSync fidelity harness against a POSE64-emulated Palm.

# ReControlClient uses QTcpSocket; ensure the Qt Network component is available
# (idempotent if a parent CMakeLists already found it).
find_package(Qt6 REQUIRED COMPONENTS Network)

# Reusable, individually-unit-tested components.
add_library(WildPalmsDeviceE2E STATIC
    recontrolclient.cpp
    emulatorfixture.cpp
    pilotlinkdecoder.cpp
)
target_link_libraries(WildPalmsDeviceE2E
    PUBLIC
        Qt::Core
        Qt::Network
        pisock
)
add_dependencies(WildPalmsDeviceE2E pilot-link-external)

# --- unit tests (always run; no emulator needed) ---

add_executable(tst_recontrol_client tst_recontrol_client.cpp)
target_link_libraries(tst_recontrol_client
    PRIVATE
        Qt::Core
        Qt::Network
        Qt::Test
        Qt::Widgets  # required by wildpalms_qtest_main.h's unconditional <QApplication> include
        WildPalmsDeviceE2E
)
add_test(NAME tst_recontrol_client COMMAND tst_recontrol_client)
set_tests_properties(tst_recontrol_client PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- integration test (opt-in; CTest label device-e2e) ---

add_executable(tst_device_e2e_hotsync tst_device_e2e_hotsync.cpp)
target_link_libraries(tst_device_e2e_hotsync
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets  # required by wildpalms_qtest_main.h's unconditional <QApplication> include
)
add_test(NAME tst_device_e2e_hotsync COMMAND tst_device_e2e_hotsync)
set_tests_properties(tst_device_e2e_hotsync PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "device-e2e"
)
