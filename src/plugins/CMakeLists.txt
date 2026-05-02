# Wild Palms plugins
# Each subdirectory here builds a conduit plugin (.so) that links against WildPalmsCore.
# Plugins are loaded at runtime by the sync engine.

add_subdirectory(calendar)
add_subdirectory(install)
add_subdirectory(memo)
add_subdirectory(contacts)
add_subdirectory(todos)
add_subdirectory(webcalendar)

# Plucker plugin is V1 (not migrated to IBackendPluginV2). Its .so trips
# a double-free in static destructors when discovered by
# KPluginMetaData::findPlugins, breaking the M5c plugin-page smoke test.
# Excluded from the build; M6 will delete the plucker plugin source tree
# entirely (see 2026-05-02 M5c plan / FINDINGS.md).
# add_subdirectory(plucker)
