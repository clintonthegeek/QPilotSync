kcoreaddons_add_plugin(qpilotsync_plucker
    SOURCES
        pluckerconduit.cpp
        pluckerconduit.h
        pluckerview.cpp
        pluckerview.h
        pluckerchanneldialog.cpp
        pluckerchanneldialog.h
        pluckerconfig.cpp
        pluckerconfig.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_plucker
    QPilotCore
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)

# Tell the plugin where to find its bundled data (parser + viewer PRCs)
target_compile_definitions(qpilotsync_plucker PRIVATE
    PLUCKER_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
