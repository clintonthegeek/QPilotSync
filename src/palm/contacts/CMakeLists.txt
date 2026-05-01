# WildPalmsPalmContacts — SyncBackend for Palm AddressDB (G.7 Task 50).

find_package(KF6 REQUIRED COMPONENTS CalendarCore)

add_library(WildPalmsPalmContacts STATIC
    palmcontactsbackend.h
    palmcontactsbackend.cpp
)

target_include_directories(WildPalmsPalmContacts
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmContacts
    PUBLIC
        Qt::Core
        KF6::CalendarCore
        WildPalmsPalmSync
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmContacts PROPERTIES POSITION_INDEPENDENT_CODE ON)
