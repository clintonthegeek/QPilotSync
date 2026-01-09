cmake_minimum_required(VERSION 3.19)
project(QPilotSync VERSION 0.1.0 LANGUAGES CXX)

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
