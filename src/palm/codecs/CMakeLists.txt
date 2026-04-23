# WildPalmsPalmCodecs — POD + pisock-driven encode/decode for Palm
# Address/Memo/Todo records, plus optional KDE PIM converters.
#
# Phase E.7 of the libkalburator integration. Sibling to
# WildPalmsPalmSync (E.3), WildPalmsPalmDevice (E.4),
# WildPalmsPalmConflict (E.5), WildPalmsPalmCalendar (E.6).
#
# Links Qt::Core (for QString/QByteArray), pisock (for pack_*/unpack_*),
# KF6::Contacts (for the optional Addressee converter TU), and
# KF6::CalendarCore (for the optional KCalendarCore::Todo converter TU).
# Deliberately does NOT link WildPalmsCore — codecs must stay usable
# without the Qt::Widgets / KF6::XmlGui transitive deps.

find_package(KF6 REQUIRED COMPONENTS Contacts CalendarCore)

add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
)

target_include_directories(WildPalmsPalmCodecs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/palm/sync>
)

target_link_libraries(WildPalmsPalmCodecs
    PUBLIC
        Qt::Core
    PRIVATE
        pisock
)

add_dependencies(WildPalmsPalmCodecs pilot-link-external)

set_target_properties(WildPalmsPalmCodecs PROPERTIES POSITION_INDEPENDENT_CODE ON)
