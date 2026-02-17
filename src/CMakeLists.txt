# QPilotCore library - Core functionality that will be reused by main app and tests

add_library(QPilotCore STATIC
    qpilotsync_core.cpp
    qpilotsync_version.h

    # Application UI
    app/mainwindow.cpp
    app/mainwindow.h
    app/logwidget.cpp
    app/logwidget.h
    app/exporthandler.cpp
    app/exporthandler.h
    app/importhandler.cpp
    app/importhandler.h
    app/conflictdialog.cpp
    app/conflictdialog.h
    app/conflictreviewwidget.cpp
    app/conflictreviewwidget.h
    app/interactiveconflicthandler.cpp
    app/interactiveconflicthandler.h

    # Settings and dialogs
    settings.cpp
    settings.h
    settingsdialog.cpp
    settingsdialog.h
    profile.cpp
    profile.h

    # Palm device communication
    palm/kpilotlink.cpp
    palm/kpilotlink.h
    palm/pilotrecord.cpp
    palm/pilotrecord.h
    palm/kpilotdevicelink.cpp
    palm/kpilotdevicelink.h
    palm/categoryinfo.cpp
    palm/categoryinfo.h
    palm/deviceworker.cpp
    palm/deviceworker.h
    palm/devicesession.cpp
    palm/devicesession.h
    palm/tickleworker.cpp
    palm/tickleworker.h

    # Data format mappers
    mappers/memomapper.cpp
    mappers/memomapper.h
    mappers/contactmapper.cpp
    mappers/contactmapper.h
    mappers/calendarmapper.cpp
    mappers/calendarmapper.h
    mappers/todomapper.cpp
    mappers/todomapper.h

    # QSyncCore - Shared sync library components (for future PlanStanLite integration)
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.cpp
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.cpp
    sync/qsynccore/baselinestore.h
    sync/qsynccore/conflictrecord.cpp
    sync/qsynccore/conflictrecord.h
    sync/qsynccore/conflictstore.cpp
    sync/qsynccore/conflictstore.h
    sync/qsynccore/conflictpolicy.cpp
    sync/qsynccore/conflictpolicy.h

    # Sync engine (Phase 4)
    sync/synctypes.h
    sync/syncstate.cpp
    sync/syncstate.h
    sync/syncbackend.h
    sync/conduit.cpp
    sync/conduit.h
    sync/syncengine.cpp
    sync/syncengine.h
    sync/localfilebackend.cpp
    sync/localfilebackend.h

    # Conduits - data type sync plugins
    sync/conduits/memoconduit.cpp
    sync/conduits/memoconduit.h
    sync/conduits/contactconduit.cpp
    sync/conduits/contactconduit.h
    sync/conduits/calendarconduit.cpp
    sync/conduits/calendarconduit.h
    sync/conduits/todoconduit.cpp
    sync/conduits/todoconduit.h
    sync/conduits/installconduit.cpp
    sync/conduits/installconduit.h
    sync/conduits/webcalendarconduit.cpp
    sync/conduits/webcalendarconduit.h

    # KF6 integration
    kf6/actionmanager.cpp
    kf6/actionmanager.h
    kf6/kf6mainwindow.cpp
    kf6/kf6mainwindow.h
    kf6/kf6settings.cpp
    kf6/kf6settings.h

    # Widgets - Browser views
    widgets/browser/calendarview.cpp
    widgets/browser/calendarview.h
    widgets/browser/contactview.cpp
    widgets/browser/contactview.h
    widgets/browser/memoview.cpp
    widgets/browser/memoview.h
    widgets/browser/taskview.cpp
    widgets/browser/taskview.h

    # Widgets - Common
    widgets/common/categorymanager.cpp
    widgets/common/categorymanager.h
    widgets/common/categorymodel.cpp
    widgets/common/categorymodel.h
    widgets/common/categoryfilterwidget.cpp
    widgets/common/categoryfilterwidget.h

    # Widgets - Dashboard
    widgets/dashboard/dashboardwidget.cpp
    widgets/dashboard/dashboardwidget.h

    # Widgets - Dialogs
    widgets/dialogs/categoryeditordialog.cpp
    widgets/dialogs/categoryeditordialog.h
    widgets/dialogs/taskeditdialog.cpp
    widgets/dialogs/taskeditdialog.h

    # Widgets - Sidebar
    widgets/sidebar/profilesidebar.cpp
    widgets/sidebar/profilesidebar.h
)

target_link_libraries(QPilotCore
    PUBLIC
        Qt::Core
        Qt::Widgets
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Notifications
        pisock
)

target_include_directories(QPilotCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/sync
    ${CMAKE_CURRENT_SOURCE_DIR}/kf6
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
)

# Ensure pilot-link is built before QPilotCore
add_dependencies(QPilotCore pilot-link-external)
