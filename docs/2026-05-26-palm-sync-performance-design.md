# Palm Sync Performance & Correctness — Design Spec

**Date:** 2026-05-26
**Branch target:** `main` (cut a `feature/palm-sync-perf` branch to implement)
**Status:** Design — awaiting implementation plan
**Audience:** a fresh agent with no prior context on this investigation.

## Context

Real-device HotSync against a Palm m515 (serial `L0JG14I11398`, 2 ports `/dev/ttyUSB0`+`ttyUSB1`)
syncing a Palm Datebook against a Nextcloud CalDAV calendar works but is **catastrophically
slow (~4–5 s per calendar event)**, and the Palm **drops the link mid-sync**. Four distinct
problems were root-caused from the device log. All four are independently shippable.

### Threading model (read this first — it de-risks #1 and #2)
All Palm DLP I/O is serialized on a **single dedicated "link thread"**
(`PalmDeviceAccess::m_linkThread`, `src/runtime/palmdeviceaccess.h:121`). `KPilotDeviceLink`,
`PilotLinkPalmDatabaseAccess`, and `PalmTickle` all live on that thread
(`palmdeviceaccess.cpp:170`). Palm backends live on the **main thread**; the libkalburator
`SyncEngine` worker runs on its own thread. Every `IPalmDatabaseAccess` call is marshalled to
the link thread via `Qt::BlockingQueuedConnection` (`palmdeviceaccess.cpp:313-319`), and the
pilot-link socket is **not** thread-safe — it is touched only from the link thread. Therefore
any state added to `PilotLinkPalmDatabaseAccess` or `PalmTickle` is single-threaded and
race-free as long as it stays on the link thread.

---

## Problem 1 — Per-record open/close of the Palm database (the 4–5 s/event bug)

### Root cause (confirmed)
`PilotLinkPalmDatabaseAccess::{createRecord,updateRecord,deleteRecord}`
(`src/palm/device/pilotlinkpalmdatabaseaccess.cpp:87,100,113`) each construct a `DbScope`
(`:11-24`) that calls `KPilotDeviceLink::openDatabase()` → `dlp_OpenDB` on construction and
`closeDatabase()` → `dlp_CloseDB` on destruction. So writing N records does N×(OpenDB +
WriteRecord + CloseDB). For 85 events that is ~255 serial DLP round-trips instead of ~87. The
class header even says so: *"Scaffold pattern; a production implementation should cache open
handles to avoid per-op overhead on real hardware."* (`pilotlinkpalmdatabaseaccess.h:14-16`).

The same per-op pattern exists in the contacts, memo, and todo Palm backends, which all call
`m_device->{create,update,delete}Record` per record (e.g. `palmmemobackend.cpp:146,159,167`).

**Why the obvious hook doesn't work:** `IBlobBackend` declares
`beginBatch/commitBatch/rollbackBatch` (`libkalburator/src/blob/iblobbackend.h:76-78`), but the
libkalburator engine **never calls them** (grep of the engine returns only definitions/no-op
overrides, never a call site). The engine's write loops (`libkalburator/src/engine/
syncengine.cpp:1722-1736` and the canon apply path) call `createRecord/updateRecord/deleteRecord`
directly with no surrounding batch bracket. Adding the calls would be a libkalburator change;
we fix this WP-side instead.

### Design (chosen): device-layer handle caching
Add a cached write handle to `PilotLinkPalmDatabaseAccess`, entirely on the link thread:

- New private state: `mutable QString m_openDbName;` `mutable int m_openHandle = -1;`
  (a single cached handle — the engine writes all of one mapping's records to one DB
  consecutively, so a single-entry cache suffices; a `QHash` is unnecessary).
- A private helper `int ensureWriteHandle(const QString &dbName)`: if `m_openDbName == dbName`
  and `m_openHandle >= 0`, return it; otherwise `flushWriteHandle()` then
  `openDatabase(dbName, /*rw=*/true)`, cache, return.
- A private helper `void flushWriteHandle()`: if `m_openHandle >= 0`, `closeDatabase(m_openHandle)`,
  reset to `-1`/empty.
- `createRecord/updateRecord/deleteRecord` use `ensureWriteHandle(dbName)` instead of `DbScope`.
- **Flush triggers** (close the cached handle so reads see committed data and handles don't leak):
  - At the start of every **read** method (`readAllRecords`, `readRecord`, `readAppBlock`,
    `recordsModifiedSince`, `availableDatabases`, `hasDatabase`) — call `flushWriteHandle()` first.
  - When a write targets a **different** DB than the cached one (handled by `ensureWriteHandle`).
  - On teardown: `flushWriteHandle()` in the destructor, and expose a public
    `void closeWriteHandle()` that `KPilotDeviceLink::endSync()`/`closeConnection()` (or
    `PalmDeviceAccess` on disconnect) calls before the socket closes, so the final mapping's
    DB is properly `dlp_CloseDB`'d before `dlp_EndOfSync`.

This collapses N open/closes into one per contiguous run of same-DB writes. No engine or
libkalburator change. Apply the identical pattern is unnecessary in the backends — the fix is
purely in `PilotLinkPalmDatabaseAccess` since every backend funnels through it.

### Testing
Unit-test against a **mock/fake KPilotLink** (or a fake `IPalmDatabaseAccess` consumer is not
applicable — we are testing the impl). Add a fake `KPilotLink` recording `openDatabase`/
`closeDatabase`/`writeRecord` calls. Assert: 50 consecutive `createRecord` to "DatebookDB"
produce **1** `openDatabase` + **50** `writeRecord` + (after a read or `closeWriteHandle()`)
**1** `closeDatabase`. Assert a read between writes flushes (forces a close+reopen). Assert a
write to a different DB closes the first and opens the second.

---

## Problem 2 — Palm link drops during the network phase (mid-sync disconnect)

### Root cause (confirmed)
The tickle keep-alive (`PalmTickle`, `dlp_GetSysDateTime` every 5 s, `palmtickle.cpp:61-79`) is
**paused for the entire engine run** — `pauseTickle()` before `runSyncFuture`
(`palmruntime.cpp:683`) and `resumeTickle()` after (`:750`). The blanket pause exists because a
long Palm DLP read (500+ records) overruns the 5 s tickle and the comment claims a mid-stream
tickle "corrupts the DLP session" (`palmruntime.cpp:680-682`, `:866-869`). But a Palm↔CalDAV
sync spends a long stretch in the **network phase** (HTTP push of incidences) where the Palm
socket is idle; with the tickle paused, the Palm's on-device HotSync times out and the cradle
disconnects. There is no in-sync inactivity timeout on our side (`pi_accept_to` is connect-only,
`kpilotdevicelink.cpp:266`); the timeout is the Palm's own.

### Design (chosen): phase-scoped tickle suspension
Suspend the tickle **only while Palm DLP work is actually happening**, and let it run during
purely-network phases. The engine already emits `syncStarted(QString mappingId)`,
`phaseChanged(SyncPhase)` where `SyncPhase ∈ {Idle, FetchingSource, FetchingTarget, Processing,
Complete}` (`libkalburator/src/engine/syncengine.h:366-372`), and PalmRuntime knows each
mapping's source/target backend ids.

WP-side implementation (in `PalmRuntime`):
- Replace the single `pauseTickle()`/`resumeTickle()` around the whole run with phase-driven
  suspension. Subscribe to the engine's `syncStarted` + `phaseChanged`. For the current mapping,
  determine which side is the Palm device backend (the backend whose id corresponds to the
  Palm plugin, e.g. source `palm:*`). Pause the tickle when entering a phase that drives Palm
  DLP (FetchingSource if Palm is source; FetchingTarget if Palm is target; the apply phase to
  the extent it writes the Palm side), and resume during phases that are purely remote/network.
- Always flush-and-resume on `runFinished` (existing teardown).

**libkalburator boundary / handoff (REQUIRED for full correctness):** the `Processing` phase
covers diff + apply for *both* sides, so `phaseChanged` alone cannot tell PalmRuntime when, within
apply, the Palm side is being written vs. the remote side. To scope the tickle precisely during
apply, the engine must emit a finer signal — e.g. a per-backend "DLP/apply begin/end" or a phase
that distinguishes writing-source vs writing-target. Per project workflow (WildPalms is the
consumer; do not edit libkalburator from WP — see `[[feedback_libkalburator_handoff_workflow]]`),
**write a handoff doc** at `docs/2026-05-26-tickle-phase-signal-handoff-libkalburator.md`
requesting that signal, and pin to the libkalburator commit that lands it. Until then, the WP
side scopes tickle at phase granularity (fetch phases) and conservatively keeps it suspended
through `Processing` — which still frees the tickle during the source/target **fetch** network
phases, the largest idle windows.

> Note on the original "corruption" concern: because tickle and DLP are both on the link thread,
> a tickle can only fire when the link thread's event loop is idle (between marshalled DLP ops),
> not truly mid-operation. The implementing agent MUST confirm whether the historical corruption
> was a same-thread issue (in which case phase-scoping is safe as-is) or predated the link-thread
> consolidation, and record the finding.

### Testing
Hard to unit-test against hardware. Add a test that drives a `PalmRuntime` (or the tickle-control
unit extracted from it) with a synthetic phase sequence and asserts the tickle is suspended
during Palm-side fetch phases and resumed during remote-side phases. Device verification:
Palm↔CalDAV sync with a large remote calendar completes without the cradle dropping.

---

## Problem 3 — Double CalDAV discovery + `connect(... nullptr)` warnings (follow-up)

### Root cause (confirmed for the nullptr warning)
`SettingsDialog::buildAccountsAndMappingsPagesIfReady()` (`src/settingsdialog.cpp:569-591`) is
called from both `setAccountController()` and `setPalmRuntime()`. It creates `AccountsPage` as
soon as `m_accountController` is set, while `m_palmRuntime` may still be null (the call order in
`KF6MainWindow::onSettings()`/`onConfigureMappings()` sets the controller first). `AccountsPage::
buildUi()` then runs `QObject::connect(m_palmRuntime, &PalmRuntime::runStarted, …)` with a null
sender (`src/app/accounts/accountspage.cpp:69-72`) → the two
`connect(PalmRuntime, AccountsPage): invalid nullptr parameter` warnings, and the page never
receives run signals.

### Root cause (double discovery — lead, confirm during implementation)
`AccountController` runs `connectAll()` in its constructor via `loadAndConnect()`
(`src/runtime/accountcontroller.cpp:67-89`), and also in `addProvider()` (`:150-158`).
CalDAV capability discovery + `RemoteCalendarBackend::registerCalendarUrl` fire on provider
connect, so two `connectAll()` invocations ⇒ the discovery/registration logged twice. **First
implementation task: add a one-line log at each `connectAll()` call site, reproduce, and confirm
which two fire** before deduping.

### Design
- **nullptr fix:** change the guard in `buildAccountsAndMappingsPagesIfReady()` to require BOTH
  `m_accountController && m_palmRuntime` before constructing `AccountsPage`; add a defensive
  null-check around the `connect`s in `AccountsPage::buildUi()`.
- **double-discovery fix:** after confirming the second trigger, make discovery idempotent —
  guard `connectAll()` so an already-connected/connecting provider is not reconnected, or remove
  the redundant trigger. Prefer making `ProviderManager::connectAll()` skip providers already in
  `Connecting`/`Connected` state.

### Testing
A `SettingsDialog` unit/smoke test: set controller then runtime (and the reverse order) and
assert exactly one `AccountsPage` is created with a non-null runtime and no nullptr-connect
warning. A discovery-idempotence test: two `connectAll()` calls ⇒ one discovery per account.

---

## Problem 4 — Sync runs twice (rawfiles defaults, then DAV) (follow-up)

### Root cause (confirmed)
`finishConnect()` emits `readyForSync` (`palmruntime.cpp` after mapping setup), which
`KF6MainWindow::onReadyForSync()` turns into an immediate `onHotSync()` when
`autoSyncOnConnect()` is true. On the **first** connect the profile's mapping set resolves to
`rawfiles` defaults (mappings `default-calendar-palm_calendar_0 → rawfiles/…`), so the first
sync runs Palm↔rawfiles (0 records, wasted). Later, after CalDAV discovery and a reconnect, the
run uses the real DAV mappings (UUID ids → CalDAV). So the user sees two full runs.

### Design (chosen): investigate persistence, then gate
The open question is **whether the user's DAV mappings are persisted to the profile's
`syncMappingsJson()` at all, or only discovered at runtime** — `loadMappingsFromProfile()` only
loads what is persisted (`palmruntime.cpp:485-498`). **First task: determine, with the actual
`profile1` on disk and a log, whether DAV mappings are in `syncMappingsJson()` before the first
connect.**
- If they ARE persisted: the first connect should load them; trace why the first
  `finishConnect()` still produced rawfiles defaults (ordering: is `setProfile`/
  `loadMappingsFromProfile` running before the mappings are written? is auto-sync firing before
  `loadMappingsFromProfile`?) and fix the ordering so one run uses the full set.
- If they are NOT persisted (only discovered at runtime via providers): gate
  `autoSyncOnConnect` so the auto-sync waits for the account providers to finish discovery and
  the mapping set to be assembled (e.g. defer `onReadyForSync`→`onHotSync` until an
  account-ready / mappings-loaded signal), so exactly one sync runs against Palm + DAV.

Pick the fix based on the persistence finding; the spec deliberately defers the mechanism to
that result rather than guessing.

### Testing
Once root-caused: a test asserting that with auto-sync-on-connect and a profile carrying DAV
mappings, a single connect produces exactly one engine run covering all mappings (no rawfiles-
only run). Device verification: one connect = one sync.

---

## Sequencing & scope
Four independent fixes; recommended order in the plan:
1. **Problem 1** (perf) — highest impact, self-contained, fully testable.
2. **Problem 3** (nullptr + double discovery) — small, removes log noise that obscures the others.
3. **Problem 4** (double run) — investigation-led; depends on profile inspection.
4. **Problem 2** (tickle) — WP-side phase-scoping now; libkalburator handoff for precise apply-
   phase scoping. Sequenced last because it benefits from #1 (shorter Palm phases) and may need
   the engine signal.

Each problem is its own task group; the branch can ship them incrementally.

## Out of scope
- Editing libkalburator directly (per workflow). Problem 2's precise apply-phase signal is a
  handoff doc + later pin bump, not a WP edit.
- The transcoding warnings (`status, recurrence, timeTransparency` etc.) are expected lossy-
  encoding notices for Palm Datebook, not a bug.
