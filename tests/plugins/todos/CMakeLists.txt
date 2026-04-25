# Phase E.11 — ToDo plugin tests.
# Tasks 2-6 build test binaries directly against the source files in
# the todos submodule.

set(TODOS_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/todos)

# --- Task 2: TodoIcsTranscoder ---
add_executable(tst_todoicstranscoder
    tst_todoicstranscoder.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todoicstranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_todoicstranscoder
    PRIVATE
        WildPalmsPalmCodecs   # decodeTodo / encodeTodo
        WildPalmsPalmSync     # PalmRecord
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todoicstranscoder COMMAND tst_todoicstranscoder)
