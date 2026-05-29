# Sub-project D — Views read the hub

**Date:** 2026-05-28
**Status:** Design approved, ready for plan
**Scope:** WildPalms only (no libkalburator changes)
**Umbrella:** `docs/superpowers/specs/2026-05-27-three-tier-sync-architecture-design.md` §6.6
**Predecessors:** Sub-projects A, B, C, and hub↔remote routing (all landed on `feature/three-tier-sync`)

---

## 1. Problem

The four PIM views (Calendar, Contacts, Memo, ToDo) currently scan
`<sync>/rawfiles/<domain>/<col>/*.<ext>` and parse on-disk file content. Three
things follow from that:

1. **Three of four views show no data.** `PalmRuntime` writes raw Palm wire
   bytes via `RawFilesBackend`; the suffix-mapper produces files like
   `*.palm_contact_0` rather than `*.vcf`/`*.ics`/`*.md`. Only Memo accidentally
   renders, because its peer is `MarkdownFilesBackend` (the lone domain whose
   peer overrides the suffix). Calendar, Contacts, and ToDo dirs contain only
   `_shapes.json`. The "contacts invisible" symptom is the original trigger for
   the three-tier sync architecture (umbrella §1).

2. **Contacts is not in the V2 sidebar at all.** `ContactsBackendPlugin::hasMainView()`
   returns `false` (`src/plugins/contacts/contactsbackendplugin.h:73`); the
   sidebar loop in `kf6mainwindow.cpp:664` filters on that flag. A working
   `ContactView` (vCard parser + display) exists in `src/plugins/contacts/`
   but is unwired.

3. **Views and sync don't share a source of truth.** Sub-project C delivered
   the canonical hub (`Kalburator::Sinks::GenericSqliteBackend` registered as
   `"wp-hub"`, one collection per domain). The hub is now where all canon
   records live after every sync run. Views have no path to it.

## 2. Goals / Non-goals

### Goals

- The four PIM views read records from the hub (`"wp-hub"`,
  `palm:<domain>` collections), not from `rawfiles/*/`.
- Contacts is registered in the V2 sidebar and displays records uniformly with
  Calendar / Memo / ToDo.
- Views refresh automatically after every sync run.
- Hub access from a view is mediated by a per-domain reader facade — the view
  never sees `Kalburator::Sync::SyncBackend`.

### Non-goals

- **Editing.** D ships read-only. New / Save / Delete affordances are *hidden*
  in the toolbar. Sub-project E adds the editability gate AND wires the
  write path through it; both land together.
- **Live per-collection signals.** D refreshes on `PalmRuntime::syncCompleted`
  only. Per-collection `recordsChanged` signals would require libkalburator
  changes and are deferred.
- **Typed canon helpers.** The reader returns `QByteArray` only. View parsers
  (existing vCard / iCal / markdown decoders) consume the bytes unchanged. A
  typed canon API on top of the reader can be added later if a domain wants it.
- **`QAbstractListModel` model layer.** Views stay `QListWidget`-based for D.
  The model/view refactor is orthogonal to D's success criterion and is not in
  scope.
- **`RawFiles`/`MarkdownFiles` removal.** Those backends remain as optional
  user-configurable spokes (sub-project F may revisit).
- **Profile switch without app restart.** Plugin `setHub()` is one-call-per-
  `PalmRuntime`-instance. Repeat-profile UX is out of scope.

## 3. Approach

A per-domain `HubFooReader` facade sits inside each of the four PIM-view
plugins (Calendar, Contacts, Memo, ToDo) and wraps the borrowed `"wp-hub"`
`SyncBackend*` for that plugin's collection id. The reader
exposes `listRecordIds()` + `recordBytes(id)`. Views consume the reader; the
view's existing parser (already in place for memo / calendar / todo / contacts)
decodes the bytes into its display model. The plugin owns the reader. Plugin's
`createMainView()` constructs the view and pre-injects the reader pointer plus
a `PalmRuntime::syncCompleted` connection that drives `view->refresh()`.

ContactsBackendPlugin's `hasMainView()` flips from `false` to `true`. The
existing `ContactView` becomes its main view, rewired against `HubContactsReader`.

## 4. Architecture

```
PalmRuntime ─┬─ owns m_hub (GenericSqliteBackend, "wp-hub")
             │     collections: palm:calendar, palm:contacts,
             │                  palm:memo, palm:todo
             │
             ├─ Q_SIGNAL syncCompleted()
             │
             └─ at hub creation:  plugin->setHub(m_hub.get())
                                   plugin->setRuntime(this)
                                              │
                                              ▼
   each PIM plugin (calendar/contacts/memo/todos)
     │     std::unique_ptr<HubFooReader> m_hubReader;     (built in setHub)
     │     PalmRuntime *m_runtime = nullptr;              (cached for connect)
     │
     │     createMainView(parent):
     │       v = new FooView(parent)
     │       v->setHubReader(m_hubReader.get())          ← borrowed
     │       QObject::connect(m_runtime, &PalmRuntime::syncCompleted,
     │                        v, &FooView::refresh)
     │       return v
     │
     └──▶  FooView talks only to the reader;
           existing parser unchanged;
           refresh() re-runs loadFoos() on syncCompleted
```

### Boundary contract

- The `HubFooReader` is the only place inside a plugin that touches
  `Kalburator::Sync::SyncBackend`.
- The view never includes libkalburator headers. The view sees only the
  reader interface + `QByteArray`.
- The plugin's other moving parts (PalmBackend, ConflictHandler,
  CategoryMappingStore, shape contributions) stay where they are.

### Read-only affordances

All four views' New / Save / Delete `QAction`s and Memo's per-record
"Memo Category" `QComboBox` are set `setVisible(false)` at view construction.
The category-*filter* widget (the toolbar dropdown for filtering the displayed
list) stays visible. Sub-project E re-enables the edit affordances behind
`canEdit()`.

## 5. Components & Files

### New files

```
src/plugins/pimplugin.h                                (new — WP PIM base)
src/plugins/calendar/hubcalendarreader.{h,cpp}
src/plugins/contacts/hubcontactsreader.{h,cpp}
src/plugins/memo/hubmemoreader.{h,cpp}
src/plugins/todos/hubtodoreader.{h,cpp}

tests/plugins/calendar/tst_hub_calendar_reader.cpp
tests/plugins/contacts/tst_hub_contacts_reader.cpp
tests/plugins/memo/tst_hub_memo_reader.cpp
tests/plugins/todos/tst_hub_todo_reader.cpp

tests/plugins/calendar/tst_calendar_view_reads_hub.cpp
tests/plugins/contacts/tst_contact_view_reads_hub.cpp
tests/plugins/memo/tst_memo_view_reads_hub.cpp
tests/plugins/todos/tst_task_view_reads_hub.cpp

tests/runtime/tst_palm_runtime_emits_sync_completed.cpp
tests/kf6/tst_contacts_in_sidebar.cpp
```

### Reader interface (uniform across domains)

```cpp
namespace WildPalms::<Domain>Plugin {

class HubFooReader {
public:
    HubFooReader(Kalburator::Sync::SyncBackend *hub,
                 QString collectionId);

    QStringList listRecordIds() const;
    QByteArray  recordBytes(const QString &id) const;
    QString     collectionId() const;

private:
    Kalburator::Sync::SyncBackend *m_hub;     // borrowed; outlived by plugin
    QString m_collectionId;
};

} // namespace WildPalms::<Domain>Plugin
```

The reader has no Q_OBJECT, no signals — it is a thin synchronous adapter.
Asserts non-null `hub` in the constructor (`Q_ASSERT(hub)`).

### Modified files (4 plugins)

Each `<domain>backendplugin.{h,cpp}` gains:

```cpp
void setHub(Kalburator::Sync::SyncBackend *hub);     // builds m_hubReader
void setRuntime(WildPalms::Runtime::PalmRuntime *r); // caches m_runtime
std::unique_ptr<HubFooReader> m_hubReader;
WildPalms::Runtime::PalmRuntime *m_runtime = nullptr;
```

`createMainView()` builds the view, calls `view->setHubReader(m_hubReader.get())`,
and connects `m_runtime->syncCompleted` → `view->refresh`.

### `ContactsBackendPlugin`

- `hasMainView()` returns `true`.
- Implements `createMainView() / mainViewName() / mainViewIcon()` matching the
  other three plugins' shape.

### Modified files (4 views)

Each view gains:

```cpp
void setHubReader(HubFooReader *reader);     // borrowed
Q_SLOT void refresh();                       // re-runs loadFoos()
```

`loadFoos()` body is rewritten: replace the `QDir(rawfiles…)` walk with a
`m_reader->listRecordIds()` + `m_reader->recordBytes(id)` loop. The parser
body is unchanged. The view's existing `loadFromPath(QString)` slot is
**kept** and continues to drive `CategoryManager::setBasePath()` — the
filesystem path inside it (the `QDir(rawfiles…)` block) is removed.

New / Save / Delete actions and Memo's category combo set
`setVisible(false)` at construction.

### Modified files (runtime)

`src/runtime/palmruntime.{h,cpp}`:

```cpp
Q_SIGNAL void syncCompleted();
```

Emitted at the end of every sync run from the engine-finish path. After
creating `m_hub`, the runtime iterates registered plugins and calls
`plugin->setHub(m_hub.get())` then `plugin->setRuntime(this)`.

### WP-local PIM plugin base (`WildPalms::Plugins::PimPlugin`)

`Kalburator::Plugin` is owned by libkalburator and not editable from WP
(standing cross-repo rule). Instead, a **new WP-local intermediate class**
sits between `Kalburator::Plugin` and the four PIM-view plugins:

```cpp
// src/plugins/pimplugin.h  (new)
namespace WildPalms::Plugins {

class PimPlugin : public Kalburator::Plugin {
public:
    virtual void setHub(Kalburator::Sync::SyncBackend *hub) { Q_UNUSED(hub); }
    virtual void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) {
        Q_UNUSED(runtime);
    }
};

} // namespace WildPalms::Plugins
```

The four PIM plugins switch their base from `Kalburator::Plugin` to
`WildPalms::Plugins::PimPlugin`. Plucker stays on `Kalburator::Plugin`
directly. Install (`IPluginAction`) is a separate hierarchy and is
unaffected.

`PalmRuntime` iterates `m_palmPlugins` and uses `dynamic_cast<PimPlugin*>`
to dispatch `setHub` + `setRuntime` — matches the existing concrete-cast
dispatch pattern in `palmruntime.cpp`. Non-PIM plugins (plucker) cast to
nullptr and are skipped naturally.

### Mainwindow

`src/kf6/kf6mainwindow.cpp:664-704` — no change required. The sidebar loop
already drives off `hasMainView()` and already calls `loadFromPath(syncPath)`
on the produced view, which keeps the category-manager basepath wiring.

## 6. Data flow

### App start (fresh profile, before HotSync)

1. Mainwindow constructs `PalmRuntime`.
2. `PalmRuntime` constructs `m_hub` (`GenericSqliteBackend("wp-hub")`).
3. `PalmRuntime` iterates `m_palmPlugins`, `dynamic_cast<PimPlugin*>` each, and
   calls `setHub(m_hub.get())` + `setRuntime(this)` on the cast. The four
   PIM-view plugins build `HubFooReader(m_hub, "palm:<domain>")` in their
   `setHub` override. Plucker (`Kalburator::Plugin` directly) casts to nullptr
   and is skipped.
4. Mainwindow's sidebar loop iterates plugins where `hasMainView()` is true
   (Calendar, **Contacts**, Memo, ToDo):
   - `createMainView(this)` returns a view pre-wired to its reader and to
     `runtime->syncCompleted`.
   - `loadFromPath(syncPath)` runs and triggers an initial `refresh()` against
     the (empty on first launch) hub. View renders its existing empty-state
     placeholder.

### After a HotSync

5. `PalmRuntime` runs `SyncEngine` over `palmMappings()`. The hub is Primary in
   every Star LC; engine writes land in `m_hub` collections.
6. On engine finish, `PalmRuntime` emits `syncCompleted()`.
7. Each plugin's view (`Qt::AutoConnection`, same thread) runs `refresh()`.
   `refresh()` iterates `m_reader->listRecordIds()`, fetches bytes per id,
   feeds them into the existing parser. List repopulates; selection state
   best-effort restored by record id if still present.

### Categories

The toolbar category filter still reads its label list from `CategoryManager`
(file-backed under `<sync>/categories/`). Per-record category lives in the
parsed canon bytes (vCard `CATEGORIES:`, iCal `CATEGORIES:`, memo frontmatter
`category:`). The reader does not decode; the view's existing parser does.

### Empty state

If `listRecordIds()` is empty (fresh profile, no sync yet, or hub collection
truly empty), the view shows its existing "No memos found" / "No contacts
found" placeholder text.

### Threading

`GenericSqliteBackend::threadDb()` (libkalburator v0.60) opens a per-thread
SQLite connection lazily. Reader calls happen on the GUI thread; engine calls
happen on the worker thread; no cross-thread connection sharing, no extra
WP-layer locking.

## 7. Error handling

| Scenario | Behaviour |
|---|---|
| `listRecordIds()` returns empty | Success-with-no-data → empty-state placeholder. Not an error. |
| `recordBytes(id)` returns empty `QByteArray` | Record skipped silently (same as current "empty file" path). |
| Reader pointer is null (composition-root bug) | View stores null; `refresh()` short-circuits to empty placeholder. `Q_ASSERT(hub)` in reader ctor catches missing wiring in tests. |
| Parser fails on a record | Record skipped; existing `qCWarning` category emits once per bad record. No popup. |
| `syncCompleted` arrives mid-refresh | Synchronous on GUI thread; second emit just queues a second `refresh()` after the first finishes. No reentrancy. |
| Hub destroyed while view alive | Lifetime contract documented: PalmRuntime outlives plugin outlives view. Dangling reader pointer is a programmer error, same as elsewhere in WP. |
| `setHub()` called twice on a plugin | Forbidden — one call per `PalmRuntime` instance. Documented in plugin header. If profile-switch-without-restart is ever added (F), the plugin will need a `readerInvalidated()` signal. |

## 8. Testing strategy

### Reader unit tests (4 files)

Per domain, headless, no UI. Construct a fresh `GenericSqliteBackend` in a
`QTemporaryDir`, create the domain collection, seed 2-3 records with realistic
canon bytes (vCard 4.0 / iCal VEVENT / iCal VTODO / markdown+frontmatter),
instantiate `HubFooReader(hub, "palm:<domain>")`, assert:

- `listRecordIds()` returns the seeded ids
- `recordBytes(id)` returns the seeded bytes verbatim
- `recordBytes("missing-id")` returns empty `QByteArray`
- `listRecordIds()` on an empty collection returns empty

### View integration tests (4 files)

Per domain. Instantiate the actual view, set a `HubFooReader` backed by a
seeded `GenericSqliteBackend`, call `refresh()`, assert the visible list
contains the expected display strings. Exercises the view's *existing* parser
end-to-end against hub bytes. Direct widget introspection
(`m_memoList->count()`, `item->text()`); no `QTest::qWaitForWindow`. Uses
Qt's offscreen platform plugin so CI doesn't need a display server.

### Runtime signal test (1 file)

`tst_palm_runtime_emits_sync_completed.cpp` — wires `PalmRuntime` with the
mock device, runs one `hotSync()`, asserts `syncCompleted` fires exactly once.

### Contacts sidebar registration test (1 file)

`tst_contacts_in_sidebar.cpp` — asserts the post-D `ContactsBackendPlugin::
hasMainView()` returns `true`. Symbolic but explicit; future-proofs against
accidental revert.

### Regression coverage

Existing view tests are audited. Tests that exercise the parser stay (they
feed bytes anyway). Tests that exercise `loadFromPath` reading actual files
get updated to seed a reader instead. Test count net-positive overall.

### No new libkalburator test work

The hub thread-safety contract is covered by libkalburator's own suite at
v0.60. WP's tests trust the lib seam.

**Total new test footprint:** 10 files.

## 9. User-visible UX after D ships

- Sidebar shows four icons: **Calendar, Contacts, Memo, ToDo**. Contacts
  appears for the first time.
- Fresh profile (no HotSync yet) — all four views open instantly with their
  existing empty-state placeholders.
- After a HotSync (no remote configured) — Palm records land in the hub; all
  four views refresh automatically and display the synced data.
- After a HotSync (remote spoke configured via Settings → Sync Mappings) —
  same view experience; sync flows Palm ↔ hub ↔ remote in one run.
- **No New / Save / Delete affordances in any view.** Toolbar buttons hidden;
  the views are pure read-out. Edit lands in sub-project E.
- Search and category-filter widgets still work — they filter the loaded list.
- `rawfiles/*/` directories are no longer the view's data source. A "LocalFiles"
  spoke (optional) can still produce inspectable `.vcf` / `.ics` / `.md` files
  on disk — for external tools, not for the view.

## 10. Success criteria

- `ContactsBackendPlugin::hasMainView()` returns `true`; Contacts appears
  in the V2 sidebar on app start with no profile loaded.
- After a synthetic `hotSync()` in test against the mock device, all four
  views (`CalendarView`, `ContactView`, `MemoView`, `TaskView`) display the
  number of records the mock seeded — verified via direct widget
  introspection.
- `PalmRuntime::syncCompleted` fires exactly once per `hotSync()` run.
- All four view parsers are unchanged in behaviour (regression tests pass).
- `rawfiles/*/` is read by zero view code paths.
- No view source file includes a libkalburator header.
- Full ctest suite green on the FetchContent build against libkalburator v0.60.

## 11. Out-of-scope / handoffs

- **Sub-project E** picks up edit affordances + a `canEdit()` gate driven by
  per-domain remote-ownership query, AND wires the view's write path through
  the gate to the hub.
- **Sub-project F** can revisit the rawfiles/markdown backends as optional
  spokes and reconcile any remaining `loadFromPath` legacy.
- No libkalburator handoff is required for D. The hub API surface
  (`SyncBackend::loadRecords` / per-record bytes accessor) is unchanged from
  v0.60.
- WebCalendar feeds (E.13) populate the calendar hub collection via
  libkalburator's `IcsFeedFetcher` — they are not a WP plugin folder and do
  not need their own view or reader. WebCalendar records appear in
  `CalendarView` automatically because they land in `palm:calendar`.
