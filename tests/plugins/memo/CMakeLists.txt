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
target_link_libraries(tst_memobackendplugin
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar
        WildPalmsCore
        PalmDeviceAccessLib     # PalmDeviceAccess symbols
        Kalburator::Sync
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memobackendplugin COMMAND tst_memobackendplugin)
set_tests_properties(tst_memobackendplugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M5c Task 3: end-to-end via the real wildpalms_memo_v2.so plumbed
# through PalmRuntime + SyncEngine::runSyncFuture (replaces the V1
# BackendPluginManager + runBlobTwoWay test that this file used pre-M5c).
add_executable(tst_memo_v2 tst_memo_v2.cpp)
target_compile_definitions(tst_memo_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_sources(tst_memo_v2 PRIVATE ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp)
target_include_directories(tst_memo_v2
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_memo_v2
    PRIVATE
        WildPalmsRuntime
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsCore
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        # Phase Ia.5: WHOLE_ARCHIVE pulls in libkalburator's static
        # DomainPlugin registrars so dispatchSync's
        # DomainRegistry::findByDomain() finds a plugin at sync time.
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        KF6::CoreAddons
        Qt::Test
        Qt::Core
        pisock
        bluetooth
        usb
)
add_dependencies(tst_memo_v2 wildpalms_memo_static)
add_test(NAME tst_memo_v2 COMMAND tst_memo_v2)
set_tests_properties(tst_memo_v2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
