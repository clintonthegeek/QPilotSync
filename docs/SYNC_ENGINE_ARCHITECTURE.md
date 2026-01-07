# Sync Engine Architecture

A modular, extensible sync pipeline designed for:
- 2-way sync (Palm ↔ Local files) now
- 3-way sync (Palm ↔ Local ↔ Cloud) later
- PlanStanLite backend compatibility
- Third-party conduit plugins (Plucker, Documents2Go, etc.)

---

## Conceptual Layers

```
┌─────────────────────────────────────────────────────────────────┐
│                    APPLICATION LAYER                            │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────────┐   │
│  │ QPilotSync UI │  │ PlanStanLite  │  │ CLI Tools         │   │
│  │ (standalone)  │  │ (integration) │  │ (scripts/cron)    │   │
│  └───────┬───────┘  └───────┬───────┘  └─────────┬─────────┘   │
└──────────┼──────────────────┼────────────────────┼─────────────┘
           │                  │                    │
           ▼                  ▼                    ▼
┌─────────────────────────────────────────────────────────────────┐
│                    BACKEND LAYER                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              SyncBackend (PlanStanLite interface)        │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │   │
│  │  │ PalmBackend  │  │ LocalBackend │  │ CalDAVBackend│   │   │
│  │  │              │  │ (.ics/.vcf)  │  │ (future)     │   │   │
│  │  └──────┬───────┘  └──────────────┘  └──────────────┘   │   │
│  └─────────┼───────────────────────────────────────────────┘   │
└────────────┼───────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    SYNC SESSION LAYER                           │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    SyncSession                           │   │
│  │  - Orchestrates sync operations                          │   │
│  │  - Manages conduit execution order                       │   │
│  │  - Handles errors and rollback                           │   │
│  │  - Reports progress                                      │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    CONDUIT LAYER                                │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐   │
│  │ Calendar   │ │ Contacts   │ │ Memos      │ │ ToDos      │   │
│  │ Conduit    │ │ Conduit    │ │ Conduit    │ │ Conduit    │   │
│  └─────┬──────┘ └─────┬──────┘ └─────┬──────┘ └─────┬──────┘   │
│        │              │              │              │           │
│  ┌─────┴──────────────┴──────────────┴──────────────┴─────┐    │
│  │                  Plugin Conduits                        │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐              │    │
│  │  │ Plucker  │  │ Docs2Go  │  │ Custom   │  ...         │    │
│  │  │ Conduit  │  │ Conduit  │  │ Conduit  │              │    │
│  │  └──────────┘  └──────────┘  └──────────┘              │    │
│  └────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    MAPPER LAYER                                 │
│  ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐   │
│  │ Calendar   │ │ Contact    │ │ Memo       │ │ ToDo       │   │
│  │ Mapper     │ │ Mapper     │ │ Mapper     │ │ Mapper     │   │
│  │ Palm↔iCal  │ │ Palm↔vCard │ │ Palm↔MD    │ │ Palm↔iCal  │   │
│  └────────────┘ └────────────┘ └────────────┘ └────────────┘   │
│                                                                 │
│  Returns MapperResult<T> with data + warnings                   │
└─────────────────────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    STORAGE LAYER                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    StorageManager                        │   │
│  │                                                          │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │   │
│  │  │ LocalStore   │  │ BaselineStore│  │ IdMapper     │   │   │
│  │  │ (working     │  │ (last sync   │  │ (Palm↔UID    │   │   │
│  │  │  copy)       │  │  state)      │  │  mappings)   │   │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘   │   │
│  │                                                          │   │
│  │  ┌──────────────────────────────────────────────────┐   │   │
│  │  │              GitStateManager                      │   │   │
│  │  │  - Commits after each sync                        │   │   │
│  │  │  - Enables history/rollback                       │   │   │
│  │  │  - Provides 3-way merge baseline                  │   │   │
│  │  └──────────────────────────────────────────────────┘   │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    DEVICE LAYER                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    KPilotLink                            │   │
│  │  ┌──────────────────┐  ┌──────────────────┐             │   │
│  │  │ KPilotDeviceLink │  │ KPilotLocalLink  │             │   │
│  │  │ (real hardware)  │  │ (test/mock)      │             │   │
│  │  └────────┬─────────┘  └──────────────────┘             │   │
│  └───────────┼─────────────────────────────────────────────┘   │
└──────────────┼─────────────────────────────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    PILOT-LINK LIBRARY                           │
│  - DLP protocol                                                 │
│  - USB/Serial I/O                                               │
│  - Pack/Unpack functions                                        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core Interfaces

### 1. SyncBackend (PlanStanLite Compatibility)

This is the interface PlanStanLite uses for backends. Our `PalmBackend` implements this.

```cpp
class SyncBackend : public QObject {
    Q_OBJECT
public:
    virtual QString backendId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isOnline() const = 0;

    // Collection discovery
    virtual QList<CollectionInfo> availableCollections() = 0;

    // Item operations
    virtual void loadItems(const QString &collectionId) = 0;
    virtual void storeItem(const QString &collectionId,
                          const PimItem &item) = 0;
    virtual void deleteItem(const QString &collectionId,
                           const QString &itemUid) = 0;

    // Sync
    virtual void startSync(const QString &collectionId,
                          const SyncPlan &plan) = 0;

signals:
    void itemLoaded(const QString &collectionId, const PimItem &item);
    void syncProgress(const QString &message, int percentage);
    void syncCompleted(const SyncReport &report);
    void error(const QString &message);
};

// Palm implementation
class PalmBackend : public SyncBackend {
public:
    // Collections map to Palm databases
    // "palm:DatebookDB" → Calendar
    // "palm:AddressDB" → Contacts
    // "palm:MemoDB" → Memos
    // "palm:ToDoDB" → Tasks

    QList<CollectionInfo> availableCollections() override {
        return {
            {"palm:DatebookDB", "Palm Calendar", CollectionType::Calendar},
            {"palm:AddressDB", "Palm Contacts", CollectionType::Contacts},
            {"palm:MemoDB", "Palm Memos", CollectionType::Notes},
            {"palm:ToDoDB", "Palm Tasks", CollectionType::Tasks}
        };
    }

private:
    KPilotLink *m_link;
    QMap<QString, Conduit*> m_conduits;
    StorageManager *m_storage;
};
```

### 2. Conduit Interface

Conduits are the workhorses - they know how to sync a specific database type.

```cpp
class Conduit : public QObject {
    Q_OBJECT
public:
    // Identity
    virtual QString conduitId() const = 0;        // "calendar"
    virtual QString displayName() const = 0;      // "Calendar Sync"
    virtual QString palmDatabaseName() const = 0; // "DatebookDB"
    virtual QString localFileExtension() const = 0; // ".ics"

    // Capabilities
    virtual bool supportsRead() const { return true; }
    virtual bool supportsWrite() const { return true; }
    virtual bool supportsCategories() const { return true; }

    // Configuration
    virtual QWidget* createConfigWidget(QWidget *parent) { return nullptr; }
    virtual void loadConfig(const QJsonObject &config) {}
    virtual QJsonObject saveConfig() const { return {}; }

    // The main sync entry point
    virtual SyncResult sync(SyncContext *context) = 0;

signals:
    void progress(const QString &message, int percent);
    void recordProcessed(const QString &recordId, RecordAction action);
    void conflictDetected(const SyncConflict &conflict);
    void warning(const DataLossWarning &warning);
};

// Context passed to conduit during sync
struct SyncContext {
    KPilotLink *deviceLink;       // Palm device connection
    LocalStore *localStore;       // Working copy storage
    BaselineStore *baselineStore; // Last sync state
    IdMapper *idMapper;           // Palm ID ↔ UID mappings
    CategoryMapper *categories;   // Category name ↔ index
    SyncMode mode;                // HotSync, Backup, etc.
    SyncPolicy policy;            // Conflict resolution rules
    MappingPolicy mappingPolicy;  // Data loss handling rules
};

enum class SyncMode {
    HotSync,        // Bidirectional merge (default)
    FastSync,       // Only sync modified records
    PalmOverwrite,  // Palm → Local (destructive)
    LocalOverwrite, // Local → Palm (destructive)
    Backup,         // Palm → Local (no Palm writes)
    Restore         // Local → Palm (no merge)
};
```

### 3. Storage Manager

Manages the local file storage with git integration.

```cpp
class StorageManager : public QObject {
    Q_OBJECT
public:
    StorageManager(const QString &userPath);

    // Store access
    LocalStore* localStore(const QString &conduitId);
    BaselineStore* baselineStore(const QString &conduitId);
    IdMapper* idMapper(const QString &conduitId);

    // Git operations
    void beginSync();                    // Commit current state, create checkpoint
    void commitSync(const QString &msg); // Commit after successful sync
    void rollbackSync();                 // Revert to checkpoint on failure

    // History
    QList<SyncHistoryEntry> history(int limit = 50);
    void revertToSync(const QString &commitId);

private:
    QString m_userPath;
    GitStateManager *m_git;
    QMap<QString, LocalStore*> m_localStores;
    QMap<QString, BaselineStore*> m_baselineStores;
    QMap<QString, IdMapper*> m_idMappers;
};
```

### 4. Sync Session

Orchestrates a complete sync operation.

```cpp
class SyncSession : public QObject {
    Q_OBJECT
public:
    SyncSession(KPilotLink *link, StorageManager *storage);

    void addConduit(Conduit *conduit);
    void setMode(SyncMode mode);
    void setPolicy(SyncPolicy policy);

    // Run the sync
    void start();
    void cancel();

signals:
    void started();
    void conduitStarted(const QString &conduitId);
    void conduitFinished(const QString &conduitId, const SyncResult &result);
    void progress(const QString &message, int overallPercent);
    void conflictNeedsResolution(const SyncConflict &conflict);
    void finished(const SyncSessionReport &report);
    void error(const QString &message);

public slots:
    void resolveConflict(const QString &conflictId, ConflictResolution resolution);

private:
    void runNextConduit();
    void onConduitFinished(const SyncResult &result);

    KPilotLink *m_link;
    StorageManager *m_storage;
    QList<Conduit*> m_conduits;
    int m_currentConduit;
    SyncMode m_mode;
    SyncPolicy m_policy;
};
```

---

## Directory Structure

```
~/.qpilotsync/
├── config.json                     # Global configuration
├── users/
│   └── {username}/                 # Per-Palm-user directory
│       ├── .git/                   # Git repository for this user
│       ├── device_info.json        # Palm device metadata
│       │
│       ├── local/                  # Working copy (editable)
│       │   ├── calendar/
│       │   │   ├── meeting.ics
│       │   │   └── birthday.ics
│       │   ├── contacts/
│       │   │   ├── john_smith.vcf
│       │   │   └── jane_doe.vcf
│       │   ├── memos/
│       │   │   └── shopping_list.md
│       │   └── todos/
│       │       └── buy_groceries.ics
│       │
│       ├── baseline/               # Last sync snapshot
│       │   ├── calendar/
│       │   ├── contacts/
│       │   ├── memos/
│       │   └── todos/
│       │
│       ├── palm_cache/             # Raw Palm database cache
│       │   ├── DatebookDB.pdb
│       │   ├── AddressDB.pdb
│       │   ├── MemoDB.pdb
│       │   └── ToDoDB.pdb
│       │
│       └── mappings/               # ID mapping tables
│           ├── calendar.json
│           ├── contacts.json
│           ├── memos.json
│           └── todos.json
│
└── plugins/                        # Third-party conduit plugins
    ├── plucker-conduit.so
    └── docs2go-conduit.so
```

---

## Sync Flow

### Phase 1: Preparation

```
┌────────────────────────────────────────────────────────────────┐
│ 1. PREPARATION                                                  │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐                                           │
│  │ Connect Device  │ ← KPilotDeviceLink.openConnection()       │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Read User Info  │ ← Identify Palm user                      │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Begin Sync      │ ← dlp_OpenConduit(), lock device          │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Git Checkpoint  │ ← Commit current local state              │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Load Conduits   │ ← Based on config + discovered DBs        │
│  └─────────────────┘                                           │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

### Phase 2: Per-Conduit Sync

```
┌────────────────────────────────────────────────────────────────┐
│ 2. PER-CONDUIT SYNC (repeated for each conduit)                │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐                                           │
│  │ Open Palm DB    │ ← dlp_OpenDB(dbName, read-write)          │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Read Categories │ ← Parse AppInfo block                     │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    GATHER STATE                          │   │
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐            │   │
│  │  │Palm State │  │Local State│  │Baseline   │            │   │
│  │  │(device)   │  │(files)    │  │(last sync)│            │   │
│  │  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘            │   │
│  └────────┼──────────────┼──────────────┼──────────────────┘   │
│           │              │              │                       │
│           └──────────────┼──────────────┘                       │
│                          ▼                                      │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 COMPUTE CHANGES                          │   │
│  │                                                          │   │
│  │  For each record ID in (Palm ∪ Local ∪ Baseline):       │   │
│  │                                                          │   │
│  │  ┌─────────────────────────────────────────────────┐    │   │
│  │  │ inPalm  │ inLocal │ inBase │ Action              │    │   │
│  │  ├─────────┼─────────┼────────┼─────────────────────┤    │   │
│  │  │   ✓     │    ✓    │   ✓    │ Check modifications │    │   │
│  │  │   ✓     │    ✓    │   ✗    │ Created both → CONF │    │   │
│  │  │   ✓     │    ✗    │   ✓    │ Local deleted       │    │   │
│  │  │   ✗     │    ✓    │   ✓    │ Palm deleted        │    │   │
│  │  │   ✓     │    ✗    │   ✗    │ Palm created        │    │   │
│  │  │   ✗     │    ✓    │   ✗    │ Local created       │    │   │
│  │  │   ✗     │    ✗    │   ✓    │ Both deleted        │    │   │
│  │  └─────────────────────────────────────────────────┘    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                          ▼                                      │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 RESOLVE CONFLICTS                        │   │
│  │                                                          │   │
│  │  For each conflict:                                      │   │
│  │    - Apply policy (auto-resolve if configured)           │   │
│  │    - Or emit signal for UI to prompt user                │   │
│  │    - Options: KeepPalm, KeepLocal, KeepBoth, Skip        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                          ▼                                      │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                 APPLY CHANGES                            │   │
│  │                                                          │   │
│  │  ┌────────────────────┐  ┌────────────────────┐         │   │
│  │  │ Apply to Palm      │  │ Apply to Local     │         │   │
│  │  │ - writeRecord()    │  │ - Write .ics/.vcf  │         │   │
│  │  │ - deleteRecord()   │  │ - Delete files     │         │   │
│  │  └────────────────────┘  └────────────────────┘         │   │
│  └─────────────────────────────────────────────────────────┘   │
│                          ▼                                      │
│  ┌─────────────────┐                                           │
│  │ Close Palm DB   │                                           │
│  └─────────────────┘                                           │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

### Phase 3: Finalization

```
┌────────────────────────────────────────────────────────────────┐
│ 3. FINALIZATION                                                 │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────┐                                           │
│  │ Update Baseline │ ← Copy local → baseline                   │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Update ID Maps  │ ← Save new Palm↔UID mappings              │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Git Commit      │ ← "Sync 2026-01-07 15:30"                 │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ End Sync        │ ← dlp_EndOfSync(), unlock device          │
│  └────────┬────────┘                                           │
│           ▼                                                     │
│  ┌─────────────────┐                                           │
│  │ Generate Report │ ← Summary of all changes                  │
│  └─────────────────┘                                           │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## ID Mapping

A critical piece for bidirectional sync. Palm uses 32-bit integer record IDs assigned by the device. External formats use string UIDs (usually UUIDs).

```cpp
class IdMapper {
public:
    struct Mapping {
        int palmRecordId;
        QString externalUid;
        QDateTime created;
        QDateTime lastSynced;
    };

    // Lookup
    std::optional<QString> externalUid(int palmRecordId);
    std::optional<int> palmRecordId(const QString &externalUid);

    // Registration
    void registerMapping(int palmId, const QString &externalUid);
    void removeByPalmId(int palmId);
    void removeByExternalUid(const QString &externalUid);

    // Bulk operations
    QList<Mapping> allMappings();
    void clear();

    // Persistence
    void save(const QString &path);
    void load(const QString &path);
};
```

**ID Mapping JSON:**
```json
{
  "conduit": "calendar",
  "mappings": [
    {
      "palm_id": 12345678,
      "external_uid": "palm-datebook-12345678",
      "created": "2026-01-05T10:00:00Z",
      "last_synced": "2026-01-07T15:30:00Z"
    }
  ]
}
```

---

## Conduit Plugin System

For third-party conduits (Plucker, Documents2Go, etc.):

```cpp
// Plugin interface
class ConduitPlugin {
public:
    virtual ~ConduitPlugin() = default;

    // Plugin metadata
    virtual QString pluginId() const = 0;
    virtual QString pluginName() const = 0;
    virtual QString pluginVersion() const = 0;
    virtual QString pluginAuthor() const = 0;
    virtual QIcon pluginIcon() const = 0;

    // Factory
    virtual Conduit* createConduit() = 0;

    // Plugin lifecycle
    virtual void initialize() {}
    virtual void shutdown() {}
};

#define QPILOTSYNC_CONDUIT_PLUGIN_IID "org.qpilotsync.ConduitPlugin/1.0"
Q_DECLARE_INTERFACE(ConduitPlugin, QPILOTSYNC_CONDUIT_PLUGIN_IID)
```

**Example plugin implementation:**
```cpp
class PluckerConduitPlugin : public QObject, public ConduitPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPILOTSYNC_CONDUIT_PLUGIN_IID)
    Q_INTERFACES(ConduitPlugin)

public:
    QString pluginId() const override { return "plucker"; }
    QString pluginName() const override { return "Plucker E-Book Sync"; }
    QString pluginVersion() const override { return "1.0.0"; }
    QString pluginAuthor() const override { return "Community"; }

    Conduit* createConduit() override {
        return new PluckerConduit();
    }
};
```

---

## Conflict Resolution

```cpp
struct SyncConflict {
    QString recordId;
    QString palmUid;
    QString externalUid;

    QByteArray palmVersion;       // Binary Palm data
    QByteArray localVersion;      // Binary Palm data
    QByteArray baselineVersion;   // Binary Palm data (if 3-way)

    QString palmSummary;          // Human-readable Palm version
    QString localSummary;         // Human-readable local version
    QDateTime palmModified;
    QDateTime localModified;

    enum Type {
        ModifiedBoth,     // Both sides changed
        CreatedBoth,      // New on both sides (different content)
        DeletedVsModified // One deleted, other modified
    } type;
};

enum class ConflictResolution {
    KeepPalm,         // Use Palm version, overwrite local
    KeepLocal,        // Use local version, overwrite Palm
    KeepBoth,         // Create duplicate records
    Merge,            // Attempt field-level merge (if supported)
    Skip              // Don't sync this record
};

struct SyncPolicy {
    // Default resolution when not prompting user
    ConflictResolution defaultResolution = ConflictResolution::KeepBoth;

    // When to prompt user
    bool promptOnModifiedBoth = true;
    bool promptOnCreatedBoth = true;
    bool promptOnDeletedVsModified = true;

    // Global overrides
    bool alwaysPreferPalm = false;
    bool alwaysPreferLocal = false;
};
```

---

## PlanStanLite Integration Path

To use `PalmBackend` in PlanStanLite:

1. **Shared Library**: Build `libqpilotcore.so` with all non-UI sync code
2. **Backend Registration**: Register `PalmBackend` as a backend type
3. **Collection Mapping**: Map Palm databases to PlanStanLite collections
4. **Event Translation**: Use existing mappers (iCal ↔ Palm)

```cpp
// In PlanStanLite
void registerPalmBackend() {
    BackendFactory::registerBackend("palm", []() {
        return new PalmBackend();
    });
}

// Configuration
{
  "backends": [
    {
      "type": "palm",
      "id": "palm-pilot-clinton",
      "device": "/dev/ttyUSB0",
      "collections": [
        {"palm_db": "DatebookDB", "as": "calendar"},
        {"palm_db": "ToDoDB", "as": "tasks"}
      ]
    }
  ]
}
```

---

## Future Extensions

### Cloud Integration (Phase 4+)

```
                    ┌──────────────┐
                    │ CalDAV       │
                    │ Backend      │
                    └──────┬───────┘
                           │
┌──────────────┐    ┌──────┴───────┐    ┌──────────────┐
│ Palm         │◄──►│ Sync Engine  │◄──►│ Local        │
│ Backend      │    │ (3-way merge)│    │ Backend      │
└──────────────┘    └──────┬───────┘    └──────────────┘
                           │
                    ┌──────┴───────┐
                    │ Google Cal   │
                    │ Backend      │
                    └──────────────┘
```

The architecture supports adding more backends. The sync engine becomes a hub that can merge changes from multiple sources.

### Real-Time Sync (if hardware permits)

Some later Palm devices support network sync. Could add:
- WebDAV-like sync server
- Bluetooth background sync
- WiFi sync for Palm TX, etc.

---

## Summary

| Component | Responsibility |
|-----------|---------------|
| **SyncBackend** | PlanStanLite-compatible interface |
| **PalmBackend** | Wraps conduits, exposes Palm as backend |
| **SyncSession** | Orchestrates full sync operation |
| **Conduit** | Syncs one database type (pluggable) |
| **Mapper** | Converts Palm ↔ standard formats |
| **StorageManager** | Manages local/baseline/git storage |
| **IdMapper** | Palm record ID ↔ external UID |
| **KPilotLink** | Device communication abstraction |

This architecture provides:
- **Modularity**: Each conduit is independent
- **Extensibility**: Plugin system for third-party conduits
- **Testability**: Mock device link for CI/CD
- **Compatibility**: SyncBackend interface for PlanStanLite
- **Reliability**: Git-backed state for rollback/history
- **Scalability**: Ready for 3-way sync with cloud backends

---

**Document Version**: 1.0
**Last Updated**: 2026-01-07
**Status**: Design (Pre-Implementation)
