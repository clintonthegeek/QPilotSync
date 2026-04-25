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
