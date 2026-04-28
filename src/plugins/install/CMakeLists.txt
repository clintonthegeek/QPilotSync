option(WILDPALMS_INSTALL_PLUGIN_V2
    "Build the new IPluginAction-based Install plugin" ON)

if (WILDPALMS_INSTALL_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_install_v2_action
        SOURCES
            installactionplugin.cpp
            installactionplugin.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_install_v2_action
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    target_include_directories(wildpalms_install_v2_action BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_install_v2_action
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            WildPalmsPalmSync
            KF6::CoreAddons
            Qt::Core
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_install
        SOURCES
            installconduit.cpp
            installconduit.h
            installview.cpp
            installview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_install
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        Qt::Widgets
    )
endif ()
