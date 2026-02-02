## Ideas

### Complex calendar events
The Palm calendar is rather limited in complexity compared to the iCalendar standard as defined in RFC 5545 (`docs/rfc5545 - iCal.txt`). We have two ways of working around this.

#### QPilotSync sidecar database
We could store simplified or decomposed events within the primary palm database which make use of the Palm's native event handling. For instance, a single complex recurring event could be divided into several simpler events. We could also upload a new database, meaningful only to QPilotSync, which carried sementic meaning preserving the original complexity and relation of the events as a single event. That way, should the palm be synced to a new computer with QPilotSync, the result would be a single `*.ics` file could reconstituted whole with the complex recurrence data instead of the several files resulting from its decomposition. 

#### DateBk6+ support
There are many more complex calendar features supported by the 3rd party calendar app DateBk6 which are specified in the appendices of its manual from page 117 to 120 (`docs/datebk6-v61a-manual.pdf`). These include icons, Timezones, events spanning days, "floating events" (tasks), custom fonts, custom alarms, and more. We could optionally feature a modified sync engine to support these features, preserving them in custom `.ics` properties. We could support templates, etc. This would entail a lot of reverse engineering of the DateBk6 database, bu
