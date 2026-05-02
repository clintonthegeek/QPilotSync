# Wild Palms plugins
# Each subdirectory here builds a conduit plugin (.so) that links against WildPalmsCore.
# Plugins are loaded at runtime by the sync engine.

add_subdirectory(calendar)
add_subdirectory(install)
add_subdirectory(memo)
add_subdirectory(contacts)
add_subdirectory(todos)
add_subdirectory(webcalendar)

if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(plucker)
endif()
