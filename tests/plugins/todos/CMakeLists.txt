# Phase E.11 — ToDo plugin tests.
# Tasks 2-6 build test binaries directly against the source files in
# the todos submodule.

set(TODOS_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/todos)

# --- Task 2: TodoIcsTranscoder ---
add_executable(tst_todoicstranscoder
    tst_todoicstranscoder.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoicstranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoicstranscoder
    PRIVATE
        WildPalmsPalmCodecs   # decodeTodo / encodeTodo
        WildPalmsPalmSync     # PalmRecord
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoicstranscoder COMMAND tst_todoicstranscoder)

# --- Task 3: TodoBlobBackend ---
add_executable(tst_todoblobbackend
    tst_todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoblobbackend
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore
        WildPalmsPalmCodecs      # encodeTodo
        WildPalmsPalmSync        # PalmBackend, PalmRecord, MockPalmDatabaseAccess
        WildPalmsCore
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoblobbackend COMMAND tst_todoblobbackend)

# --- Task 4: TodoConflictHandler ---
add_executable(tst_todoconflicthandler
    tst_todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoconflicthandler
    PRIVATE
        WildPalmsPalmConflict   # PalmConflictHandler
        WildPalmsPalmCodecs     # decodeTodo / encodeTodo
        WildPalmsPalmSync       # PalmRecord, MockPalmDatabaseAccess
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoconflicthandler COMMAND tst_todoconflicthandler)

# --- Task 5: TodoBackendPlugin (in-process, no .so loading) ---
add_executable(tst_todobackendplugin
    tst_todobackendplugin.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todobackendplugin.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoconflicthandler.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
    ${TODOS_PLUGIN_SRC_DIR}/taskview.cpp
)
target_include_directories(tst_todobackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_todobackendplugin
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore + parseCategoryAppInfo
        WildPalmsPalmConflict
        WildPalmsPalmCodecs
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
add_test(NAME tst_todobackendplugin COMMAND tst_todobackendplugin)
