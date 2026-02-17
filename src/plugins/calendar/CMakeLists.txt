kcoreaddons_add_plugin(qpilotsync_calendar
    SOURCES
        calendarconduit.cpp
        calendarconduit.h
        calendarmapper.cpp
        calendarmapper.h
        calendarview.cpp
        calendarview.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_calendar
    QPilotCore
    KF6::CoreAddons
    KF6::CalendarCore
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)
