# First-Run Simulated Shakedown — WildPalms as a New User

- **Date:** 2026-08-22
- **WildPalms:** `main` @ `7a4d564` (`7a4d564ce68fd87d200367d38e74d87f59fdf6d8`,
  "port PalmCalendarBackend to libkalburator's batch itemsFetched signal") plus
  the working-tree v0.97→v1.01 API ports and pin bump (uncommitted at time of
  writing; none of the findings below depend on them).
- **libkalburator:** tag **v1.01** (`b847ab8`, O55+O55-fix/O56) via local source dir.
- **Graffodil:** v0.2.0 (local source).
- **Method:** code-derived walkthrough (not an instrumented run). Every UI
  string quoted below is a literal from the source; every claim carries a
  `file:line` reference. Purpose: user-testing guide + pre-smoke-test
  shakedown. Line numbers refer to the commits above and will drift.

---

## Part I — The walkthrough

### 1. Launch

Binary `wildpalms` (`CMakeLists.txt:129`, `src/main.cpp`). Window title
**"Wild Palms - Palm Pilot Synchronization"**, min 1000×700
(`src/kf6/kf6mainwindow.cpp:273-274`). On startup:

- Tray icon created unconditionally: `KStatusNotifierItem`, icon `"phone"`,
  tooltip **"Wild Palms" / "Listening for Palm devices"**
  (`kf6mainwindow.cpp:183-189`).
- Status bar: **"Ready - No device connected"** (`kf6mainwindow.cpp:203`).
- Log dock visible by default; seeded `[INFO] Wild Palms <ver> initialized`
  plus either *"Failed to start udev monitor. Use Device → Connect for manual
  connection."* or **"Listening for Palm USB devices..."**
  (`kf6mainwindow.cpp:206,223-228`).
- Dashboard strip initial state: **"No device" / "Disconnected" / "No
  profile" / "Last sync: Never"**
  (`src/widgets/dashboard/dashboardwidget.cpp:39,41,56,58`).
- Main page area (`KPageWidget`) is **completely empty pre-profile** — pages
  are added only in `loadProfile()` (`kf6mainwindow.cpp:686-712`). No empty-
  state hint anywhere.
- udev listener starts at app startup (not profile load):
  `src/palm/palmdevicemonitor.cpp:23-80`.

### 2. First-run interruption — the stopgap trap

With an empty profile registry, startup auto-fires
`resolveStartupProfile()` → `showProfilePickerStopgap()`
(`kf6mainwindow.cpp:1852-1878, 1880-1901`):

1. Info box **"No Profile"**: *"No WildPalms profile has been created yet.\nLet's create one to get started."*
2. Input dialog **"New Profile"**: *"Profile name:"*

Accepting creates a **hollow profile** (name only — no accounts, no bindings)
via `registerNew(name)` + immediate `loadProfile()`. Declaring cancels leaves
the app idle forever.

> **FINDING F1 (High):** the first-run prompt bypasses the accounts-first
> wizard entirely. A user following it ends up unconfigured, and — combined
> with F3 — one HotSync click later their dashboard hangs in "Syncing…"
> forever.

### 3. The real wizard (File → Profile → New Profile...)

`NewProfileWizard` ("New Wild Palms Profile", ModernStyle,
`src/app/wizard/newprofilewizard.cpp:20-21`), strictly sequential pages
(`newprofilewizard.cpp:34-39`):

| Page | Title | Notes |
|---|---|---|
| 0 | "Profile name" | Mandatory field, live duplicate-name check blocking Next: *"A profile with this name already exists."* (`namepage.cpp:32-61`) |
| 1 | "Accounts" | Skippable by design; zero accounts ⇒ local-files profile (`accountssetuppage.cpp:60-63`) |
| 2 | "Sync targets" | One combo per conduit: **Calendar / Contacts / Memos / Tasks** (`targetpickerpage.cpp`, `targetpickerrow.cpp`) |
| 3 | "Review" | Finish always enabled (`reviewpage.cpp` — no `isComplete()` override) |

**Accounts page.** "Add Account…" opens `AddAccountDialog`
(`src/app/accounts/addaccountdialog.cpp`) wrapping `AccountFormWidget`.
Kind dropdown labels (`accountformwidget.cpp:44-47`):

- `multiproto-dav` → **"DAV server (calendar + contacts)"**
- `akonadi` → **"Akonadi (local)"**
- `local-folder` → **"Local folder"**

Per-row status strings: *"Connecting…"*, *"Connected — %n collection(s)"*,
*"Not connected"*, *"Failed: %1"* (`accountssetuppage.cpp:180-224`). Connect
failures never block Next (`tests/runtime/tst_accountssetuppage.cpp:144-158`).

Test Connection label sequence: **"Testing…" → "Connected"/"Failed"/"Failed:
%1"** with provider errors such as *"No server URL configured."*
(lib `multiprotocoldavprovider.cpp:99`), *"No folders configured"*
(`localfolderprovider.cpp:40`).

> **FINDING F2 (Med):** "Local folder" is a dead end —
> `LocalFolderProvider::createConfigWidget` returns `nullptr`
> (`src/runtime/localfolderprovider.cpp:111-117`); the dialog accepts an empty
> local-folder account that then fails forever with "No folders configured",
> and no UI exists to add folder entries.
>
> **FINDING F2b (Low):** OK is never validity-gated —
> `AccountFormWidget::isValid()` exists (`accountformwidget.cpp:152-155`) but
> nothing calls it. "Testing…" does not disable the button; clicks stack.

**Sync targets page.** Index 0 always **"Local files (default)"**; connected
account collections filtered by `PimPlugin::matchesCollection`
(`src/plugins/pimplugin.cpp:8-29`; VEVENT/VTODO/VCARD/memos content-type
matching), labelled `"%1 ▸ %2"`, read-only entries disabled with suffix
*" (read-only)"* (`targetpickerrow.cpp:73-80`).

> **FINDING F4 (High, lib-dependent):** Akonadi task lists are invisible to
> Tasks and tasks-only collections leak into Calendar — `AkonadiProvider`
> types every collection `"calendar"` and never populates `contentTypes`
> (lib `akonadiprovider.cpp:126-141`), so `pimplugin.cpp:19-21` never matches
> todo while the calendar fallback `type=="calendar"` over-matches. Only DAV
> populates contentTypes (lib `multiprotocoldavprovider.cpp:241-242`).
>
> **FINDING F5 (Med):** when an account failed to connect, combos silently
> show only "Local files" — the hint *"No matching collections on your
> accounts."* is suppressed precisely when there are zero *matching*
> collections because zero collections exist (`targetpickerrow.cpp:107-108`).

**Review page** renders raw pluginIds (*"memo"*, not "Memos"), hard-coded
non-tr()ed strings, and lists connect-failed accounts under "New accounts to
be created" (`reviewpage.cpp:28-32,60`). Low severity.

**Finish** persists three INI files under `~/.wildpalms/profileN/`
(`profile.conf` / `accounts.conf` / `mappings.conf`,
`src/profile.cpp:561-677`; row shape written at `kf6mainwindow.cpp:1725-1743`
with composed target ids `"<accountUuid>:<collectionId>"` — the fa67c83 fix),
then `loadProfile()`. User lands on the **Patchbay** page
(`kf6mainwindow.cpp:704-712`); sidebar gains Calendar/Contacts/Memos/Tasks/
Patchbay; accounts reconnect asynchronously. No sync starts
(`autoSyncOnConnect` defaults false, `src/profile.h:351`). ✓

### 4. Plugging in the Palm

Detection is passive udev on subsystem `"tty"` filtered to vendor `0830`
(`palmdevicemonitor.cpp:11,23-80`); 100 ms debounce then
`collectPalmPorts()` → `palmDetected(ports, serial)` (:171-183).

Sequence: dashboard headline *"Palm device detected…"*
(`syncstatusmodel.cpp:127`) → unknown serial pops modal **"New Palm Device"**
(*"An unrecognised Palm device was detected… Create a new profile for this
device?"*, `kf6mainwindow.cpp:1381-1411`) → multi-port probe → status bar
**"Connecting…"** → handshake budget ~5 s (`kpilotdevicelink.h:48`;
`ConnectionWorker::doConnect`, `kpilotdevicelink.cpp:130-347`, all off the
GUI thread) → success: log *"Connection established!"*, KNotification
**"Device Connected"**, dashboard green **"Connected"** + **"Sync Now"**
button, cached device info logged (`kf6mainwindow.cpp:993-1136`).
Fingerprint check with **Wrong Device** dialog (Continue Anyway / Switch
Profile / Disconnect) at `:1237-1279`. Uninitialized device prompts username +
`dlp_WriteUserInfo`. ✓

At connect, `finishConnect()` (GUI thread, `src/runtime/palmruntime.cpp:484-616`)
performs category reconciliation — the first device WRITE
(`readAppBlock`/`writeAppBlock`, :495-532) — then per-conduit backend
construction, then star-mapping generation.

> **FINDINGS (device path):**
> - **F6 (Med):** plugging USB without pressing HotSync triggers detection →
>   guaranteed handshake timeout → raw headline *"No HotSync data detected on
>   any of %1 port(s)"* (`kpilotdevicelink.cpp:202`) with no guidance to press
>   the button (hint exists only in the manual-connect dialog text).
> - **F7 (Med):** a device plugged in *before* app launch is never detected —
>   no initial enumeration in the monitor; manual Device → Connect is the only
>   path, and using it pre-profile fails console-only
>   *"Cannot connect: PalmRuntime not initialized"* (`kf6mainwindow.cpp:929-931`)
>   while the action stays enabled.
> - **F8 (Low):** reconciliation failures are console-only qWarnings —
>   *"[PalmRuntime] AppInfo write failed for \<db\>"* / *"no free category slot…"*
>   (`palmruntime.cpp:522,529`).
> - **F9 (Low):** `finishConnect`'s BlockingQueued DLP hops can stutter the GUI;
>   bind errors conflate EACCES/EBUSY (*"Failed to bind to %1 (result: %2)"*,
>   `kpilotdevicelink.cpp:245`).

### 5. HotSync (Ctrl+H)

Guard: menu disabled without connected+profile
(`actionmanager.cpp:246-253`); handler backstop logs only
(`kf6mainwindow.cpp:1959-1969`). Fires immediately, no confirmation.

Execution: `PalmRuntime::hotSync()` → `runAllMappings(maxPasses=3,
skipUnchanged=true)` (`palmruntime.cpp:842-1031`); engine worker thread, UI
responsive; per-pass chip updates; fixpoint termination via
`shouldContinueSync`.

Feedback during run: dashboard spinner `◐◓◑◒` + **"Syncing…"**, Cancel
button, conduit chips (`dashboardwidget.cpp:149-241`); status bar silent;
Patchbay inert (listens only to route/device signals,
`patchbaypage.cpp:65-72`). Completion: log + status flash **"HotSync
complete"** / **"HotSync finished with errors"**, PIM views refresh from hub
(`onPalmRunFinished`, `kf6mainwindow.cpp:2254-2278`). Multi-hop propagation
in one click works. ✓

### 6. Conflicts

Mappings are generated `conflictPolicy=AskUser` (`palmruntime.cpp:600-604`)
but dispatched **Unmonitored** (`:921`): the engine defers conflicts instead
of pausing (lib `syncengine.cpp:4115-4121`). WP never installs a
ConflictManager, so batch presentation early-returns (lib
`syncengine.cpp:1918-1944`). What the user gets:

1. Status-bar badge *"%1 conflicts pending"* + dashboard button
   (`kf6mainwindow.cpp:2200-2213`).
2. `ConflictReviewDialog` (opened at `kf6mainwindow.cpp:2183`) offering
   Use Source/Use Target/Duplicate Both/Skip/Resolve All/**"Apply Resolutions
   (Sync)"** (`conflictreviewwidget.cpp:121-181`).

> **FINDING F10 (High):** "Apply Resolutions (Sync)" is wired to nothing.
> `applyResolutionsRequested` is re-emitted by the dialog wrapper
> (`widgets/dialogs/conflictreviewdialog.cpp:31-32`) but has **zero
> connections** in `kf6mainwindow.cpp` (verified by grep). Decisions live in
> `m_uiConflictStore` (cleared every profile load, `kf6mainwindow.cpp:676`);
> the engine's `SyncConflictStore` accessor exists (`palmruntime.cpp:725-729`)
> with no UI callers. Conflicts are counted, never resolved.

### 7. Other findings observed along the way

- **F11 (High):** empty-profile HotSync hangs the dashboard in "Syncing…":
  `hotSync()` emits `runStarted` then early-returns without ever emitting
  `runFinished` (`palmruntime.cpp:1026-1031`). Reachable via F1's hollow
  profiles. Re-entrant HotSync mid-run similarly warns console-only and
  resets chips (`palmruntime.cpp:857-861`).
- **F12 (Med):** engine messages never reach the in-app Log dock —
  transcoding warnings (e.g. known lossy "alarms"), skip-unchanged markers,
  mapping failures beyond the folded summary line. Zero references to
  `transcodingWarning` in WP src; `PalmRuntime::runLog` declared
  (`palmruntime.h:238`) but never emitted.
- **F13 (Med):** cancellation framed as failure — Cancel yields
  `[ERROR] HotSync finished with errors: Sync cancelled`
  (`palmruntime.cpp:971-975`).
- **F14 (Med):** "Last sync" never updates after runs — `Profile::
  setLastSyncTime` (`profile.cpp:158`) has no caller in the sync path
  (verified by grep); dashboard keeps showing "Never".
- **F15 (Med):** dead UI: View ▸ Navigate Ctrl+1–5 emit signals never
  connected (`actionmanager.cpp:206-234`; zero connects in
  `kf6mainwindow.cpp` — verified); "Review &Conflicts..." permanently
  disabled (`actionmanager.cpp:269-272`); **"Change Sync Folder..." invokes
  the F1 stopgap name-prompt instead of any folder picker**
  (`kf6mainwindow.cpp:2057-2061`).
- **F16 (Med):** File→Quit hides to tray by default (`MinimizeToTray=true`,
  `closeEvent` ignores close, `kf6mainwindow.cpp:257-269`;
  `kf6settings.cpp:248-251`) — window vanishes, process lives, no
  explanation, no quit confirmation, no sync-in-progress guard.
- **F17 (Low):** no record counts or summary anywhere post-sync; aggregate
  `perPluginStats` folded under a hardcoded `"calendar"` key and unread
  (`palmruntime.cpp:978`); Patchbay footers/hub counts unbuilt.
- **F18 (Low):** window-title inconsistency: startup title replaced by
  plain "Wild Palms [ - \<profile\>]" on load (`updateWindowTitle`,
  `kf6mainwindow.cpp:487-494`).

---

## Part II — Consolidated findings (roadmap input)

| ID | Sev | Summary | Primary ref |
|----|-----|---------|-------------|
| F1 | High | First-run stopgap bypasses wizard → hollow profiles | `kf6mainwindow.cpp:1852-1901` |
| F2 | Med | Local-folder account kind is a config dead end | `localfolderprovider.cpp:111-117` |
| F3/F11 | High | Empty-mapping HotSync never emits `runFinished` → eternal "Syncing…"; same for re-entrant clicks | `palmruntime.cpp:1026-1031,857-861` |
| F4 | High | Akonadi contentTypes absent → Tasks unbindable, Calendar over-matches (lib) | lib `akonadiprovider.cpp:126-141` |
| F5 | Med | Empty-binding silence: hint suppressed when account failed to connect | `targetpickerrow.cpp:107-108` |
| F6 | Med | No "press HotSync" guidance on plug-in-without-button timeout | `kpilotdevicelink.cpp:202` |
| F7 | Med | Pre-launch-plugged devices never auto-detected; manual connect pre-profile fails silently-enabled | `palmdevicemonitor.cpp:23-80`, `kf6mainwindow.cpp:929-931` |
| F8 | Low | Category reconcile write failures invisible to UI | `palmruntime.cpp:522,529` |
| F9 | Low | Opaque bind errors (EACCES vs EBUSY); finishConnect GUI stalls | `kpilotdevicelink.cpp:245`, `palmdeviceaccess.cpp:403-421` |
| F10 | High | Conflict review "Apply Resolutions" dead-ended; AskUser never surfaces during HotSync | `conflictreviewdialog.cpp:31-32`, `palmruntime.cpp:600-604,921` |
| F12 | Med | Engine warnings/logs never reach the Log dock | (absence; `palmruntime.h:238` unused) |
| F13 | Med | Cancel reported as error | `palmruntime.cpp:971-975` |
| F14 | Med | "Last sync" timestamp never updates | `profile.cpp:158` |
| F15 | Med | Dead Navigate menu; permanently-disabled Review Conflicts; mislabeled Change Sync Folder | `actionmanager.cpp:206-272`, `kf6mainwindow.cpp:2057-2061` |
| F16 | Med | Quit hides to tray silently by default | `kf6mainwindow.cpp:257-269` |
| F17 | Low | No post-sync counts/summary; discarded stats | `palmruntime.cpp:978` |

## Suggested priority order

1. **F1 + F3/F11** (compound into the worst first-run outcome) — route the
   stopgap through `runProfileWizard()`, and emit `runFinished` on the
   empty/re-entrant early returns.
2. **F10** — bridge `applyResolutionsRequested` into the engine's
   `SyncConflictStore` (or hide conflict UI until it works).
3. **F4** — lib handoff: Akonadi contentTypes population.
4. **F12–F16** — feedback-honesty batch (log bridging, cancel tone, last-sync
   stamp, dead menu cleanup, quit behavior).
5. **F2, F5–F9, F17, F18** — polish queue.

## Verification hooks for user testing

When user-testing, specifically exercise: first launch (F1), wizard with
failing DAV account (F2b/F5), wizard with Akonadi + Tasks binding (F4),
plug-in without HotSync button (F6), plug-in before launch (F7), HotSync with
hollow profile (F3/F11), engineered conflict (F10), mid-sync Cancel (F13),
mid-sync unplug (chip ⚠ path), File→Quit (F16).
