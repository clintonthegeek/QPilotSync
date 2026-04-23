kcoreaddons_add_plugin(dummy_action
    SOURCES
        dummyactionplugin.cpp
        dummyactionplugin.h
    INSTALL_NAMESPACE "wildpalms_test/plugins"
)

target_link_libraries(dummy_action
    WildPalmsCore
    KF6::CoreAddons
    Qt::Core
)

target_include_directories(dummy_action PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
