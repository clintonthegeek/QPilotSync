# End-to-end HotSync fidelity harness against a POSE64-emulated Palm.
# The integration test is opt-in (CTest label "device-e2e") and QSKIPs unless
# WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF are set.

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
