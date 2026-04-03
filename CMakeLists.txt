cmake_minimum_required(VERSION 3.19)
project(WildPalms VERSION 0.1.1 LANGUAGES CXX)

# Build shared libs (required for KDE plugin .so loading via kcoreaddons_add_plugin)
set(BUILD_SHARED_LIBS ON)

# Required by kcoreaddons_add_plugin for MODULE library output location
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

# KDE Extra CMake Modules — provides KDE_INSTALL_PLUGINDIR etc.
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
include(KDEInstallDirs6)

# RPATH for installed binaries/libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# Find KDE Frameworks
find_package(KF6CalendarCore REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6Crash REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)

# System libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)

# Build pilot-link first
add_subdirectory(lib)

# Main source library
add_subdirectory(src)

# Testing support
option(BUILD_TESTS "Build the test suite" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# Main executable
qt_add_executable(wildpalms
    WIN32 MACOSX_BUNDLE
    src/main.cpp
)

target_link_libraries(wildpalms
    PRIVATE
        Qt::Core
        Qt::Widgets
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Crash
        KF6::Notifications
        KF6::StatusNotifierItem
        WildPalmsCore
)

# Install target
install(TARGETS wildpalms
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Install desktop file and icon for AppImage/Linux desktop integration
install(FILES data/ca.vibekoder.wildpalms.desktop
    DESTINATION share/applications
)
install(FILES data/ca.vibekoder.wildpalms.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES data/wildpalmsui.rc
    DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/wildpalms
)

# === SDK: CMake config-file package ===

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

configure_package_config_file(
    cmake/WildPalmsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    INSTALL_DESTINATION lib/cmake/WildPalms
)

install(EXPORT WildPalmsTargets
    FILE WildPalmsTargets.cmake
    NAMESPACE WildPalms::
    DESTINATION lib/cmake/WildPalms
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    DESTINATION lib/cmake/WildPalms
)
