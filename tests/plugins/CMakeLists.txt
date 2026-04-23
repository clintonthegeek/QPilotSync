# Phase E.8 — dummy plugins for exercising the new-ABI managers' factory
# load path. Install namespace "wildpalms_test/plugins" avoids clashing
# with real plugins (wildpalms/conduits, wildpalms/plugins).

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_subdirectory(dummy_backend)
# add_subdirectory(dummy_action)  # lands in Task 9
