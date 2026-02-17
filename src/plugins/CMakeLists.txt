# QPilotSync plugins
# Each subdirectory here builds a conduit plugin (.so) that links against QPilotCore.
# Plugins are loaded at runtime by the sync engine.

add_subdirectory(memo)
add_subdirectory(contacts)
add_subdirectory(calendar)
add_subdirectory(todos)
add_subdirectory(webcalendar)
