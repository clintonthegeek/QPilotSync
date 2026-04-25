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
    ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp
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

# --- Task 4: CalendarBlobBackend ---
add_executable(tst_calendarblobbackend
    tst_calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_calendarblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
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

# --- Task 6: CalendarBackendPlugin (in-process, no .so loading) ---
add_executable(tst_calendarbackendplugin
    tst_calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarbackendplugin.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarblobbackend.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarconflicthandler.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/categoryappinforeader.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendarview.cpp
)
target_include_directories(tst_calendarbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_calendarbackendplugin
    PRIVATE
        WildPalmsPalmCalendar
        WildPalmsPalmConflict
        WildPalmsPalmSync
        WildPalmsCore
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
