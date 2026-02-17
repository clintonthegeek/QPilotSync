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
