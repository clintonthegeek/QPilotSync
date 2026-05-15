# Phase E.8 — dummy plugins for exercising the new-ABI managers' factory
# load path. Install namespace "wildpalms_test/plugins" avoids clashing
# with real plugins (wildpalms/conduits, wildpalms/plugins).

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

# K.8b T13: dummy_backend deleted along with IBackendPluginV2.
add_subdirectory(dummy_action)

# Phase E.10 — Calendar plugin tests.
add_subdirectory(calendar)

# Phase E.15a — Install action tests.
add_subdirectory(install)

# M4: memo/contacts/todos/webcal re-enabled; _v2 integration tests gated
# within each subdirectory pending M6 rewrite against new SyncEngine API.
add_subdirectory(memo)
add_subdirectory(todos)
add_subdirectory(contacts)
add_subdirectory(webcalendar)
