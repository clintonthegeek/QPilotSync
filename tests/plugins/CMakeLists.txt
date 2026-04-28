# Phase E.8 — dummy plugins for exercising the new-ABI managers' factory
# load path. Install namespace "wildpalms_test/plugins" avoids clashing
# with real plugins (wildpalms/conduits, wildpalms/plugins).

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_subdirectory(dummy_backend)
add_subdirectory(dummy_action)

# Phase E.9 Task 4 — MemoMarkdown encoder/decoder tests.
add_subdirectory(memo)

# Phase E.10 — Calendar plugin tests.
add_subdirectory(calendar)

# Phase E.11 — ToDo plugin tests.
add_subdirectory(todos)

# Phase E.12 — Contacts plugin tests.
add_subdirectory(contacts)

# Phase E.13 — WebCalendar plugin tests.
add_subdirectory(webcalendar)

# Phase E.14 — Plucker plugin tests.
add_subdirectory(plucker)
