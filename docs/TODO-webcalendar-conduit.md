# TODO: WebCalendarConduit Needs UI and Possible Rearchitecture

## Current State

`WebCalendarConduit` fetches remote `.ics` feeds (Google Calendar, etc.),
splits them into individual event files, and drops them into the calendar
sync folder for `CalendarConduit` to pick up. Core fetch/filter logic works.

## What's Missing

1. **No settings widget** — `createSettingsWidget()` returns `nullptr`.
   Users cannot add, edit, or remove feeds without hand-editing profile JSON.

2. **No view page** — `hasView()` returns `false`, so the conduit is
   invisible in the KPageWidget sidebar. Users have no way to discover
   or interact with it.

## Architectural Oddity

`WebCalendarConduit` extends `SyncConduitBase` but stubs out every record
conversion method (`palmToBackend`, `backendToPalm`, `recordsEqual` all
return nullptr/false). It never opens a Palm database, never generates
conflicts, and sets `canSyncToPalm = false` / `canSyncFromPalm = false`.

It only uses `SyncConduitBase` for the plugin infrastructure, the `sync()`
entry point, and `runBefore() → {"calendar"}` ordering.

### Options

- **Keep as-is**: It works. Just add the settings widget and a view page.
- **Reclassify as IToolConduit**: Better semantic fit (it's a download tool),
  but would need `SyncEngine` ordering support for tool conduits.
- **Make it a pre-sync hook on CalendarConduit**: Simplest conceptually,
  but couples the two conduits.

## Next Steps

- Implement `createSettingsWidget()` (feed list editor with add/remove/edit)
- Consider adding a minimal view page showing feed status and last fetch time
- Decide on architectural classification (low priority — current approach works)
