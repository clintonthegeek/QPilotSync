# Phase E.14 — Plucker Plugin Design

**Status:** Draft, 2026-04-26
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.14
**Predecessor:** Phase E.13 (WebCalendar plugin landed 2026-04-26)
**Successor:** Phase E.15 (Install action plugin)

## Goal

Rewrite the legacy `PluckerConduit` (currently `IToolConduit`) as the
sixth new-ABI `IBackendPlugin`, after Memo (E.9), Calendar (E.10), ToDo
(E.11), Contacts (E.12), and WebCalendar (E.13). Plucker is a *one-way
fetch source* — like WebCalendar, but unlike WebCal it emits whole Palm
databases, not records: each configured channel produces one `.pdb` blob
by spidering a homepage URL through PyPlucker's `Spider.py`.

The runtime cross-plugin pairing — Plucker source → install pipeline —
defers to **E.15** (when `Install` becomes `IPluginAction`); E.14 ships
source-only with `MockBlobBackend` as the e2e install-drain target,
following the same deferral pattern E.13 established for WebCal's Palm-
side mirror. Bootstrap of the device-side Plucker viewer (`SysZLib.prc`
+ `viewer_en.prc`) is folded into the same blob inventory, gated on
`IPalmDatabaseAccess::hasDatabase("Plucker")`.

Behind a new `WILDPALMS_PLUCKER_PLUGIN_V2=ON` toggle (default ON)
following the established Memo/Calendar/ToDo/Contacts/WebCal pattern; the
legacy `PluckerConduit` stays buildable in-tree until E.16 deletes the
old surface.

Unlike E.13, **nothing lands upstream in libkalburator** for E.14.
Plucker DB is a Palm-specific wire format with no consumer outside
WildPalms; pushing a Plucker fetcher into a sibling slot of
`SubscriptionBackend` would violate the library/backend principle in
`feedback_library_vs_backend_responsibility.md`. The PyPlucker subprocess
wrapper lives plugin-side at `src/plugins/plucker/pluckerfetcher.{h,cpp}`.

## Decisions

The five questions answered during brainstorming.

### Decision #1 — Source-only blob backend, install-drain via E.15

Plucker writes one record per channel into its own `IBlobBackend`
(`PluckerBlobBackend`). E.14 e2e uses `MockBlobBackend` as the install-
drain target. Runtime pairing — Plucker source → E.15's `Install`
action sink — is wired when `Install` becomes `IPluginAction`.

**Why:** Symmetric with E.13's deferral of Palm-side runtime pairing for
WebCal (E.13 plan, "Scope excluded"). Avoids inventing a half-shaped
write-only `PalmInstallBackend` that E.15 would re-shape weeks later.
The `IPluginAction` contract is the natural home for "drain a backend
into the install queue".

**Alternative considered:**
- (A) End-to-end via a new `PalmInstallBackend` mirror target.
  Rejected — invents a write-only sink whose `loadRecords` returns
  nothing meaningful; half-shaped against the `SyncBackend` contract.
- (C) Reclassify Plucker as `IPluginAction` directly, overriding the
  spec. Rejected — contradicts spec row E.14, and the IBackendPlugin
  shape *does* fit if we accept "channel = record" granularity.

### Decision #2 — Plugin-side `PluckerFetcher`, no libkalburator upstream

`PluckerFetcher` is a small `QObject` helper at
`src/plugins/plucker/pluckerfetcher.{h,cpp}` that wraps `QProcess`
invocation of PyPlucker's `Spider.py` per channel. API:

```cpp
namespace WildPalms::PluckerPlugin {

class PluckerFetcher : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool       success = false;
        QString    errorMessage;
        QByteArray pdbBytes;       ///< Empty on failure.
        QString    docFile;        ///< Sanitised channel name → file basename.
    };

    explicit PluckerFetcher(QObject *parent = nullptr);

    /// Synchronous (uses QEventLoop internally; matches IcsFeedFetcher
    /// pattern). Spawns python3 + Spider.py with channel-derived CLI
    /// args, awaits completion, reads back the produced .pdb bytes.
    /// Default timeout: 5 minutes per channel (legacy parity).
    Result fetch(const PluckerChannel &channel, int timeoutMs = 300000);

    /// Override for tests. Default resolves PYTHONPATH/Spider.py via the
    /// PLUCKER_DATA_DIR build-time symbol or AppImage layout.
    void setSpiderScriptPath(const QString &absPath);
    void setPythonExecutable(const QString &execName);
    void setOutputDirectoryOverride(const QString &dir);

Q_SIGNALS:
    void progress(const QString &message);
};

}
```

**Why plugin-side:** Plucker DB is Palm-only. No PlanStan/ShadowStan
consumer. Per `feedback_library_vs_backend_responsibility.md`,
backend peculiarities degrade inside the backend; libkalburator stays
focused on iCalendar.

**Why subprocess (vs. native C++ rewrite):** PyPlucker is ~30 years of
HTML→Plucker-DB conversion logic (image transcoding, depth-first/
breadth-first spidering, zlib compression, link rewriting). A native
rewrite is a multi-week project for a hobbyist conduit (`DefaultEnabled:
false` in the legacy manifest). The subprocess is mature, the wrapper
is small, and tests can mock the fetcher cleanly.

### Decision #3 — Bootstrap viewer PRCs as conditional blob records

The legacy conduit checks `KPilotLink::findDatabase("Plucker")` on the
device and, if absent, queues `SysZLib.prc` + `viewer_en.prc` (shipped
under `src/plugins/plucker/viewer/`) onto `SyncContext::installQueue`.

In the V2 plugin, the same gate runs against
`IPalmDatabaseAccess::hasDatabase("Plucker")`, queried via the
`PalmDeviceConnection` passed to `createBackends()`. When the device
lacks the Plucker DB, `PluckerBlobBackend::loadRecords` emits two
synthetic records in addition to the per-channel `.pdb` records:

| Record ID                | Collection                       | Blob bytes                  |
|--------------------------|----------------------------------|-----------------------------|
| `bootstrap:syszlib`      | `plucker:bootstrap`              | bytes of `SysZLib.prc`      |
| `bootstrap:viewer`       | `plucker:bootstrap`              | bytes of `viewer_en.prc`    |
| `channel:<channelId>`    | `plucker:channels`               | bytes of produced `.pdb`    |

E.15's `Install` action drains the entire blob inventory; the same
install pipeline handles channels and bootstrap uniformly. Idempotence
is structural — Palm install replaces by Palm DB name, so re-installing
on subsequent syncs after the bootstrap initially landed is a no-op
beyond the device-roundtrip cost. The bootstrap records vanish from
the inventory once `hasDatabase("Plucker") == true`.

**Why one collection per record-class:** Mirroring (E.15) drains by
collection. Channels and bootstrap items have different scheduling
semantics — channels honour `isDue()`, bootstrap is "once per absent
viewer". Splitting them into two collections lets the E.15 action
treat each list independently without leaking Plucker-internal flags
into the `BackendRecord` shape.

**Alternative considered:**
- (B) New ABI hook `IBackendPlugin::bootstrapItems()`. Rejected — adds
  plugin-ABI surface for one consumer. The blob model already
  expresses "items to install".
- (C) Defer entirely to E.15 with hardcoded Plucker knowledge in the
  Install action. Rejected — leaks Plucker-specific DB names into a
  generic action.
- (D) Drop bootstrap; require manual viewer install. Rejected — the
  produced `.pdb` files are useless without the viewer, and the bundled
  PRCs already exist in-tree.

### Decision #4 — JSON-shaped settings, no migration from legacy INI

The V2 plugin's `loadSettings(QJsonObject)` consumes a `channels[]`
array; each entry carries all 25 fields verbatim from `PluckerChannel`,
plus a per-channel `last_fetched` ISO string for scheduling
persistence (Decision #5). `saveSettings()` writes the same shape back
including the updated `last_fetched`.

```json
{
  "channels": [
    {
      "id": "uuid-...",
      "name": "BBC News",
      "home_url": "https://www.bbc.co.uk/news",
      "max_depth": 2,
      "stay_on_host": true,
      "depth_first": false,
      "user_agent": "",
      "url_pattern": "",
      "bpp": 8,
      "max_width": 150,
      "max_height": 250,
      "alt_max_width": 450,
      "alt_max_height": 800,
      "no_images": false,
      "image_compression_limit": 50,
      "compression": "zlib",
      "category": "",
      "storage_mode": "ram",
      "card_directory": "",
      "update_enabled": true,
      "update_frequency": 1,
      "update_period": "days",
      "last_fetched": "2026-04-26T08:00:00"
    }
  ]
}
```

**No migration from legacy `.wildpalms.conf`** `[Plucker-<id>]` INI
groups. Plucker has `DefaultEnabled: false` and is recent (March 2026
landing); the live config base is small. V1 users reconfigure once via
the V2 settings widget. The legacy INI loader stays alive in
`PluckerConfig` for as long as the legacy `PluckerConduit` ships
(through E.16).

**Why all 25 fields:** Every field maps to a CLI flag PyPlucker honours
(`--maxdepth`, `--bpp`, `--noimages`, `--staybelow`, …). Trimming any
field removes a knob the user could legitimately want. Cost of keeping
all 25 is one JSON line each; cost of dropping any one is "user can't
configure that anymore". YAGNI does not apply — this is a port, not a
new design.

**Alternative considered:** auto-migrate INI → JSON on first V2 load.
Rejected — migration code costs more long-term maintenance than it
saves for a small live config base, and the field set is small enough
to recreate via the widget in minutes.

### Decision #5 — Per-channel `isDue()` scheduling, persisted in settings

Legacy Plucker schedules each channel independently with
`updateFrequency`/`updatePeriod` (hours/days/weeks/months) and a
`lastFetched` timestamp. Only `isDue` channels get spidered per sync.

V2 keeps this. `PluckerBlobBackend::loadRecords("plucker:channels")`
iterates `m_channels`, calls `PluckerChannel::isDue()`, fetches via
`PluckerFetcher` for due channels, and updates `lastFetched` in place.
The plugin's `saveSettings()` round-trips `last_fetched` back to
storage so cadences (days/weeks/months) survive across runs.

**Why persist (vs. WebCal's in-memory-only):** WebCal cadences are
hours; in-memory was acceptable because the host process typically
outlives the cadence. Plucker cadences are days to months; in-memory
only would mean every cold run re-spiders every channel — wasteful and
slow.

**Why not stateless engine-driven scheduling:** Plucker's per-channel
scheduling is a user-visible feature with UI controls. Moving timing
to the engine layer would either flatten the per-channel knob (one
cadence per plugin) or require the engine to grow per-record schedules
— neither serves E.14. Out of scope.

### Decision #6 — No main view, channel-management widget moves to settings

V2 ships `IBackendPlugin::hasMainView() == false`. The legacy
`PluckerView` is really the channel-management surface (tree of
channels + add/edit/remove/fetch buttons); its V2 equivalent goes in
`IPlugin::createSettingsWidget()`. The legacy `pluckerview.{h,cpp}` and
`pluckerchanneldialog.{h,cpp}` stay with the legacy `PluckerConduit`
until E.16/E.17 deletes them.

The new settings widget is a thin re-skin of the same controls
operating against the V2 channel list (in-memory `QList<PluckerChannel>`
on the plugin) with `loadSettings`/`saveSettings` round-trip. The
"fetch now" button issues a synchronous `PluckerFetcher::fetch(channel)`
and writes the result into the plugin's `last_fetched` map; this stays
local to the widget and does not cross the IBackendPlugin contract.

**Why no separate view:** Plucker has no per-record view to render
(unlike Memo/Contacts which show records). The "view" is operational
status, which the settings widget already covers.

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│  src/plugins/plucker/  (submodule — same git layout as today)  │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│   PluckerBackendPlugin : QObject, WildPalms::IBackendPlugin    │
│       │                                                        │
│       │  pluginId  = "plucker"                                 │
│       │  claimedDatabases() -> {}    (no Palm DB claimed)      │
│       │  createBackends(host, device) -> { blob, calendar }    │
│       │      │                                                 │
│       │      └─ blob = new PluckerBlobBackend(...)             │
│       │           calendar = nullptr                           │
│       │                                                        │
│       │  loadSettings(QJsonObject)  -> m_channels              │
│       │  saveSettings()             <- m_channels              │
│       │                                                        │
│       │  hasSettings() -> true; createSettingsWidget(parent)   │
│       │      -> new PluckerSettingsWidget(this, parent)        │
│       │                                                        │
│       │  hasMainView() -> false                                │
│       │  runAfter() -> {"memo","calendar","todos","contacts"}  │
│                                                                │
│   PluckerBlobBackend : Kalburator::Sync::IBlobBackend          │
│       │  Collections:                                          │
│       │    - "plucker:channels"   -> per-channel .pdb records  │
│       │    - "plucker:bootstrap"  -> SysZLib + viewer (cond.)  │
│       │  loadRecords(coll)                                     │
│       │      channels   -> spider due channels via fetcher     │
│       │      bootstrap  -> if !device->hasDatabase("Plucker"), │
│       │                    emit SysZLib + viewer PRCs          │
│       │  createRecord/updateRecord/deleteRecord                │
│       │      -> read-only no-ops (mirror is one-way fetch→sink)│
│                                                                │
│   PluckerFetcher : QObject                                     │
│       │  fetch(channel) -> Result{success, pdbBytes, ...}      │
│       │  Wraps QProcess + python3 + parser/PyPlucker/Spider.py │
│                                                                │
│   PluckerChannel  (struct, ported verbatim from V1)            │
│   PluckerSettingsWidget  (channel-management UI)               │
│   plucker-backend-plugin.json  (KPlugin manifest)              │
│                                                                │
└────────────────────────────────────────────────────────────────┘
                              │
                              ▼
       runtime-paired (E.15) with InstallPluginAction sink
```

## Components

### `PluckerChannel` (struct)

Verbatim from `pluckerconfig.h` legacy: 25 fields per channel.
Lifted into V2 via `pluckerchannel.h` shared between V1 and V2
(both link against the same translation unit; reduces drift). The
V2 plugin uses snake_case JSON keys; the V1 INI loader uses camelCase
keys; serialisation lives in plugin-side code (V2:
`pluckerchannelserializer.cpp`, V1: `pluckerconfig.cpp` unchanged).

`isDue(channel)` and `nextDueTime(channel)` are static helpers in
`pluckerchannel.h` reused by both V1 and V2.

### `PluckerFetcher`

Synchronous wrapper around `QProcess` + `python3` +
`parser/PyPlucker/Spider.py`. Resolves the Spider script via:
1. Build-time `PLUCKER_DATA_DIR` symbol (development build).
2. `${ApplicationDirPath}/../share/wildpalms/plucker/parser/...`
   (AppImage / installed).
Both paths already exist in legacy `PluckerConduit::parserPath()`;
the fetcher takes them over.

CLI args constructed via the same `PluckerConfig::buildCLIArgs(channel,
outputDir)` static helper used by V1 (lifted into a free function
`pluckerCliArgs(channel, outputDir)` in `pluckerchannel.h`). Output
directory is a fresh `QTemporaryDir` per fetch; the fetcher reads back
the expected `.pdb` (named `sanitizeDocFile(channel.name) + ".pdb"`)
into `Result::pdbBytes` and returns.

Test seam: `setSpiderScriptPath` / `setPythonExecutable` /
`setOutputDirectoryOverride`. A `MockPluckerFetcher` test double
implements the same struct API directly without `QProcess` for unit
tests of `PluckerBlobBackend`.

### `PluckerBlobBackend`

```
backendId()   -> "plucker"
displayName() -> "Plucker"
isAvailable() -> true (no upstream device check; channels with no
                 .pdb produced fail at loadRecords time, not here)

availableCollections() -> [
    {id:"plucker:channels",  displayName:"Plucker Channels"},
    {id:"plucker:bootstrap", displayName:"Plucker Bootstrap PRCs"}
]

loadRecords("plucker:channels"):
    for ch in m_channels:
        if !ch.update_enabled: continue
        if !PluckerChannel::isDue(ch): continue
        result = m_fetcher->fetch(ch)
        if !result.success:
            log error; skip channel; do NOT update last_fetched
            continue
        emit BackendRecord{
            id: "channel:" + ch.id,
            collectionId: "plucker:channels",
            content: result.pdbBytes,
            contentType: "application/vnd.palm",
            modifiedAt: now()
        }
        ch.last_fetched = now()
        signal channelFetched(ch.id)  // settings widget reacts

loadRecords("plucker:bootstrap"):
    if m_device == nullptr: return []
    if m_device->hasDatabase("Plucker"): return []
    emit two records:
        bootstrap:syszlib  (SysZLib.prc bytes)
        bootstrap:viewer   (viewer_en.prc bytes)

createRecord / updateRecord / deleteRecord -> false / no-op
modifiedSince -> loadRecords() filtered by modifiedAt (unused in E.14
                  e2e since MockBlobBackend mirror reads everything,
                  but contract-correct for E.15)
deletedSince  -> []  (Plucker source never deletes)
```

The "cache-on-failure" pattern from `WebcalBlobBackend` doesn't apply:
WebCal needs a cached fetch to avoid mirror's empty-source-deletes-
target footgun, which is record-deletion semantics. Plucker channels
are atomic .pdb installs — a failed fetch means "skip this channel
this run", not "feed an empty source into mirror". On failure:
emit nothing for that channel, leave `last_fetched` unchanged (so
`isDue` still fires next run), surface the error via the plugin's
log signal.

### `PluckerBackendPlugin`

```
pluginId          = "plucker"
displayName       = "Plucker"
description       = "Fetch web content as Palm Plucker documents"
version           = "2.0.0"
icon              = QIcon::fromTheme("text-html")

claimedDatabases() = {}   // no DB claimed; Plucker creates DBs by name
createBackends(host, device):
    if (m_fetcher == nullptr) m_fetcher = new PluckerFetcher(this);
    auto *blob = new PluckerBlobBackend(m_channels, m_fetcher,
                                         device, viewerPaths());
    m_backend = blob;
    return { blob, nullptr };

createConflictHandler() -> nullptr   (no conflicts; one-way fetch)

runAfter() = {"memo","calendar","todo","contacts","webcalendar"}
runBefore() = {}
```

The plugin keeps `m_channels: QList<PluckerChannel>` as the source of
truth between `loadSettings` / `saveSettings`. The settings widget
operates against the same list by reference. `PluckerBlobBackend`
takes a *copy* of the list at construction; the manager re-creates
backends per session, so settings edits are visible on next sync.

### `PluckerSettingsWidget`

Re-skin of `pluckerview.cpp` adapted to operate against the V2
channel list:

- `QTreeWidget` of channels with columns: name, last fetched, status.
- Add / Edit / Remove buttons → modal `PluckerChannelDialog` (lifted
  V1 dialog, adapted to operate on a `PluckerChannel*` rather than
  `PluckerConfig*`).
- "Fetch now" button → synchronous `PluckerFetcher::fetch()` for the
  selected channel; updates row's status column.
- On any modification: emits a `settingsChanged()` signal the host
  picks up to call `saveSettings()`.

### Manifest — `plucker-backend-plugin.json`

```json
{
    "KPlugin": {
        "Name": "Plucker",
        "Description": "Fetch web content as Palm Plucker documents",
        "Icon": "text-html",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0.0",
        "Category": "Sync",
        "Id": "plucker"
    },
    "X-WildPalms-PluginType":   "backend",
    "X-WildPalms-PalmDatabases": [],
    "X-WildPalms-DefaultEnabled": false,
    "X-WildPalms-SortOrder":     50
}
```

`DefaultEnabled: false` matches V1 — Plucker is opt-in.

## Data flow

1. **Plugin construction** (per session): `BackendPluginManager`
   instantiates `PluckerBackendPlugin`, calls `loadSettings(json)`
   which fills `m_channels` from `channels[]` JSON array, then calls
   `createBackends(host, device)` which returns a fresh
   `PluckerBlobBackend` aware of the device + viewer PRC paths.

2. **Sync tick** (E.14 driven by `tst_plucker_v2_e2e` test driver;
   E.15+ driven by runtime):
   - Test driver constructs `MockBlobBackend` as the install-drain
     target.
   - For each collection (`plucker:channels`, `plucker:bootstrap`):
     - Test driver invokes `BlobSyncEngine::mirror(source=plucker,
       target=mockSink, collectionId)`.
     - Mirror reads `loadRecords(collectionId)` from source.
     - For `plucker:channels`: source iterates due channels, spiders
       each via `PluckerFetcher`, emits records with .pdb bytes;
       updates per-channel `last_fetched`.
     - For `plucker:bootstrap`: source checks
       `device->hasDatabase("Plucker")`; if absent, emits SysZLib +
       viewer records reading `.prc` bytes from disk.
     - Mirror writes everything to `mockSink` (would be E.15's
       `InstallPluginAction` sink in production).

3. **Settings persistence**: After the sync tick, the test driver (or
   E.15+ runtime) invokes `pluginManager->saveSettings("plucker")`,
   which calls `PluckerBackendPlugin::saveSettings()` and writes the
   JSON back to `.wildpalms.conf` (or wherever the manager persists
   plugin settings; E.8 manager establishes the convention).

4. **Settings widget interactions** are local: edits mutate
   `m_channels` directly; "Fetch now" runs `PluckerFetcher::fetch()`
   inline and updates the channel's `last_fetched` and the row's
   status column. The widget emits `settingsChanged()` so the host
   knows to call `saveSettings()`.

## Error handling

- **PyPlucker subprocess failure** (non-zero exit, timeout, missing
  Spider.py): `PluckerFetcher::Result{success: false, errorMessage}`.
  Backend skips the channel, leaves `last_fetched` untouched, surfaces
  via plugin log signal. Sync continues for other channels.
- **Bundled viewer PRCs missing on disk**: `loadRecords("plucker:
  bootstrap")` returns the records it can find; if both PRCs are
  missing, returns `[]` and logs a warning (parity with legacy
  conduit's "bundled viewer PRCs not available" branch).
- **Device not connected** (`m_device == nullptr`): bootstrap
  collection returns `[]`; channel collection still spiders (channels
  don't depend on device state in V2). E.15's runtime decides whether
  to run channel sync without a device — Plucker doesn't gate.
- **Settings widget "Fetch now" failure**: row status column shows
  "Failed: <message>"; widget does not abort.
- **Slot allocation conflicts** (analogue to WebCal): N/A. Plucker
  channels create independent Palm DBs by sanitised name; no slot
  contention. Two channels with the same `name` would collide on
  the device — surface this as a settings-widget validation warning
  ("two channels share the same Palm DB name; the latter overwrites
  the former on install").

## Testing

Five test executables under `tests/plugins/plucker/`, mirroring the
five-file split E.13 established:

| Test                          | Coverage                                                              |
|-------------------------------|-----------------------------------------------------------------------|
| `tst_pluckerchannel`          | `isDue` / `nextDueTime` / `sanitizeDocFile` / CLI-args round-trip     |
| `tst_pluckerfetcher`          | `QProcess` invocation against a stub `Spider.py` (writes a fake .pdb) |
| `tst_pluckerblobbackend`      | `loadRecords` for both collections, mocked fetcher, mocked device     |
| `tst_pluckerbackendplugin`    | Manifest + `KPluginFactory` round-trip; settings save/load            |
| `tst_plucker_v2_e2e`          | Fetcher-mock → blob → `MockBlobBackend` install-drain target          |

The stub `Spider.py` for `tst_pluckerfetcher` is a 20-line Python
script that writes `<output_dir>/<doc_file>.pdb` containing a fixed
header "PLUCKER_TEST" and exits 0; the test verifies the fetcher reads
those bytes back into `Result::pdbBytes`. Failure paths exercised via
a stub that exits non-zero and a stub that hangs (timeout test uses
`setSpiderScriptPath` to a sleep-looping script).

`tst_plucker_v2_e2e` exercises the complete fetch→blob→sink flow:
- 2 channels (one due, one not).
- Mocked `IPalmDatabaseAccess` returning `hasDatabase("Plucker")=false`
  for the bootstrap-emitted run, then `true` for a follow-up run.
- Asserts: due channel produces a record; non-due channel does not;
  bootstrap PRCs appear in the mock sink on first run only;
  `last_fetched` for the due channel is updated; `saveSettings()`
  round-trips it.

## Scope excluded

The following are explicitly **not** in E.14:

- **Runtime cross-plugin pairing** (Plucker source → Install action
  sink). Lands in E.15 when `Install` becomes `IPluginAction`. E.14
  e2e uses `MockBlobBackend`.
- **Live-device POSE64 integration**. E.18.
- **Legacy `PluckerConduit` removal**. E.16.
- **Legacy `PluckerView` / `PluckerChannelDialog` removal**. E.16/E.17;
  V2 ships its own `PluckerSettingsWidget` and `PluckerChannelDialog`
  in parallel until then.
- **Native C++ Plucker DB writer**. The PyPlucker subprocess wrapper
  is intentionally chosen over a rewrite; see Decision #2.
- **Persistent fetch retry / backoff**. On transient fetch failure,
  the channel is skipped and retried on the next sync per `isDue`.
  No backoff state is persisted.
- **VFS-card storage validation**. `storage_mode = "vfs"` /
  `card_directory` flow through to the fetcher's CLI args verbatim
  per legacy behaviour; no new validation added.
- **Auto-migration from legacy `[Plucker-<id>]` INI groups**.
  See Decision #4.
- **libkalburator changes**. Plucker is Palm-only; nothing lands
  upstream. See Decision #2.

## Files touched

```
NEW (src/plugins/plucker/ — submodule, separate commit history):
  plucker-backend-plugin.json
  pluckerbackendplugin.{h,cpp}
  pluckerblobbackend.{h,cpp}
  pluckerfetcher.{h,cpp}
  pluckerchannel.h               (lifted shared struct + helpers)
  pluckerchannelserializer.{h,cpp}  (V2 JSON ↔ struct)
  pluckersettingswidget.{h,cpp}
  pluckerchanneldialog_v2.{h,cpp}   (V2 dialog operating on PluckerChannel*)

NEW (tests/plugins/plucker/):
  tst_pluckerchannel.cpp
  tst_pluckerfetcher.cpp
  tst_pluckerblobbackend.cpp
  tst_pluckerbackendplugin.cpp
  tst_plucker_v2_e2e.cpp
  fixtures/spider_stub.py        (success path)
  fixtures/spider_fail.py        (non-zero exit)
  fixtures/spider_hang.py        (timeout test)
  fixtures/SysZLib.prc           (small test fixture)
  fixtures/viewer_test.prc       (small test fixture)

MODIFIED:
  src/plugins/plucker/CMakeLists.txt  (add WILDPALMS_PLUCKER_PLUGIN_V2 toggle,
                                        gate legacy vs V2 sources)
  src/plugins/plucker/pluckerconfig.{h,cpp}  (extract PluckerChannel into
                                               pluckerchannel.h; legacy loader
                                               keeps using it)
  src/plugins/plucker/pluckerconduit.cpp     (use lifted PluckerChannel struct)
  tests/plugins/CMakeLists.txt               (add plucker subdirectory)

UNCHANGED:
  src/plugins/plucker/pluckerview.{h,cpp}        (legacy view stays put)
  src/plugins/plucker/pluckerchanneldialog.{h,cpp} (legacy dialog stays put)
  src/plugins/plucker/parser/PyPlucker/**        (Spider.py + bundled libs)
  src/plugins/plucker/viewer/{SysZLib.prc,viewer_en.prc}
```

## Acceptance criteria

- [ ] `cmake --preset dev && cmake --build build-dev` succeeds with
      `WILDPALMS_PLUCKER_PLUGIN_V2=ON` (default).
- [ ] `cmake -DWILDPALMS_PLUCKER_PLUGIN_V2=OFF -B build-legacy && cmake
      --build build-legacy` succeeds (legacy `PluckerConduit` still
      buildable).
- [ ] `ctest --preset dev` passes for all 5 new test executables.
- [ ] WP `ctest` baseline (current 67 tests pre-E.14) does not regress.
- [ ] libkalburator `ctest` baseline unchanged (no upstream changes
      land in E.14).
- [ ] Memory index entry for E.14 added at session-end.

## Spec exit gate

The exit gate from the parent spec row E.14 is: *"Smoke passes against a
mock web source."* This spec maps that to: `tst_plucker_v2_e2e` passes
with a mocked `PluckerFetcher` returning fixed `.pdb` bytes for a due
channel, plus bootstrap PRC emission against a mocked
`IPalmDatabaseAccess`.

---

**Author:** Claude (E.14 brainstorming session, 2026-04-26)
