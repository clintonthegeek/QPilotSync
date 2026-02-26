# WildPalmsCore library - Core functionality that will be reused by main app and tests

add_library(WildPalmsCore SHARED
    wildpalms_core.cpp
    wildpalms_version.h

    # Application UI
    app/logwidget.cpp
    app/logwidget.h
    app/conflictdialog.cpp
    app/conflictdialog.h
    app/conflictreviewwidget.cpp
    app/conflictreviewwidget.h
    app/interactiveconflicthandler.cpp
    app/interactiveconflicthandler.h

    # Settings and dialogs
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
    palm/palmdevicemonitor.cpp
    palm/palmdevicemonitor.h

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

    # Conduits - core device operations (non-plugin conduits)
    sync/conduits/installconduit.cpp
    sync/conduits/installconduit.h

    # KF6 integration
    kf6/actionmanager.cpp
    kf6/actionmanager.h
    kf6/autosyncorchestrator.cpp
    kf6/autosyncorchestrator.h
    kf6/conduitmanager.cpp
    kf6/conduitmanager.h
    kf6/kf6mainwindow.cpp
    kf6/kf6mainwindow.h
    kf6/kf6settings.cpp
    kf6/kf6settings.h

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

target_link_libraries(WildPalmsCore
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
        KF6::StatusNotifierItem
        pisock
        ${UDEV_LIBRARIES}
)

target_include_directories(WildPalmsCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/sync
    ${CMAKE_CURRENT_SOURCE_DIR}/kf6
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${UDEV_INCLUDE_DIRS}
)

# For development builds: tell the app where to find wildpalmsui.rc
# without requiring a full install
target_compile_definitions(WildPalmsCore PRIVATE
    WILDPALMS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
)

# Ensure pilot-link is built before WildPalmsCore
add_dependencies(WildPalmsCore pilot-link-external)

# Plugin subdirectory (conduit plugins that link against WildPalmsCore)
add_subdirectory(plugins)
