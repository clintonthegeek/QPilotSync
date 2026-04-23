# WildPalmsPalmAdapters — stateless typed wrappers over PalmBackend
# for WP-internal UI consumption. NOT a libkalburator type. NOT on
# the plugin ABI. Transient convenience surface per E.7 spec.
#
# Phase E.7 sibling to WildPalmsPalmCodecs (same phase).
#
# Links WildPalmsPalmCodecs (POD + encode/decode), WildPalmsPalmSync
# (PalmBackend + PalmRecord + IPalmDatabaseAccess), WildPalmsPalmCalendar
# (CategoryMappingStore).

add_library(WildPalmsPalmAdapters STATIC
    palmadapters_placeholder.cpp
)

target_include_directories(WildPalmsPalmAdapters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmAdapters
    PUBLIC
        Qt::Core
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsPalmCalendar
)

set_target_properties(WildPalmsPalmAdapters PROPERTIES POSITION_INDEPENDENT_CODE ON)
