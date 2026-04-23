# WildPalmsPalmCodecs — POD + pisock-driven encode/decode for Palm
# Address/Memo/Todo records, plus optional KDE PIM converters.
#
# Phase E.7 of the libkalburator integration. Sibling to
# WildPalmsPalmSync (E.3), WildPalmsPalmDevice (E.4),
# WildPalmsPalmConflict (E.5), WildPalmsPalmCalendar (E.6).
#
# Links Qt::Core (for QString/QByteArray) and pisock (for pack_*/unpack_*).
# Later tasks grow the target:
#   T5 adds find_package(KF6 ... Contacts CalendarCore) + promotes them to
#   PUBLIC when kde_pim_convert.{h,cpp} land.
# Deliberately does NOT link WildPalmsCore — codecs must stay usable
# without the Qt::Widgets / KF6::XmlGui transitive deps.

add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
    memocodec.h
    memocodec.cpp
    contactcodec.h
    contactcodec.cpp
)

target_include_directories(WildPalmsPalmCodecs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmCodecs
    PUBLIC
        Qt::Core
    PRIVATE
        pisock
)

add_dependencies(WildPalmsPalmCodecs pilot-link-external)

set_target_properties(WildPalmsPalmCodecs PROPERTIES POSITION_INDEPENDENT_CODE ON)
