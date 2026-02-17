kcoreaddons_add_plugin(qpilotsync_todos
    SOURCES
        todoconduit.cpp
        todoconduit.h
        todomapper.cpp
        todomapper.h
        taskview.cpp
        taskview.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_todos
    QPilotCore
    KF6::CoreAddons
    KF6::CalendarCore
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)
