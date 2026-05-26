add_executable(tst_syncstatusmodel
    tst_syncstatusmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/syncstatusmodel.cpp
)
target_include_directories(tst_syncstatusmodel
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/widgets/dashboard
)
target_link_libraries(tst_syncstatusmodel
    PRIVATE
        Qt::Core
        Qt::Test
)
add_test(NAME tst_syncstatusmodel COMMAND tst_syncstatusmodel)
set_tests_properties(tst_syncstatusmodel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
