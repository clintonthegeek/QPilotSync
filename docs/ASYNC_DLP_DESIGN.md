# Async DLP Operations Design

## Problem Statement

Currently, all DLP (Desktop Link Protocol) operations run on the main Qt thread:
- **Connection** is async (worker thread) - GOOD
- **Sync operations** block the main thread - BAD
- **Install operations** block the main thread - BAD
- **Export operations** block the main thread - BAD

This causes:
1. **Frozen UI** during any device operation
2. **Palm screen stuck on "Identifying User"** (no `dlp_OpenConduit()` call)
3. **No progress feedback** during long operations
4. **Cannot cancel** mid-operation
5. **Device may timeout** during long UI interactions (no tickle)

---

## Architecture Goals

1. **Responsive UI**: Never block the main thread for DLP operations
2. **Progress Reporting**: Real-time feedback on operation progress
3. **Cancellation**: Ability to abort any operation gracefully
4. **Palm Screen Updates**: Proper `dlp_OpenConduit()` calls to update device display
5. **Keep-Alive (Tickle)**: Prevent device sleep during long operations
6. **Future-Proof**: Support for:
   - Plugin conduits (Plucker, Docs2Go)
   - PlanStanLite backend integration
   - Multiple simultaneous backends (Palm + CalDAV)
   - CLI tools sharing device session

---

## Conceptual Design

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           MAIN THREAD                                    │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────────────────┐ │
│  │   MainWindow   │  │  SyncEngine    │  │  Other UI Components       │ │
│  │   (UI events)  │  │  (orchestrates)│  │  (progress bars, dialogs)  │ │
│  └───────┬────────┘  └───────┬────────┘  └────────────────────────────┘ │
│          │ signals           │ signals                                   │
└──────────┼───────────────────┼───────────────────────────────────────────┘
           │                   │
           ▼                   ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        DeviceSession (QObject)                           │
│  Lives on: MAIN THREAD                                                   │
│  Purpose: Thread-safe interface to device operations                     │
│                                                                          │
│  Methods (all async, return immediately):                                │
│    - requestSync(mode, conduits) → emits syncFinished(result)           │
│    - requestInstall(files) → emits installFinished(results)             │
│    - requestExport(conduit, records) → emits exportFinished()           │
│    - requestCancel() → interrupts current operation                      │
│                                                                          │
│  Signals:                                                                │
│    - operationStarted(QString operationName)                            │
│    - progressUpdated(int current, int total, QString message)           │
│    - palmScreenChanged(QString message)                                  │
│    - operationFinished(bool success, QString summary)                   │
│    - error(QString message)                                              │
└────────────────────────────┬────────────────────────────────────────────┘
                             │ QMetaObject::invokeMethod (queued)
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        WORKER THREAD                                     │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                    DeviceWorker (QObject)                         │   │
│  │  Lives on: WORKER THREAD                                          │   │
│  │  Owns: socket descriptor (m_socket)                               │   │
│  │                                                                    │   │
│  │  Blocking DLP calls executed here:                                │   │
│  │    - dlp_OpenConduit()                                            │   │
│  │    - dlp_OpenDB() / dlp_CloseDB()                                │   │
│  │    - dlp_ReadRecordByIndex() / dlp_WriteRecord()                 │   │
│  │    - dlp_DeleteRecord()                                           │   │
│  │    - dlp_ReadAppBlock() / dlp_WriteAppBlock()                    │   │
│  │    - pi_file_install()                                            │   │
│  │                                                                    │   │
│  │  State Machine:                                                   │   │
│  │    Idle → Running → (Idle | Cancelled | Error)                   │   │
│  │                                                                    │   │
│  │  Cancellation:                                                    │   │
│  │    - Atomic flag checked between operations                       │   │
│  │    - Can interrupt after each record                              │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                             │
                             │ (optional, Phase 2)
                             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        TICKLE THREAD                                     │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                    TickleWorker (QObject)                         │   │
│  │  Purpose: Prevent Palm from sleeping during long operations       │   │
│  │                                                                    │   │
│  │  Behavior:                                                        │   │
│  │    - Sends periodic "tickle" commands every 5-10 seconds         │   │
│  │    - Uses separate DLP call (dlp_Tickle or similar)              │   │
│  │    - Runs while worker is busy                                    │   │
│  │    - Stops when worker finishes                                   │   │
│  │                                                                    │   │
│  │  Note: May require socket sharing or separate socket              │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Key Components

### 1. DeviceSession

The main interface for all device operations. Lives on the main thread but dispatches work to the worker thread.

```cpp
class DeviceSession : public QObject
{
    Q_OBJECT

public:
    explicit DeviceSession(QObject *parent = nullptr);
    ~DeviceSession();

    // Connection (already async via existing ConnectionWorker)
    void connectDevice(const QString &devicePath);
    void disconnectDevice();
    bool isConnected() const;
    int socket() const;  // For read-only access if needed

    // === Async Operations ===

    // Sync operation - runs all conduits
    void requestSync(Sync::SyncMode mode,
                     const QStringList &enabledConduits);

    // Install files to Palm
    void requestInstall(const QStringList &filePaths);

    // Export data from Palm (one-shot, no sync)
    void requestExport(const QString &conduitId,
                       const QString &outputPath);

    // Backup entire device
    void requestBackup(const QString &backupPath);

    // Restore from backup
    void requestRestore(const QString &backupPath);

    // Cancel current operation
    void requestCancel();

    // State
    bool isBusy() const;
    QString currentOperation() const;

signals:
    // Connection signals (existing)
    void connectionStarted();
    void connectionComplete(bool success);
    void deviceReady(const QString &userName, const QString &deviceId);

    // Operation lifecycle
    void operationStarted(const QString &operationName);
    void operationFinished(bool success, const QString &summary);
    void operationCancelled();

    // Progress
    void progressUpdated(int current, int total, const QString &message);
    void palmScreenMessage(const QString &message);  // What Palm is showing

    // Results
    void syncFinished(const Sync::SyncResult &result);
    void installFinished(const QList<Sync::InstallResult> &results);
    void exportFinished(bool success, const QString &path);
    void backupFinished(bool success, const QString &path);
    void restoreFinished(bool success);

    // Errors
    void error(const QString &message);
    void warning(const QString &message);

    // Log messages for UI
    void logMessage(const QString &message);

private slots:
    void onWorkerProgress(int current, int total, const QString &msg);
    void onWorkerFinished(bool success);
    void onWorkerError(const QString &error);

private:
    void ensureWorkerThread();
    void stopWorkerThread();

    QThread *m_workerThread = nullptr;
    DeviceWorker *m_worker = nullptr;
    std::atomic<bool> m_busy{false};
    QString m_currentOperation;
};
```

### 2. DeviceWorker

Runs on the worker thread, executes all blocking DLP operations.

```cpp
class DeviceWorker : public QObject
{
    Q_OBJECT

public:
    explicit DeviceWorker(QObject *parent = nullptr);
    ~DeviceWorker();

    // Called from main thread via queued connection
    void setSocket(int socket);

public slots:
    // === Slot-based API (invoked via QMetaObject::invokeMethod) ===

    void doOpenConduit(const QString &conduitName);

    void doSync(Sync::SyncMode mode,
                const QStringList &conduitIds,
                Sync::SyncEngine *engine);

    void doInstall(const QStringList &filePaths);

    void doExport(const QString &conduitId,
                  const QString &outputPath,
                  Sync::Conduit *conduit);

    void doBackup(const QString &backupPath);

    void doRestore(const QString &backupPath);

    void doCancel();

signals:
    void progress(int current, int total, const QString &message);
    void palmScreen(const QString &message);
    void syncResult(const Sync::SyncResult &result);
    void installResult(const QList<Sync::InstallResult> &results);
    void finished(bool success);
    void error(const QString &message);

private:
    bool checkCancelled();
    void emitProgress(int current, int total, const QString &msg);
    void setPalmScreen(const QString &message);

    int m_socket = -1;
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_running{false};
};
```

### 3. Integration with Existing Components

The existing `SyncEngine` and `Conduit` classes need minimal changes:

```cpp
// SyncEngine changes:
class SyncEngine : public QObject
{
    // ... existing interface ...

    // New: Execute sync on current thread (called by DeviceWorker)
    // Returns result directly instead of emitting signals
    Sync::SyncResult syncAllBlocking(Sync::SyncMode mode);

    // New: Progress callback for worker to emit signals
    void setProgressCallback(std::function<void(int, int, QString)> callback);
    void setCancelledCheck(std::function<bool()> check);
};

// Conduit changes:
class Conduit : public QObject
{
    // ... existing interface ...

    // New: Check if should abort
    void setCancelledCheck(std::function<bool()> check);

protected:
    bool isCancelled() const;  // Check during long operations
};
```

---

## Palm Screen Management

The Palm device displays status messages during sync. We control this with `dlp_OpenConduit()`:

```cpp
// In DeviceWorker::doSync()
void DeviceWorker::doSync(...)
{
    // 1. Notify Palm we're starting a conduit
    dlp_OpenConduit(m_socket);  // Shows "Syncing..." on Palm
    emit palmScreen("Syncing...");

    // 2. For each conduit, we can update the message
    for (const auto &conduitId : conduitIds) {
        setPalmScreen(QString("Syncing %1...").arg(conduitName));

        // ... run conduit ...
    }

    // 3. End sync
    dlp_EndOfSync(m_socket, dlpEndCodeNormal);
    emit palmScreen("Sync Complete");
}

void DeviceWorker::setPalmScreen(const QString &message)
{
    // Note: dlp_OpenConduit doesn't take a custom message parameter
    // The Palm shows a generic "Syncing" message
    // For custom messages, would need dlp_AddSyncLogEntry()
    emit palmScreen(message);  // Update our UI at least
}
```

---

## Thread Safety Considerations

1. **Socket Ownership**: Only `DeviceWorker` uses the socket after connection
2. **Atomic Flags**: `m_cancelRequested`, `m_running` are atomic
3. **Queued Connections**: All cross-thread calls use `Qt::QueuedConnection`
4. **No Shared State**: Worker has its own copy of data it needs
5. **Signal/Slot**: All results returned via signals, not return values

---

## Operation Flow Example: Sync

```
MainWindow                 DeviceSession              DeviceWorker (thread)
    │                           │                           │
    │ onHotSync()               │                           │
    │ ─────────────────────────>│                           │
    │                           │ requestSync(HotSync, [...])
    │                           │ ─────────────────────────>│
    │                           │                           │
    │                           │   [queued invocation]     │
    │                           │                           │
    │                           │                           │ dlp_OpenConduit()
    │                           │                           │
    │                           │<──── palmScreen("Syncing")│
    │<── palmScreenMessage() ───│                           │
    │                           │                           │
    │                           │                           │ [for each conduit]
    │                           │                           │   dlp_OpenDB()
    │                           │<──── progress(1, 4, "Memos")
    │<── progressUpdated() ─────│                           │
    │                           │                           │   ... sync ...
    │                           │                           │   dlp_CloseDB()
    │                           │<──── progress(2, 4, "Contacts")
    │<── progressUpdated() ─────│                           │
    │      ...                  │                           │
    │                           │                           │
    │                           │<──── syncResult(result)   │
    │<── syncFinished() ────────│                           │
    │                           │                           │
    │                           │<──── finished(true)       │
    │<── operationFinished() ───│                           │
    │                           │                           │
```

---

## Cancellation Flow

```
MainWindow                 DeviceSession              DeviceWorker (thread)
    │                           │                           │
    │ onCancelSync()            │                           │
    │ ─────────────────────────>│                           │
    │                           │ requestCancel()           │
    │                           │ ─────────────────────────>│
    │                           │                           │
    │                           │   m_cancelRequested = true│
    │                           │                           │
    │                           │                           │ [worker checks flag]
    │                           │                           │ if (checkCancelled())
    │                           │                           │   break loop
    │                           │                           │
    │                           │<──── finished(false)      │
    │<── operationCancelled() ──│                           │
    │                           │                           │
```

---

## Tickle Thread (Future Enhancement)

Palm devices can timeout during long operations. KPilot used a "tickle" thread:

```cpp
class TickleWorker : public QObject
{
    Q_OBJECT

public:
    void start(int socket);
    void stop();

private slots:
    void sendTickle();

private:
    QTimer *m_timer;
    int m_socket;
    std::atomic<bool> m_running{false};
};

// Implementation
void TickleWorker::sendTickle()
{
    if (!m_running) return;

    // Option 1: Use pi_tickle() if available
    // Option 2: Read system time (lightweight DLP call)
    // Option 3: Use dlp_ReadSysInfo()

    // This keeps the connection alive
    time_t dummy;
    dlp_GetSysDateTime(m_socket, &dummy);
}
```

**Note**: The tickle mechanism may require careful socket sharing or might work with the same socket. Needs testing with real hardware.

---

## Migration Path

### Phase 1: Basic Async (Immediate)

1. Create `DeviceSession` and `DeviceWorker` classes
2. Move install operations to worker thread
3. Add `dlp_OpenConduit()` after connection
4. UI remains responsive during install

### Phase 2: Full Async Sync

1. Migrate `SyncEngine` to work with worker
2. Add cancellation support to conduits
3. Progress reporting per-conduit and per-record
4. Full async for all sync modes

### Phase 3: Tickle & Polish

1. Implement tickle thread
2. Add retry logic for failed operations
3. Better error recovery
4. Timeout handling

### Phase 4: PlanStanLite Integration

1. `DeviceSession` becomes shareable across apps
2. Backend interface compatibility
3. CLI tools can use same session

---

## Files to Create/Modify

### New Files

```
src/palm/devicesession.h
src/palm/devicesession.cpp
src/palm/deviceworker.h
src/palm/deviceworker.cpp
src/palm/tickleworker.h      (Phase 3)
src/palm/tickleworker.cpp    (Phase 3)
```

### Modified Files

```
src/palm/kpilotdevicelink.h   - May be simplified or wrapped by DeviceSession
src/palm/kpilotdevicelink.cpp
src/sync/syncengine.h         - Add blocking sync method, callbacks
src/sync/syncengine.cpp
src/sync/conduit.h            - Add cancellation check
src/sync/conduit.cpp
src/app/mainwindow.h          - Use DeviceSession instead of direct KPilotDeviceLink
src/app/mainwindow.cpp
```

---

## Testing Strategy

1. **Unit Tests**: Mock DeviceWorker for main thread tests
2. **Integration Tests**: Test with real device (manual)
3. **Stress Tests**: Large databases, many records
4. **Cancellation Tests**: Cancel at various points
5. **Error Recovery**: Disconnect during operation

---

## Summary

| Component | Thread | Responsibility |
|-----------|--------|----------------|
| MainWindow | Main | UI events, display results |
| DeviceSession | Main | Thread-safe API, signal routing |
| DeviceWorker | Worker | All blocking DLP calls |
| TickleWorker | Tickle | Keep-alive pings (future) |
| SyncEngine | Worker (via callback) | Sync logic |
| Conduits | Worker (via callback) | Record conversion |

This design:
- Keeps UI responsive
- Supports cancellation
- Reports progress
- Manages Palm screen state
- Extensible for future backends
- Compatible with PlanStanLite integration path

---

**Document Version**: 1.1
**Last Updated**: 2026-01-08
**Status**: Phase 1 & 2 Complete

## Implementation Status

### Phase 1: Basic Async ✅
- [x] Created `DeviceSession` and `DeviceWorker` classes
- [x] Moved install operations to worker thread
- [x] Added `dlp_OpenConduit()` after connection
- [x] UI remains responsive during install

### Phase 2: Full Async Sync ✅
- [x] Migrated `SyncEngine` to work with worker
- [x] Added cancellation support to conduits
- [x] Progress reporting per-conduit
- [x] Full async for all sync modes (HotSync, FullSync, Backup, Restore, Copy)
- [x] SyncResult passed via signal for proper result display

### Phase 3: Tickle & Polish (Planned)
- [ ] Implement tickle thread
- [ ] Add retry logic for failed operations
- [ ] Better error recovery
- [ ] Timeout handling

### Phase 4: PlanStanLite Integration (Planned)
- [ ] `DeviceSession` becomes shareable across apps
- [ ] Backend interface compatibility
- [ ] CLI tools can use same session
