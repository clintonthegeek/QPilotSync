# QPilotSync Architecture Overview

**Version**: 2.0
**Date**: 2026-01-08
**Status**: Phase 4 Complete (Bidirectional Sync + Async Operations)

---

## Executive Summary

QPilotSync is a modern Qt6/KDE application for synchronizing Palm Pilot devices with Linux. It provides bidirectional sync of Memos, Contacts, Calendar, and Todos between Palm devices and standard file formats (Markdown, vCard, iCalendar).

**Current State**: Full bidirectional sync is operational with async device operations, connection management, and profile-based configuration.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Qt6 GUI Layer                               │
│  ┌─────────────┐  ┌──────────────┐  ┌─────────────┐  ┌───────────────┐ │
│  │ MainWindow  │  │ LogWidget    │  │ Settings    │  │ Profile       │ │
│  │             │  │              │  │ Dialog      │  │ Settings      │ │
│  └──────┬──────┘  └──────────────┘  └─────────────┘  └───────────────┘ │
│         │                                                               │
└─────────┼───────────────────────────────────────────────────────────────┘
          │
┌─────────┼───────────────────────────────────────────────────────────────┐
│         ▼             Session Management Layer                          │
│  ┌─────────────────┐                                                    │
│  │ DeviceSession   │◄──────── Main thread API                          │
│  │                 │          - Connection lifecycle                    │
│  │  ┌───────────┐  │          - Operation dispatch                     │
│  │  │WorkerThrd│  │          - Progress/result aggregation             │
│  │  └───────────┘  │                                                    │
│  │  ┌───────────┐  │                                                    │
│  │  │TickleThrd│  │◄──────── Keep-alive thread                         │
│  │  └───────────┘  │                                                    │
│  └────────┬────────┘                                                    │
│           │                                                             │
└───────────┼─────────────────────────────────────────────────────────────┘
            │
┌───────────┼─────────────────────────────────────────────────────────────┐
│           ▼                 Sync Engine Layer                           │
│  ┌─────────────────┐    ┌────────────────┐    ┌────────────────────┐   │
│  │  SyncEngine     │───►│ Conduit (base) │    │ SyncState          │   │
│  │                 │    │                │    │ - ID mappings      │   │
│  │ Orchestrates:   │    │ Subclasses:    │    │ - Baseline hashes  │   │
│  │ - Conduit exec  │    │ - MemoConduit  │    │ - Sync timestamps  │   │
│  │ - Progress      │    │ - ContactCond. │    └────────────────────┘   │
│  │ - State mgmt    │    │ - CalendarCond │                             │
│  └─────────────────┘    │ - TodoConduit  │    ┌────────────────────┐   │
│                         │ - InstallCond. │    │ SyncBackend        │   │
│                         └────────────────┘    │ └─LocalFileBackend │   │
│                                               └────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
            │
┌───────────┼─────────────────────────────────────────────────────────────┐
│           ▼              Palm Communication Layer                       │
│  ┌─────────────────┐    ┌────────────────┐    ┌────────────────────┐   │
│  │ DeviceWorker    │    │KPilotDeviceLink│    │ PilotRecord        │   │
│  │ (worker thread) │───►│                │    │ CategoryInfo       │   │
│  │                 │    │ DLP operations │    └────────────────────┘   │
│  │ - doInstall()   │    │ via pilot-link │                             │
│  │ - doSync()      │    └────────────────┘    ┌────────────────────┐   │
│  │ - doOpenConduit │                          │ TickleWorker       │   │
│  └─────────────────┘                          │ - Keep-alive ping  │   │
│                                               └────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
            │
┌───────────┼─────────────────────────────────────────────────────────────┐
│           ▼                 Data Mapping Layer                          │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐            │
│  │ MemoMapper     │  │ ContactMapper  │  │ CalendarMapper │            │
│  │ Palm↔Markdown  │  │ Palm↔vCard 4.0 │  │ Palm↔iCalendar │            │
│  └────────────────┘  └────────────────┘  └────────────────┘            │
│                                          ┌────────────────┐            │
│                                          │ TodoMapper     │            │
│                                          │ Palm↔iCal TODO │            │
│                                          └────────────────┘            │
└─────────────────────────────────────────────────────────────────────────┘
            │
┌───────────┼─────────────────────────────────────────────────────────────┐
│           ▼                   External Libraries                        │
│  ┌────────────────┐    ┌────────────────┐    ┌────────────────────┐    │
│  │ pilot-link     │    │ KCalendarCore  │    │ Qt6 Core/Widgets   │    │
│  │ (libpisock)    │    │ (KDE Frameworks│    │                    │    │
│  │                │    │  for iCal)     │    │                    │    │
│  └────────────────┘    └────────────────┘    └────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Layer Descriptions

### 1. GUI Layer (`src/app/`)

| Class | Purpose |
|-------|---------|
| `MainWindow` | Main application window, menu system, toolbar, MDI container |
| `LogWidget` | Timestamped log display with levels (Info, Warning, Error) |
| `ExportHandler` | Manual export to files (not sync) |
| `ImportHandler` | Manual import from files (not sync) |

**Configuration** (`src/`):
| Class | Purpose |
|-------|---------|
| `Settings` | Global app settings (QSettings-based) |
| `SettingsDialog` | Global settings UI |
| `Profile` | Per-device sync profile (stored in sync folder) |

### 2. Session Management Layer (`src/palm/`)

The async operation architecture ensures the UI never blocks during device operations.

| Class | Thread | Purpose |
|-------|--------|---------|
| `DeviceSession` | Main | Public API for all device operations. Manages worker and tickle threads. |
| `DeviceWorker` | Worker | Executes blocking DLP operations (install, sync, openConduit) |
| `TickleWorker` | Tickle | Sends periodic keep-alive pings to prevent Palm timeout |

**Connection Modes** (Profile setting):
- **KeepAlive**: Connection stays open, tickle keeps it alive between operations
- **DisconnectAfterSync**: Traditional HotSync behavior, disconnects when done

**Key Features**:
- Device polling (waits for USB device to appear)
- Auto-sync on connect
- Configurable sync type (HotSync vs Full Sync)
- Cancellation support at any point

### 3. Sync Engine Layer (`src/sync/`)

| Class | Purpose |
|-------|---------|
| `SyncEngine` | Orchestrates sync: runs conduits, manages state, reports progress |
| `Conduit` | Abstract base for data type handlers |
| `SyncState` | ID mappings (Palm↔PC), baseline hashes, sync timestamps |
| `SyncBackend` | Abstract interface for PC-side storage |
| `LocalFileBackend` | File-based backend (Markdown, vCard, iCalendar files) |
| `SyncContext` | Context passed to conduits during sync |

**Sync Modes** (`SyncMode` enum):
| Mode | Direction | Behavior |
|------|-----------|----------|
| HotSync | Bidirectional | Only modified records (uses dirty flags + baseline) |
| FullSync | Bidirectional | Compare all records |
| CopyPalmToPC | Palm → PC | Overwrite PC with Palm data |
| CopyPCToPalm | PC → Palm | Overwrite Palm with PC data |
| Backup | Palm → PC | Copy Palm, don't delete old PC files |
| Restore | PC → Palm | Full restore, delete Palm records not on PC |

### 4. Conduits (`src/sync/conduits/`)

| Conduit | Palm DB | PC Format | File Extension |
|---------|---------|-----------|----------------|
| `MemoConduit` | MemoDB | Markdown with YAML frontmatter | `.md` |
| `ContactConduit` | AddressDB | vCard 4.0 (RFC 6350) | `.vcf` |
| `CalendarConduit` | DatebookDB | iCalendar VEVENT (RFC 5545) | `.ics` |
| `TodoConduit` | ToDoDB | iCalendar VTODO (RFC 5545) | `.ics` |
| `InstallConduit` | N/A | .prc/.pdb files | N/A |

**Conduit Responsibilities**:
1. Convert Palm records ↔ backend format
2. Implement sync algorithms (hot, full, first, copy, backup, restore)
3. Handle ID mapping lookups
4. Detect and report conflicts
5. Write categories back to Palm when needed

### 5. Palm Communication (`src/palm/`)

| Class | Purpose |
|-------|---------|
| `KPilotLink` | Abstract device interface (from KPilot heritage) |
| `KPilotDeviceLink` | Concrete implementation using pilot-link |
| `PilotRecord` | Wrapper for raw Palm record data |
| `CategoryInfo` | Parser for Palm AppInfo category blocks |

**DLP Operations Used**:
- `dlp_OpenConduit()` - Advance Palm screen, start sync session
- `dlp_ReadUserInfo()` / `dlp_WriteUserInfo()` - User identification
- `dlp_OpenDB()` / `dlp_CloseDB()` - Database access
- `dlp_ReadRecordByIndex()` - Read records
- `dlp_WriteRecord()` / `dlp_DeleteRecord()` - Modify records
- `dlp_GetSysDateTime()` - Keep-alive ping (tickle)
- `pi_file_install()` - Install .prc/.pdb files

### 6. Data Mappers (`src/mappers/`)

| Mapper | Conversion |
|--------|------------|
| `MemoMapper` | Palm memo ↔ Markdown with YAML frontmatter |
| `ContactMapper` | Palm address ↔ vCard 4.0 |
| `CalendarMapper` | Palm datebook ↔ iCalendar VEVENT |
| `TodoMapper` | Palm todo ↔ iCalendar VTODO |

**Encoding**: All mappers handle Windows-1252 (Palm native) ↔ UTF-8 conversion.

---

## Directory Structure

```
QPilotSync/
├── src/
│   ├── main.cpp                 # Application entry point
│   ├── settings.cpp/h           # Global settings
│   ├── settingsdialog.cpp/h     # Settings UI
│   ├── profile.cpp/h            # Per-device profile
│   │
│   ├── app/                     # GUI components
│   │   ├── mainwindow.cpp/h     # Main application window
│   │   ├── logwidget.cpp/h      # Log display widget
│   │   ├── exporthandler.cpp/h  # Manual export
│   │   └── importhandler.cpp/h  # Manual import
│   │
│   ├── palm/                    # Device communication
│   │   ├── devicesession.cpp/h  # Async session manager
│   │   ├── deviceworker.cpp/h   # Worker thread operations
│   │   ├── tickleworker.cpp/h   # Keep-alive thread
│   │   ├── kpilotlink.cpp/h     # Abstract device interface
│   │   ├── kpilotdevicelink.cpp/h # pilot-link implementation
│   │   ├── pilotrecord.cpp/h    # Record data wrapper
│   │   └── categoryinfo.cpp/h   # Category parser
│   │
│   ├── sync/                    # Sync engine
│   │   ├── syncengine.cpp/h     # Main orchestrator
│   │   ├── conduit.cpp/h        # Base conduit class
│   │   ├── synctypes.h          # Enums, structs (SyncMode, SyncResult, etc.)
│   │   ├── syncstate.cpp/h      # ID mappings, baseline
│   │   ├── syncbackend.h        # Backend interface
│   │   ├── localfilebackend.cpp/h # File-based backend
│   │   │
│   │   └── conduits/            # Data type conduits
│   │       ├── memoconduit.cpp/h
│   │       ├── contactconduit.cpp/h
│   │       ├── calendarconduit.cpp/h
│   │       ├── todoconduit.cpp/h
│   │       └── installconduit.cpp/h
│   │
│   └── mappers/                 # Format converters
│       ├── memomapper.cpp/h
│       ├── contactmapper.cpp/h
│       ├── calendarmapper.cpp/h
│       └── todomapper.cpp/h
│
├── lib/                         # External dependencies
│   └── CMakeLists.txt           # pilot-link ExternalProject
│
├── docs/                        # Documentation
│   ├── ARCHITECTURE_2026.md     # This document
│   ├── ROADMAP.md               # Development phases
│   ├── SYNC_ENGINE_ARCHITECTURE.md
│   ├── ASYNC_DLP_DESIGN.md
│   └── ...
│
└── build/                       # Build output
```

---

## Profile & Sync Folder Structure

When a user creates/opens a profile, the sync folder contains:

```
~/PalmSync/                      # User's sync folder (profile)
├── .qpilotsync.conf             # Profile settings (INI format)
├── .state/                      # Sync state (ID mappings, baseline)
│   └── <username>/
│       ├── memos/
│       │   ├── mappings.json
│       │   └── baseline.json
│       ├── contacts/
│       ├── calendar/
│       └── todos/
│
├── memos/                       # Synced memos (Markdown)
│   ├── Shopping List.md
│   └── Meeting Notes.md
│
├── contacts/                    # Synced contacts (vCard)
│   ├── John Doe.vcf
│   └── Jane Smith.vcf
│
├── calendar/                    # Synced calendar (iCalendar)
│   └── events/
│       ├── abc123.ics
│       └── def456.ics
│
├── todos/                       # Synced todos (iCalendar)
│   └── tasks/
│       └── ghi789.ics
│
└── install/                     # Files to install on next sync
    ├── MyApp.prc
    └── installed/               # Successfully installed files moved here
```

---

## Data Flow: HotSync Operation

```
1. User clicks "HotSync" (or auto-sync triggers)
         │
         ▼
2. MainWindow::onHotSync()
   └── runInstallConduit() first (if files pending)
         │
         ▼
3. DeviceSession::requestSync(HotSync, engine)
   └── Stops tickle thread
   └── Invokes DeviceWorker::doSync() on worker thread
         │
         ▼
4. DeviceWorker::doSync()
   └── dlp_OpenConduit() - update Palm screen
   └── SyncEngine::syncAll(HotSync)
         │
         ▼
5. SyncEngine iterates enabled conduits:
   ┌─────────────────────────────────────────┐
   │ For each conduit:                       │
   │   1. Load SyncState (ID mappings)       │
   │   2. Create SyncContext                 │
   │   3. Call conduit->sync(context)        │
   │   4. Aggregate results                  │
   └─────────────────────────────────────────┘
         │
         ▼
6. Conduit::hotSync()
   └── Read Palm records (modified only via dirty flag)
   └── Read backend records
   └── Load baseline for change detection
   └── For each record pair:
       ├── Compare with baseline
       ├── Determine action (create/update/delete)
       ├── Handle conflicts per policy
       └── Execute writes
   └── Save new baseline
   └── Save ID mappings
         │
         ▼
7. Results flow back up:
   DeviceWorker → DeviceSession → MainWindow
         │
         ▼
8. DeviceSession::onWorkerSyncFinished()
   └── If KeepAlive mode: restart tickle
   └── If DisconnectAfterSync: disconnectDevice()
         │
         ▼
9. MainWindow shows result dialog
```

---

## Future Development Roadmap

### Completed (Phases 1-4)
- [x] Device connection and communication
- [x] Read-only export to files
- [x] Bidirectional sync engine
- [x] All four PIM conduits (Memo, Contact, Calendar, Todo)
- [x] Install conduit (.prc/.pdb files)
- [x] Async operations (non-blocking UI)
- [x] Tickle thread (keep-alive)
- [x] Profile-based configuration
- [x] Auto-sync and connection modes
- [x] Device listening mode (wait for HotSync)

### Phase 5: Polish & Features (Next)
- [ ] Conflict resolution UI (side-by-side comparison dialog)
- [ ] Backup/Restore operations (full device backup)
- [ ] Database browser (view Palm DBs)
- [ ] Category editor
- [ ] Dry-run preview mode
- [ ] Undo/rollback support

### Phase 6: Testing & Release
- [ ] Unit tests for mappers and sync logic
- [ ] Integration tests
- [ ] User documentation
- [ ] Packaging (AppImage, AUR, etc.)
- [ ] Public release

### Future Enhancements (Post-1.0)
- [ ] Akonadi integration (sync to KDE PIM)
- [ ] CalDAV/CardDAV backend (sync to cloud)
- [ ] Network HotSync (WiFi-capable Palms)
- [ ] Multi-platform (Windows, macOS via POSE emulator)

---

## Key Design Decisions

### 1. File-Based Backend (Not Database)
- Sync to plain files (Markdown, vCard, iCalendar)
- Git-friendly: user can version control their PIM data
- Interoperable: files work with any standard PIM app
- Human-readable: easy manual editing

### 2. Three-Way Sync with Baseline
- Store snapshot of PC files after each sync
- Detect PC-side changes by comparing current vs baseline
- Palm-side changes detected via dirty flags
- Enables accurate conflict detection

### 3. ID Mapping (Not UID Rewriting)
- Palm records have numeric IDs, PC files have UIDs
- Maintain bidirectional mapping in SyncState
- Don't modify Palm's internal IDs
- Handle orphaned mappings gracefully

### 4. Async Everything
- Worker thread for all DLP operations
- Tickle thread for keep-alive
- Main thread never blocks
- Qt signal/slot for cross-thread communication

### 5. Profile-Based Configuration
- Settings stored in sync folder (.qpilotsync.conf)
- Portable: move folder = move settings
- Per-device fingerprinting
- Multiple profiles supported

---

## External Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| Qt | 6.2+ | GUI framework, threading, signals/slots |
| KDE Frameworks | 6.x | KCalendarCore for iCalendar parsing |
| pilot-link | 0.12.5 | Palm USB/serial communication |

**Build System**: CMake 3.16+

---

## Configuration Reference

### Profile Settings (.qpilotsync.conf)
```ini
[profile]
name=My Palm

[device]
path=/dev/ttyUSB1
baudRate=115200
userId=12345
userName=Clinton
connectionMode=keepalive  # or "disconnect"
autoSyncOnConnect=false
defaultSyncType=hotsync   # or "fullsync"

[sync]
conflictPolicy=ask

[conduits/memos]
enabled=true

[conduits/contacts]
enabled=true
# ... etc
```

### Global Settings (~/.config/qpilotsync.conf)
```ini
[general]
defaultProfilePath=/home/user/PalmSync

[recentProfiles]
1/path=/home/user/PalmSync
2/path=/home/user/WorkPalm

[devices]
# Device fingerprint → profile path registry
```

---

**Document Maintainer**: QPilotSync Development
**Last Updated**: 2026-01-08
