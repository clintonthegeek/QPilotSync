kcoreaddons_add_plugin(qpilotsync_memo
    SOURCES
        memoconduit.cpp
        memoconduit.h
        memomapper.cpp
        memomapper.h
        memoview.cpp
        memoview.h
    INSTALL_NAMESPACE "qpilotsync/conduits"
)

target_link_libraries(qpilotsync_memo
    QPilotCore
    KF6::CoreAddons
    KF6::I18n
    KF6::WidgetsAddons
    Qt::Widgets
)
