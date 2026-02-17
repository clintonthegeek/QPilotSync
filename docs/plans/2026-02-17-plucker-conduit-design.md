# Plucker Conduit Design

Date: 2026-02-17

## Overview

Build a Plucker conduit plugin for QPilotSync that fetches web content via
the bundled PyPlucker engine, produces `.pdb` files, and installs them on
Palm devices during sync. This is the first `IToolConduit` implementation.

## Decisions

- **Engine source:** Bundle PyPlucker Python package with the plugin
- **Install mechanism:** Add `installFile()` to KPilotDeviceLink, wire up
  `SyncContext::installQueue` as a post-conduit phase in SyncEngine
- **Spidering model:** Sync-time (Option A) — spider due channels during
  the sync pipeline, install results in the same sync cycle
- **UI scope:** Full feature parity with Plucker Desktop
- **Viewer install:** Auto-detect on first sync, offer to install viewer PRCs
- **Config storage:** Per-profile (in `.qpilotsync.conf` under `[Plucker]`)

## Plugin Identity

```json
{
    "KPlugin": {
        "Name": "Plucker Conduit",
        "Description": "Fetches web content and installs as Plucker documents on Palm",
        "Icon": "text-html"
    },
    "X-QPilotSync-ConduitId": "plucker",
    "X-QPilotSync-ConduitType": "tool",
    "X-QPilotSync-PalmCreatorId": "Plkr",
    "X-QPilotSync-PalmDatabase": "",
    "X-QPilotSync-RequiresDevice": true,
    "X-QPilotSync-DefaultEnabled": false,
    "X-QPilotSync-SortOrder": 50,
    "X-QPilotSync-RunAfter": ["memos", "contacts", "calendar", "todos"]
}
```

Creator ID `Plkr`. Sort order 50 (after sync conduits 0-3). Runs after all
data conduits since spidering involves network I/O. Disabled by default.

## File Layout

```
src/plugins/plucker/
    pluckerconduit.h/.cpp           IToolConduit implementation
    pluckerview.h/.cpp              Channel list sidebar page
    pluckerchanneldialog.h/.cpp     5-tab per-channel config dialog
    pluckerconfig.h/.cpp            Data model + profile persistence
    plucker-conduit.json            Plugin metadata
    CMakeLists.txt                  Builds qpilotsync_plucker.so
    parser/
        PyPlucker/                  Bundled PyPlucker engine (vendored)
            Spider.py
            PluckerDocs.py
            TextParser.py
            ImageParser.py
            ...
    viewer/
        viewer_en.prc               Standard-res English viewer
        viewer_hires_en.prc         Hi-res English viewer
        SysZLib.prc                 zlib library (standard-res)
        SysZLib_hires.prc           zlib library (hi-res)
```

## Classes

### PluckerConduit (QObject + IToolConduit)

Plugin entry point. Registered via `K_PLUGIN_FACTORY_WITH_JSON`.

- `prepareExecution(context)` — determine which channels are due based on
  scheduling config, write a temp `plucker.ini` with channel sections
- `sync(context)` — for each due channel, spawn `python3 Spider.py` via
  QProcess with appropriate flags, wait for completion
- `installResults(context)` — scan output dir for `.pdb` files, append
  paths to `context->installQueue`
- Viewer auto-install: check if `PlkrMain` exists on device via
  `deviceLink->findDatabase("PlkrMain")`. If not, queue viewer PRCs.

### PluckerView (QWidget)

Sidebar page, same pattern as MemoView/ContactView. Provides:

- QTreeWidget channel list: checkbox (enabled), name, last-fetched date,
  due indicator (red dot if past due)
- Buttons: Add, Edit, Remove, Fetch Now
- Detail panel below: selected channel summary (URL, settings, schedule)
- `loadFromPath(syncPath)` slot for profile loading

### PluckerChannelDialog (QDialog)

5-tab dialog matching Plucker Desktop's channel_dialog.xrc:

1. **Starting Page** — URL, doc name, category
2. **Spidering** — max depth, stay on host, depth-first/breadth-first,
   URL pattern, user-agent
3. **Images** — bpp, max dimensions, compression limit
4. **Destination** — storage mode (RAM/SD/MS/CF), compression type
5. **Scheduling** — auto-update toggle, frequency + period, next-due display

### PluckerConfig (plain class)

Data model for channel list. Reads/writes from profile config file.

```cpp
struct PluckerChannel {
    QString id;              // UUID
    QString name;            // Display name / Palm doc name
    QString homeUrl;         // Starting URL
    int maxDepth = 2;
    bool stayOnHost = false;
    bool depthFirst = false;
    QString userAgent;
    QString urlPattern;
    int bpp = 8;             // 0/1/2/4/8/16
    int maxWidth = 150;
    int maxHeight = 250;
    bool noImages = false;
    QString compression;     // "zlib" or "doc"
    QString category;
    // Destination
    QString storageMode;     // "ram", "sd", "ms", "cf"
    QString cardDirectory;
    // Scheduling
    bool updateEnabled = true;
    int updateFrequency = 1;
    QString updatePeriod;    // "hours"/"days"/"weeks"/"months"
    QDateTime lastFetched;
    QDateTime nextDue;
};
```

## Sync Pipeline Integration

### New: KPilotDeviceLink::installFile(path)

Wraps `pi_file_install()`. Returns success/fail + installed database name.
Clean abstraction — no conduit touches pilot-link directly.

### New: KPilotDeviceLink::findDatabase(name)

Wraps `dlp_FindDBInfo()`. Used by Plucker conduit to check if the viewer
is already installed on the device.

### New: SyncEngine post-conduit install phase

After all conduits have run in `syncAllOrdered()`, SyncEngine checks
`context->installQueue`. If non-empty, iterates the list and calls
`deviceLink->installFile()` for each path. Logs results.

```
[sync conduits run in order]
  webcalendar → memos → contacts → calendar → todos → plucker
[post-conduit install phase]
  installQueue: ClinicalExam.pdb → installed
  installQueue: BBCNews.pdb → installed
```

### Unchanged

- Existing sync conduits (memos, contacts, calendar, todos, webcalendar)
- IToolConduit interface (used as-is)
- InstallConduit (continues to work independently for drag-drop installs)

## PyPlucker Invocation

The conduit invokes Python directly against the bundled Spider.py:

```bash
python3 <plugin_dir>/parser/PyPlucker/Spider.py \
    --home-url="http://example.com" \
    --doc-file="MyDoc" \
    --doc-name="My Document" \
    --pluckerdir="/tmp/plucker-output" \
    --maxdepth=2 \
    --compression=zlib \
    --bpp=8 \
    --stayonhost \
    --category="News"
```

Output: `/tmp/plucker-output/MyDoc.pdb`

Per-channel settings from `PluckerChannel` map directly to CLI flags.
The temp output directory is created under `QStandardPaths::TempLocation`.
