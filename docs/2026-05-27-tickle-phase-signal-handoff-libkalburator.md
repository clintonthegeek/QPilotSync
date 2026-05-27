# Handoff: Finer Engine Apply-Phase Signal for Tickle Scoping

**Date:** 2026-05-27  
**Target:** libkalburator  
**Requested by:** WildPalms runtime synchronization  
**Status:** Proposed for libkalburator feature phase F.X

## Context

WildPalms runtime manages a Palm device keep-alive tickle (periodic DLP requests to prevent connection timeout during long sync operations). Currently, the tickle is paused conservatively during the entire `Processing` phase—both diff calculation and apply, for both source and target backends.

This is functionally correct but suboptimal: the apply phase consists of two distinct sub-phases with different Palm-device implications:

1. **Diff & Merge (in-memory)** — Computes 3-way diffs and conflict resolution. No device I/O. The tickle *should* be paused here because decisions depend on baseline state.
2. **Write to Source** — Applies changes to the source backend (Palm). **Tickle *should* pause** here (device I/O).
3. **Write to Target** — Applies changes to the target backend (CalDAV, WebDAV, etc.). This is network I/O, often multi-minute PUT operations. **Tickle *should* resume** here to keep the Palm link alive.

The current `phaseChanged(Processing)` signal cannot distinguish which backend is being written, so `PalmRuntime::shouldPauseTickle()` returns `true` conservatively for the entire `Processing` phase.

## Current Limitation

In `WildPalms/src/runtime/palmticklephase.h` (lines 18–30), the logic is:

```cpp
inline bool shouldPauseTickle(Kalburator::Engine::SyncEngine::SyncPhase phase,
                               bool palmIsSource, bool palmIsTarget)
{
    using Phase = Kalburator::Engine::SyncEngine::SyncPhase;
    switch (phase) {
    case Phase::FetchingSource: return palmIsSource;
    case Phase::FetchingTarget: return palmIsTarget;
    case Phase::Processing:     return true;   // conservative: apply phase touches Palm
    case Phase::Idle:
    case Phase::Complete:       return false;
    }
    return false;
}
```

The `Phase::Processing` case cannot distinguish:
- Diff & merge (in-memory, before any writes)
- Writing to source (Palm device I/O)
- Writing to target (network I/O, CalDAV PUT, etc.)

The `palmIsSource` and `palmIsTarget` parameters are available to the function, but without a signal that identifies *which backend is currently being written*, the decision cannot be refined.

## Request to libkalburator

A finer, per-backend apply-phase signal is requested. Two approaches are proposed:

### Option A: Sub-phases Within Processing

Add two new `SyncPhase` enum values:
- `WritingSource` — apply phase, writing to source backend
- `WritingTarget` — apply phase, writing to target backend

Progression would be:
```
Idle → FetchingSource → FetchingTarget → Processing 
  → WritingSource → WritingTarget → Complete
```

or merged into `Processing`:
```
Idle → FetchingSource → FetchingTarget → Processing (diff & merge)
  → Processing + WritingSource sub-signal
  → Processing + WritingTarget sub-signal
  → Complete
```

**Advantage:** Minimal API change; `SyncPhase` enum extends by two values.  
**Disadvantage:** Sequence implies source is written before target; not always true if overrides are applied.

### Option B: Per-Backend Backend-Write Signal

Add a new signal:
```cpp
void backendApplyStarted(const QString &backendId);
void backendApplyFinished(const QString &backendId);
```

or a single state-change signal:
```cpp
void applyingToBackend(const QString &backendId, bool starting);
```

Fired from the apply work queue in `libkalburator/src/engine/syncengine.cpp` around lines 2617 and 2639, where `applyBatch()` is invoked for target and source backends:

```cpp
// Line ~2617: applyBatch(tgtWriter.get(), tgtBackend, tgtBlob, tgtColId, toWrite,
//                        /*backendRegistryId=*/"<target-backend-id>");

// Line ~2639: applyBatch(srcWriter.get(), srcBackend, srcBlob, srcColId, toWrite,
//                        /*backendRegistryId=*/"<source-backend-id>");
```

The `backendRegistryId` string is already threaded through the apply path and uniquely identifies the backend being written.

**Advantage:** Precise, explicit, backend-agnostic.  
**Disadvantage:** New signal type (more API surface); callers must track backend identity separately.

## Recommended Approach

**Option B** is recommended for precision and symmetry with existing per-mapping signals like `syncStarted(mappingId)`. The signal carries:
- `backendId` — identifier of the backend currently being written (e.g., "calendar", "contacts", "caldav-remote")
- `applying` (bool) — true when apply begins, false when finished (or two signals, one for each transition)

## Integration in WildPalms

Once the signal lands in libkalburator, WildPalms will:

1. Connect to the new signal in `PalmRuntime::PalmRuntime()` (near line 181 where `phaseChanged` is connected):
   ```cpp
   QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::backendApplyStarted,
                    this, [this](const QString &backendId) {
       // Track which backend is being written
       m_currentApplyingBackend = backendId;
       // If this is a remote backend (not Palm), resume tickle
       if (!isPalmBackend(backendId) && m_device)
           m_device->resumeTickle();
   });

   QObject::connect(m_engine.get(), &Kalburator::Sync::SyncEngine::backendApplyFinished,
                    this, [this](const QString &backendId) {
       // Clear tracking
       if (m_currentApplyingBackend == backendId)
           m_currentApplyingBackend.clear();
   });
   ```

2. Refine `shouldPauseTickle()` in `palmticklephase.h` to accept the current backend ID:
   ```cpp
   inline bool shouldPauseTickle(Kalburator::Engine::SyncEngine::SyncPhase phase,
                                  bool palmIsSource, bool palmIsTarget,
                                  const QString &currentApplyingBackend = {})
   {
       using Phase = Kalburator::Engine::SyncEngine::SyncPhase;
       switch (phase) {
       case Phase::FetchingSource: return palmIsSource;
       case Phase::FetchingTarget: return palmIsTarget;
       case Phase::Processing:
           // During apply, only pause if writing to Palm
           return isPalmBackendId(currentApplyingBackend, palmIsSource, palmIsTarget)
               || currentApplyingBackend.isEmpty(); // Before apply begins, use conservative default
       case Phase::Idle:
       case Phase::Complete:       return false;
       }
       return false;
   }
   ```

3. Update the lambda in `PalmRuntime::PalmRuntime()` (line ~187) to pass the current backend ID:
   ```cpp
   if (shouldPauseTickle(phase, m_currentPalmIsSource, m_currentPalmIsTarget, m_currentApplyingBackend))
       m_device->pauseTickle();
   else
       m_device->resumeTickle();
   ```

## Expected Behavior Change

**Before:** Tickle paused for the entire `Processing` phase (Diff + Merge + Write Source + Write Target).  
**After:** Tickle paused only during:
- `FetchingSource` (if Palm is source)
- `FetchingTarget` (if Palm is target)
- `Processing` while writing to Palm backend
- `Processing` while doing diff/merge (before any writes)

Tickle resumed during write to remote backends (CalDAV, WebDAV, etc.), allowing multi-minute PUT operations without connection timeout.

## File References

### WildPalms
- `src/runtime/palmticklephase.h` — Current `shouldPauseTickle()` logic
- `src/runtime/palmruntime.cpp` — Signal connections and tick-pause calls (lines 181–192)
- `src/runtime/palmruntime.h` — May need new member `m_currentApplyingBackend`

### libkalburator
- `src/engine/syncengine.h` — `SyncPhase` enum and signal definitions
- `src/engine/syncengine.cpp` — Apply work queue and `applyBatch()` calls (lines ~2500–2640)
- `src/engine/syncengine.cpp` — `SyncEngineWorker` signal emissions (if Option A sub-phases are chosen)

## Next Steps

1. **libkalburator decision:** Choose Option A or B; author implements signal in libkalburator and bumps version.
2. **Pin in WildPalms:** Update to libkalburator with the new signal; integrate per Integration in WildPalms above.
3. **Test:** Verify tickle stays alive during multi-minute remote writes (CalDAV PUT); verify it still pauses during Palm device writes.
4. **Measure:** Compare sync duration and device timeout rates before/after.
