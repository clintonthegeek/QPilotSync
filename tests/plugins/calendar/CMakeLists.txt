# Phase E.10 — Calendar plugin tests.
# Tasks 2-7 build test binaries directly against the source files in
# the calendar submodule.

set(CALENDAR_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/calendar)

# --- Task 2: CategoryAppInfoReader ---
# WildPalmsPalmCalendar links pisock PRIVATE, so the test TU itself
# (which #includes <pi-appinfo.h>) needs the pilot-link include path
# wired up explicitly. Linking the `pisock` IMPORTED target picks up
# libpisock.a transitively.
add_executable(tst_categoryappinforeader
    tst_categoryappinforeader.cpp
)
target_include_directories(tst_categoryappinforeader
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_categoryappinforeader
    PRIVATE
        WildPalmsPalmCalendar   # CategoryMappingStore + pisock (private dep)
        pisock
        Qt::Test
        Qt::Core
)
add_test(NAME tst_categoryappinforeader COMMAND tst_categoryappinforeader)
set_tests_properties(tst_categoryappinforeader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 3: IcsTranscoder ---
add_executable(tst_icstranscoder
    tst_icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_icstranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_icstranscoder
    PRIVATE
        WildPalmsPalmCalendar   # DatebookCodec
        WildPalmsPalmSync       # PalmRecord
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_icstranscoder COMMAND tst_icstranscoder)
set_tests_properties(tst_icstranscoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 4: PalmCalendarBackend (migrated from CalendarBlobBackend, Phase J) ---
add_executable(tst_calendarblobbackend
    tst_calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmcalendarbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmtoicstransformation.cpp
)
target_include_directories(tst_calendarblobbackend
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CALENDAR_PLUGIN_SRC_DIR}
)
# Prepend libkalburator's include dirs so "syncbackend.h" resolves to
# Kalburator::Sync::SyncBackend (same as the plugin target itself).
target_include_directories(tst_calendarblobbackend BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_calendarblobbackend
    PRIVATE
        WildPalmsPalmCalendar
        WildPalmsPalmSync
        WildPalmsCore
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendarblobbackend COMMAND tst_calendarblobbackend)
set_tests_properties(tst_calendarblobbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 5: CalendarConflictHandler ---
add_executable(tst_calendarconflicthandler
    tst_calendarconflicthandler.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
)
target_include_directories(tst_calendarconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_calendarconflicthandler
    PRIVATE
        WildPalmsPalmConflict
        WildPalmsPalmSync
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendarconflicthandler COMMAND tst_calendarconflicthandler)
set_tests_properties(tst_calendarconflicthandler PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 6: CalendarBackendPlugin (in-process, no .so loading) ---
add_executable(tst_calendarbackendplugin
    tst_calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmcalendarbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmtoicstransformation.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendardomainextension.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarview.cpp
)
target_include_directories(tst_calendarbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CALENDAR_PLUGIN_SRC_DIR}
        ${PILOT_LINK_INCLUDE_DIR}
)
# Prepend libkalburator's include dirs so "syncbackend.h" resolves to
# Kalburator::Sync::SyncBackend (same resolution as the plugin target itself).
target_include_directories(tst_calendarbackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_calendarbackendplugin
    PRIVATE
        WildPalmsPalmCalendar
        WildPalmsPalmConflict
        WildPalmsPalmSync
        WildPalmsCore
        PalmDeviceAccessLib     # PalmDeviceAccess symbols (plugin includes palmdeviceaccess.h)
        Kalburator::Sync
        KF6::CoreAddons
        KF6::CalendarCore
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        ${PILOT_LINK_LIBRARIES}
)
add_test(NAME tst_calendarbackendplugin COMMAND tst_calendarbackendplugin)
set_tests_properties(tst_calendarbackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# tst_calendar_v2 was deleted in M5c — its source used the F1-facade APIs
# (runBlobTwoWay) deleted in Plan 1. Calendar transcoding remains covered
# by tst_icstranscoder + tst_calendarblobbackend, and end-to-end engine
# integration is covered by libkalburator's tests/calendar contract suite
# plus the V2-style tst_memo_v2 / tst_contacts_v2 / tst_todo_v2 / tst_webcal_v2_e2e.

# --- Phase 3: calendar canon round-trip ---
add_executable(tst_calendar_canon_roundtrip
    tst_calendar_canon_roundtrip.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendardomainextension.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmtoicstransformation.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_calendar_canon_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CALENDAR_PLUGIN_SRC_DIR}
)
target_include_directories(tst_calendar_canon_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_calendar_canon_roundtrip
    PRIVATE
        WildPalmsPalmCalendar   # DatebookCodec, CategoryMappingStore
        WildPalmsPalmSync       # PalmRecord
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendar_canon_roundtrip COMMAND tst_calendar_canon_roundtrip)
set_tests_properties(tst_calendar_canon_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
