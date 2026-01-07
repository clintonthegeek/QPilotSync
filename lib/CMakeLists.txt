include(ExternalProject)

# Use pilot-link git clone (pristine condition)
set(PILOT_LINK_SOURCE_DIR ${CMAKE_SOURCE_DIR}/pilot-link)
set(PILOT_LINK_PATCH_DIR ${CMAKE_SOURCE_DIR}/pilot-link-git)
set(PILOT_LINK_BUILD_DIR ${CMAKE_BINARY_DIR}/pilot-link-build)
set(PILOT_LINK_INSTALL_DIR ${CMAKE_BINARY_DIR}/pilot-link-install)

# Build pilot-link using autotools (following Arch Linux pilot-link-git PKGBUILD)
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

# Create imported library target for libpisock
add_library(pisock STATIC IMPORTED GLOBAL)
add_dependencies(pisock pilot-link-external)

set_target_properties(pisock PROPERTIES
    IMPORTED_LOCATION ${PILOT_LINK_INSTALL_DIR}/lib/libpisock.a
)

# Make pilot-link headers available to all targets
include_directories(SYSTEM ${PILOT_LINK_INSTALL_DIR}/include)

# Export variables for use in other CMakeLists
set(PILOT_LINK_INCLUDE_DIR ${PILOT_LINK_INSTALL_DIR}/include PARENT_SCOPE)
set(PILOT_LINK_LIB_DIR ${PILOT_LINK_INSTALL_DIR}/lib PARENT_SCOPE)
