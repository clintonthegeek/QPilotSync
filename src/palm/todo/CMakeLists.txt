# WildPalmsPalmToDo — SyncBackend for Palm ToDoDB (G.7 Task 52).

find_package(KF6 REQUIRED COMPONENTS CalendarCore)

add_library(WildPalmsPalmToDo STATIC
    palmtodobackend.h
    palmtodobackend.cpp
)

target_include_directories(WildPalmsPalmToDo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmToDo
    PUBLIC
        Qt::Core
        KF6::CalendarCore
        WildPalmsPalmSync
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmToDo PROPERTIES POSITION_INDEPENDENT_CODE ON)
