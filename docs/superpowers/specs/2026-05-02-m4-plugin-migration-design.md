# M4 Design: IBackendPluginV2 migration — memo, contacts, todos, webcal

**Date:** 2026-05-02
**Status:** Approved
**Scope:** WildPalms only. libkalburator unaffected.
**Branch:** `palm-rewrite` (already in use for M2/M3)
**Predecessor:** Plan 2 (M2+M3) — calendar-only MVP, all six sync modes, real-device verified

---

## Context

Plan 2 introduced `IBackendPluginV2` and migrated the calendar plugin as
the reference implementation. Memo, contacts, todos, and webcal were
disabled via `WILDPALMS_CALENDAR_MVP_ONLY=ON` for the duration of M2/M3.
M4 re-enables them by migrating each to the new contract.

Plucker is out of scope (install-only conduit, handled separately).

The three new libkalburator domain plugins (`KalburatorDomainMemo`,
`KalburatorDomainContacts`, `KalburatorDomainTodo`) that landed after G.10
are NOT activated by this migration. All four plugins continue to route
through `BlobBackendAdapter`'s `{blob/blob}` path — no typed-routing work
required for M4. That is a future milestone.

---

## Migration pattern

The calendar plugin (`calendarbackendplugin.{h,cpp}`) is the canonical
template. The same four changes apply to each of the four plugins:

### 1. Header

```cpp
// Before
#include "core/ibackendplugin.h"
class XBackendPlugin : public QObject, public WildPalms::IBackendPlugin {
    Q_INTERFACES(WildPalms::IBackendPlugin)
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                    PalmDeviceConnection *device) override;

// After
#include "core/ibackendplugin_v2.h"
class XBackendPlugin : public QObject, public WildPalms::IBackendPluginV2 {
    Q_INTERFACES(WildPalms::IBackendPluginV2)
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;
```

### 2. Implementation (memo / contacts / todos)

`createBackends` body becomes `createPalmBackend`:
- Drop `host` parameter (unused)
- `PalmDeviceConnection*` → `PalmDeviceAccess*`
- Construct `std::make_unique<WildPalms::PalmSync::PalmBackend>(device)`
  (PalmDeviceAccess IS-A IPalmDatabaseAccess — no cast needed)
- Return blob backend as `std::unique_ptr<IBlobBackend>`

### 3. Implementation (webcal)

`WebcalBlobBackend` takes no device access at all — it fetches from URLs.
`createPalmBackend` ignores `device`, constructs from feeds + fetcher,
returns `unique_ptr<WebcalBlobBackend>`.

### 4. CMake re-enable

Remove the `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` guard from:
- `src/plugins/<name>/CMakeLists.txt` inclusion in `src/plugins/CMakeLists.txt`
- The corresponding test target(s)

---

## What does NOT change

- Blob backends (`MemoBlobBackend`, `ContactsBlobBackend`, `TodoBlobBackend`,
  `WebcalBlobBackend`) — no internal changes required
- `BlobBackendAdapter` in `palmruntime.cpp` — still hard-codes `{blob/blob}`,
  still dispatches via `dispatchBlobSync`
- Domain routing — all four use blob path, same as calendar
- The libkalburator domain plugins (memo/contacts/todo) are not activated;
  `contacts/palm-address` → vcard stage stub remains deferred

---

## Test strategy

Re-enabling the CMake guards re-enables the existing test suites for each
plugin. If any test directly instantiates the old `createBackends` contract,
update the call site (blob backends don't change, so most logic compiles
cleanly).

After all four are re-enabled: confirm test count grows from 49 to N and
all pass. Run a real-device HotSync to verify all four plugins participate.

---

## Commit structure

- One commit per plugin (4 commits), one CMake re-enable commit (5th)
- Final commit: update `CURRENT-STATUS.md` and `FINDINGS.md`
