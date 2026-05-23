# F.1d — Device-binding lifecycle integrity — design

**Date:** 2026-05-23
**Status:** Design approved through brainstorming. Spec ready for plan.
**Phase:** F.1d (fourth Phase F.1 sub-project; follows F.1a/F.1b/F.1c).
**Predecessors:** F.1a ✅ (`ProfileRegistry`), F.1b ✅ (Forget Profile menu), F.1c ✅ (NewProfileWizard).

---

## 1. Why this exists

A user-reported scenario, traced 2026-05-23:

1. Open WildPalms; F.1a auto-load picks up a previous profile.
2. `File → Profile → Close Profile`.
3. `File → Profile → Forget Profile → <that profile>` with **delete files** checked.
4. Profile dir removed from disk; `ProfileRegistry::unregister` called; `m_currentProfile` null.
5. User presses HotSync on Palm.
6. A "Wrong Device" dialog appears:
   > Device Mismatch!
   > Expected: S/N: L0JG14I11398
   > Connected: PalmCard (Clinton)
   > This profile is configured for a different Palm device.

The dialog is nonsense in this context — there is no "this profile",
the same physical device produced both labels, and the two strings
compare different fields ("S/N" vs user name).

### Root-cause trace

A serial→profile binding lives in **two** places:

| Registry | Owner | Populated by | Cleared by |
|---|---|---|---|
| `ProfileRegistry` (F.1a) — `wildpalmsrc` | `WildPalms::Runtime::ProfileRegistry` | `registerNew` / `registerExisting` | `unregister` (Forget) |
| `KF6Settings DeviceSerials` group (Phase L Task 0.B, 2026-05-15) | `KF6Settings` singleton | `registerDeviceBySerial` | `unregisterDeviceBySerial` (never called) |

`onForgetProfile` (`kf6mainwindow.cpp:1697`) calls
`ProfileRegistry::unregister` and `QDir::removeRecursively`. It never
calls `KF6Settings::unregisterDeviceBySerial`; `grep -rn` finds zero
callers of that function in `src/`. So:

- After Forget+delete, `KF6Settings` still maps `L0JG14I11398 → /path/to/now-gone/profile`.
- Next HotSync: `AutoSyncOrchestrator::handleUsbDevice` looks up the serial, sees the path, checks `QDir(path).exists()` → false, emits `unregisteredDeviceDetected`.
- `onUnregisteredDeviceDetected` prompts "Create profile for this device?" — user clicks Yes.
- `createProfileForDevice` builds a fresh `Profile` whose `DeviceFingerprint` has *only* `usbSerialNumber` set (userName="", userId=0). Saves + registers serial → new path.
- `onAutoDeviceDetected` loads the new profile; `m_currentProfile` populated.
- Connection handshake completes; `onConnectionComplete` (line 925) builds `connectedDevice` from the link's handshake info. **It does not populate `connectedDevice.usbSerialNumber`** even though the serial is known to the orchestrator upstream.
- `handleDeviceFingerprint` compares expected (serial-only) against connected (userName+userId-only). `DeviceFingerprint::matches` requires both sides to share at least one populated field; they don't. Mismatch.
- The dialog renders both sides through `DeviceFingerprint::displayString`, which encodes wildly different shapes depending on which fields are filled. Result: "S/N: L0JG14I11398" vs "PalmCard (Clinton)".

The whole chain is internally consistent and entirely broken. Three
overlapping defects:

1. **Two registries, no integration.** `ProfileRegistry` and the
   `KF6Settings DeviceSerials` group track overlapping state with no
   coordination. Forget cleans one, not both.
2. **`connectedDevice` drops the USB serial.** `onConnectionComplete`
   builds a partial fingerprint that omits the serial the orchestrator
   already detected.
3. **`createProfileForDevice` seeds a partial fingerprint** (serial
   only) into the new profile. Combined with #2, the profile and its
   own device produce a guaranteed mismatch on the very next handshake.

F.1d fixes all three as one coherent sub-project: unify the registries
(C), populate fingerprints completely (A), and render the dialog from
correct data (B). The user's reported scenario stops producing a
mismatch dialog at all; if a mismatch *does* fire (genuine wrong-device
case), the dialog shows comparable like-for-like fields.

## 2. Scope

### 2.1 In scope

- **Add `usbSerial` to `ProfileEntry`** and round-trip it through
  `ProfileRegistry::{load,save}`. (Per brainstorming Q3: usbSerial only;
  full fingerprint stays in `profile.conf [device]`.)
- **`ProfileRegistry::findBySerial(serial)`** and
  **`ProfileRegistry::bindSerial(id, serial)`** API surface.
- **Forget atomically clears the binding.** Free for the implementor:
  `ProfileEntry` is removed; the serial binding goes with it because
  it's *on* the entry now.
- **`connectedDevice.usbSerialNumber` always populated** from the
  device link at `onConnectionComplete` (was unconditionally empty).
- **`createProfileForDevice` defers fingerprint capture** until the
  first complete handshake (or seeds only the field it knows: the USB
  serial, but inside `ProfileRegistry::ProfileEntry`, not into
  `profile.conf [device]`).
- **Drop `KF6Settings` device-serial API entirely.** No migration
  (per brainstorming Q1: clean break). Users with prior bindings
  re-pair on first connect via the existing "unrecognised device,
  create profile?" prompt.
- **`DeviceFingerprint::matches` distinguishes insufficient-evidence
  from contradiction.** Returns one of `Match / MismatchKnown /
  Indeterminate`.
- **Mismatch dialog data fix.** Render a fixed-shape comparison
  (serial / name / model rows, each side, `—` when missing). Same
  three buttons; same QMessageBox shell. (Per brainstorming Q2:
  data fix only, no full UX redesign.)

### 2.2 Out of scope

- Full UX redesign of the mismatch dialog (custom widget, etc.).
- KWallet hardening of credentials (deferred from F.1c).
- Per-Palm-category routing UX (F.3).
- Radicale E2E + user docs (F.4).
- Multi-device-per-profile (deprecated per Phase L Task 0.C; not
  resurrected here).
- Storing additional fingerprint fields on `ProfileEntry` (full
  fingerprint stays in `profile.conf [device]`).
- Adding a new "unbind device" UI affordance. Forget Profile is the
  affordance; cleaning the binding is automatic.

## 3. Confirmed design choices

Established during 2026-05-23 brainstorming (record below for
future re-appraisal):

1. **Single source of truth for serial binding: `ProfileEntry.usbSerial`**
   in `wildpalmsrc`. The `KF6Settings DeviceSerials` group goes away
   entirely.
2. **Clean break — no migration helper.** Existing users will re-pair
   on next connect via the existing prompt. The cost (one prompt per
   device) is bounded; the benefit (zero migration code, no two-source
   period) is significant.
3. **`ProfileEntry.usbSerial` only.** Full fingerprint stays in
   `profile.conf [device]`. The registry holds a *lookup key*, not a
   cache of profile contents.
4. **Mismatch dialog: data fix only.** Render a side-by-side fixed
   shape; keep QMessageBox + three buttons. Full UX redesign deferred.
5. **`DeviceFingerprint::matches` returns `enum MatchResult`** —
   `Match`, `MismatchKnown`, `Indeterminate`. Callers decide policy
   (today's call sites treat `Indeterminate` as Match for the
   not-yet-paired case; the mismatch dialog only fires on
   `MismatchKnown`).
6. **`onConnectionComplete` ALWAYS populates `connectedDevice.usbSerialNumber`**
   from the device link (and falls back to the orchestrator's known
   serial if the link doesn't expose one). The contract becomes "the
   connection layer is responsible for assembling the most complete
   fingerprint it can."

## 4. Architecture

### 4.1 Where state lives

```
~/.config/wildpalms/wildpalmsrc
├── [General]
│   └── lastActiveProfileId = profile1
└── [profile-profile1]
    ├── name = "Palm m505"
    ├── path = /home/clinton/.wildpalms/profile1
    ├── lastOpened = 2026-05-23T14:30:00Z
    └── usbSerial = L0JG14I11398        ← NEW (F.1d)

~/.wildpalms/profile1/profile.conf
├── [profile]
│   ├── id = profile1
│   └── name = "Palm m505"
└── [device]                            ← Unchanged: full fingerprint
    ├── userId = 12345
    ├── userName = "clinton"
    ├── usbSerialNumber = L0JG14I11398
    ├── modelName = "Palm m505"
    ├── manufacturer = "Palm, Inc."
    └── ...
```

`ProfileEntry.usbSerial` is the *lookup key*. `profile.conf [device]`
remains the *full fingerprint store*. They are kept in sync by
`ProfileRegistry::bindSerial`, which is called from the place that
also writes `profile.conf` (i.e., post-handshake in
`registerDeviceWithCurrentProfile`).

### 4.2 New `ProfileRegistry` API surface

```cpp
struct ProfileEntry {
    QString   id;
    QString   name;
    QString   path;
    QDateTime lastOpened;
    QString   usbSerial;       // NEW
    bool isValid() const { return !id.isEmpty(); }
};

class ProfileRegistry : public QObject {
public:
    // Existing ...
    QList<ProfileEntry> entries() const;
    ProfileEntry        entry(const QString &id) const;
    QString             lastActiveId() const;
    ProfileEntry        registerNew(const QString &name, ...);
    ProfileEntry        registerExisting(const QString &path);
    bool                unregister(const QString &id);
    void                setLastActive(const QString &id);
    bool                rename(const QString &id, const QString &newName);

    // NEW (F.1d):
    /// Find the entry with the given USB serial, or invalid entry if
    /// none. Returns the first match (entries should never share serials
    /// — bindSerial enforces uniqueness).
    ProfileEntry findBySerial(const QString &usbSerial) const;

    /// Set or clear the usbSerial binding on entry `id`. Passing an
    /// empty serial clears the binding. If another entry already has
    /// that serial, the binding is *moved* (the other entry's
    /// usbSerial is cleared) — one serial may not bind to two entries.
    /// Emits entryUpdated for any entry whose serial changed.
    /// Returns true if the registry was modified.
    bool bindSerial(const QString &id, const QString &usbSerial);
};
```

`unregister(id)` already removes the entire `[profile-<id>]` group;
the new `usbSerial` field disappears with it. **No extra Forget code
needed.**

### 4.3 Removed `KF6Settings` API

Deleted in F.1d:

- `KF6Settings::registerDeviceBySerial(serial, path)`
- `KF6Settings::findProfileBySerial(serial)`
- `KF6Settings::unregisterDeviceBySerial(serial)` (was dead code)
- `KF6Settings::deviceSerialsGroup()` (Settings dialog's "Registered Devices" page)
- The `[DeviceSerials]` config group writes; reads continue working
  on an empty group (existing users see "no device registered" until
  re-pair).

Three call sites switch from `KF6Settings::findProfileBySerial` to
`ProfileRegistry::findBySerial`:

1. `AutoSyncOrchestrator::handleUsbDevice` (line 71)
2. `KF6MainWindow::onConnectionComplete`, no-profile branch (line 955)
3. `KF6MainWindow::handleDeviceFingerprint`, Switch Profile button (line 1122)

Two call sites switch from `KF6Settings::registerDeviceBySerial` to
`ProfileRegistry::bindSerial`:

1. `AutoSyncOrchestrator::createProfileForDevice` (line 186)
2. `KF6MainWindow::registerDeviceWithCurrentProfile` (line 1151)

`AccountsPage` and `SettingsDialog`'s "Registered Devices" surface (if
any) are checked and removed/redirected as part of the work.

### 4.4 `DeviceFingerprint::matches` becomes tri-state

```cpp
enum class MatchResult {
    Match,            // shared field that is equal
    MismatchKnown,    // shared field that contradicts
    Indeterminate,    // no shared field populated; can't say
};

MatchResult DeviceFingerprint::compare(const DeviceFingerprint &other) const;
```

Priority order matches today's `matches()`: serial > userId > userName.

```cpp
MatchResult DeviceFingerprint::compare(const DeviceFingerprint &other) const
{
    // 1. USB serial (highest priority when both populated)
    if (!usbSerialNumber.isEmpty() && !other.usbSerialNumber.isEmpty()) {
        return usbSerialNumber == other.usbSerialNumber
            ? MatchResult::Match : MatchResult::MismatchKnown;
    }
    // 2. userId
    if (userId != 0 && other.userId != 0) {
        return userId == other.userId
            ? MatchResult::Match : MatchResult::MismatchKnown;
    }
    // 3. userName
    if (!userName.isEmpty() && !other.userName.isEmpty()) {
        return userName == other.userName
            ? MatchResult::Match : MatchResult::MismatchKnown;
    }
    // 4. No shared field populated.
    return MatchResult::Indeterminate;
}
```

`matches()` is **kept** as a wrapper returning `compare() == Match`
for legacy call sites that don't care about the indeterminate case.
New code (the mismatch dialog) uses `compare()`.

### 4.5 `onConnectionComplete` fingerprint construction

Today (`kf6mainwindow.cpp:928-941`):

```cpp
DeviceFingerprint connectedDevice;
connectedDevice.userId   = userId;
connectedDevice.userName = userName;
// ROM, storage info...
// usbSerialNumber NEVER populated
```

After F.1d:

```cpp
DeviceFingerprint connectedDevice;
connectedDevice.userId   = userId;
connectedDevice.userName = userName;
connectedDevice.usbSerialNumber = deviceLink->handshakeUsbSerial();
if (connectedDevice.usbSerialNumber.isEmpty() && m_autoSync) {
    // Fallback: orchestrator detected the serial during USB scan.
    connectedDevice.usbSerialNumber = m_autoSync->currentUsbSerial();
}
// ROM, storage info...
```

`KPilotDeviceLink::handshakeUsbSerial()` is added if missing; it
returns the serial extracted during the connection handshake or empty
if the link can't get one (some pilot-link backends).

### 4.6 `createProfileForDevice` fingerprint discipline

Today, line 138-141:

```cpp
DeviceFingerprint fingerprint;
fingerprint.userId          = userId;          // 0 from onUnregisteredDeviceDetected
fingerprint.userName        = userName;        // "" from onUnregisteredDeviceDetected
fingerprint.usbSerialNumber = usbSerial;
// ...
profile->setDeviceFingerprint(fingerprint);    // saves partial fingerprint
```

After F.1d:

```cpp
// Do NOT seed the profile's full fingerprint with partial info. The
// registry binding (serial) is enough to look up the profile next
// connect; the full fingerprint will be saved by
// registerDeviceWithCurrentProfile after the first complete handshake.
auto *profile = new Profile(finalPath);
profile->setName(safeName);
// NO setDeviceFingerprint call here.

if (!profile->initialize()) { ... }
profile->save();   // [device] is empty/default

// Bind serial via the new ProfileRegistry API (replaces KF6Settings).
if (!usbSerial.isEmpty()) {
    registry->bindSerial(profile->id(), usbSerial);
}
```

`registerDeviceWithCurrentProfile` continues to save the complete
fingerprint into `profile.conf [device]` after the handshake.

### 4.7 Mismatch dialog data fix

`handleDeviceFingerprint` (line 1094-1101) replaces the
`displayString`-based message with a structured table.

```cpp
struct ComparisonRow { QString label; QString lhs; QString rhs; };

QList<ComparisonRow> rows = DeviceFingerprint::comparisonRows(
    expectedDevice, connectedDevice);
// Returns 3-4 rows: { "Serial", expected.usbSerial.or("—"), connected.usbSerial.or("—") },
//                  { "User",   expected.userName.or("—"),   connected.userName.or("—") },
//                  { "Model",  expected.modelName.or("—"),  connected.modelName.or("—") }

QString message;
message += i18n("This profile is associated with a different Palm device.\n\n");
message += QStringLiteral("<table>\n");
message += QStringLiteral("<tr><th></th><th>%1</th><th>%2</th></tr>\n")
    .arg(i18n("Expected"), i18n("Connected"));
for (const auto &r : rows) {
    message += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td><td>%3</td></tr>\n")
        .arg(r.label.toHtmlEscaped(),
             r.lhs.toHtmlEscaped(),
             r.rhs.toHtmlEscaped());
}
message += QStringLiteral("</table>");

QMessageBox msgBox(this);
msgBox.setTextFormat(Qt::RichText);
msgBox.setText(message);
// ... same three buttons ...
```

Buttons unchanged: Continue Anyway / Switch Profile / Disconnect.

### 4.8 `handleDeviceFingerprint` no-mismatch-on-indeterminate

```cpp
const auto result = expectedDevice.compare(connectedDevice);
if (result == DeviceFingerprint::MatchResult::Match) {
    // ... existing merge-extended-info path ...
    return true;
}
if (result == DeviceFingerprint::MatchResult::Indeterminate) {
    // Insufficient evidence. Treat as match; once the handshake completes
    // and the fingerprint is populated via registerDeviceWithCurrentProfile,
    // the next connect will have enough fields.
    m_logWidget->logInfo(i18n("Device fingerprint indeterminate — assuming match"));
    registerDeviceWithCurrentProfile(connectedDevice);
    return true;
}
// MismatchKnown: render the dialog as in §4.7.
```

This is the key behavioral change for the reported scenario: after
the F.1d fixes to §4.5 + §4.6, the freshly-created-by-the-wizard
case produces matching serials on both sides → `Match` → no dialog.
If somehow serials still don't both populate (legacy / link can't
extract / etc.), `Indeterminate` fires and the dialog stays
hidden. Only genuine contradictions show the dialog.

## 5. Migration

**None.** Per Q1: clean break.

On first launch after upgrade:
- `KF6Settings` reads the legacy `[DeviceSerials]` group (still
  present in the config file) — but no code calls those getters
  anymore.
- The group remains as dead data; we don't actively delete it (the
  user can clean their config file or KF6Settings will lazily ignore
  it).
- Users with previously-bound devices see the "unrecognised device,
  create profile?" prompt once per device on the next HotSync. They
  click "No" since the profile already exists, then either use
  `File → Profile → Switch` to load the existing profile manually,
  or wait for `findBySerial` to populate as soon as they next
  hand-load the profile and connect (which writes the binding).

That last detail is the only sharp edge of "clean break": until the
user opens an existing profile, the registry doesn't know the serial.
This is a one-time cost. Spec records it as the known migration
seam; future polish (a "Bind serial" button in the profile properties
dialog) is out of scope.

## 6. Error handling

| Failure | Surfacing | Recovery |
|---|---|---|
| `bindSerial` called with empty id | No-op + qWarning | Caller fixes id |
| `bindSerial` called with serial already on another entry | Move the binding (per §4.2 contract); emit `entryUpdated` on both | Implementor: matches the natural "this serial physically is one device" invariant |
| `findBySerial` with empty arg | Return invalid entry | Caller fallback |
| `compare()` with both fingerprints empty | Returns `Indeterminate` | Caller treats as match per §4.8 |
| Mismatch dialog opened with one fingerprint completely empty (corruption) | Each row renders `—` for the empty side | User clicks Disconnect; investigates |
| Legacy `KF6Settings DeviceSerials` group leftover | Ignored; no code reads it | None needed; group can be hand-deleted |

## 7. Testing

### 7.1 New / extended tests

| File | Coverage |
|---|---|
| `tests/runtime/tst_profileregistry_serial_binding.cpp` (NEW) | `bindSerial` writes + round-trips; `findBySerial` returns invalid for empty/unknown; `bindSerial` moves a serial off another entry; `unregister` removes the binding implicitly; persistence across reload. |
| `tests/runtime/tst_devicefingerprint_compare.cpp` (NEW) | Three-way `compare()` results across the serial/userId/userName priority ladder; indeterminate-when-no-overlap; symmetric (a.compare(b) == b.compare(a)). |
| `tests/runtime/tst_palmruntime_devicebinding.cpp` (NEW) | `onConnectionComplete` populates `connectedDevice.usbSerialNumber` from the link (via a stub link); fallback to orchestrator's serial when link returns empty. (Mocking the device link is heavier than other tests in this dir — may end up exercising via `runConnectionCompleteForTest` seam if one isn't already exposed.) |
| `tests/runtime/tst_kf6mainwindow_mismatch_dialog.cpp` (NEW) | The dialog's structured-table message contains "Serial", "User", "Model" rows; `—` placeholder for missing fields; indeterminate doesn't open the dialog (handleDeviceFingerprint returns true silently). |
| `tests/runtime/tst_kf6mainwindow_forget_profile.cpp` (EXTENDED) | New: Forget removes the serial binding from `wildpalmsrc` (assert via `ProfileRegistry::findBySerial` returning invalid afterward). |
| Existing `tst_kf6mainwindow_startup`, `tst_palm_runtime_*` | Stay green. |

### 7.2 Deleted tests

The `test_kf6settings_registry_migration` test (Phase L Task 0.B)
remains — it covers the migration from the older two-registry world
to `DeviceSerials`, which is part of the codebase's startup. Its
data path becomes a no-op after F.1d (`DeviceSerials` is dead), but
keeping the test ensures upgrade safety from very-old configs.

Any test directly asserting `KF6Settings::findProfileBySerial` /
`registerDeviceBySerial` round-trips moves to
`tst_profileregistry_serial_binding`.

### 7.3 What's not tested

- A real `KPilotDeviceLink::handshakeUsbSerial()` against hardware.
  Belongs to manual smoke testing.
- POSE64 integration. Out of scope.

## 8. Success criteria

1. **Reported scenario no longer produces a dialog.** Open profile →
   Close → Forget+delete → HotSync. The "unrecognised device, create
   profile?" prompt fires, user clicks Yes, profile is created, sync
   proceeds. No "Wrong Device" dialog appears anywhere in this path.
2. **Genuine wrong-device case still fires a dialog.** Manually rename
   an existing profile's `[device]` section to a different serial,
   reconnect: dialog shows with structured side-by-side rows; user
   can Continue / Switch / Disconnect.
3. **Forget clears the binding atomically.** After Forget+delete,
   `wildpalmsrc` has no entry for the old serial; the next HotSync's
   `findBySerial(L0JG14I11398)` returns invalid (not stale).
4. **`KF6Settings DeviceSerials` group is no longer written.** A
   fresh install has zero entries there; F.1d-era code never
   touches it.
5. **No regressions.** Existing 92 tests still pass; ~5–10 new tests
   for the new APIs.

## 9. Open implementation points

- **`KPilotDeviceLink::handshakeUsbSerial()`** may already exist
  under a different name. Plan T3 surveys; if absent, adds it.
- **Test seam for `onConnectionComplete`**. The current
  `KF6MainWindow` exposes test seams for `runStartupForTest` /
  `runLoadProfileForTest` / `runConflictDetectedForTest`. The
  mismatch-dialog test needs to drive `handleDeviceFingerprint`
  directly; plan T9 adds a `runHandleDeviceFingerprintForTest(expected,
  connected)` seam if it doesn't exist.
- **AutoSyncOrchestrator construction order**. The orchestrator
  is created in `KF6MainWindow`'s ctor; `ProfileRegistry` is created
  immediately after. Plan checks the order is correct so the
  orchestrator can borrow the registry pointer.
- **`AccountsPage` "Registered Devices" surface**. Check whether
  this exists and how; if it does, plan deletes/redirects it. If it
  doesn't, no action.

## 10. Reversible architectural choices

### 10.1 Single registry vs dual

**Decision:** drop `KF6Settings DeviceSerials` entirely; only
`ProfileEntry.usbSerial` holds the binding.

**Why:** two registries were the bug source. The cost of unification
(one new field + one new API method + change three callsites) is
trivially small compared to the cost of letting them drift.

**When to revisit:** if `ProfileEntry` ever needs to be very small
(e.g., we want to memoize 1000s of entries) and the serial would
bloat lookups, the binding could move to a sibling group. Unlikely
short of multi-thousand-profile scenarios.

### 10.2 `compare()` returns enum vs bool

**Decision:** `compare()` returns `MatchResult` (Match /
MismatchKnown / Indeterminate); legacy `matches()` is preserved as a
wrapper.

**Why:** the bug was caused by treating "no overlap" as "different".
The enum makes the indeterminate case explicit. Mismatch dialog only
fires on `MismatchKnown`.

**When to revisit:** if `Indeterminate` is never used productively
(no caller distinguishes it from `Match`), collapse back to a bool
and remove the wrapper. Unlikely.

### 10.3 No migration helper

**Decision:** clean break. Existing users re-pair on next connect.

**Why:** migration code is dead weight a release after it ships and
risks introducing its own bugs. The user-visible cost is bounded:
one prompt per device.

**When to revisit:** if user feedback indicates the re-pair experience
is confusing for non-technical users, a one-shot migration could be
added in a follow-up patch (read legacy DeviceSerials group, write
into ProfileRegistry, deleteGroup). Code template: Phase L Task 0.B's
`migrateLegacyDeviceRegistry`.

## 11. References

- Reported scenario: 2026-05-23 conversation transcript (this session).
- F.1a spec: `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md`.
- F.1b spec: `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md`.
- F.1c spec: `docs/superpowers/specs/2026-05-23-f1c-profile-wizard-design.md`.
- Phase L Task 0.B commit: `5b24516` (collapse of two device registries).
- USB serial introduction: `91b4060` (2026-02-17).
- Call sites: `kf6mainwindow.cpp:925-1156`, `autosyncorchestrator.cpp:60-195`,
  `profileregistry.{h,cpp}`, `profile.h:42-100` (DeviceFingerprint),
  `kf6settings.cpp:266-285`.
