# Phase E.15a — Install Action Plugin Design

**Status:** Draft, 2026-04-26
**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.15
**Predecessor:** Phase E.14 (Plucker plugin landed 2026-04-26)
**Successor:** Phase E.15b (fullsync → runtime relocation, separate spec)

## Goal

Rewrite the legacy `InstallConduit` (`IConduit`) as the **first**
`IPluginAction`, replacing sync-time file installation with an
explicitly-triggered action. The action installs `.prc`/`.pdb` files
to a connected Palm via `pi_file_install`, sourcing them from two
places:

1. The legacy `<syncFolder>/install/*.{prc,pdb}` folder (preserves
   user workflow).
2. Records emitted by loaded `IBackendPlugin`s with `application/vnd.palm`
   blobs — the runtime cross-plugin pairing deferred from E.14
   (Plucker channels + bootstrap PRCs).

The aggregation lives in a separate helper (`InstallSourceCollector`);
the action itself consumes a flat `files[]` list and is host-neutral.
A new `IPalmFileInstaller` abstraction wraps `pi_file_install` so
tests can drive the action without a real Palm. UI trigger lands as
a generic `Tools → Actions → …` menu auto-populated from
`PluginActionManager`'s loaded actions.

Behind a new `WILDPALMS_INSTALL_PLUGIN_V2=ON` toggle (default ON);
the legacy `InstallConduit` and `InstallView` stay buildable until
E.16 deletes the old surface.

The fullsync → runtime relocation called out in the parent spec's
E.15 row is **deferred to a separate sub-phase E.15b**. E.15a is
self-contained: it lands the action, the abstraction, the collector,
the UI trigger, and the toggle.

## Decisions

The five questions answered during brainstorming.

### Decision #1 — File-list action, aggregation in a separate helper

`InstallActionPlugin::execute(ctx, device, params)` consumes
`params["files"]` — a JSON array of `{path, displayName}` entries —
and installs each via `device->fileInstaller()->installFile(path)`.
The action is **stateless and source-blind**: it does not scan
folders, does not query plugin managers, and does not write temp
files.

Aggregation lives in **`InstallSourceCollector`** at
`src/runtime/installsourcecollector.{h,cpp}`. The collector:

- Scans a folder for `*.{prc,pdb}` (case-insensitive) and emits one
  entry per file; tracks them as folder-sourced for post-install
  move-to-`installed/`.
- Queries a `BackendPluginManager*` for loaded backend plugins;
  iterates each backend's `availableCollections()`; for each
  collection where the first record's `type` field starts with
  `plucker-` (or is otherwise marked installable — see Decision #5),
  loads records and writes each `BackendRecord::data` blob to a
  fresh `QTemporaryDir`-managed file named
  `<displayName>.<inferred-ext>` (or `record-<id>.pdb` if no clue).
- Returns a `Result` containing the file list (paths + display names),
  the temp-dir handle (caller owns lifetime), and the list of
  folder-sourced paths needing post-install move.

**Why this split:** the action is small and trivially testable
(install N paths → N pisock calls). Aggregation has its own concerns
— folder I/O, plugin discovery, temp-file management, post-install
moves — that are orthogonal to the install loop. Each unit is
isolated and tested independently.

**Alternatives considered:**
- (B) Action does aggregation itself. Rejected — couples folder I/O
  to plugin queries; needs a new `IPluginRegistry` ABI for one
  consumer; the action becomes hard to mock for tests.
- (C) Two separate actions (`install-files`, `install-plugin-blobs`).
  Rejected — adds two manifests for what users perceive as a single
  "install" operation; ordering becomes the UI's problem.

### Decision #2 — `IPalmFileInstaller` abstraction

New interface at `src/palm/device/ipalmfileinstaller.h`:

```cpp
namespace WildPalms::PalmSync {

class IPalmFileInstaller
{
public:
    virtual ~IPalmFileInstaller() = default;
    /// Install `path` (.prc or .pdb) onto the device. Returns true
    /// on success. On failure, `errorMessage` (if non-null) gets a
    /// human-readable diagnostic.
    virtual bool installFile(const QString &path,
                              QString *errorMessage = nullptr) = 0;
};

}
```

Two concrete implementations:

- **`PilotLinkPalmFileInstaller`** at `src/palm/device/`. Constructed
  with a `KPilotLink*`; `installFile` opens the file with
  `pi_file_open`, calls `pi_file_install(pf, link->socketDescriptor(),
  0, nullptr)`, closes, and returns success. Same logic as legacy
  `InstallConduit::installFile` but extracted from the conduit. Lives
  in `WildPalmsCore` (matches `PilotLinkPalmDatabaseAccess`).
- **`MockPalmFileInstaller`** at `src/palm/sync/`. Records each
  `installFile(path)` call into an in-memory `QStringList`; supports
  setting per-call return values for failure-path tests. Sibling of
  `MockPalmDatabaseAccess`.

`PalmDeviceConnection` gains a new constructor parameter and accessor:

```cpp
PalmDeviceConnection(IPalmDatabaseAccess *device,
                     IPalmFileInstaller  *fileInstaller = nullptr,
                     QObject *parent = nullptr);

IPalmFileInstaller *fileInstaller() const;
```

When `fileInstaller == nullptr` (current call sites), `fileInstaller()`
returns `nullptr` and the install action's preconditions check fails
with a clear message. The runtime-app construction site
(`MainWindow` / `DeviceWorker`) wires the real `PilotLinkPalmFileInstaller`
into the connection at HotSync start.

**Why a new interface:** mixing `installFile(path)` into
`IPalmDatabaseAccess` blends abstraction layers — the latter is
record-shaped (load/create/update/delete records inside an existing
DB) whereas install is "deploy a whole DB from disk." Adding the
method blurs the contract for the four backends already implementing
`IPalmDatabaseAccess`.

**Alternatives considered:**
- (B) Extend `IPalmDatabaseAccess`. Rejected — see above.
- (C) `std::function<bool(QString)>` callback in the action's
  `parameters`. Rejected — every call site has to wire the callback;
  no compile-time type safety; harder to mock cleanly.

### Decision #3 — Collection install order is plugin-author's choice; ordering is presentation, not correctness

`InstallSourceCollector` iterates a backend's collections in the
order returned by `availableCollections()`. Plucker's
`availableCollections()` is updated to return `{bootstrap, channels}`
(currently `{channels, bootstrap}`) so the user-facing log shows
`SysZLib.prc → viewer_en.prc → channel-foo.pdb`.

**Why this is presentation-only:** during a HotSync the device is
frozen and inert. There is no scenario where installing a `.pdb`
before a `.prc` causes runtime breakage on the device; the only
edge case is a HotSync interrupted mid-stream leaving an orphan
`.pdb` without its viewer, which is cosmetic (the next sync installs
the viewer; the user can also delete the orphan manually).

Cross-plugin ordering — when multiple backend plugins each emit
installable items — falls out of `IBackendPlugin::runAfter()`
topological order (already used by the manager for sync ordering).
For E.15a we have only Plucker, so cross-plugin ordering is
moot; it inherits for free when a second plugin lands.

**Alternatives considered:**
- (B) Add `installOrder: int` to `Kalburator::Sync::CollectionInfo`.
  Rejected — upstream change for one consumer; field has no meaning
  to non-install drains. Violates `feedback_library_vs_backend_responsibility.md`.
- (C) Alphabetic by collection-id. Rejected — couples ordering to
  naming choices forever.

### Decision #4 — UI trigger deferred to E.17

The parent spec's E.17 row owns app-layer call-site migration —
specifically `kf6mainwindow`, `devicesession`, `deviceworker`,
`conflictdialog`, `conflictreviewwidget`, `interactiveconflicthandler`,
and `profile`. `PluginActionManager` and `PalmDeviceConnection`
construction are not yet wired into `KF6MainWindow` (existing
inline comments at `kf6mainwindow.cpp:538,625` confirm "lands in
E.15/E.17"). E.15a delivers the action + abstraction + collector;
**E.17 owns the menu wiring**.

The intended UI shape (recorded here so E.17 inherits it cleanly):
generic auto-populated `Tools → Actions →` submenu listing every
loaded `IPluginAction`. Selecting an entry:

1. Builds an `InstallSourceCollector::Result` (folder path from the
   active sync profile; backend-plugin-manager from the runtime).
2. Constructs `parameters = {"files": [...]}` JSON from the result.
3. Constructs a `SimpleActionContext`; wires `progress`/`message`
   signals into the existing log widget.
4. Runs `QtConcurrent::run([=]() { action->execute(ctx, device, params); })`.
5. On completion, calls `collector.moveSucceededToInstalled(result, succeededPaths)`.

For E.15a the action ships behind `WILDPALMS_INSTALL_PLUGIN_V2=ON`
without a UI trigger. Verifying the plugin against an in-memory
mock — exit criterion "action executes against mock device; progress
signals fire" — happens via `tst_installactionplugin`. The legacy
`InstallView` continues to provide drag-drop UX while V2 plumbing
matures.

**Alternatives considered:**
- (A) Land minimal MainWindow wiring in E.15a. Rejected — touches
  DeviceWorker/PluginActionManager construction not in scope; defers
  cleanly to E.17.
- (B) Hardcoded `File → Install Files…`. Rejected — every future
  action becomes custom UI work.
- (C) New "Install" tab/dock with drag-drop staging area. Rejected
  for E.15 ("not styled yet" per parent spec); legacy `InstallView`
  already provides this and stays available behind toggle OFF.

### Decision #5 — Installable-blob detection by record `type` field

`InstallSourceCollector` filters which collections to drain via the
record's `type` field (the `BackendRecord::type` string set by the
producing backend). For E.15a the predicate is:

```
type starts with "plucker-"
```

This catches both `plucker-bootstrap` and `plucker-pdb` (the two
types Plucker's blob backend emits). Future plugins emitting
installable items use a `*-pdb`/`*-prc`/`*-installable` suffix;
the collector accepts any of these.

**Refined predicate (final):**

```
type ends with "-pdb" OR type ends with "-prc" OR type starts with
"*-bootstrap"
```

The collector is conservative: collections whose first record's
`type` doesn't match are skipped. This prevents accidental drain of
record-shaped backends (e.g., calendar events) into the install
queue.

**Why a `type` predicate:** the backend record type is already
declared by every emitting backend and travels with the record.
Adding a new "installable" flag/marker would be redundant infra.

**Per-record file extension inference:**
- `type` ends with `"-prc"` → `.prc`
- `type` ends with `"-pdb"` → `.pdb`
- `type` starts with `*-bootstrap` → derive from `displayName`'s
  extension if present, else `.prc` (bootstrap PRCs are typically
  `.prc`-shaped: viewer apps).
- otherwise: `.pdb` (Plucker's primary case).

Filename for the temp file: `pluckerSanitizeDocFile(displayName) + ext`.

### Decision #6 — Action settings: none

`InstallActionPlugin::hasSettings() == false`. All input comes via
`parameters` JSON. The plugin is stateless; `loadSettings`/`saveSettings`
return empty.

**Why no settings:** folder path is a per-sync-profile concern
already managed by the host (`MainWindow` reads it from the active
profile); the drain-plugin-blobs flag is a UI-time choice (the menu
trigger always sets it true; advanced users can pass `false` via
parameters to fire a folder-only install). No state survives across
runs that the action itself owns.

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  src/plugins/install/  (NOT a submodule — lives in parent repo)  │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   InstallActionPlugin : QObject, WildPalms::IPluginAction        │
│       │  pluginId       = "install"                              │
│       │  displayName    = "Install Files"                        │
│       │  preconditions  = {requiresDeviceConnection: true}       │
│       │  execute(ctx, device, params)                            │
│       │      for f in params["files"]:                           │
│       │          if ctx->isCancelled(): break                    │
│       │          ok = device->fileInstaller()->installFile(...)  │
│       │          ctx->setCurrent(++i)                            │
│       │          ctx->log("...")                                 │
│       │      return all_succeeded                                │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
                               │
                               │ (fed by)
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  src/runtime/                                                    │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   InstallSourceCollector                                         │
│       │  collect(folderPath, BackendPluginManager*) -> Result    │
│       │      Result {                                            │
│       │          QList<FileEntry> files;                         │
│       │          QSharedPointer<QTemporaryDir> tempDir;          │
│       │          QStringList folderSourcedPaths; // post-move    │
│       │      }                                                   │
│       │  moveSucceededToInstalled(Result, QStringList ok_paths)  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
                               │
                               │ (calls)
                               ▼
┌──────────────────────────────────────────────────────────────────┐
│  src/palm/device/                                                │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   IPalmFileInstaller (interface)                                 │
│       virtual bool installFile(path, errorMsg) = 0;              │
│                                                                  │
│   PilotLinkPalmFileInstaller : IPalmFileInstaller                │
│       │  PilotLinkPalmFileInstaller(KPilotLink *link)            │
│       │  installFile(path, ...): pi_file_install(socket, ...)    │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│  src/palm/sync/  (mock side)                                     │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   MockPalmFileInstaller : IPalmFileInstaller                     │
│       installFile(path) -> records call; returns m_nextResult    │
│       installedPaths() -> QStringList                            │
│       setNextResult(bool success, QString error)                 │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘

   PalmDeviceConnection — gains fileInstaller() accessor +
                            constructor overload taking installer.
```

## Components

### `IPalmFileInstaller`

Pure abstract; one method (`installFile`). Lives in `src/palm/device/`
because the real implementation is the pisock wrapper that already
lives there alongside `PilotLinkPalmDatabaseAccess`.

### `PilotLinkPalmFileInstaller`

Holds a non-owning `KPilotLink*`. `installFile`:

```cpp
bool PilotLinkPalmFileInstaller::installFile(const QString &path,
                                              QString       *errorMessage)
{
    if (!m_link) {
        if (errorMessage) *errorMessage = "no link";
        return false;
    }
    pi_file_t *pf = pi_file_open(path.toLocal8Bit().constData());
    if (!pf) {
        if (errorMessage) *errorMessage = "pi_file_open failed";
        return false;
    }
    int rc = pi_file_install(pf, m_link->socketDescriptor(), 0, nullptr);
    pi_file_close(pf);
    if (rc < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("pi_file_install rc=%1").arg(rc);
        return false;
    }
    return true;
}
```

Verbatim port of legacy `InstallConduit::installFile`. Compiled into
`WildPalmsCore`.

### `MockPalmFileInstaller`

In-memory recorder. Tests configure pre-canned per-call return values:

```cpp
class MockPalmFileInstaller : public IPalmFileInstaller {
public:
    bool installFile(const QString &path, QString *errorMessage = nullptr) override;
    QStringList installedPaths() const { return m_paths; }
    void setNextResult(bool success, const QString &errorMsg = {});
    void setAllowAll(bool ok);  // permanently succeed (true) or fail (false)
private:
    QStringList m_paths;
    QQueue<QPair<bool, QString>> m_queue;  // success, error
    std::optional<bool> m_blanket;
};
```

Compiled into `WildPalmsPalmSync` next to `MockPalmDatabaseAccess`.

### `PalmDeviceConnection` (modified)

Adds:

```cpp
explicit PalmDeviceConnection(IPalmDatabaseAccess *device,
                               IPalmFileInstaller  *fileInstaller = nullptr,
                               QObject *parent = nullptr);

IPalmFileInstaller *fileInstaller() const { return m_fileInstaller; }
```

The single-arg constructor stays for backward compat; existing
test sites (e.g. PluckerBackendPlugin's createBackends) pass through.

### `InstallSourceCollector`

Lives at `src/runtime/installsourcecollector.{h,cpp}` (compiled into
`WildPalmsRuntime`).

```cpp
namespace WildPalms {

class InstallSourceCollector {
public:
    struct FileEntry {
        QString path;
        QString displayName;
    };

    struct Result {
        QList<FileEntry>                files;
        QSharedPointer<QTemporaryDir>   tempDir;     // empty if no plugin blobs
        QStringList                     folderSourcedPaths;
    };

    InstallSourceCollector() = default;

    /// Aggregate folder + plugin-blob sources. `folderPath` may be
    /// empty (skip folder scan). `manager` may be null (skip plugin
    /// scan).
    Result collect(const QString          &folderPath,
                   BackendPluginManager   *manager);

    /// Move folder-sourced files that succeeded into `installed/`
    /// subfolder. `succeededPaths` is the subset of `result.files`
    /// paths that the action reported as installed OK.
    void moveSucceededToInstalled(const Result      &result,
                                   const QStringList &succeededPaths);

    /// Test seam.
    void setSanitizer(std::function<QString(QString)> fn);

private:
    QList<FileEntry>  scanFolder(const QString &folderPath,
                                  QStringList    *outFolderPaths);
    QList<FileEntry>  drainPluginBlobs(BackendPluginManager *manager,
                                        QTemporaryDir       *dir);
    static bool       isInstallableType(const QString &type);
    static QString    inferExtension(const QString &type,
                                       const QString &displayName);
};

}
```

`collect()` flow:

1. If `folderPath` non-empty and exists: scan for `*.{prc,pdb}` (case-
   insensitive), append entries with `path = absoluteFilePath` and
   `displayName = fileName`. Track paths in `result.folderSourcedPaths`.
2. If `manager` non-null: iterate `manager->backends()`. For each
   backend, iterate `backend->blob()->availableCollections()`. For
   each collection, load records via `loadRecords`. If any record's
   `type` matches `isInstallableType`, write each record's `data` to
   `tempDir/<sanitized-name><inferred-ext>`. Append entries.
3. Return assembled `Result`.

`moveSucceededToInstalled()` flow: for each path in the intersection
of `succeededPaths` and `folderSourcedPaths`, move from `<folder>/X`
to `<folder>/installed/X`, creating `installed/` if absent. Mirrors
legacy `InstallConduit::moveToInstalled`.

`isInstallableType()` matches `*-prc`, `*-pdb`, `*-bootstrap`.

### `InstallActionPlugin`

```cpp
class InstallActionPlugin : public QObject, public WildPalms::IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)
public:
    explicit InstallActionPlugin(QObject *parent = nullptr);

    QString pluginId()    const override { return QStringLiteral("install"); }
    QString displayName() const override { return QStringLiteral("Install Files"); }
    QString description() const override
    { return QStringLiteral("Install .prc / .pdb files onto the connected Palm"); }
    QString version()     const override { return QStringLiteral("2.0.0"); }
    QIcon   icon()        const override
    { return QIcon::fromTheme(QStringLiteral("document-import")); }

    bool hasSettings()    const override { return false; }

    bool execute(ActionContext       *ctx,
                  PalmDeviceConnection *device,
                  const QJsonObject   &parameters) override;

    Preconditions preconditions() const override
    {
        return { /*requiresDeviceConnection=*/ true,
                  /*requiresFiles=*/             {} };
    }
};
```

`execute()` flow:

```cpp
bool InstallActionPlugin::execute(ActionContext       *ctx,
                                    PalmDeviceConnection *device,
                                    const QJsonObject   &params)
{
    auto *installer = device ? device->fileInstaller() : nullptr;
    if (!installer) {
        if (ctx) ctx->log("Install: no file installer available");
        return false;
    }

    const QJsonArray files = params.value("files").toArray();
    if (ctx) ctx->setTotal(files.size());

    int succeeded = 0;
    int failed    = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (ctx && ctx->isCancelled()) {
            if (ctx) ctx->log(QStringLiteral("Install: cancelled at %1/%2")
                               .arg(i).arg(files.size()));
            break;
        }
        const QJsonObject f = files[i].toObject();
        const QString path  = f.value("path").toString();
        const QString name  = f.value("display_name").toString();
        QString err;
        const bool ok = installer->installFile(path, &err);
        if (ok) {
            ++succeeded;
            if (ctx) ctx->log(QStringLiteral("Installed: %1").arg(name));
        } else {
            ++failed;
            if (ctx) ctx->log(QStringLiteral("Failed: %1 (%2)").arg(name, err));
        }
        if (ctx) ctx->setCurrent(i + 1);
    }
    if (ctx) ctx->log(QStringLiteral("Install: %1 succeeded, %2 failed")
                       .arg(succeeded).arg(failed));
    return failed == 0;
}
```

`execute()` returns `true` only when all files installed
successfully. Cancellation mid-loop returns `false` (cancelled is a
non-success outcome from the action's perspective).

The action does not move files to `installed/`. That's the caller's
job (using `InstallSourceCollector::moveSucceededToInstalled` with
the list of paths the action reported as successful).

To bridge this — the action can't return individual per-file outcomes
through the bool return — we add a signal:

```cpp
Q_SIGNALS:
    void fileInstalled(const QString &path);
    void fileFailed(const QString &path, const QString &errorMessage);
```

The UI/caller subscribes, accumulates the success list, and passes
it to `moveSucceededToInstalled()` after `execute()` returns.

### Manifest — `install-action-plugin.json`

```json
{
    "KPlugin": {
        "Name": "Install Files",
        "Description": "Install .prc / .pdb files onto the connected Palm",
        "Icon": "document-import",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0.0",
        "Category": "Sync",
        "Id": "install"
    },
    "X-WildPalms-PluginType":   "action",
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder":     50
}
```

`X-WildPalms-PluginType: "action"` — distinguishes from `"backend"`,
filtered by `PluginActionManager` at discovery.

### UI — deferred to E.17

E.15a does not modify `KF6MainWindow`. The intended wiring shape is
documented in Decision #4; E.17's plan inherits it. The
`InstallSourceCollector` API shape is fixed at this phase so E.17 only
has to wire it.

### Plucker `availableCollections()` order swap

`src/plugins/plucker/pluckerblobbackend.cpp` — return
`{bootstrap, channels}` instead of `{channels, bootstrap}` so the
collector logs SysZLib + viewer first. One-line fix in E.15a Task 1.

## Data flow

1. **Plugin construction** (per app start): `PluginActionManager`
   discovers `wildpalms/plugins/*` filtered by
   `X-WildPalms-PluginType == "action"`. Loads
   `InstallActionPlugin`. Adds `Tools → Actions → Install Files…`
   menu entry.
2. **User triggers menu** entry:
   - MainWindow constructs `InstallSourceCollector`.
   - Collector scans `<sync>/install/` → finds 0+ folder files.
   - Collector iterates `BackendPluginManager`'s loaded plugins:
     - For Plucker: enumerates `plucker:bootstrap` (writes
       SysZLib.prc, viewer_en.prc to tempdir) and `plucker:channels`
       (spiders due channels via `PluckerFetcher`, writes resulting
       .pdb bytes to tempdir).
   - Returns `Result{files: [...], tempDir, folderSourcedPaths}`.
3. **MainWindow runs action** on QtConcurrent worker:
   - For each `files[i]`: `installer->installFile(path)` (mock or
     pisock); emit `fileInstalled` or `fileFailed`; check
     `ctx->isCancelled()` between each.
   - On finish, MainWindow calls
     `collector.moveSucceededToInstalled(result, succeededList)`.
4. **`Result.tempDir` destructs** → temp files cleaned up.

## Error handling

- **No file installer wired** (`device->fileInstaller() == nullptr`):
  action returns `false`, logs "no file installer available". UI
  shows "device not ready for install".
- **`pi_file_install` rc < 0** for one file: log per-file failure,
  continue installing remaining files. Action returns `false` (since
  `failed > 0`) but partial successes are recorded via
  `fileInstalled` signals so move-to-installed still happens for
  successful files.
- **Empty input list** (`params.files.size() == 0`): action returns
  `true` (vacuous success), logs "no files to install". UI shows a
  status-bar message.
- **Cancellation mid-loop**: action returns `false`, logs "cancelled
  at N/M". Already-succeeded files get moved to `installed/`.
- **Folder doesn't exist**: collector returns empty folder list, no
  error.
- **Plugin blob fetch fails** (e.g., Plucker can't spider a channel):
  collector skips that record per channel (already handled by
  `PluckerBlobBackend::loadChannelRecords`); no record reaches the
  install list; no install attempt; no error surfaced to install
  action.

## Testing

Five test executables under `tests/plugins/install/`:

| Test                            | Coverage                                                                 |
|---------------------------------|--------------------------------------------------------------------------|
| `tst_palmfileinstaller`         | `MockPalmFileInstaller` records calls, returns canned values; `PilotLinkPalmFileInstaller` smoke test (skipped without `WILDPALMS_TEST_REAL_DEVICE` env var, à la E.18 deferral) |
| `tst_installsourcecollector`    | Folder scan (real tmpdir fixtures); plugin-blob aggregation against a mock backend manager; `isInstallableType` predicate; `moveSucceededToInstalled` |
| `tst_installactionplugin`       | `execute()` against `MockPalmFileInstaller`; progress signal counts; cancellation respected; empty-list returns true; no-installer returns false |
| `tst_install_v2_e2e`            | Folder + Plucker-mock backend → collector → action → mock installer; verify both bootstrap PRCs and channel .pdbs reach the installer in declared order |
| `tst_palmdeviceconnection_installer` | One new test case in existing `tst_palmdeviceconnection`: constructor-with-installer round-trip; null-installer path |

The e2e test composes the same pieces the runtime composes, against
mocks; the missing piece (live `pi_file_install` against POSE64) lands
in E.18.

## Scope excluded

The following are explicitly **not** in E.15a:

- **fullsync → runtime relocation**. Lands in E.15b; this spec is
  self-contained without it.
- **Live-device POSE64 integration**. E.18.
- **Legacy `InstallConduit` removal**. E.16.
- **Legacy `InstallView` removal**. E.16/E.17. The drag-drop staging
  area is no longer the primary UX once V2 ships, but the widget
  stays buildable behind `WILDPALMS_INSTALL_PLUGIN_V2=OFF` until the
  legacy IConduit family deletes.
- **Per-plugin enable/disable for the install drain**. The collector
  unconditionally drains every loaded backend plugin's installable
  collections. If a future user wants to skip Plucker drain on a
  particular install run, they pass an explicit `files[]` list via
  parameters (no UI for this in E.15a; an API hook exists).
- **Worker-thread plumbing helper**. The `QtConcurrent::run` +
  `QFutureWatcher` glue lives directly in MainWindow's
  `runActionWithCollector()` for E.15a. If a third action joins
  later (Backup, Restore), extracting a helper becomes worthwhile.
- **Settings widget**. `hasSettings() == false`.
- **Toolbar button**. Menu only for E.15a; toolbar wiring is a
  styling concern post-E.18.
- **Action progress UI** beyond the existing log widget. Progress
  bar / cancel button live in the log dock area as plain
  `setCurrent`/`setTotal` text updates for now.
- **Per-record-type install policy**. `isInstallableType` is
  hardcoded to `*-prc`/`*-pdb`/`*-bootstrap`. No metadata-driven
  configuration.

## Files touched

```
NEW (parent repo):
  src/palm/device/ipalmfileinstaller.h
  src/palm/device/pilotlinkpalmfileinstaller.{h,cpp}
  src/palm/sync/mockpalmfileinstaller.{h,cpp}
  src/runtime/installsourcecollector.{h,cpp}
  src/plugins/install/installactionplugin.{h,cpp}
  src/plugins/install/install-action-plugin.json

NEW (parent repo, tests):
  tests/plugins/install/CMakeLists.txt
  tests/plugins/install/tst_palmfileinstaller.cpp
  tests/plugins/install/tst_installsourcecollector.cpp
  tests/plugins/install/tst_installactionplugin.cpp
  tests/plugins/install/tst_install_v2_e2e.cpp
  tests/plugins/install/fixtures/dummy.prc      (4 bytes, content irrelevant)
  tests/plugins/install/fixtures/dummy.pdb      (4 bytes)

MODIFIED (parent repo):
  src/palm/palmdeviceconnection.{h,cpp}    add fileInstaller() accessor + ctor overload
  src/palm/device/CMakeLists.txt           build pilotlinkpalmfileinstaller
  src/palm/sync/CMakeLists.txt             build mockpalmfileinstaller
  src/runtime/CMakeLists.txt               build installsourcecollector
  src/plugins/install/CMakeLists.txt       add WILDPALMS_INSTALL_PLUGIN_V2 toggle
  tests/plugins/CMakeLists.txt             add_subdirectory(install)
  tests/plugins/dummy_action/...           untouched (separate test fixture)

NOT MODIFIED IN E.15a (deferred to E.17):
  src/kf6/kf6mainwindow.{h,cpp}            Tools → Actions submenu + worker plumbing

MODIFIED (plucker submodule):
  src/plugins/plucker/pluckerblobbackend.cpp  swap availableCollections() order to {bootstrap, channels}

UNCHANGED:
  src/plugins/install/installconduit.{h,cpp}    (legacy, lives until E.16)
  src/plugins/install/installview.{h,cpp}       (legacy, lives until E.16)
  src/plugins/install/install-conduit.json      (legacy, lives until E.16)
  src/fullsync/**                                (relocation is E.15b)
```

## Acceptance criteria

- [ ] `cmake --build build-dev --target wildpalms_install_v2_action` succeeds
      with `WILDPALMS_INSTALL_PLUGIN_V2=ON` (default).
- [ ] `cmake -DWILDPALMS_INSTALL_PLUGIN_V2=OFF -B build-legacy && cmake
      --build build-legacy --target wildpalms_install` succeeds (legacy
      `InstallConduit` still buildable).
- [ ] `ctest --preset dev` passes for all 5 new test executables.
- [ ] WP `ctest` baseline (current 72 pre-E.15a) does not regress; new
      total = 72 + 5 = 77.
- [ ] libkalburator `ctest` baseline unchanged (no upstream changes
      land in E.15a).
- [ ] Memory index entry for E.15a added at session-end.

## Spec exit gate

The exit gate from the parent spec row E.15 is: *"Action executes
against mock device; progress signals fire."* This spec maps that to:
`tst_installactionplugin` passes with `MockPalmFileInstaller` recording
file paths and `SimpleActionContext` emitting `progress(current, total)`
signals (verified via `QSignalSpy`).

The end-to-end story — Plucker → install → mock device — is verified
by `tst_install_v2_e2e`. POSE64 live coverage lands in E.18.

---

**Author:** Claude (E.15a brainstorming session, 2026-04-26)
