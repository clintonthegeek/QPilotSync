# Phase E.14 — Plucker plugin tests.
# Tasks 3-7 build test binaries directly against the source files in
# the plucker submodule.

set(PLUCKER_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/plucker)
set(PLUCKER_FIXTURE_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# --- Task 3: PluckerChannel + serializer ---
add_executable(tst_pluckerchannel
    tst_pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannelserializer.cpp
)
target_include_directories(tst_pluckerchannel
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_pluckerchannel
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_pluckerchannel COMMAND tst_pluckerchannel)

# --- Task 4: PluckerFetcher ---
add_executable(tst_pluckerfetcher
    tst_pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
)
target_compile_definitions(tst_pluckerfetcher
    PRIVATE
        PLUCKER_FIXTURE_DIR="${PLUCKER_FIXTURE_DIR}"
)
target_include_directories(tst_pluckerfetcher
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
)
target_link_libraries(tst_pluckerfetcher
    PRIVATE
        Qt::Test
        Qt::Core
)
add_test(NAME tst_pluckerfetcher COMMAND tst_pluckerfetcher)

# --- Task 5: PluckerBlobBackend ---
add_executable(tst_pluckerblobbackend
    tst_pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
    ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
    ${CMAKE_SOURCE_DIR}/src/palm/sync/mockpalmdatabaseaccess.cpp
)
target_include_directories(tst_pluckerblobbackend
    PRIVATE
        ${PLUCKER_PLUGIN_SRC_DIR}
        ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(tst_pluckerblobbackend BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_pluckerblobbackend
    PRIVATE
        WildPalmsCore
        Qt::Test
        Qt::Core
        Kalburator::Sync
)
add_test(NAME tst_pluckerblobbackend COMMAND tst_pluckerblobbackend)

# --- Task 7: PluckerBackendPlugin ---
if (WILDPALMS_PLUCKER_PLUGIN_V2)
    add_executable(tst_pluckerbackendplugin
        tst_pluckerbackendplugin.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerbackendplugin.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerblobbackend.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerfetcher.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannel.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchannelserializer.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckersettingswidget.cpp
        ${PLUCKER_PLUGIN_SRC_DIR}/pluckerchanneleditor.cpp
        ${CMAKE_SOURCE_DIR}/src/palm/sync/mockpalmdatabaseaccess.cpp
    )
    target_compile_definitions(tst_pluckerbackendplugin
        PRIVATE
            PLUCKER_PLUGIN_SO_PATH="$<TARGET_FILE:wildpalms_plucker_v2>"
            PLUCKER_DATA_DIR="${PLUCKER_PLUGIN_SRC_DIR}"
    )
    target_include_directories(tst_pluckerbackendplugin
        PRIVATE
            ${PLUCKER_PLUGIN_SRC_DIR}
            ${CMAKE_SOURCE_DIR}/src
    )
    target_include_directories(tst_pluckerbackendplugin BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(tst_pluckerbackendplugin
        PRIVATE
            WildPalmsCore
            WildPalmsRuntime
            WildPalmsPalmSync
            Qt::Test
            Qt::Core
            Qt::Widgets
            KF6::CoreAddons
            Kalburator::Sync
    )
    add_dependencies(tst_pluckerbackendplugin wildpalms_plucker_v2)
    add_test(NAME tst_pluckerbackendplugin COMMAND tst_pluckerbackendplugin)
endif ()
