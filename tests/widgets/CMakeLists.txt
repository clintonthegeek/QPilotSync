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

add_executable(tst_dashboardwidget
    tst_dashboardwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/dashboardwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/dashboard/syncstatusmodel.cpp
)
target_include_directories(tst_dashboardwidget PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/widgets/dashboard)
target_link_libraries(tst_dashboardwidget PRIVATE
    Qt::Widgets Qt::Test KF6::I18n)
add_test(NAME tst_dashboardwidget COMMAND tst_dashboardwidget)
set_tests_properties(tst_dashboardwidget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
