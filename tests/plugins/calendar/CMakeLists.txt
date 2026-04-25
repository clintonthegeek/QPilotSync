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
