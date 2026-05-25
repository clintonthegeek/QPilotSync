# Tasks 4-6 build test binaries directly against the source files.
# Task 8 packages these into WildPalmsMemoPluginCore static lib; we
# re-link the test binaries then.

set(MEMO_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/memo)

add_executable(tst_memomarkdown
    tst_memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
)
target_include_directories(tst_memomarkdown
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memomarkdown
    PRIVATE
        WildPalmsPalmCodecs   # MemoCodec provides PalmCodecs::Memo POD
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memomarkdown COMMAND tst_memomarkdown)
set_tests_properties(tst_memomarkdown PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_memoblobbackend
    tst_memoblobbackend.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoblobbackend.cpp
)
target_include_directories(tst_memoblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
# Prepend libkalburator's include dirs so "syncbackend.h" resolves to
# Kalburator::Sync::SyncBackend (same as the plugin target itself).
target_include_directories(tst_memoblobbackend BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_memoblobbackend
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar   # CategoryMappingStore
        WildPalmsCore
        Kalburator::Sync
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memoblobbackend COMMAND tst_memoblobbackend)
set_tests_properties(tst_memoblobbackend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_memobackendplugin
    tst_memobackendplugin.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoblobbackend.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memobackendplugin.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoview.cpp
)
target_include_directories(tst_memobackendplugin
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
# Prepend libkalburator's include dirs so "syncbackend.h" resolves to
# Kalburator::Sync::SyncBackend (same as the plugin target itself).
target_include_directories(tst_memobackendplugin BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_memobackendplugin
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar
        WildPalmsCore
        PalmDeviceAccessLib     # PalmDeviceAccess symbols
        Kalburator::Sync
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memobackendplugin COMMAND tst_memobackendplugin)
set_tests_properties(tst_memobackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- Phase 5: memo (note) canon round-trip ---
add_executable(tst_memo_note_roundtrip
    tst_memo_note_roundtrip.cpp
    ${MEMO_PLUGIN_SRC_DIR}/notedomainextension.cpp
    ${MEMO_PLUGIN_SRC_DIR}/palmnotetransformation.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
)
target_include_directories(tst_memo_note_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${MEMO_PLUGIN_SRC_DIR}
)
target_include_directories(tst_memo_note_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_memo_note_roundtrip
    PRIVATE
        WildPalmsPalmCodecs     # encodeMemo / decodeMemo
        WildPalmsPalmSync       # PalmRecord
        Kalburator::Sync
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memo_note_roundtrip COMMAND tst_memo_note_roundtrip)
set_tests_properties(tst_memo_note_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# K.8b T13: tst_memo_v2 deleted along with IBackendPluginV2 + SyncEngine
# (replaced by the in-process PalmRuntime plugin path; the contract is
# exercised by tst_palm_runtime_modes).
