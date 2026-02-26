kcoreaddons_add_plugin(wildpalms_install
    SOURCES
        installconduit.cpp
        installconduit.h
        installview.cpp
        installview.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_install
    WildPalmsCore
    KF6::CoreAddons
    KF6::I18n
    Qt::Widgets
)
