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

add_executable(tst_memoblobbackend
    tst_memoblobbackend.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memoblobbackend.cpp
)
target_include_directories(tst_memoblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
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
target_link_libraries(tst_memobackendplugin
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memobackendplugin COMMAND tst_memobackendplugin)
