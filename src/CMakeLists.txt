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

    # Data format mappers
    mappers/memomapper.cpp
    mappers/memomapper.h
    mappers/contactmapper.cpp
    mappers/contactmapper.h
    mappers/calendarmapper.cpp
    mappers/calendarmapper.h
    mappers/todomapper.cpp
    mappers/todomapper.h

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
)

target_link_libraries(QPilotCore
    PUBLIC
        Qt::Core
        Qt::Widgets
        KF6::CalendarCore
        pisock
)

target_include_directories(QPilotCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

# Ensure pilot-link is built before QPilotCore
add_dependencies(QPilotCore pilot-link-external)
