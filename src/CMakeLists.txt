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
    widgets/dialogs/conflictreviewdialog.cpp
    widgets/dialogs/conflictreviewdialog.h
    widgets/dialogs/profilepropertiesdialog.cpp
    widgets/dialogs/profilepropertiesdialog.h
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
        KF6::I18n
    PRIVATE
        Qt::Network
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Notifications
        KF6::StatusNotifierItem
        pisock
        bluetooth  # pilot-link transitive dep (Bluetooth HotSync)
        usb        # pilot-link transitive dep (USB HotSync)
        ${UDEV_LIBRARIES}
)

target_include_directories(WildPalmsCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sync>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/palm>
    $<INSTALL_INTERFACE:include/wildpalms>
    $<INSTALL_INTERFACE:include/wildpalms/core>
    $<INSTALL_INTERFACE:include/wildpalms/sync>
    $<INSTALL_INTERFACE:include/wildpalms/palm>
    $<INSTALL_INTERFACE:include/wildpalms/pilot-link>
)

target_include_directories(WildPalmsCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/kf6
    ${CMAKE_CURRENT_SOURCE_DIR}/widgets
    ${UDEV_INCLUDE_DIRS}
)

set_target_properties(WildPalmsCore PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME Core
)

# For development builds: tell the app where to find wildpalmsui.rc
# and Plucker data without requiring a full install.
# Installed/AppImage builds set -DWILDPALMS_INSTALLED=ON so the app
# uses standard KDE resource lookup instead.
if(NOT WILDPALMS_INSTALLED)
    target_compile_definitions(WildPalmsCore PRIVATE
        WILDPALMS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    )
endif()

# Install the shared library with export
install(TARGETS WildPalmsCore
    EXPORT WildPalmsTargets
    LIBRARY DESTINATION lib
)

# Install public SDK headers
install(FILES
    core/iconduit.h
    core/isyncconduit.h
    core/itoolconduit.h
    DESTINATION include/wildpalms/core
)

install(FILES
    sync/conduit.h
    sync/synctypes.h
    sync/syncstate.h
    sync/syncbackend.h
    DESTINATION include/wildpalms/sync
)

install(FILES
    sync/qsynccore/synccommon.h
    sync/qsynccore/idmappingstore.h
    sync/qsynccore/baselinestore.h
    sync/qsynccore/conflictrecord.h
    sync/qsynccore/conflictstore.h
    sync/qsynccore/conflictpolicy.h
    DESTINATION include/wildpalms/sync/qsynccore
)

install(FILES
    palm/pilotrecord.h
    palm/categoryinfo.h
    palm/kpilotlink.h
    DESTINATION include/wildpalms/palm
)

# Ensure pilot-link is built before WildPalmsCore
add_dependencies(WildPalmsCore pilot-link-external)

# Plugin subdirectory (conduit plugins that link against WildPalmsCore)
add_subdirectory(plugins)

# Full Sync Mode host-interface implementations (Phase D of libkalburator
# integration). Keeps the Kalburator::Sync dependency out of WildPalmsCore
# until Phase F wires the UI.
add_subdirectory(fullsync)

# Palm-side Kalburator::Sync integration (Phase E.3+ of libkalburator
# integration). PalmBackend : IBlobBackend + device-access abstraction.
# Does not link pisock; the real pilot-link adapter lands in E.4.
add_subdirectory(palm/sync)

# Pilot-link adapter for IPalmDatabaseAccess (Phase E.4 of libkalburator
# integration). Bridges WildPalmsCore (pisock) and WildPalmsPalmSync
# (Kalburator::Sync).
add_subdirectory(palm/device)
