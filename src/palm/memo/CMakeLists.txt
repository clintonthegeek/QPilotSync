# WildPalmsPalmMemo — SyncBackend for Palm MemoDB (G.7 Task 51).

find_package(KF6 REQUIRED COMPONENTS CalendarCore)

add_library(WildPalmsPalmMemo STATIC
    palmmemobackend.h
    palmmemobackend.cpp
)

target_include_directories(WildPalmsPalmMemo
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmMemo
    PUBLIC
        Qt::Core
        KF6::CalendarCore
        WildPalmsPalmSync
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmMemo PROPERTIES POSITION_INDEPENDENT_CODE ON)
