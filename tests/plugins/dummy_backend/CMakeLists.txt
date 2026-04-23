kcoreaddons_add_plugin(dummy_backend_plugin
    SOURCES
        dummybackendplugin.cpp
        dummybackendplugin.h
    INSTALL_NAMESPACE "wildpalms_test/plugins"
)

target_link_libraries(dummy_backend_plugin
    WildPalmsCore
    KF6::CoreAddons
    Qt::Core
)

target_include_directories(dummy_backend_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
