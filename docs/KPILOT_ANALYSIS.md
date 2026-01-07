# KPilot Architecture Analysis

Analysis of KPilot from kdepim-4.3.4 to inform QPilotSync design decisions.

---

## KPilot Architecture Overview

KPilot follows a sophisticated layered architecture developed over many years:

```
┌─────────────────────────────────────────────────────────────────┐
│                     KPilot Application                          │
├─────────────────────────────────────────────────────────────────┤
│                     ActionQueue                                 │
│              (FIFO queue of SyncActions)                        │
├─────────────────────────────────────────────────────────────────┤
│                     SyncAction (base)                           │
│                          │                                      │
│              ┌───────────┴───────────┐                          │
│         ConduitAction           Other Actions                   │
│              │                                                  │
│      RecordConduit (base)                                       │
│              │                                                  │
│   ┌──────────┼──────────┬──────────────┐                        │
│ TodoConduit  CalendarConduit  ContactsConduit  (etc.)           │
├─────────────────────────────────────────────────────────────────┤
│                     DataProxy Layer                             │
│    ┌──────────────┬──────────────┬──────────────┐               │
│    HHDataProxy    BackupProxy    PCDataProxy                    │
│    (Palm device)  (local backup) (Akonadi/File)                 │
├─────────────────────────────────────────────────────────────────┤
│                     IDMapping                                   │
│        (Palm ID ↔ PC ID bidirectional mapping)                  │
├─────────────────────────────────────────────────────────────────┤
│                     KPilotLink                                  │
│        (Device abstraction with tickle mechanism)               │
└─────────────────────────────────────────────────────────────────┘
```

---

## Key Design Patterns in KPilot

### 1. ActionQueue Pattern

From `actionQueue.h`:
```cpp
class ActionQueue : public SyncAction {
    void addAction(SyncAction *);
    bool exec(); // Runs actions sequentially
};
```

Actions are queued and executed one at a time. Each action emits `syncDone()` when complete.

**Pros:**
- Clear separation of sync phases
- Can chain arbitrary actions
- Supports interruptibility

**Cons:**
- Sequential only (no parallelism)
- Complex signal/slot chaining

### 2. Three-Way Sync with Backup Database

KPilot maintains THREE data sources:
1. **HHDataProxy** - Current state on Palm device
2. **BackupDataProxy** - State after last successful sync
3. **PCDataProxy** - Current state on PC (Akonadi/files)

The backup database serves as the "common ancestor" for conflict detection:
```cpp
void RecordConduit::syncRecords(Record *pcRecord, HHRecord *backupRecord, HHRecord *hhRecord) {
    // Compare current states against backup to detect:
    // - What changed on Palm (hhRecord vs backupRecord)
    // - What changed on PC (pcRecord vs backup via mapping)
}
```

### 3. DataProxy Abstraction

From `dataproxy.h`:
```cpp
class DataProxy {
    QString create(Record *record);
    void remove(const QString &id);
    void update(const QString &id, Record *record);

    bool commit();    // Apply all changes
    bool rollback();  // Undo changes

    // Transaction tracking
    QMap<QString, bool> fCreated;
    QMap<QString, Record*> fOldRecords;  // For rollback
    QMap<QString, bool> fDeleted;
};
```

All changes are staged in memory, then committed atomically. Rollback is possible if commit fails.

### 4. IDMapping with XML Persistence

From `idmapping.h`:
```cpp
class IDMapping {
    void map(const QString &hhRecordId, const QString &pcRecordId);
    QString hhRecordId(const QString &pcRecordId) const;
    QString pcRecordId(const QString &hhRecordId) const;

    void storeHHCategory(const QString &hhRecordId, const QString &category);
    void storePCCategories(const QString &pcRecordId, const QStringList &categories);

    QDateTime lastSyncedDate() const;
    bool commit();
    bool rollback();
};
```

Stores per-user, per-conduit mappings with category tracking.

### 5. SyncMode Enum

From `syncAction.h`:
```cpp
class SyncMode {
    enum Mode {
        eHotSync,        // Only modified records
        eFullSync,       // All records
        eCopyHHToPC,     // Overwrite PC with Palm
        eCopyPCToHH,     // Overwrite Palm with PC
        eBackup,         // Palm → local backup only
        eRestore         // Local backup → Palm
    };
};
```

### 6. Conflict Resolution Strategies

From `syncAction.h`:
```cpp
enum ConflictResolution {
    eUseGlobalSetting,
    eHHOverrides,      // Palm wins
    ePCOverrides,      // PC wins
    eDuplicate,        // Keep both
    eDoNothing,        // Skip
    eAskUser,          // Interactive prompt
    ePreviousSyncOverrides  // Use backup value
};
```

### 7. Volatility Check

KPilot includes a "sanity check" before committing large changes:
```cpp
bool RecordConduit::checkVolatility() {
    int hhVolatility = percentDeleted + percentUpdated + percentCreated;
    int allowedVolatility = 70;  // 70% threshold

    if (hhVolatility > allowedVolatility) {
        // Ask user: "Large changes detected, proceed?"
    }
}
```

This prevents accidental mass deletions.

### 8. Tickle Mechanism

From `kpilotlink.h` - keeps Palm awake during long operations:
```cpp
void KPilotLink::startTickle(unsigned timeout = 0);
void KPilotLink::stopTickle();
```

Runs in a background thread, periodically pings Palm to prevent timeout.

### 9. ConduitProxy - Lazy Loading

Conduits are loaded on-demand:
```cpp
class ConduitProxy : public ConduitAction {
    bool exec() {
        // Load the actual conduit plugin
        fConduit = loadPlugin(fLibraryName);
        fConduit->exec();
    }
};
```

### 10. Plugin API Versioning

```cpp
namespace Pilot {
    static const unsigned int PLUGIN_API = 20090624;  // YYYYMMDD format
}
```

---

## What KPilot Does Well

### 1. **Comprehensive Sync Logic**
The `syncRecords()` function handles ~20 different cases (documented as "Case 6.5.1" through "Case 6.5.18"). This exhaustive case handling prevents data loss.

### 2. **Transactional Semantics**
Changes are staged, then committed atomically. If PC commit fails, HH changes are rolled back.

### 3. **Category Sync Sophistication**
KPilot handles the asymmetry between Palm (1 category) and PC (multiple categories) with detailed logic for:
- Preserving existing PC categories when syncing from Palm
- Choosing which PC category to sync to Palm when multiple exist
- Creating new categories on Palm if space available

### 4. **First-Sync Detection**
Automatic detection when:
- No local backup exists
- ID mapping is invalid
- Database was just retrieved from Palm

### 5. **Extensive Testing**
The test suite includes:
- `rcfirstsynctest.cc` - First sync scenarios
- `rchotsynchhtest.cc` - HotSync from HH
- `rcfullsynchhtest.cc` - FullSync scenarios
- `rccopyhhtopctest.cc` - Copy HH→PC
- `rccopypctohhtest.cc` - Copy PC→HH
- `categoryhotsynctest.cc` - Category handling

---

## What KPilot Could Have Done Better

### 1. **Over-Engineering**
- Too many abstraction layers for simple operations
- 1000+ line files with complex inheritance hierarchies
- Heavy KDE/Qt4 dependencies (KConfig, KIO, KMessageBox)

### 2. **Blocking GUI**
Despite async patterns, some operations still blocked the UI.

### 3. **No Modern Backend Support**
- Tied to Akonadi (KDE-specific)
- No CalDAV/CardDAV
- No cloud service integration

### 4. **Complex Configuration**
- Each conduit has its own kcfg settings
- Hard to understand what settings affect behavior

### 5. **Error Recovery Limitations**
- Rollback is incomplete in some cases
- No journaling for crash recovery

---

## What QPilotSync Does Better

### 1. **Modern Technology Stack**
- Qt 6 / KDE Frameworks 6
- CMake with ExternalProject for pilot-link
- C++20 features

### 2. **Simpler Architecture**
Our design is more streamlined:
```
QPilotSync
    │
    ├── KPilotDeviceLink (device I/O)
    ├── Mappers (format conversion)
    ├── StorageManager (file I/O + git)
    └── SyncEngine (orchestration)
```

### 3. **Standard File Formats**
- Export to iCalendar, vCard, Markdown
- Not locked to KDE PIM ecosystem
- Files are human-readable and git-trackable

### 4. **Git-Based State Tracking**
Instead of XML ID mappings and backup databases, we can use:
```
~/.qpilotsync/
├── .git/                    # History is the "backup"
├── memos/
├── contacts/
├── calendar/
└── .sync-state/
    └── mappings.json        # ID mappings
```

Git provides:
- Full history (not just last sync)
- Branching for experiments
- Easy rollback to any point
- Conflict markers for manual resolution

### 5. **MapperResult Pattern**
Our design includes data loss warnings embedded in mapper results:
```cpp
struct MapperResult<T> {
    T data;
    bool isLossy;
    QList<DataLossWarning> warnings;
};
```

KPilot silently drops unsupported data.

### 6. **Backend Abstraction for Extensibility**
Our `SyncBackend` interface enables:
- Local file storage
- CalDAV/CardDAV servers
- Google Calendar/Contacts
- Other Palm sync tools (future)

### 7. **Async from the Ground Up**
Our `KPilotDeviceLink` uses Qt signals/slots and worker threads from the start.

---

## Lessons to Incorporate from KPilot

### 1. Adopt the Backup Database Concept
For 3-way sync, we need:
- `.sync-state/baseline/` - Snapshot after last sync
- Compare current files against baseline to detect PC changes
- Compare Palm records against baseline to detect Palm changes

### 2. Implement Volatility Checks
Before committing large changes, warn user:
```cpp
if (deleteCount > records.size() * 0.5) {
    // "This will delete 50% of your records. Proceed?"
}
```

### 3. Comprehensive Case Handling
Document and handle all sync scenarios:
- Both modified, same content → No-op
- Both modified, different content → Conflict
- PC modified, Palm unchanged → Update Palm
- Palm modified, PC unchanged → Update PC
- PC deleted, Palm modified → Conflict or policy
- etc.

### 4. First-Sync Matching
KPilot's `findMatch()` function tries to match records by content (description) on first sync, avoiding duplicates when ID mappings don't exist.

### 5. Category Preservation
When syncing multi-category PC records to single-category Palm:
- Preserve additional PC categories
- Only sync the "primary" category to Palm
- Track which category was synced

---

## Recommended QPilotSync Improvements

Based on KPilot analysis:

### Priority 1: Three-Way Sync Infrastructure
1. Implement baseline snapshots in `.sync-state/baseline/`
2. Add `SyncStateManager` to compare current vs baseline
3. Implement `IDMapping` class similar to KPilot's

### Priority 2: Conflict Resolution
1. Add `ConflictResolution` enum
2. Implement `solveConflict()` with policy support
3. Add user prompts for interactive resolution

### Priority 3: Safety Mechanisms
1. Volatility check before commit
2. Full rollback capability
3. Audit logging

### Priority 4: Conduit Plugin System
1. Define `Conduit` interface
2. Implement dynamic loading
3. Support third-party conduits

---

## Summary Comparison

| Feature | KPilot | QPilotSync |
|---------|--------|------------|
| Sync Algorithm | 3-way with backup DB | Currently 2-way, planned 3-way |
| State Storage | XML + local .pdb | Git + JSON |
| Output Formats | Internal only | iCal, vCard, Markdown |
| Backend Support | Akonadi only | Planned: CalDAV, local, cloud |
| Data Loss Handling | Silent | Planned: MapperResult warnings |
| Plugin System | Yes (.so loading) | Planned: Conduit interface |
| Conflict Resolution | 6 modes | TBD |
| Testing | Extensive unit tests | Manual testing |
| Dependencies | KDE 4 stack | Qt 6, KF6, minimal |

---

**Document Version**: 1.0
**Last Updated**: 2026-01-07
**Status**: Analysis Complete
