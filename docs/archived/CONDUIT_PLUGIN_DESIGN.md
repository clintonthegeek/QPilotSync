# Conduit Plugin Architecture Design

**Status**: RFC (Request for Comments)
**Date**: 2026-01-08

---

## Goals

1. **Modularity**: Each conduit is self-contained with its own settings, icon, and logic
2. **Extensibility**: Easy to add new conduits without modifying core code
3. **Script Support**: Allow conduits written in Python, shell, or any executable
4. **Standardization**: Common interface for all conduit types

---

## Proposed Directory Structure

```
~/.local/share/qpilotsync/conduits/     # User-installed conduits
/usr/share/qpilotsync/conduits/         # System-wide conduits

Each conduit directory:
webcalendar/
├── manifest.json          # Required: Conduit metadata
├── icon.svg               # Optional: 48x48 or scalable icon
├── settings-schema.json   # Optional: Settings UI definition
├── sync.py                # Script conduit: executable
└── README.md              # Optional: Documentation
```

### Built-in Conduits (compiled)
```
src/sync/conduits/
├── memos/
│   ├── memoconduit.cpp/h
│   ├── icon.svg
│   └── settings-schema.json
├── contacts/
├── calendar/
├── todos/
└── install/
```

---

## Manifest Format

```json
{
  "id": "webcalendar",
  "name": "Web Calendar Subscriptions",
  "version": "1.0.0",
  "description": "Subscribe to remote iCalendar feeds (read-only)",
  "author": "QPilotSync",
  "icon": "icon.svg",

  "type": "script",           // "builtin", "script", or "binary"
  "script": "sync.py",        // For script/binary types
  "interpreter": "python3",   // Optional: interpreter path

  "capabilities": {
    "sync_to_palm": true,     // Can write to Palm
    "sync_from_palm": false,  // Can read from Palm
    "requires_device": false, // Needs Palm connected (false for web fetch)
    "standalone": true        // Can run without full sync
  },

  "palm_database": null,      // null for non-Palm conduits
  "file_extension": ".ics",

  "dependencies": {
    "before": ["calendar"],   // Run before these conduits
    "after": []               // Run after these conduits
  },

  "settings_schema": "settings-schema.json"
}
```

---

## Settings Schema Format

Defines the UI for conduit-specific settings:

```json
{
  "sections": [
    {
      "title": "Calendar Feeds",
      "settings": [
        {
          "id": "feeds",
          "type": "list",
          "label": "Subscribed Calendars",
          "item_schema": {
            "url": {"type": "url", "label": "URL", "required": true},
            "name": {"type": "string", "label": "Name"},
            "color": {"type": "color", "label": "Color", "default": "#3498db"}
          }
        }
      ]
    },
    {
      "title": "Fetch Schedule",
      "settings": [
        {
          "id": "fetch_interval",
          "type": "choice",
          "label": "Fetch Frequency",
          "options": [
            {"value": "every_sync", "label": "Every HotSync"},
            {"value": "daily", "label": "Daily"},
            {"value": "weekly", "label": "Weekly"},
            {"value": "monthly", "label": "Monthly"}
          ],
          "default": "weekly"
        },
        {
          "id": "last_fetch",
          "type": "datetime",
          "label": "Last Fetched",
          "readonly": true
        }
      ]
    },
    {
      "title": "Import Options",
      "settings": [
        {
          "id": "merge_mode",
          "type": "choice",
          "label": "When Importing",
          "options": [
            {"value": "merge", "label": "Merge with existing events"},
            {"value": "replace", "label": "Replace all events from this feed"}
          ],
          "default": "merge"
        },
        {
          "id": "date_range",
          "type": "choice",
          "label": "Import Events",
          "options": [
            {"value": "all", "label": "All events"},
            {"value": "future", "label": "Future events only"},
            {"value": "90days", "label": "Next 90 days"}
          ],
          "default": "future"
        }
      ]
    }
  ]
}
```

### Supported Setting Types
- `string` - Text input
- `url` - URL with validation
- `number` - Numeric input with optional min/max
- `boolean` - Checkbox
- `choice` - Dropdown/radio selection
- `color` - Color picker
- `path` - File/directory path with browse button
- `datetime` - Date/time display
- `list` - Dynamic list of items (with item_schema)
- `password` - Masked text input

---

## Script Conduit Protocol

Script conduits communicate via stdin/stdout JSON:

### Input (stdin)
```json
{
  "action": "sync",           // "sync", "preflight", "settings_validate"
  "mode": "hotsync",          // Sync mode
  "profile_path": "/home/user/PalmSync",
  "state_path": "/home/user/PalmSync/.state/user/webcalendar",
  "settings": {
    "feeds": [...],
    "fetch_interval": "weekly",
    "last_fetch": "2026-01-01T12:00:00Z"
  },
  "palm_connected": false
}
```

### Output (stdout)
```json
{
  "success": true,
  "palm_stats": {"added": 5, "modified": 2, "deleted": 0},
  "pc_stats": {"added": 0, "modified": 0, "deleted": 0},
  "log": [
    {"level": "info", "message": "Fetched 7 events from Municipal Calendar"},
    {"level": "info", "message": "Skipped 3 past events"}
  ],
  "updated_settings": {
    "last_fetch": "2026-01-08T15:30:00Z"
  },
  "files_to_install": [
    "/tmp/webcal_events.ics"
  ]
}
```

### Progress (stderr, optional)
```
PROGRESS:50:Downloading calendar...
PROGRESS:75:Parsing events...
```

---

## Core Changes Required

### 1. Conduit Base Class Extensions

```cpp
class Conduit : public QObject {
public:
    // Existing...

    // NEW: Plugin metadata
    virtual QString conduitId() const = 0;
    virtual QString displayName() const = 0;
    virtual QIcon icon() const;                    // NEW
    virtual QString description() const;            // NEW
    virtual QString version() const;                // NEW

    // NEW: Settings UI
    virtual bool hasSettings() const { return false; }
    virtual QWidget* createSettingsWidget(QWidget *parent);
    virtual void loadSettings(const QJsonObject &settings);
    virtual QJsonObject saveSettings() const;

    // NEW: Capabilities
    virtual bool requiresDevice() const { return true; }
    virtual bool canSyncToPalm() const { return true; }
    virtual bool canSyncFromPalm() const { return true; }
    virtual bool isStandalone() const { return false; }

    // NEW: Pre-sync check (e.g., "should we fetch now?")
    virtual bool shouldRun(SyncContext *context) const;
};
```

### 2. New Classes

```cpp
// Loads and manages external conduits
class ConduitLoader {
    QList<Conduit*> discoverConduits();
    Conduit* loadConduit(const QString &path);
    ScriptConduit* loadScriptConduit(const QString &manifestPath);
};

// Wrapper for script-based conduits
class ScriptConduit : public Conduit {
    QString m_scriptPath;
    QString m_interpreter;
    QJsonObject m_manifest;
    QJsonObject m_settingsSchema;

    SyncResult sync(SyncContext *context) override;
    // Runs script, parses JSON output
};

// Generates settings UI from schema
class SettingsSchemaWidget : public QWidget {
    void loadSchema(const QJsonObject &schema);
    QJsonObject currentValues() const;
    void setValues(const QJsonObject &values);
};
```

### 3. Profile Settings Storage

Per-conduit settings stored in profile:
```ini
[conduits/webcalendar]
enabled=true
settings={"feeds":[...],"fetch_interval":"weekly"}
```

---

## WebCalendarConduit Implementation

### Behavior

1. **Pre-sync check**: Compare last_fetch + interval vs now
2. **Fetch phase**: HTTP GET each feed URL
3. **Parse phase**: Parse .ics, filter by date range
4. **Merge phase**: Combine with existing calendar events
5. **Output**: Write merged .ics files to calendar/ folder
6. **Palm sync**: Calendar conduit picks up new events normally

### Integration with CalendarConduit

Option A: **Separate conduit, shared folder**
- WebCalendarConduit writes to `calendar/subscriptions/`
- CalendarConduit includes subscription files when syncing to Palm
- Clear separation of concerns

Option B: **WebCalendarConduit extends CalendarConduit**
- Inherits calendar sync logic
- Adds web fetch as pre-sync step
- Tighter integration

**Recommendation**: Option A - keeps conduits independent and composable.

---

## UI Integration

### Conduit List in Profile Settings

```
┌─────────────────────────────────────────────────────────┐
│ Conduits                                                │
├─────────────────────────────────────────────────────────┤
│ ☑ [📝] Memos                              [Settings...] │
│ ☑ [👤] Contacts                           [Settings...] │
│ ☑ [📅] Calendar                           [Settings...] │
│ ☑ [✓] Todos                               [Settings...] │
│ ☐ [📥] Install                            [Settings...] │
│ ☑ [🌐] Web Calendar Subscriptions         [Settings...] │
├─────────────────────────────────────────────────────────┤
│ [+ Add Conduit...]                                      │
└─────────────────────────────────────────────────────────┘
```

### Conduit Settings Dialog

Clicking "Settings..." opens a dialog generated from the settings schema:

```
┌─────────────────────────────────────────────────────────┐
│ Web Calendar Subscriptions Settings                     │
├─────────────────────────────────────────────────────────┤
│ Calendar Feeds                                          │
│ ┌─────────────────────────────────────────────────────┐ │
│ │ Municipal Garbage    https://city.gov/garbage.ics   │ │
│ │ School Calendar      https://school.edu/cal.ics    │ │
│ │                                          [+ Add]    │ │
│ └─────────────────────────────────────────────────────┘ │
│                                                         │
│ Fetch Schedule                                          │
│ Fetch Frequency: [Weekly          ▼]                    │
│ Last Fetched:    2026-01-01 12:00                       │
│                                                         │
│ Import Options                                          │
│ When Importing:  [Merge with existing ▼]                │
│ Import Events:   [Future events only  ▼]                │
│                                                         │
│                              [Cancel]  [Save]           │
└─────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Add settings support to Conduit base class
- [ ] Create SettingsSchemaWidget (JSON schema → Qt widgets)
- [ ] Add conduit settings storage to Profile
- [ ] Update Profile Settings dialog with conduit settings buttons

### Phase 2: WebCalendarConduit (Built-in)
- [ ] Implement WebCalendarConduit as C++ class
- [ ] HTTP fetching with Qt Network
- [ ] iCalendar parsing (use KCalendarCore)
- [ ] Interval-based fetch logic
- [ ] Integration with CalendarConduit

### Phase 3: Script Conduit Support
- [ ] Create ConduitLoader for discovering external conduits
- [ ] Implement ScriptConduit wrapper class
- [ ] JSON stdin/stdout protocol
- [ ] Error handling and timeout

### Phase 4: Distribution
- [ ] Conduit packaging format
- [ ] "Add Conduit" dialog (install from folder/URL)
- [ ] Example script conduits (Python, shell)

---

## Questions for Discussion

1. **Settings storage**: Profile-level vs global? (I suggest profile-level)

2. **Fetch timing**: Should web fetch happen:
   - Before sync starts (blocking)?
   - In parallel with sync?
   - As separate background operation?

3. **Script sandboxing**: Any security restrictions on script conduits?

4. **Event sourcing**: Should web calendar events be:
   - Merged into main calendar?
   - Kept in separate "subscriptions" category?
   - Marked with source URL metadata?

5. **Offline handling**: What if web fetch fails during sync?
   - Skip and continue?
   - Use cached data?
   - Warn user?

---

## Example: Garbage Pickup Script Conduit

`~/.local/share/qpilotsync/conduits/garbage-pickup/manifest.json`:
```json
{
  "id": "garbage-pickup",
  "name": "Garbage Pickup Schedule",
  "version": "1.0.0",
  "description": "Fetches municipal garbage pickup calendar",
  "type": "script",
  "script": "fetch.sh",
  "capabilities": {
    "sync_to_palm": true,
    "sync_from_palm": false,
    "requires_device": false
  },
  "settings_schema": "settings.json"
}
```

`fetch.sh`:
```bash
#!/bin/bash
# Reads settings from stdin JSON, outputs result JSON

INPUT=$(cat)
URL=$(echo "$INPUT" | jq -r '.settings.calendar_url')

# Fetch calendar
curl -s "$URL" > /tmp/garbage.ics

# Output result
cat <<EOF
{
  "success": true,
  "log": [{"level": "info", "message": "Fetched garbage schedule"}],
  "files_to_install": ["/tmp/garbage.ics"]
}
EOF
```

---

**Next Steps**: Review this design, then implement Phase 1 (core infrastructure).
