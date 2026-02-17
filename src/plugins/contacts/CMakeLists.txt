kcoreaddons_add_plugin(qpilotsync_contacts
    SOURCES
        contactconduit.cpp
        contactconduit.h
        contactmapper.cpp
        contactmapper.h
        contactview.cpp
        contactview.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_contacts
    QPilotCore
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)
