add_executable(tst_ibackendplugin_defaults tst_ibackendplugin_defaults.cpp)
target_link_libraries(tst_ibackendplugin_defaults
    PRIVATE
        WildPalmsCore
        Qt::Test
        Kalburator::Sync
)
add_test(NAME tst_ibackendplugin_defaults COMMAND tst_ibackendplugin_defaults)
set_tests_properties(tst_ibackendplugin_defaults PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
