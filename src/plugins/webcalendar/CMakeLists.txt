kcoreaddons_add_plugin(qpilotsync_webcalendar
    SOURCES
        webcalendarconduit.cpp
        webcalendarconduit.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_webcalendar
    QPilotCore
    KF6::CoreAddons
    KF6::I18n
    Qt::Network
    Qt::Widgets
)
