# First-Run & Connection UX Design

**Date:** 2026-02-17
**Status:** Approved
**Goal:** Make QPilotSync "just work" for single-device users while supporting
multi-device power users. The user presses HotSync on their Palm. That's it.

---

## Design Principles

1. **Zero-click for USB users.** Press HotSync on Palm, app does the rest.
2. **Profiles are invisible until needed.** Auto-created on first sync, named
   after the Palm's username, stored at `~/PalmSync/<username>/`.
3. **Always listening.** App stays resident in system tray, watching for Palms
   via udev. The user never "connects" manually unless they want to.
4. **Multi-device is the exception, not the rule.** Second device triggers a
   friendly prompt, not a setup wizard.
5. **Serial/IR is manual config.** No auto-detection possible on plain serial.
   User tells us the port. We listen on it.

---

## The Happy Path (Single USB Device, First Use)

```
User launches QPilotSync
  -> Dashboard: "Listening for Palm devices..."
  -> System tray icon appears (idle state)
  -> udev monitor starts watching for idVendor=0830

User puts Palm in cradle, presses HotSync
  -> visor driver creates ttyUSB0 + ttyUSB1
  -> udev fires: "Palm Handheld" detected, serial L0JG14I11398
  -> App spawns two parallel ConnectionWorkers (one per port)
  -> ttyUSB1 succeeds, ttyUSB0 attempt closed
  -> dlp_ReadUserInfo: username="Clinton", userID=0
  -> No profile matches serial L0JG14I11398
  -> Auto-create profile at ~/PalmSync/Clinton/
  -> Set as default profile
  -> Log: "Created profile for Clinton at ~/PalmSync/Clinton/"
  -> Run HotSync automatically
  -> Sync 618 records (contacts, memos, todos, calendar)
  -> Disconnect cleanly
  -> Dashboard: "Last sync: just now - 618 records"
  -> KNotification: "Synced! Data stored at ~/PalmSync/Clinton/.
     Click here to change location."

Next time: press HotSync -> detected by serial -> profile loaded -> sync -> done
```

---

## System Tray & App Lifecycle

The app is a long-running listener, like a print spooler or Bluetooth manager.

**System tray icon** is always present when the app is running. Visual states:

| State | Icon | Tooltip |
|-------|------|---------|
| Idle/Listening | Gray Palm silhouette | "QPilotSync - Listening for devices" |
| Device detected | Normal Palm icon | "QPilotSync - Palm detected" |
| Syncing | Animated (rotate/pulse) | "QPilotSync - Syncing Clinton's Palm..." |
| Error | Red badge on Palm | "QPilotSync - Sync error" |

**Tray context menu:**
- Open QPilotSync
- Sync Now (enabled when a device is connected or a known device port exists)
- Separator
- Quit

**Window close behavior:**
- Close button minimizes to system tray (default).
- Setting: "Close button quits the application" (off by default).
- Quit from tray menu or File menu exits fully.

---

## udev-Based Device Detection

### Implementation

Use `libudev` (via `QSocketNotifier` on the udev monitor fd) to watch for USB
device events.

**Filter:** `idVendor=0830` (Palm, Inc.). This is specific enough to never
false-positive on Arduinos, FTDI adapters, or other USB-serial devices. The
`visor` kernel module itself only loads for known Palm/Handspring device IDs.

**Event flow:**

```
udev "add" event for ttyUSB device
  -> Check parent USB device: idVendor == "0830"?
  -> Yes: collect all ttyUSB ports for this USB device
  -> Wait briefly (100ms) for both ports to appear
  -> Spawn parallel connection attempts on all ports
  -> First successful pi_accept wins
  -> Close losing connection attempts
  -> Proceed to identification and sync
```

**Parallel connection strategy:**

Two `ConnectionWorker` threads race. Each does `pi_socket` -> `pi_bind` ->
`pi_listen` -> `pi_accept`. The first to get a successful `pi_accept` emits
`connectionEstablished(socket)`. The main thread closes the other attempt via
`forceCloseSocket()`. This typically completes in under a second.

### Known Device IDs (Palm, Inc.)

| idVendor | idProduct | Device |
|----------|-----------|--------|
| 0830 | 0001 | Palm m500, m505, m515, Tungsten, Zire, etc. |
| 0830 | 0002 | Palm Tungsten variants |
| 0830 | 0003 | Palm Zire 71 |
| 0830 | 0010 | Palm Tungsten T5 |
| 0830 | 0020 | Palm TX |
| 0830 | 0030 | Palm LifeDrive |
| 0830 | 0060 | Palm Treo 650 |
| 082D | 0100 | Handspring Visor |

Note: the `visor` kernel module already handles this identification. If the
visor driver loaded and created ttyUSB ports, it's a Palm. We just confirm
via udev attributes.

---

## Profile Management

### Auto-Creation

On first sync with an unknown device:

1. Read Palm username and serial number.
2. Create `~/PalmSync/<username>/` with subdirectories:
   `contacts/`, `memos/`, `todos/`, `calendar/`, `.state/`, `install/`
3. Write `.qpilotsync.conf` with device serial, username, user ID.
4. Set as default profile.
5. After sync completes, show a KNotification offering to change the location.

### Device Identification

Each profile stores:
- **USB serial number** (from USB descriptor, e.g. `L0JG14I11398`) — primary key
- **User ID** (from `dlp_ReadUserInfo`) — secondary key
- **Username** (from `dlp_ReadUserInfo`) — display name

When a Palm connects:

| Serial known? | User ID matches? | Action |
|--------------|-----------------|--------|
| Yes | Yes | Load profile, auto-sync |
| Yes | Different username | "Username changed from X to Y. Update profile?" then sync |
| No | - | New device. Auto-create profile, sync. |

### Multi-Device

When a second, unknown Palm connects:

> "New Palm detected! This is a different device than your usual Palm
> (Clinton's Palm, serial L0JG...).
>
> Username: Bob
> Serial: M3KP29...
>
> [Create New Profile] [Use Existing Profile: Clinton]"

"Use Existing Profile" covers the case where the user cloned their old Palm to
a new one and wants to keep the same sync data.

---

## Manual Device Configuration (Serial / IR)

For non-USB connections, the user explicitly configures a port.

**Settings > Devices** page:

| Port | Type | Baud | Status |
|------|------|------|--------|
| (USB auto-detect) | USB | auto | Listening |
| /dev/ttyS0 | Serial | 57600 | Listening |

- "Add Device" button opens a dialog: port path, type (Serial/Infrared),
  baud rate. The app starts a background `pi_listen` on that port at startup.
- USB auto-detect is always enabled and listed as a built-in row (not removable).
- Multiple serial/IR ports can be configured simultaneously.
- Each serial/IR port gets its own persistent `ConnectionWorker` thread that
  blocks on `pi_accept` until a Palm connects.

---

## Dashboard Redesign

The Dashboard reflects the listen-first, sync-automatically philosophy.

### Layout

```
+------------------------------------------------------------------+
|                    QPilotSync                                      |
|                                                                    |
|  +---------------------+  +--------------------+  +--------------+|
|  | Device              |  | Last Sync          |  | Quick Actions||
|  |                     |  |                    |  |              ||
|  | [Palm icon]         |  | 3 minutes ago      |  | [Sync Now]   ||
|  | Listening for       |  | 618 records synced |  |              ||
|  | Palm devices...     |  |   20 contacts      |  | [Configure   ||
|  |                     |  |    7 memos         |  |  Devices...] ||
|  | -- or --            |  |    9 todos         |  |              ||
|  |                     |  |  582 events        |  |              ||
|  | Connected to        |  |                    |  |              ||
|  | Clinton's Palm      |  | Profile: Clinton   |  |              ||
|  | (m500, USB)         |  | ~/PalmSync/Clinton/|  |              ||
|  +---------------------+  +--------------------+  +--------------+|
+------------------------------------------------------------------+
```

### State Changes

| App State | Device Card | Last Sync Card | Quick Actions |
|-----------|------------|----------------|---------------|
| Listening, no history | "Listening for Palm devices. Press HotSync on your Palm." | "No syncs yet" | Configure Devices |
| Listening, has history | "Listening... Last device: Clinton's Palm" | Last sync summary | Sync Now (grayed), Configure |
| Connected | "Connected to Clinton's Palm (m500)" | Last sync summary | Sync Now, Disconnect |
| Syncing | "Syncing Clinton's Palm..." + progress | Updating... | Cancel Sync |

---

## What This Design Does NOT Cover

These are out of scope for this design and will be addressed separately:

- **Viewer page auto-reload after sync** (separate task)
- **Conduit settings / enable-disable UI** (Plan Task 18)
- **Plucker conduit** (Plan Task 17)
- **Backup/Restore workflows**
- **Network HotSync** (WiFi-capable Palms)
- **The manual Connect dialog** stays in the Device menu as a power-user tool,
  unchanged. It's just no longer the primary way to connect.
