# QPilotSync Technical Architecture

## Architectural Overview

QPilotSync follows a multi-layered architecture inspired by PlanStanLite's backend system and KPilot's daemon/conduit pattern.

```
┌─────────────────────────────────────────────────────────────┐
│                      Qt6 GUI Layer                          │
│  ┌──────────────┬──────────────┬────────────┬────────────┐ │
│  │ Main Window  │  Sync Log    │  File      │  Config    │ │
│  │              │  Viewer      │  Installer │  Dialog    │ │
│  └──────────────┴──────────────┴────────────┴────────────┘ │
└──────────────────────────┬──────────────────────────────────┘
                           │ Qt Signals/Slots
┌──────────────────────────┴──────────────────────────────────┐
│                    Controller Layer                         │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  CollectionController                                │  │
│  │  - Manages sync sessions                             │  │
│  │  - Coordinates backend operations                    │  │
│  │  - Handles undo/redo stack                          │  │
│  └──────────────────────────────────────────────────────┘  │
└──────────────────────────┬──────────────────────────────────┘
                           │ Backend Interface
┌──────────────────────────┴──────────────────────────────────┐
│                     Backend Layer                           │
│  ┌──────────────┬──────────────┬──────────────────────────┐│
│  │ PalmBackend  │ LocalBackend │  (Future: Akonadi, etc.) ││
│  │              │  (.ics/.vcf) │                          ││
│  └──────┬───────┴──────────────┴──────────────────────────┘│
└─────────┼──────────────────────────────────────────────────┘
          │
┌─────────┴─────────────────────────────────────────────────┐
│              Palm Communication Layer                      │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  PilotLink (device abstraction)                      │ │
│  │  - KPilotLink interface (KPilotDeviceLink impl)     │ │
│  │  - Connection state machine                          │ │
│  │  - Tickle mechanism                                  │ │
│  └──────────────┬───────────────────────────────────────┘ │
└─────────────────┼──────────────────────────────────────────┘
                  │ pilot-link library
┌─────────────────┴──────────────────────────────────────────┐
│                   pilot-link (libpisock)                   │
│  - DLP protocol implementation                             │
│  - Database pack/unpack functions                          │
│  - USB/Serial communication                                │
└────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. Backend System (Inspired by PlanStanLite)

**Abstract Backend Interface:**
```cpp
class SyncBackend : public QObject {
    Q_OBJECT
public:
    // Discovery
    virtual void loadCalendars(const QString &collectionId) = 0;
    virtual void loadItems(KCalendarCore::MemoryCalendar* cal) = 0;

    // CRUD Operations
    virtual void storeItems(KCalendarCore::MemoryCalendar* cal,
                           const QList<KCalendarCore::Incidence::Ptr> &items) = 0;
    virtual void updateItem(KCalendarCore::MemoryCalendar* cal,
                           const KCalendarCore::Incidence::Ptr &item) = 0;
    virtual void removeItem(const QString &calId, const QString &itemUid) = 0;

    // Sync
    virtual void startSync(const QString &collectionId,
                          KCalendarCore::MemoryCalendar* calendar,
                          const QList<KCalendarCore::Incidence::Ptr> &created,
                          const QList<KCalendarCore::Incidence::Ptr> &updated,
                          const QMap<QString, QString> &deleted) = 0;

signals:
    void itemLoaded(KCalendarCore::Incidence::Ptr incidence);
    void syncProgress(const QString &message, int percentage);
    void syncCompleted(bool success);
    void error(const QString &message);
};
```

**PalmBackend Implementation:**
- Implements SyncBackend interface
- Uses PilotLink for device communication
- Manages sync state (modified/created/deleted tracking)
- Handles three-way merge (current Palm, current PC, last sync backup)

**LocalBackend (for .ics/.vcf files):**
- Reuse PlanStanLite's LocalBackend pattern
- Store each calendar/contact collection in separate directories
- Individual .ics files per event, .vcf per contact

### 2. Data Model Layer

**Dual Calendar System (from PlanStanLite):**
```cpp
class Collection {
    // Working copy - user edits
    QMap<QString, KCalendarCore::MemoryCalendar*> m_workingCalendars;

    // Base copy - last synced state
    QMap<QString, KCalendarCore::MemoryCalendar*> m_baseCalendars;

    // Backend associations
    QHash<QString, QString> m_calendarToBackend;

    // Compute changes for sync
    DirtyChanges pendingChanges(const QString &calendarId) const;
};
```

**Global Data Model:**
- `GlobalIncidenceModel` (QAbstractTableModel) - All events/todos across sources
- `GlobalContactModel` (QAbstractTableModel) - All contacts
- Filter/sort proxy models for views

### 3. Palm Device Communication Layer

**Link Abstraction (from KPilot):**
```cpp
class KPilotLink : public QObject {
    Q_OBJECT
public:
    enum LinkStatus {
        Init, WaitingForDevice, FoundDevice,
        DeviceOpen, AcceptedDevice, SyncDone, Error
    };

    virtual bool openConnection() = 0;
    virtual void closeConnection() = 0;
    virtual LinkStatus status() const = 0;

    virtual int openDatabase(const QString &dbName) = 0;
    virtual void closeDatabase(int handle) = 0;

    virtual QList<PilotRecord*> readAllRecords(int dbHandle) = 0;
    virtual bool writeRecord(int dbHandle, PilotRecord *record) = 0;

signals:
    void statusChanged(LinkStatus status);
    void deviceReady(const PilotUser &userInfo);
};

class KPilotDeviceLink : public KPilotLink {
    // Uses pilot-link (libpisock) for actual communication
};

class KPilotLocalLink : public KPilotLink {
    // Reads/writes .pdb files for testing without device
};
```

**Tickle Thread:**
- Separate QThread that sends periodic "keep alive" commands
- Prevents Palm from sleeping during long operations
- Critical for UI interaction during sync

### 4. Sync Engine (from KPilot's RecordConduit)

**Action Queue Pattern:**
```cpp
class SyncAction : public QObject {
    Q_OBJECT
public:
    virtual bool exec() = 0;

signals:
    void progress(const QString &message, int percentage);
    void completed(bool success);
};

class ActionQueue : public QObject {
    void enqueue(SyncAction *action);
    void executeNext();

private:
    QQueue<SyncAction*> m_actions;
};
```

**Conduit Architecture:**
```cpp
class ConduitAction : public SyncAction {
protected:
    KPilotLink *m_link;
    Collection *m_collection;
    SyncMode m_mode;
};

class CalendarConduit : public ConduitAction {
    bool exec() override {
        // 1. Load Palm database
        // 2. Load PC calendar
        // 3. Load backup state
        // 4. Perform three-way merge
        // 5. Write changes to both sides
        // 6. Update backup
    }
};
```

**Three-Way Sync Algorithm:**
```
For each record:
  if (inPalm && inPC && inBackup):
    if (palm == backup && pc != backup): PC modified → copy to Palm
    elif (pc == backup && palm != backup): Palm modified → copy to PC
    elif (palm != pc && palm != backup && pc != backup): CONFLICT!
  elif (inPalm && inPC && !inBackup): Both created → CONFLICT!
  elif (inPalm && !inPC && inBackup): PC deleted → delete from Palm
  elif (!inPalm && inPC && inBackup): Palm deleted → delete from PC
  elif (inPalm && !inPC && !inBackup): Palm created → copy to PC
  elif (!inPalm && inPC && !inBackup): PC created → copy to Palm
```

### 5. Data Mapping Layer

**Palm ↔ iCalendar:**
```cpp
class PalmToICalMapper {
public:
    static KCalendarCore::Event::Ptr fromDatebookRecord(const PilotDateEntry *palmEvent);
    static PilotDateEntry* toDatebookRecord(const KCalendarCore::Event::Ptr &event);

    static KCalendarCore::Todo::Ptr fromToDoRecord(const PilotToDo *palmTodo);
    static PilotToDo* toToDoRecord(const KCalendarCore::Todo::Ptr &todo);
};
```

**Palm ↔ vCard:**
```cpp
class PalmToVCardMapper {
public:
    static QString fromAddressRecord(const PilotAddress *palmAddr);
    static PilotAddress* toAddressRecord(const QString &vcardData);

    // Handle contact photos (extended contacts)
    static QImage extractPhoto(const PilotContact *contact);
    static void setPhoto(PilotContact *contact, const QImage &photo);
};
```

**Field Mapping Challenges:**
- Timezone conversion (Palm uses local time, iCal uses UTC+TZID)
- Repeat rule mapping (Palm has limited repeat types)
- Category mapping (Palm: 16 categories, iCal: unlimited)
- Custom fields (extended contacts have more fields)

### 6. Configuration System

**Settings Structure:**
```cpp
class QPilotSettings {
    // Device
    QString devicePath;           // /dev/ttyUSB0, /dev/pilot
    int baudRate;                 // 9600, 57600, 115200
    QString encoding;             // Character encoding

    // Sync
    SyncMode defaultSyncMode;     // HotSync, FullSync, etc.
    ConflictResolution conflictStrategy;
    bool autoBackup;
    int backupFrequency;          // days

    // Storage
    QString backupDirectory;
    QString localStoragePath;     // for .ics/.vcf files

    // Conduits
    QStringList enabledConduits;
    QMap<QString, QVariant> conduitSettings;
};
```

**Config File Format (.qpilot):**
```json
{
  "version": "1.0",
  "device": {
    "path": "/dev/ttyUSB0",
    "speed": 57600,
    "encoding": "UTF-8"
  },
  "sync": {
    "mode": "hotsync",
    "conflictResolution": "ask",
    "autoBackup": true,
    "backupFrequency": 7
  },
  "backends": [
    {
      "id": "palm-backend",
      "type": "palm",
      "enabled": true
    },
    {
      "id": "local-backend",
      "type": "local",
      "rootPath": "./calendars"
    }
  ],
  "conduits": {
    "calendar": {
      "enabled": true,
      "syncMode": "hotsync"
    },
    "contacts": {
      "enabled": true,
      "syncMode": "hotsync"
    }
  }
}
```

## Technology Stack

### Core Libraries
- **Qt6 Core/Widgets** (6.2+): UI framework
- **KCalendarCore** (KF6): iCalendar parsing/serialization
- **pilot-link** (0.12.5+): Palm device communication
- **CMake** (3.19+): Build system
- **C++20**: Language standard

### Optional Libraries
- **KContacts** (KF6): vCard handling (alternative to hand-rolling)
- **Qt6 DBus**: For daemon communication (if we separate GUI/daemon)

### Testing
- **Qt Test**: Unit testing
- **Catch2**: Alternative test framework
- **Mock devices**: Virtual link for CI/CD

## Build System Structure

```
QPilotSync/
├── CMakeLists.txt              # Main build config
├── lib/                        # External libraries
│   ├── pilot-link/            # Build pilot-link here
│   └── CMakeLists.txt         # Builds pilot-link
├── src/                       # Source code
│   ├── CMakeLists.txt         # QPilotCore library
│   ├── backends/              # Backend implementations
│   ├── models/                # Data models
│   ├── ui/                    # UI components
│   ├── sync/                  # Sync engine
│   ├── palm/                  # Palm communication
│   └── main.cpp               # Entry point
├── tests/                     # Test suites
│   ├── backends/
│   ├── sync/
│   └── mappers/
├── docs/                      # Documentation
└── build/                     # Build output (gitignored)
```

## Key Design Decisions

### 1. Integrated vs Daemon Architecture
**Decision**: Start with integrated (GUI + sync in one process), refactor to daemon later if needed

**Rationale**:
- Simpler for initial development
- Qt6's threading is robust enough
- Can refactor to D-Bus daemon in Phase 3

### 2. Backend Interface Reuse
**Decision**: Use PlanStanLite's SyncBackend interface pattern

**Rationale**:
- Proven architecture
- Enables future Akonadi/CalDAV integration
- Supports multi-backend collections

### 3. Data Format
**Decision**: iCalendar (KCalendarCore) for events/todos, vCard for contacts

**Rationale**:
- Industry standard
- Maximum interoperability
- Mature libraries available
- Future-proof

### 4. Sync Algorithm
**Decision**: Three-way merge with backup state

**Rationale**:
- Only way to detect deletions reliably
- Enables intelligent conflict resolution
- Proven by KPilot

### 5. Build System
**Decision**: CMake with in-tree pilot-link build

**Rationale**:
- pilot-link not in modern distro repos
- Control over build flags
- Ensures compatibility

## Development Phases

### Phase 1: Infrastructure (Weeks 1-2)
- CMake setup
- Build pilot-link
- Basic Qt6 app skeleton
- Device detection and connection
- Read Palm user info
- Logging system

### Phase 2: Read-Only Sync (Weeks 3-4)
- Palm database reading
- Calendar conduit (Palm → iCal)
- Contact conduit (Palm → vCard)
- Export to local files
- Basic UI with sync log

### Phase 3: Bidirectional Sync (Weeks 5-8)
- Backup state tracking
- Three-way merge implementation
- Write to Palm
- Conflict detection/resolution UI
- Undo/redo support

### Phase 4: Polish (Weeks 9-12)
- File installer
- Full KPilot-style UI
- Configuration dialog
- Multiple device profiles
- Documentation
- Testing

---

**Document Version**: 1.0
**Last Updated**: 2026-01-05
**Next Review**: After Phase 1 completion
