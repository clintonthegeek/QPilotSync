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
