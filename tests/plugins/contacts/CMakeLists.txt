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
        WildPalmsPalmCalendar # CategoryMappingStore (cats + dbName thread)
        WildPalmsPalmCodecs   # Contact POD, encodeContact/decodeContact, toAddressee/fromAddressee
        WildPalmsPalmSync     # PalmRecord
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsvcardtranscoder COMMAND tst_contactsvcardtranscoder)
set_tests_properties(tst_contactsvcardtranscoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Task 2: PalmContactsBackend (migrated from ContactsBlobBackend, Phase J) ---
add_executable(tst_contactsblobbackend
    tst_contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/palmcontactsbackend.cpp
)
target_include_directories(tst_contactsblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_contactsblobbackend BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
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
        WildPalmsPalmCalendar   # CategoryMappingStore (transcoder dependency)
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
    ${CONTACTS_PLUGIN_SRC_DIR}/palmcontactsbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsdomainextension.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/palmtovcardtransformation.cpp
)
target_include_directories(tst_contactsbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
# Prepend libkalburator's include dirs so "syncbackend.h" resolves to
# Kalburator::Sync::SyncBackend (same resolution as the plugin target itself).
target_include_directories(tst_contactsbackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
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

# K.8b T13: tst_contacts_v2 + tst_contacts_palm_engine_sync deleted along
# with IBackendPluginV2 + SyncEngine. Contract is covered end-to-end via
# tst_palm_runtime_* (in-process Kalburator::Plugin path).

# --- Phase 2: contacts canon round-trip (verify-and-lock) ---
add_executable(tst_contacts_canon_roundtrip
    tst_contacts_canon_roundtrip.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsdomainextension.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/palmtovcardtransformation.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contacts_canon_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CONTACTS_PLUGIN_SRC_DIR}
)
target_include_directories(tst_contacts_canon_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_contacts_canon_roundtrip
    PRIVATE
        WildPalmsPalmCalendar   # CategoryMappingStore (cats + dbName thread)
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        Kalburator::Sync
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contacts_canon_roundtrip COMMAND tst_contacts_canon_roundtrip)
set_tests_properties(tst_contacts_canon_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Phase Ia Task 14: ContactsDomainExtension (path A registration) ---
add_executable(tst_contactsdomainextension
    tst_contactsdomainextension.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsdomainextension.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/palmtovcardtransformation.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsdomainextension
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CONTACTS_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_contactsdomainextension
    PRIVATE
        WildPalmsPalmCalendar # CategoryMappingStore (transcoder dependency)
        WildPalmsPalmCodecs   # encodeContact / decodeContact / Contact
        WildPalmsPalmSync     # PalmRecord
        Kalburator::Sync      # TransformationRegistry / DomainRegistry / Shape
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsdomainextension COMMAND tst_contactsdomainextension)
set_tests_properties(tst_contactsdomainextension PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
