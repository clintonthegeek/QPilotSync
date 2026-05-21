include(ExternalProject)

# pilot-link integration with two modes:
#
# 1. **Vendored** (default when ${CMAKE_SOURCE_DIR}/pilot-link/ contains source
#    files): build pilot-link from a local checkout via ExternalProject_Add.
#    This mode lets developers pin to a specific pilot-link revision and apply
#    project-local patches from pilot-link-git/.
#
# 2. **System** (fallback when no vendored source is present): find pilot-link
#    via the system package (Arch: pilot-link, Debian: libpisock-dev). Uses
#    /usr/lib/libpisock.so + /usr/include/pi-*.h. No build step needed.
#
# The vendored-source check looks for pilot-link/configure.in (a canonical
# autotools source-tree marker) so a broken self-symlink or empty directory
# falls through to system mode.

set(PILOT_LINK_VENDORED_DIR ${CMAKE_SOURCE_DIR}/pilot-link)

if(EXISTS ${PILOT_LINK_VENDORED_DIR}/configure.in OR
   EXISTS ${PILOT_LINK_VENDORED_DIR}/configure.ac)
    message(STATUS "pilot-link: using vendored source at ${PILOT_LINK_VENDORED_DIR}")

    set(PILOT_LINK_SOURCE_DIR  ${PILOT_LINK_VENDORED_DIR})
    set(PILOT_LINK_PATCH_DIR   ${CMAKE_SOURCE_DIR}/pilot-link-git)
    set(PILOT_LINK_BUILD_DIR   ${CMAKE_BINARY_DIR}/pilot-link-build)
    set(PILOT_LINK_INSTALL_DIR ${CMAKE_BINARY_DIR}/pilot-link-install)

    ExternalProject_Add(pilot-link-external
        SOURCE_DIR ${PILOT_LINK_SOURCE_DIR}
        BINARY_DIR ${PILOT_LINK_BUILD_DIR}
        CONFIGURE_COMMAND ${CMAKE_SOURCE_DIR}/scripts/configure-pilot-link.sh
            ${PILOT_LINK_SOURCE_DIR}
            ${PILOT_LINK_PATCH_DIR}
            ${PILOT_LINK_BUILD_DIR}
            ${PILOT_LINK_INSTALL_DIR}
        BUILD_COMMAND make -C ${PILOT_LINK_BUILD_DIR}
        INSTALL_COMMAND make -C ${PILOT_LINK_BUILD_DIR} install
        BUILD_IN_SOURCE 0
    )

    add_library(pisock STATIC IMPORTED GLOBAL)
    add_dependencies(pisock pilot-link-external)
    set_target_properties(pisock PROPERTIES
        IMPORTED_LOCATION ${PILOT_LINK_INSTALL_DIR}/lib/libpisock.a
    )

    include_directories(SYSTEM ${PILOT_LINK_INSTALL_DIR}/include)

    set(PILOT_LINK_INCLUDE_DIR ${PILOT_LINK_INSTALL_DIR}/include PARENT_SCOPE)
    set(PILOT_LINK_LIB_DIR     ${PILOT_LINK_INSTALL_DIR}/lib     PARENT_SCOPE)

    install(DIRECTORY ${PILOT_LINK_INSTALL_DIR}/include/
        DESTINATION include/wildpalms/pilot-link
    )
else()
    message(STATUS "pilot-link: no vendored source — using system pilot-link package")

    find_library(PISOCK_LIBRARY NAMES pisock REQUIRED
        HINTS /usr/lib /usr/lib64 /usr/local/lib)
    find_path(PISOCK_INCLUDE_DIR pi-dlp.h REQUIRED
        HINTS /usr/include /usr/local/include)

    message(STATUS "  pisock library: ${PISOCK_LIBRARY}")
    message(STATUS "  pisock include: ${PISOCK_INCLUDE_DIR}")

    add_library(pisock SHARED IMPORTED GLOBAL)
    set_target_properties(pisock PROPERTIES
        IMPORTED_LOCATION ${PISOCK_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${PISOCK_INCLUDE_DIR}
    )

    include_directories(SYSTEM ${PISOCK_INCLUDE_DIR})

    set(PILOT_LINK_INCLUDE_DIR ${PISOCK_INCLUDE_DIR} PARENT_SCOPE)
    # No PILOT_LINK_LIB_DIR export in system mode — consumers should link
    # `pisock` target, not assemble a path themselves.

    install(DIRECTORY ${PISOCK_INCLUDE_DIR}/
        DESTINATION include/wildpalms/pilot-link
        FILES_MATCHING PATTERN "pi-*.h"
    )

    # Other CMakeLists in the tree do
    #   add_dependencies(<target> pilot-link-external)
    # to serialize against the vendored build. In system mode there is
    # nothing to build, so provide an empty custom target so those
    # add_dependencies calls remain valid.
    add_custom_target(pilot-link-external)
endif()
