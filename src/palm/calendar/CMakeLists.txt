# WildPalmsPalmCalendar — Calendar-typed SyncBackend for Palm Datebook.
#
# Phase E.6 of the libkalburator integration. Provides:
#   - PalmCalendarBackend : Kalburator::Sync::SyncBackend
#   - DatebookCodec: Palm Datebook record bytes <-> KCalendarCore::Event
#   - CategoryMappingStore: slot -> display-name map for virtual calendars
#
# Links WildPalmsPalmSync (PalmBackend / PalmRecord / IPalmDatabaseAccess),
# Kalburator::Sync (SyncBackend + operation types), KF6::CalendarCore
# (Event/Alarm/Recurrence), and pisock (pi-datebook.h for the Datebook
# record pack/unpack). Deliberately does NOT link WildPalmsCore —
# keeps the Qt::Widgets / KF6::XmlGui dep out of the calendar codec.

find_package(KF6 REQUIRED COMPONENTS CalendarCore)

add_library(WildPalmsPalmCalendar STATIC
    categorymappingstore.h
    categorymappingstore.cpp
    datebookcodec.h
    datebookcodec.cpp
    palmcalendarbackend.h
    palmcalendarbackend.cpp
)

target_include_directories(WildPalmsPalmCalendar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmCalendar
    PUBLIC
        Qt::Core
        KF6::CalendarCore
        WildPalmsPalmSync
    PRIVATE
        pisock
)

# Ensure pilot-link is built before the codec tries to include pi-datebook.h
add_dependencies(WildPalmsPalmCalendar pilot-link-external)

set_target_properties(WildPalmsPalmCalendar PROPERTIES POSITION_INDEPENDENT_CODE ON)
