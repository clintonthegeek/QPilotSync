# WildPalmsPalmTransformations — TransformationStage stubs for Palm encodings (G.7 Task 53).
# Real decode logic is a G.10 item requiring WildPalmsCodecs.

add_library(WildPalmsPalmTransformations STATIC
    palmaddresstovcard.h
    palmaddresstovcard.cpp
    palmemotoplaintext.h
    palmemotoplaintext.cpp
    palmtodotovtodo.h
    palmtodotovtodo.cpp
)

target_include_directories(WildPalmsPalmTransformations
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmTransformations
    PUBLIC
        Qt::Core
        Kalburator::Sync
)

set_target_properties(WildPalmsPalmTransformations PROPERTIES POSITION_INDEPENDENT_CODE ON)
