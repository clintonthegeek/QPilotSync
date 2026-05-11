kcoreaddons_add_plugin(dummy_backend
    SOURCES
        dummybackendplugin.cpp
        dummybackendplugin.h
    INSTALL_NAMESPACE "wildpalms_test/plugins"
)

target_link_libraries(dummy_backend
    WildPalmsCore
    Kalburator::Sync
    KF6::CoreAddons
    Qt::Core
)

target_include_directories(dummy_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
