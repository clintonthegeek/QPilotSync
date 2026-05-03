# Phase E.12 — Contacts plugin tests.
# Tasks 1-5 build test binaries directly against the source files in
# the contacts submodule.

find_package(KF6 REQUIRED COMPONENTS Contacts)

set(CONTACTS_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/contacts)

# --- Task 1: ContactsVcardTranscoder ---
add_executable(tst_contactsvcardtranscoder
    tst_contactsvcardtranscoder.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsvcardtranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsvcardtranscoder
    PRIVATE
        WildPalmsPalmCodecs   # Contact POD, encodeContact/decodeContact, toAddressee/fromAddressee
        WildPalmsPalmSync     # PalmRecord
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsvcardtranscoder COMMAND tst_contactsvcardtranscoder)
set_tests_properties(tst_contactsvcardtranscoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 2: ContactsBlobBackend ---
add_executable(tst_contactsblobbackend
    tst_contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsblobbackend
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore
        WildPalmsPalmCodecs      # encodeContact / Contact
        WildPalmsPalmSync        # PalmBackend, PalmRecord, MockPalmDatabaseAccess
        WildPalmsCore
        Kalburator::Sync
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsblobbackend COMMAND tst_contactsblobbackend)
set_tests_properties(tst_contactsblobbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 3: ContactsConflictHandler ---
add_executable(tst_contactsconflicthandler
    tst_contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsconflicthandler
    PRIVATE
        WildPalmsPalmConflict   # PalmConflictHandler
        WildPalmsPalmCodecs     # decodeContact / encodeContact
        WildPalmsPalmSync       # PalmRecord, MockPalmDatabaseAccess
        Kalburator::Sync
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsconflicthandler COMMAND tst_contactsconflicthandler)
set_tests_properties(tst_contactsconflicthandler PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 4: ContactsBackendPlugin (in-process, no .so loading) ---
add_executable(tst_contactsbackendplugin
    tst_contactsbackendplugin.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsbackendplugin.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_contactsbackendplugin
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore + parseCategoryAppInfo
        WildPalmsPalmConflict
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsCore
        PalmDeviceAccessLib      # PalmDeviceAccess symbols
        Kalburator::Sync
        KF6::CoreAddons
        KF6::Contacts
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        ${PILOT_LINK_LIBRARIES}
)
add_test(NAME tst_contactsbackendplugin COMMAND tst_contactsbackendplugin)
set_tests_properties(tst_contactsbackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M5c Task 4: end-to-end via the real wildpalms_contacts_v2.so plumbed
# through PalmRuntime + SyncEngine::runSyncFuture (replaces the V1
# BackendPluginManager + runBlobTwoWay test that this file used pre-M5c).
add_executable(tst_contacts_v2
    tst_contacts_v2.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_compile_definitions(tst_contacts_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(tst_contacts_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_contacts_v2
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmCalendar
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsPalmDevice
        WildPalmsCore
        PalmDeviceAccessLib
        Kalburator::Sync
        KF6::CoreAddons
        KF6::Contacts
        Qt::Test
        Qt::Core
        pisock
        bluetooth
        usb
        ${PILOT_LINK_LIBRARIES}
)
add_dependencies(tst_contacts_v2 wildpalms_contacts_v2)
add_test(NAME tst_contacts_v2 COMMAND tst_contacts_v2)
set_tests_properties(tst_contacts_v2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
