# Phase L addendum — multi-device cleanup design

**Date:** 2026-05-15
**Status:** design (paired with `2026-05-15-phase-l-akonadi-plan.md`)
**Pair:** companion to the existing Phase L plan; folds three small
multi-device cleanup items into Phase L as a Task 0 pre-task block.

## Motivation

WildPalms's `Profile` ↔ `DeviceFingerprint` model is already 1:1 in
storage (a profile holds exactly one fingerprint; `setDeviceFingerprint`
overwrites). The "multi-device" surface that complicated the UI and
program logic was three latent items, not the six-bullet deletion list
originally proposed during brainstorming:

1. `AutoSyncOrchestrator::findOrCreateProfile` silently creates a new
   profile directory for any unrecognised Palm device (no consent
   dialog), conflating "device detected" with "I want a profile for
   this device."
2. Two parallel device→profile registries
   (`KF6Settings::DeviceRegistry` keyed on full fingerprint, and
   `KF6Settings::DeviceSerials` keyed on USB serial) coexist because
   the serial-keyed one was added later as "more reliable." Both write
   on every device-register; `onPalmDetected` checks both. Redundant.
3. `WildPalms/docs/ROADMAP.md §5.5` advertises a "Multiple Device
   Support" feature (separate backups per device, separate ID mappings
   per device) that was never implemented and that contradicts the
   current 1:1 model.

The retained features under this design — registry-based profile
lookup on device detect, mismatch warning on wrong-Palm-for-profile,
device-info display on the dashboard, and the `KF6Settings::Device*`
registry concept itself (now consolidated to one group) — match the
user's framing: *one profile, one device; suggest a profile for the
detected device, never silently swap*.

Phase L's primary scope (`AkonadiProvider`, `AkonadiContactsBackend`,
plugin-ization, `BackendConfiguration::enabled` plumbing, WildPalms
per-provider + per-mapping enable/disable UX) is **unchanged**. This
addendum is a Task 0 pre-task block that runs once before Task 1 of
the existing Phase L plan.

## Scope — three concrete changes

### A. Confirmation-dialog rework for new-device path

Today, `AutoSyncOrchestrator::findOrCreateProfile(usbSerial, userName,
userId)` does five things in one call when the serial isn't recognised:

1. Resolves a safe profile directory name under `~/PalmSync/<userName>/`,
   with `_1`/`_2`/… suffix collision-avoidance.
2. Constructs and initialises a `Profile` at that path.
3. Saves it.
4. Registers it in `KF6Settings::DeviceRegistry` and
   `KF6Settings::DeviceSerials`.
5. Adds to recent profiles, sets default-profile-path, syncs settings,
   emits `profileCreated`.

No consent dialog appears. The user finds out a profile was created
by seeing `profileCreated(path, userName)` flow through the log
widget, after the fact.

**Change.** Split this into two callables:

- `AutoSyncOrchestrator::onPalmDetected(...)` (existing slot, modified):
  on serial-miss, emit a new signal
  `unregisteredDeviceDetected(QString usbSerial, QString userName,
  quint32 userId)`. Do *not* synchronously create or register
  anything.
- `AutoSyncOrchestrator::createProfileForDevice(QString usbSerial,
  QString userName, quint32 userId)` (new public method): the body
  of today's `findOrCreateProfile`'s steps 1–5, called only from the
  confirmation-dialog accept path.

`KF6MainWindow` gains a slot that connects to
`unregisteredDeviceDetected` and shows a modal `KMessageBox::questionTwoActions`
(or equivalent) with text along the lines of "An unrecognised Palm
device (S/N: <serial>) was detected. Create a new profile for it?"
Modal so the decision is explicit; the connect-event flow is brief
and the user isn't in the middle of other work when a device is
plugged in.
On accept, the slot calls
`m_autoSyncOrchestrator->createProfileForDevice(...)`. On decline,
nothing happens — the device is acknowledged in the log widget and
the user can still create a profile manually via the existing menu
entry.

`findOrCreateProfile(...)` itself stays as a convenience wrapper that
calls `createProfileForDevice(...)` directly (the test fixtures use
it for end-to-end profile bootstrap without going through the UI).

### B. Registry consolidation: drop fingerprint-keyed registry

Delete `KF6Settings::DeviceRegistry` group entirely. Five methods
deleted from `KF6Settings`:

- `registerDevice(const DeviceFingerprint &, const QString &)`
- `unregisterDevice(const DeviceFingerprint &)`
- `findProfileForDevice(const DeviceFingerprint &)`
- `deviceRegistry()`
- `clearDeviceRegistry()`

Plus the two `DeviceFingerprint` utilities that only exist to serve
the deleted group:

- `DeviceFingerprint::registryKey()` (composes the
  `userId-userName-usbSerial-…` key string)
- `DeviceFingerprint::fromRegistryKey(const QString &)` (parses it
  back)

`DeviceSerials` (USB serial → profile path, three methods:
`registerDeviceBySerial`, `findProfileBySerial`,
`unregisterDeviceBySerial`) becomes the sole device→profile lookup.

**Migration.** `KF6Settings` constructor (or a one-shot
`migrateLegacyDeviceRegistry()` called from its constructor) runs
once: read all entries in the soon-to-be-deleted `DeviceRegistry`
group, parse the embedded `usbSerial` from each
fingerprint-composed key, and if the corresponding `DeviceSerials`
entry doesn't already exist, write it. Then `deleteGroup()` the
old `DeviceRegistry` group. Subsequent runs find an empty
`DeviceRegistry` and do nothing. Idempotent.

**Caller updates.** `AutoSyncOrchestrator::onPalmDetected` currently
falls through to a fingerprint-based registry lookup if the serial-
based lookup misses. That fallback is deleted in this task —
serial-only henceforth. `AutoSyncOrchestrator::findOrCreateProfile`
no longer calls `settings.registerDevice(...)`, only
`settings.registerDeviceBySerial(...)`.

**SettingsDialog "Registered Devices" page.** The list at
`WildPalms/src/settingsdialog.cpp:417` currently iterates
`KF6Settings::deviceRegistry()` (the deleted method). Flip its data
source to `KF6Settings::deviceSerialsGroup().entryMap()`. Display
columns become "USB Serial" and "Profile Path" (instead of the
full fingerprint breakdown). The page survives — it's how users
audit "which device is bound to which profile" — but the
fingerprint-fields-as-columns format goes.

### C. `ROADMAP.md §5.5` deletion

Lines 349-353 of `WildPalms/docs/ROADMAP.md` (the four "Multiple
Device Support" task bullets — "Device profiles / Switch between
devices / Separate backups per device / Separate ID mappings per
device") are removed. The section heading goes with them. No
replacement section. The contemporary stance is documented by the
absence: WildPalms is single-device-per-profile; there is no
roadmap entry for multi-device-per-profile.

## Phase L integration

The addendum lands as **Task 0** of the existing Phase L plan,
before Task 1 (`AkonadiProvider` skeleton). It's a single task with
three checklist items A/B/C above; each item gets its own commit on
`refactor/engine-merger` in the WildPalms worktree (no
libkalburator-side code change, no engine change).

Existing Phase L Task 9 ("WildPalms `AccountsPage` per-provider
enable checkbox, `AccountController` fan-out") gains a single
sentence pointing reviewers at Task 0 for the SettingsDialog
"Registered Devices" page data-source flip — so a reviewer skimming
the SettingsDialog diff in Task 9 isn't confused by a column
rewrite they didn't expect.

No engine-side change. No libkalburator change. No new dependencies.

## Test posture

**New tests** (`WildPalms/tests/`):

- `tst_kf6settings_registry_migration.cpp` — fixture writes legacy
  `DeviceRegistry` entries into a scratch KSharedConfig, constructs
  `KF6Settings`, asserts (i) `DeviceSerials` now contains the
  serials, (ii) `DeviceRegistry` group is empty, (iii) repeat
  construction is a no-op.
- `tst_autosyncorchestrator_unregistered_device.cpp` — fixture
  emits a `palmDetected(ports, usbSerial)` for a serial not in the
  registry, asserts that `unregisteredDeviceDetected` fires *and
  no profile directory was created*. Second test: calls
  `createProfileForDevice(serial, name, id)` directly, asserts the
  profile dir + serial registry entry exist.

**Existing tests** updated:

- Any test that used `KF6Settings::registerDevice(...)` or
  `findProfileForDevice(...)` must switch to the serial-based
  equivalents. Survey via `grep -rn "registerDevice\b\|findProfileForDevice\b" WildPalms/tests/`.
- `AutoSyncOrchestrator` tests that used `findOrCreateProfile` to
  bootstrap a fixture profile continue to work (wrapper retained).

**Baselines.** `verify-all.sh` exit-code-0 acceptance unchanged. WildPalms
test count grows by +2; libkalburator unchanged at 92; PlanStan
unchanged at 82/105.

## Out of scope (explicit)

- `Profile↔device fingerprint binding` itself — stays. The
  per-profile fingerprint is still loaded/saved, still used by
  `KF6MainWindow::handleDeviceFingerprint` for the "wrong device
  for this profile" mismatch dialog. Only the multi-device
  *registry* and the silent-create path go.
- Device info display in `DashboardWidget` / `ProfileSidebar` —
  stays. Reads `Profile::deviceFingerprint()` and renders RAM/ROM/
  model. Source data untouched by this addendum.
- `DeviceFingerprint::matches(...)`, `isValid()`, `isEmpty()`,
  `formatMemorySize(...)` — stay. They serve the mismatch dialog
  and the display panels.
- ID mappings, backups, or any other per-device data partitioning
  — never existed; nothing to delete.
- Layer B silent-success-on-disconnect bug (the K-closing gate) —
  deferred until after Phase L lands per the user's preferred
  ordering. Tracked in `CURRENT-STATUS.md`.

## Acceptance

- All three items A/B/C land as three commits on `refactor/engine-
  merger` in WildPalms.
- `verify-all.sh` exits 0 against refreshed baselines (WildPalms
  test count = 73; previously 71).
- `grep -rn "registerDevice\b\|deviceRegistry\b\|findProfileForDevice\b\|registryKey\b\|fromRegistryKey\b" WildPalms/src/`
  returns zero hits in `src/`. Hits in `tests/` are limited to the
  new migration test (which reads the legacy group via raw
  `KConfigGroup` access).
- `WildPalms/docs/ROADMAP.md` no longer contains the string
  "Multiple Device Support".
- One-time migration verified manually against a real WildPalms
  profile directory with legacy `DeviceRegistry` entries.
- New entry in `04w-deferred-work.md` cross-references this
  design under section D ("Consumer UX"), recording the
  multi-device cleanup as resolved alongside Phase L's UX work.
- `CURRENT-STATUS.md` updated when Task 0 lands; `FINDINGS.md`
  appended only if a non-obvious gotcha surfaces.

## How this fits the larger plan

After this addendum + the existing Phase L body lands:

- WildPalms has a coherent "one profile per device" story, the
  add-account UX gains Akonadi as a third provider alongside
  CalDAV / CardDAV, and per-provider + per-mapping enable
  checkboxes give users the failover ergonomics described in
  `04w-deferred-work.md §C.1`.
- The K closing tag `v0.40-phase-k-engine-generalized` is still
  pending (gated on Layer B silent-success-on-disconnect — see
  CURRENT-STATUS). That gate is unrelated to Phase L and rides
  separately when the user is ready.
- Phase L closing tag: `v0.41-phase-l-akonadi-provider` after all
  L tasks (including Task 0) land and verify-all is clean.
