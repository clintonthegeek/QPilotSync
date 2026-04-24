# Phase E.9 — Memo plugin (first new-ABI IBackendPlugin)

**Status:** Design approved 2026-04-23. Ready for implementation plan.

**Parent spec:** `2026-04-21-phase-e-plugin-abi-rewrite-design.md`
(§"Plugin ABI" lines 170–314, §"Sub-phases" row E.9 line 587).
This document refines the sub-phase's design; for the overall Phase-E
architecture see the parent.

**Dependencies:**

- E.7 (typed codecs + adapters) — landed 2026-04-23. Provides
  `src/palm/codecs/memocodec.h` and `src/palm/adapters/palmmemosadapter.h`.
- E.8 (plugin ABI + managers) — landed 2026-04-23. Provides
  `src/core/iplugin.h`, `src/core/ibackendplugin.h`, and
  `src/runtime/backendpluginmanager.{h,cpp}`.
- E.6 (PalmCalendarBackend + CategoryMappingStore) — landed
  2026-04-21. Establishes the wrapper-over-PalmBackend pattern that
  memo mirrors.

**Exit gate (per parent spec row E.9):** WP `ctest` passes. New
`tst_memo_v2` exercises the memo plugin's end-to-end round-trip
against `PalmBackend` + `LocalBlobBackend` via `BlobSyncEngine`.

---

## Intent

Rewrite the Memo conduit as the first plugin on the new ABI, proving
the ABI on a boring record type before the calendar-typed adapter
layers in (E.10). Ship it behind a CMake toggle so the existing
`MemoConduit`-based plugin keeps working until E.16 deletes both the
old surface and the toggle.

Three secondary outcomes fall out of this sub-phase:

1. **`PalmDeviceConnection` acquires a concrete definition.** E.8 left
   it forward-declared; the first real plugin needs it concrete to
   reach the shared `PalmBackend`. E.9 lands the concrete type in
   `src/palm/`.
2. **`IBackendPlugin` grows optional view and conflict-presentation
   hooks** so that `MemoView` continues to appear in the main window's
   tab bar and `ConflictDialog` keeps rendering memo records.
3. **The wrapper-over-PalmBackend pattern is formalised for blob
   targets** (PalmCalendarBackend already did it for typed
   SyncBackend consumers). Future plugins (ToDo, Contacts) replicate
   the shape; libkalburator stays format-agnostic.

## Decisions recorded during brainstorm (2026-04-23)

| # | Decision | Rationale |
|---|---|---|
| 1 | `PalmBackend` stays byte-passing; memo plugin provides a `MemoBlobBackend` wrapper that transcodes Palm wire bytes ↔ Markdown on the fly. | The landed E.3 `PalmBackend` stores raw Palm bytes in `BackendRecord::data` (`palmbackend.cpp:39-46`). Adding a codec registry would centralise plugin formats in the shared backend; the wrapper pattern already exists (E.6's `PalmCalendarBackend`). Keeps `PalmBackend` format-agnostic. |
| 2 | Shared `PalmBackend` reached via a concrete `PalmDeviceConnection` — plugins call `device->palmBackend()` inside `createBackends(host, device)`. No `IBackendPlugin` signature change. | `ISyncHost` is upstream; can't extend it. `PalmDeviceConnection` is WP-owned and currently forward-declared — E.9 is the natural moment to make it concrete. Aligns with parent spec's R2 (spec line 686). |
| 3 | `IBackendPlugin` grows view hooks (`hasMainView`, `createMainView`, `mainViewName`, `mainViewIcon`) and conflict presentation hooks (`enrichConflictSnapshot`, `formatConflictRecordHtml`). `IPlugin` (base) is untouched. | Actions don't produce main views or conflicts; backend plugins do. Keeps `IPluginAction` lean. All new hooks have no-op defaults so the dummy-backend fixture from E.8 keeps compiling. |
| 4 | Semantic `recordsEqual()` is dropped. The sync engine uses hash equality; the memo plugin guarantees round-trip byte identity via careful normalisation in the Markdown encoder. | `BlobSyncEngine::twoWayWithBaseline` is hash-based. The old conduit's Unfiled/category-casing quirks become the encoder's problem, not the engine's. Matches `memory/feedback_library_vs_backend_responsibility.md`. |
| 5 | Memo's Markdown frontmatter uses the **integer slot** as the authoritative `category` value (0..15), not the Palm category name. A human-readable `categoryName` field is emitted alongside when a `CategoryMappingStore` is supplied. Parsing accepts either. | Slot is stable; names rename. Keeps round-trip hash-identical regardless of whether the app has loaded a category map yet. Future sub-phase can flip the default if a strong reason appears. |
| 6 | `MemoBlobBackend::backendId() == "palm-memo"`. Conflict handlers register under that id. For E.9, no memo-specific handler — the default (`PalmConflictHandler` registered under `"palm"`) handles it as fallback. | Distinct backend id keeps future memo-specific handler registration clean. Default-fallback is how `ConflictHandlerRegistry` is already documented to behave. |
| 7 | The old `MemoConduit`, `memomapper.{h,cpp}`, and `memo-conduit.json` stay in tree until E.16. A CMake option `WILDPALMS_MEMO_PLUGIN_V2` (default ON) switches between old-and-new plugin builds; `MemoView` is shared between both. | Spec line 587 calls for the toggle. Dual-install across `wildpalms/conduits` and `wildpalms/plugins` would let `ConduitManager` and `BackendPluginManager` both pick up a memo — not desirable before E.16's full cutover. |
| 8 | `CategoryMappingStore` keeps its current home at `src/palm/calendar/categorymappingstore.{h,cpp}`. Memo uses it by database key `"MemoDB"`. A rename/move (e.g. to `src/palm/palmcategorymap.h`) is deferred until a third consumer needs it. | Store is already keyed by Palm dbName and is calendar-agnostic in shape. Premature generalisation; rename when contacts/todos actually use it (E.11/E.12). |
| 9 | `tst_memo_v2` drives the engine directly (`BlobSyncEngine::twoWayWithBaseline`), not `SyncCoordinator`. Coordinator-level end-to-end coverage lands in E.18 alongside the POSE64 integration tests. | Matches existing smoke-test style (`tst_palmbackend_roundtrip`, `tst_palmdevice_roundtrip`). Coordinator adds signal-forwarding + QEventLoop without buying E.9 additional correctness signal. |

---

## Architecture

### End-state at run time

```
   Application / UI
          │
          │ constructs at startup
          ▼
┌──────────────────────────────┐       ┌─────────────────────┐
│ PalmDeviceConnection         │ owns  │ PalmBackend         │
│  (concrete in E.9)           ├──────▶│  (IBlobBackend,     │
│  src/palm/palmdeviceconnect  │       │   raw Palm bytes,   │
│                              │       │   all device DBs)   │
│  .palmBackend() ─────────────┼───┐   └─────────────────────┘
│  .device()  (IPalmDBAccess)  │   │           ▲
└──────────────────────────────┘   │           │ borrows
          │                        │           │
          │ passed to each plugin  │           │
          ▼                        │           │
┌──────────────────────────────┐   │   ┌──────────────────────┐
│ BackendPluginManager         │   └──▶│ MemoBlobBackend      │
│   └── MemoBackendPlugin      │       │  src/plugins/memo/   │
│        .createBackends(host, │──────▶│  IBlobBackend        │
│                       device)│       │  exposes palm:memo   │
│                              │       │  transcodes bytes    │
│                              │       │  via MemoCodec +     │
│                              │       │  MemoMarkdown        │
│                              │       └──────────────────────┘
└──────────────────────────────┘              │
          │                                   │ registered with
          │                                   ▼
          │                           ┌──────────────────────┐
          └──────────────────────────▶│ SyncCoordinator /    │
                                      │ BlobSyncEngine       │
                                      │ ::twoWayWithBaseline │
                                      └──────────────────────┘
                                              ▲
                                              │ SyncMapping target
                                              │
                                      ┌──────────────────────┐
                                      │ LocalBlobBackend     │
                                      │  (upstream; writes   │
                                      │   .md files to disk) │
                                      └──────────────────────┘
```

### Read-path (Palm → Markdown file)

1. Engine calls `MemoBlobBackend::loadRecords("palm:memo")`.
2. `MemoBlobBackend` delegates to `PalmBackend::loadPalmRecords("MemoDB")`
   — the category-aware escape hatch from `palmbackend.h:80-85` —
   because memo needs the `PalmRecord::category` slot and the
   `BackendRecord` public API does not expose it.
3. For each `PalmRecord`:
   - `MemoCodec::decodeMemo(pr.data)` → `PalmCodecs::Memo { text, isPrivate }`.
   - `MemoMarkdown::encode(memo, pr.category, categoryName)` →
     Markdown string with YAML frontmatter. `categoryName` is
     resolved via `CategoryMappingStore::slotName("MemoDB", slot)`
     when a store is supplied (empty otherwise; the frontmatter then
     omits the `categoryName` key).
   - Build a `BackendRecord { id = "palm:memo:<recordId>",
     type = "memos", data = markdownBytes,
     contentHash = sha256(data), lastModified = pr.lastModified }`.
4. Return the list. Engine diffs against baseline + target.

### Write-path (Markdown file → Palm)

1. Engine calls `MemoBlobBackend::createRecord` or `updateRecord`
   with a markdown-bytes `BackendRecord`.
2. `MemoMarkdown::decode(markdownBytes)` → POD
   `{ Memo content, int slot, optional<QString> categoryName }`.
   Acceptance rules:
   - `category: <int>` → used as slot directly.
   - `category: <string>` → looked up in `CategoryMappingStore` for
     `"MemoDB"`; falls back to slot 0 if unresolved.
   - Missing `category:` → slot 0 (Unfiled).
3. Build a `PalmRecord { recordId = decoded id or 0,
   category = slot, data = MemoCodec::encodeMemo(content),
   isPrivate = content.isPrivate, lastModified = now }`.
4. Delegate to `PalmBackend::createPalmRecord("MemoDB", pr)` or
   (for updates) go via `PalmBackend::updateRecord(BackendRecord)`
   after re-encoding the updated bytes. For the category-bearing
   update path, `MemoBlobBackend` calls a new thin helper
   `PalmBackend::updatePalmRecord("MemoDB", pr)` that mirrors
   `createPalmRecord` and exists because `updateRecord(BackendRecord)`
   cannot carry a category slot.

### Hash-stability normalisation

For the engine to recognise "same memo after round-trip" the Markdown
bytes a memo produces when it came from Palm must byte-match the
Markdown bytes it produces when it came from a local file that was
decoded back to a `Memo` POD and re-encoded. The encoder achieves
this with three rules:

1. **Canonical key order** in the frontmatter:
   `id`, `category`, `categoryName`, `private`.
2. **Omit keys** whose value is the default: no `category:` line for
   slot 0 unless a `categoryName` is supplied; no `private:` line
   when `false`; no `categoryName:` when the store has no name for
   the slot.
3. **Body trailing newline:** exactly one `\n` after the body. Palm
   records may lack it; markdown files conventionally have it. The
   encoder always adds exactly one; the decoder accepts zero or one.

The old `memomapper.cpp` emitted a `created: <now>` timestamp. That
would defeat hash equality (timestamp changes per round-trip). E.9
**drops the `created:` field** — the Palm record's `lastModified`
already flows through `BackendRecord::lastModified` for change-
tracking purposes; storing a creation time in the body is redundant
and harmful to hash stability.

### Conflict handling

- `MemoBlobBackend::backendId() == "palm-memo"`. No memo-specific
  `ConflictHandler` is registered; the coordinator's handler registry
  falls back to the default handler, which in this runtime is
  `PalmConflictHandler` registered under `"palm"`.
- `MemoBackendPlugin::enrichConflictSnapshot(snapshot, isSourceSide)`
  is the E.9 port of the old conduit method of the same name:
  - If `isSourceSide` is true, `snapshot.content` holds raw Palm
    bytes — decode via `MemoCodec`, replace content with the memo
    text as UTF-8.
  - Extract the first line (≤ 60 chars, with ellipsis) and store as
    `snapshot.metadata["title"]`.
  - Set `snapshot.contentType = "text/plain"`.
- `MemoBackendPlugin::formatConflictRecordHtml(snapshot)` renders
  `<h3>title</h3><pre>content</pre>`, matching the old conduit.

`ConflictDialog` (the caller) changes in a small follow-up to look up
the plugin by backend id (`"palm-memo"`) through `BackendPluginManager`
rather than by conduit id through `ConduitManager`. For E.9 this is
scoped to the new memo path; the conflict dialog itself keeps working
against the old `ConduitManager` path for the other plugins until
E.16.

### View wiring

- `MemoView` (`src/plugins/memo/memoview.{h,cpp}`) stays unchanged —
  it's a standalone widget that reads from a filesystem path.
- `MemoBackendPlugin::hasMainView()` returns `true`;
  `createMainView(parent)` returns `new MemoView(parent)`;
  `mainViewName()` returns `"Memos"`; `mainViewIcon()` returns the
  themed `view-pim-notes` icon.
- `kf6mainwindow.cpp:537` (current conduit-view loop) grows a
  parallel loop over `BackendPluginManager::plugins()` that calls
  the same four methods. Old-path and new-path plugins coexist in
  the tab bar until E.16 retires `ConduitManager`.

### Settings

- `MemoBackendPlugin::hasSettings() == false` for E.9. The old
  conduit had no settings UI either; no regression.
- `loadSettings`/`saveSettings` return `{}` and no-op. Future
  sub-phases may add memo-specific settings (e.g. sync folder
  layout) via these hooks.

---

## File layout

### Files to CREATE

```
src/plugins/memo/
├── memobackendplugin.h           New IBackendPlugin
├── memobackendplugin.cpp
├── memoblobbackend.h             IBlobBackend wrapper over PalmBackend
├── memoblobbackend.cpp
├── memomarkdown.h                POD Memo ↔ Markdown (extracted from memomapper)
├── memomarkdown.cpp
└── memo-backend-plugin.json      New manifest (X-WildPalms-PluginType: "backend")

src/palm/
├── palmdeviceconnection.h        Concrete; was forward-declared
└── palmdeviceconnection.cpp

tests/plugins/memo/
├── CMakeLists.txt
└── tst_memo_v2.cpp               Plugin-factory + engine round-trip
```

### Files to MODIFY

- `src/core/ibackendplugin.h` — add view hooks (`hasMainView`,
  `createMainView`, `mainViewName`, `mainViewIcon`) and conflict
  hooks (`enrichConflictSnapshot`, `formatConflictRecordHtml`). All
  six have no-op defaults. Forward-declare
  `Kalburator::Sync::QSyncCore::RecordSnapshot`.
- `src/plugins/memo/CMakeLists.txt` — add `WILDPALMS_MEMO_PLUGIN_V2`
  option (default `ON`). When on: build new plugin into
  `wildpalms/plugins` namespace; exclude `memoconduit.*` sources.
  When off: build old conduit into `wildpalms/conduits` namespace as
  today; exclude new plugin sources.
- `src/palm/sync/palmbackend.h` / `palmbackend.cpp` — add
  `updatePalmRecord(dbName, PalmRecord)` mirroring the existing
  `createPalmRecord` category-aware helper.
- `src/palm/CMakeLists.txt` — add
  `palmdeviceconnection.{h,cpp}` to `WildPalmsPalmSync` (or a new
  tiny static lib `WildPalmsPalmRuntime` if circularity forces it;
  see Risks R1).
- `src/kf6/kf6mainwindow.cpp` — parallel loop for
  `BackendPluginManager::plugins()` producing the same
  `KPageWidgetItem` entries that the conduit loop does.
- `src/runtime/CMakeLists.txt` — link `WildPalmsPalmSync` (or the
  new runtime lib) so that `BackendPluginManager`'s consumers have
  `PalmDeviceConnection` in scope.
- `tests/CMakeLists.txt` — add `add_subdirectory(plugins/memo)`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
  — flip row E.9 to `✅ **E.9**` once landed.
- `docs/plans/2026-04-20-libkalburator-integration.md` — mark E.9
  landed in the Phase E sub-phases table.

### Files to LEAVE UNTOUCHED

- `src/plugins/memo/memoconduit.{h,cpp}`,
  `src/plugins/memo/memomapper.{h,cpp}`,
  `src/plugins/memo/memo-conduit.json` — kept until E.16 behind the
  CMake toggle.
- `src/plugins/memo/memoview.{h,cpp}` — shared by both plugin
  versions; no changes.
- `tests/test_memomapper.cpp` — retained verbatim. Tests the old
  mapper; new plugin has its own coverage.
- `src/core/iplugin.h` — base stays lean; only `IBackendPlugin` grows.
- `src/core/ipluginaction.h` — actions don't gain view/conflict hooks.
- `pilot-link/`, `pilot-link-git/` — read-only per
  `PROJECT_VISION.md:105`.

---

## `IBackendPlugin` extensions

Added to `src/core/ibackendplugin.h`, all with sensible defaults so
the dummy-backend fixture from E.8 keeps compiling:

```cpp
// Forward declaration in the Kalburator::Sync namespace block:
namespace Kalburator::Sync::QSyncCore {
    struct RecordSnapshot;
}

class IBackendPlugin : public IPlugin
{
    // ... existing members ...

    // ========== Main view surface ==========
    //
    // Returns a dockable main-window widget (e.g. MemoView, CalendarView).
    // Default: no view.
    virtual bool     hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent)
    {
        Q_UNUSED(parent)
        return nullptr;
    }
    virtual QString mainViewName() const { return {}; }
    virtual QIcon   mainViewIcon() const { return {}; }

    // ========== Conflict presentation ==========
    //
    // enrichConflictSnapshot: mutate `snapshot` so downstream HTML
    //   rendering has normalised `content`/`metadata`/`contentType`.
    //   `isSourceSide` tells the plugin whether the snapshot holds
    //   its own wire format (true) or a target-backend format (false).
    //
    // formatConflictRecordHtml: produce an HTML string for the
    //   ConflictDialog's detail pane. Defaults to a minimal
    //   `<pre>` wrapper around UTF-8-decoded `content`.
    virtual void enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const
    {
        Q_UNUSED(snapshot)
        Q_UNUSED(isSourceSide)
    }
    virtual QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const;
        // Default implementation defined out-of-line in ibackendplugin.cpp
        // (since it references `snapshot.content`).
};
```

Adding a `.cpp` compilation unit for the default
`formatConflictRecordHtml` implementation is the only reason
`IBackendPlugin` needs a source file; everything else stays
header-only. The new `.cpp` is added to `WildPalmsCore`'s source list.

The IID string (`"ca.vibekoder.WildPalms.IBackendPlugin/1.0"`) stays
at `1.0` — the ABI is internal-only (no third-party plugins per
spec decision #3); all in-tree plugins rebuild together. A bump to
`1.1` happens only if the SDK ever ships externally (deferred).

---

## `PalmDeviceConnection` concrete shape

Placed at `src/palm/palmdeviceconnection.{h,cpp}` so it sits
alongside `devicesession.cpp`, `palmdevicemonitor.cpp`, and the
other device-layer types.

```cpp
// src/palm/palmdeviceconnection.h
namespace WildPalms::PalmSync { class PalmBackend; class IPalmDatabaseAccess; }

class PalmDeviceConnection : public QObject {
    Q_OBJECT
public:
    // Takes ownership of nothing; device must outlive this.
    // Constructs and owns one PalmBackend wrapping `device`.
    explicit PalmDeviceConnection(
        WildPalms::PalmSync::IPalmDatabaseAccess *device,
        QObject *parent = nullptr);
    ~PalmDeviceConnection() override;

    WildPalms::PalmSync::IPalmDatabaseAccess *device() const;
    WildPalms::PalmSync::PalmBackend         *palmBackend() const;

signals:
    void connected();
    void disconnected();

private:
    WildPalms::PalmSync::IPalmDatabaseAccess *m_device = nullptr;
    WildPalms::PalmSync::PalmBackend         *m_palmBackend = nullptr;
};
```

E.9 does not wire up the `connected/disconnected` signals — they're
declared for future runtime wiring (E.15/E.17). Today's device
lifecycle is managed elsewhere (`devicesession`), and the spec's
runtime-collapse work will thread those signals through later.

Placement in global namespace (not `WildPalms::`) matches the
forward declaration in `src/core/ibackendplugin.h:18`.

---

## Memo plugin internals

### `MemoBlobBackend` (new)

```cpp
namespace WildPalms::Memo {

class MemoBlobBackend : public Kalburator::Sync::IBlobBackend {
    Q_OBJECT
public:
    // categoryStore is optional; when null, encoder omits categoryName
    // and decoder falls back to slot 0 on name-only frontmatter.
    explicit MemoBlobBackend(
        WildPalms::PalmSync::PalmBackend *palmBackend,
        WildPalms::PalmCalendar::CategoryMappingStore *categoryStore = nullptr,
        QObject *parent = nullptr);

    // --- Identity ---
    QString backendId()    const override { return QStringLiteral("palm-memo"); }
    QString displayName()  const override { return QStringLiteral("Palm Memos"); }
    bool    isAvailable()  const override;

    // --- Collections (exactly one: palm:memo) ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo         collectionInfo(
        const QString &collectionId) override;
    QString createCollection(
        const Kalburator::Sync::CollectionInfo &) override; // not supported

    // --- Records (transcoding layer over PalmBackend) ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(
        const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(
        const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &r) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord &r) override;
    bool    deleteRecord(const QString &recordId) override;

    // --- Change detection (delegates to PalmBackend) ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId,
                             const QDateTime &since) override;
    bool supportsDeleteTracking() const override;

private:
    WildPalms::PalmSync::PalmBackend              *m_palmBackend = nullptr;
    WildPalms::PalmCalendar::CategoryMappingStore *m_categoryStore = nullptr;
};

} // namespace WildPalms::Memo
```

The transcoding pattern matches `PalmCalendarBackend` closely:
every call delegates to `m_palmBackend` with `"MemoDB"` / `"palm:memo"`
and runs Palm bytes through `MemoCodec` + `MemoMarkdown` on either
direction.

### `MemoMarkdown` (new; factored out of `memomapper.cpp`)

```cpp
namespace WildPalms::Memo {

struct MarkdownMemo {
    WildPalms::PalmCodecs::Memo content;
    int                         categorySlot = 0;
    std::uint32_t               recordId = 0;
    std::optional<QString>      categoryName; // decorative
};

QString encode(const MarkdownMemo &memo);
MarkdownMemo decode(const QString &markdown);

// Filename derivation (preserved from memomapper::generateFilename,
// factored so the plugin and the view widget can both use it).
QString filenameFor(const MarkdownMemo &memo);

} // namespace WildPalms::Memo
```

`MemoMarkdown` has **no** Palm wire-format code (that's `MemoCodec`)
and **no** Windows-1252 transcoding (that lives in `MemoCodec`). It
operates purely on the POD and a QString.

### `MemoBackendPlugin` (new)

```cpp
namespace WildPalms::Memo {

class MemoBackendPlugin : public QObject, public WildPalms::IBackendPlugin {
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit MemoBackendPlugin(QObject *parent = nullptr);

    // IPlugin
    QString pluginId()    const override { return QStringLiteral("memo"); }
    QString displayName() const override { return QStringLiteral("Memos"); }
    QIcon   icon()        const override
        { return QIcon::fromTheme(QStringLiteral("view-pim-notes")); }
    QString description() const override
        { return QStringLiteral("Synchronizes Palm MemoDB with Markdown files"); }
    QString version()     const override { return QStringLiteral("2.0"); }

    // IBackendPlugin - database claim
    QStringList claimedDatabases() const override { return {"MemoDB"}; }

    // IBackendPlugin - backend construction
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                    PalmDeviceConnection *device) override;

    // IBackendPlugin - main view
    bool     hasMainView()   const override { return true; }
    QWidget *createMainView(QWidget *parent) override;
    QString  mainViewName()  const override { return QStringLiteral("Memos"); }
    QIcon    mainViewIcon()  const override
        { return QIcon::fromTheme(QStringLiteral("view-pim-notes")); }

    // IBackendPlugin - conflict presentation
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const override;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const override;
};

} // namespace WildPalms::Memo
```

### JSON manifest

```json
{
    "KPlugin": {
        "Id": "memo",
        "Name": "Memo Sync",
        "Description": "Syncs Palm MemoDB to Markdown files.",
        "Icon": "view-pim-notes",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["MemoDB"],
    "X-WildPalms-ClaimDescriptions": {
        "MemoDB": "Syncs MemoDB to Markdown files."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 10
}
```

`X-WildPalms-ConduitId`, `X-WildPalms-ConduitType`,
`X-WildPalms-PalmCreatorId`, `X-WildPalms-RequiresDevice`,
`X-WildPalms-RunBefore/After` from the old manifest are **not**
carried over — the new ABI subsumes them (`PluginType`, claim map,
`runBefore()`/`runAfter()` are virtuals on `IBackendPlugin`).

### CMake toggle

```cmake
# src/plugins/memo/CMakeLists.txt

option(WILDPALMS_MEMO_PLUGIN_V2 "Build the new IBackendPlugin-based Memo plugin" ON)

if (WILDPALMS_MEMO_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_memo_v2
        SOURCES
            memobackendplugin.cpp  memobackendplugin.h
            memoblobbackend.cpp    memoblobbackend.h
            memomarkdown.cpp       memomarkdown.h
            memoview.cpp           memoview.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_link_libraries(wildpalms_memo_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync            # PalmBackend
            WildPalmsPalmCodecs          # MemoCodec
            WildPalmsPalmCalendar        # CategoryMappingStore
            KF6::CoreAddons
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_memo
        SOURCES
            memoconduit.cpp memoconduit.h
            memomapper.cpp  memomapper.h
            memoview.cpp    memoview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_memo
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
endif ()
```

---

## Testing strategy

### Scope

1. **`MemoMarkdown` unit tests** — encode/decode round-trip; key
   ordering canonicalisation; default omission; body trailing-
   newline rule; parse tolerance for integer-vs-string categories,
   missing keys, malformed frontmatter.
2. **`MemoBlobBackend` unit tests** — using `MockPalmDatabaseAccess`
   seeded with a `PalmBackend`: `loadRecords` returns markdown bytes
   with stable hashes; `createRecord` writes back a correctly
   category-slotted `PalmRecord`; `updateRecord`, `deleteRecord`
   round-trips; `modifiedSince`/`deletedSince` pass through; private-
   flag preservation both directions.
3. **End-to-end `tst_memo_v2`** — the sub-phase's headline test:
   - Fixture: `QTemporaryDir` + `MockPalmDatabaseAccess` +
     `PalmBackend` + `MemoBlobBackend` + `LocalBlobBackend`.
   - Load `wildpalms_memo_v2.so` via `BackendPluginManager` (using
     the `setPluginSubdir` test seam + a test-install path, same
     shape as E.8's `tst_plugin_factory_roundtrip`).
   - Call `plugin->createBackends(nullptr, &devConn)` with a real
     `PalmDeviceConnection` wrapping the mock device.
   - Seed three memos on the mock (two in slot 0, one in slot 2).
   - Run `BlobSyncEngine::twoWayWithBaseline(memoBackend,
     localBackend, "palm:memo", "test-mapping-1", &baseline,
     &registry, &store, policy)`.
   - Assert three `.md` files appear under `tmpdir/memo/`.
   - Modify one on the local side, run again, assert mock sees the
     change.
   - Delete one on the mock, run again, assert local file is
     removed.
   - Assert hash-stability: re-run a no-op sync and see zero
     changes.
4. **Plugin manifest test** — round-trip `memo-backend-plugin.json`
   through `KPluginMetaData` and verify `PluginType` and
   `claimedDatabases` parse correctly. (Mirrors E.8's metadata
   parsing tests.)

### Not in scope (deferred)

- **`SyncCoordinator`-level e2e** — covered by E.18.
- **`ConflictDialog` rendering test** — covered when `ConflictDialog`
  is ported to the new plugin registry (small follow-up after E.9
  lands; may ride with E.10 or be its own commit).
- **Live POSE64 device** — covered by E.18.

### Existing test impact

- `tests/test_memomapper.cpp` stays green (tests the old mapper).
- No Phase-D host or fullsync test changes.
- `tst_plugin_factory_roundtrip` (E.8) keeps passing — memo plugin
  loads through the same `BackendPluginManager` code path; only the
  dummy fixture was its subject before.

---

## Explicitly deferred / non-goals

- **Generalising `CategoryMappingStore`** — stays at
  `src/palm/calendar/`. Rename/move when contacts/todos need it
  (E.11/E.12).
- **Memo-specific `ConflictHandler`** — fallback to `PalmConflictHandler`
  is sufficient; memo doesn't have per-record Palm oddities beyond
  what the generic handler covers.
- **`ConflictDialog` new-plugin wiring** — out-of-band follow-up;
  doesn't gate E.9.
- **Main-window tab-bar ergonomics** for mixed old + new plugins —
  both loops coexist in `kf6mainwindow`; polish in E.17.
- **User-data migration of existing `~/.wildpalms/.../memo/*.md`
  files** — the new encoder omits the `created:` field; old files
  still parse (decoder ignores unknown keys). The first sync writes
  the canonical form; content is preserved.
- **Memo plugin settings UI** — `hasSettings()` returns false.
- **CMake toggle default flip** — E.9 ships `WILDPALMS_MEMO_PLUGIN_V2`
  defaulting ON; no fallback path exercised in CI.

---

## Risks / open items

**R1 — `WildPalmsPalmSync` ↔ `WildPalmsRuntime` dependency direction.**
`PalmDeviceConnection` belongs logically with the device layer
(`src/palm/`) but must be accessible from plugins loaded by
`BackendPluginManager` (in `WildPalmsRuntime`). If
`WildPalmsPalmSync` already depends on `WildPalmsRuntime`, a
circular dependency forms. Mitigation: `PalmDeviceConnection` has
no knowledge of `WildPalmsRuntime` types — it depends only on
`IPalmDatabaseAccess` (in `WildPalmsPalmSync`) and `PalmBackend`.
Plugins depend on `WildPalmsPalmSync`, not the other way.
Implementation may still surface a build-graph issue; if so, hoist
`PalmDeviceConnection` into `WildPalmsCore` (it has no state
beyond two pointers).

**R2 — Category-name decoration loses precision across rounds.** If
the user loads their Palm's `AppInfo` block *between* syncs,
previously-synced memos with no `categoryName` in frontmatter now
gain one on the next write → byte change → new baseline. This is
correct behaviour (not a bug) but will show up as a "changed
everything" sync on the first run after AppInfo becomes available.
Mitigation: call it out in the release notes; no code change.

**R3 — Main-window icon for main-view.** `MemoBackendPlugin::icon()`
and `mainViewIcon()` both return `view-pim-notes`. Some desktops
have it; others don't. The old conduit had the same concern and
was shipped this way — not new risk for E.9. `QIcon::fromTheme`
falls back gracefully to a missing-icon silhouette.

**R4 — Plugin-factory install path for tests.** `tst_memo_v2`
depends on `wildpalms_memo_v2.so` being on
`QCoreApplication::libraryPaths()`. The E.8 pattern
(`tests/plugins/dummy_backend/` installs to
`wildpalms_test/plugins`) is the template; memo tests must either
(a) install to the same test-only namespace and build both
`wildpalms_memo_v2` (production, real namespace) and a
`wildpalms_memo_v2_testfixture` duplicate, or (b) have the test
point `libraryPaths()` at the real build output. Option (b) is
simpler if `WILDPALMS_MEMO_PLUGIN_V2=ON` is the CI default; option
(a) isolates tests from install paths. Decision: option (b) with a
fallback comment — E.9 tests pick the build-output path directly
(same as `tst_plugin_factory_roundtrip` does for the dummy fixture).

**R5 — `CategoryMappingStore` lifetime.** Non-owning pointer shared
between `MemoBlobBackend` and potentially `MemoView`. For E.9 the
plugin doesn't hand one in (null fallback works); the runtime
construction of the store happens in E.10+ when AppInfo parsing
lands. E.9 proves the null-store path works end-to-end.

**R6 — `ConflictDialog` lookup path.** Today it asks `ConduitManager`
for a conduit by id. After E.9 lands, memo conflicts have no
conduit to look up — the dialog needs a fall-through to
`BackendPluginManager` by backend id. This is a real gap for any
memo conflict raised before the follow-up lands. Mitigation: the
coordinator's default `PalmConflictHandler` resolves memo conflicts
automatically via its policy; it does not need to open the dialog.
Interactive memo conflicts remain a known regression until the
follow-up; flagged in the E.9 release note.

---

## Cross-references

- **Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
  — §"Plugin ABI" (lines 170-314), §"Sub-phases" row E.9 (line 587).
- **Parent plan:** `docs/plans/2026-04-20-libkalburator-integration.md`
  — Phase E sub-phases table gets E.9 flipped to landed on completion.
- **Prior landed sub-phases:**
  - E.7 spec: `docs/superpowers/specs/2026-04-23-phase-e7-typed-adapters-design.md`
  - E.8 plan: `docs/superpowers/plans/2026-04-23-phase-e8-plugin-abi.md`
  - E.6 plan: `docs/superpowers/plans/2026-04-21-phase-e6-palm-calendar-backend.md`
- **Memory references:**
  - `memory/feedback_library_vs_backend_responsibility.md` — backends
    handle format degradation.
  - `memory/project_plugin_abi_e8.md` — new ABI lives alongside old
    until E.16.
  - `memory/project_palm_category_routing.md` — calendar category
    model; memo reuses the store keyed by dbName.
