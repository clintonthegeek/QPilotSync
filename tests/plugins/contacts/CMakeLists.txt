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
