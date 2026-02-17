cmake_minimum_required(VERSION 3.19)
project(QPilotSync VERSION 0.1.1 LANGUAGES CXX)

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

# Find Qt6
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network)
qt_standard_project_setup()

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
qt_add_executable(qpilotsync
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/settings.cpp
    src/settings.h
)

target_link_libraries(qpilotsync
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
        QPilotCore
        pisock  # pilot-link library
        bluetooth  # Required by pilot-link for Bluetooth support
        usb  # Required by pilot-link for USB support
)

# Install target
install(TARGETS qpilotsync
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Install desktop file and icon for AppImage/Linux desktop integration
install(FILES data/org.qpilotsync.QPilotSync.desktop
    DESTINATION share/applications
)
install(FILES data/org.qpilotsync.QPilotSync.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES data/qpilotsyncui.rc
    DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/qpilotsync
)
