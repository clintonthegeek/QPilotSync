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
set_tests_properties(tst_todoicstranscoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
set_tests_properties(tst_todoblobbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
set_tests_properties(tst_todoconflicthandler PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
        PalmDeviceAccessLib      # PalmDeviceAccess symbols
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
set_tests_properties(tst_todobackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M5c Task 5: end-to-end via the real wildpalms_todos_v2.so plumbed
# through PalmRuntime + SyncEngine::runSyncFuture (replaces the V1
# BackendPluginManager + runBlobTwoWay test that this file used pre-M5c).
add_executable(tst_todo_v2
    tst_todo_v2.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoblobbackend.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_compile_definitions(tst_todo_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(tst_todo_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_todo_v2
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmCalendar
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsPalmDevice
        WildPalmsCore
        PalmDeviceAccessLib
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        KF6::CoreAddons
        KF6::CalendarCore
        Qt::Test
        Qt::Core
        pisock
        bluetooth
        usb
        ${PILOT_LINK_LIBRARIES}
)
add_dependencies(tst_todo_v2 wildpalms_todos_v2)
add_test(NAME tst_todo_v2 COMMAND tst_todo_v2)
set_tests_properties(tst_todo_v2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
