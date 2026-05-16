# Phase E.9 — tests for concrete PalmDeviceConnection aggregator.
# Links WildPalmsPalmSync (for PalmBackend + MockPalmDatabaseAccess) and
# provides src/ on the include path so "palm/palmdeviceconnection.h"
# resolves. Does NOT link WildPalmsCore — PalmDeviceConnection lives in
# WildPalmsPalmSync and has no WildPalmsCore dependency.

add_executable(tst_palmdeviceconnection tst_palmdeviceconnection.cpp)

target_include_directories(tst_palmdeviceconnection PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(tst_palmdeviceconnection
    PRIVATE
        Qt::Core
        Qt::Test
        Kalburator::Sync
        WildPalmsPalmSync
)

add_test(NAME tst_palmdeviceconnection COMMAND tst_palmdeviceconnection)
set_tests_properties(tst_palmdeviceconnection PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_executable(tst_palm_backend_disconnect tst_palm_backend_disconnect.cpp)
target_include_directories(tst_palm_backend_disconnect PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_palm_backend_disconnect
    PRIVATE
        Qt::Core
        Qt::Test
        Kalburator::Sync
        WildPalmsPalmSync
        WildPalmsPalmMemo
        WildPalmsPalmToDo
        WildPalmsPalmContacts
        WildPalmsPalmCalendar
)
add_test(NAME tst_palm_backend_disconnect COMMAND tst_palm_backend_disconnect)
set_tests_properties(tst_palm_backend_disconnect PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
