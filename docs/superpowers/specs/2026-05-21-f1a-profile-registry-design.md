# F.1a — Profile persistence refactor + app-level registry — design

**Date:** 2026-05-21
**Status:** Design approved through brainstorming. Spec ready for plan.
**Phase:** F.1a (first of three Phase F.1 sub-projects; precedes F.1b new menu and F.1c wizard).
**Predecessor:** Phase E ✅ closed 2026-05-21.
**Replaces:** `2026-05-21-f1-new-profile-wizard-design.md` (superseded — see banner there).

---

## 1. Why this exists

Today, a WildPalms "profile" is identified by an on-disk directory
that the user picks via `QFileDialog::getExistingDirectory`. The
directory contains a single `.wildpalms.conf` (KConfig INI) holding
profile identity, device fingerprint, sync settings, conflict
policy, all accounts (with JSON-string-in-INI params), and all sync
mappings (as one big JSON-array-string-in-INI). Plus two dead INI
sections — `[conduits]` and `[databaseHandlers]` — left over from
the pre-Phase-E `IConduit` family.

This is awkward in three ways:

- **Open-by-directory is the wrong UX.** Users routinely don't know
  where their data lives; making them pick a directory every time
  they switch profiles is friction. PlanStan and most KDE apps
  maintain an app-level list of "things you can open"; we should
  too.
- **One big file is harder to reason about.** Accounts and mappings
  in particular grow with usage and benefit from per-entry sections;
  JSON-string-in-INI defeats KConfig's hierarchical reading.
- **Dead code is dead weight.** Loading `[conduits]` and
  `[databaseHandlers]` keeps a code path alive that no production
  consumer touches post-Phase E.

F.1a fixes all three. After F.1a:

- Profiles are app-registered. Discovery is via the registry, not
  via a `QFileDialog`.
- Each profile is three files (`profile.conf`, `accounts.conf`,
  `mappings.conf`) with no JSON-string-in-INI.
- Dead `[conduits]` and `[databaseHandlers]` are gone.

F.1a delivers **no end-user-visible UX win on its own.** It is
structural work that unblocks F.1b (the new File menu) and F.1c
(the wizard). It is shippable independently because: (a) the app
still launches and loads profiles, (b) tests pass, and (c) the
existing AccountController / PalmRuntime / plugin / sync surface is
untouched.

## 2. Scope

In scope:

- New `ProfileRegistry` class (`src/runtime/profileregistry.{h,cpp}`)
  maintaining `~/.config/wildpalms/wildpalmsrc`'s `[profile-<id>]`
  sections.
- Rewrite `Profile::load()` and `Profile::save()` to use three files.
- Drop `Profile`'s `[conduits]` and `[databaseHandlers]` API and
  member storage.
- Add `Profile::id()`, `Profile::defaultPathForId()`,
  `Profile::schemaVersion()`.
- Move `KF6MainWindow`'s startup path from "open last directory" to
  "consult registry, pick last-active or stopgap-picker".
- Replace `KF6MainWindow::onNewProfile()` / `onOpenProfile()` with a
  stopgap one-field name prompt and a stopgap pick-from-registry
  dialog (F.1b replaces these with the real menu).
- Tests: `tst_profileregistry`, rewritten `test_profile`, integration
  test, KF6MainWindow startup regression.

Out of scope (separate sub-projects):

- **F.1b** — Real File menu: Switch / Import / Forget items;
  removal of stopgap dialogs; auto-launching the wizard on empty
  registry.
- **F.1c** — The full multi-page wizard.
- Migration of existing `.wildpalms.conf` profiles. Per the
  brainstorm decision: ignored. Old files stay on disk; nothing in
  WildPalms reads them after F.1a.
- Hardening account password storage to KWallet. Today's
  passing-through-INI is preserved; harden later (probably in F.1c
  when accounts are first created interactively).

## 3. Confirmed design choices

Established during brainstorming:

1. **No migration.** Existing `.wildpalms.conf` is ignored; users
   start with an empty registry on first launch after F.1a.
2. **Three files by concern:** `profile.conf` (identity + device +
   sync metadata), `accounts.conf` (one INI group per account),
   `mappings.conf` (one INI group per mapping).
3. **Drop legacy sections.** `[conduits]` and `[databaseHandlers]`
   sections + their `Profile` accessors are removed entirely.
4. **Profile id is the directory basename.** New profiles get the
   next free `profileN` integer suffix; renaming the display name
   doesn't change the id. Ids are sticky.
5. **In-place rewrite.** No transitional `ProfileV2` class.
   `Profile` is rewritten in one landing; all callers move.
6. **F.1a stopgap UI.** `KF6MainWindow`'s "New Profile" becomes a
   one-field name prompt (no folder picker); "Open Profile" is
   removed; missing-last-active falls back to a `QInputDialog::getItem`
   profile picker.

## 4. On-disk layout

### 4.1 App-level registry — `~/.config/wildpalms/wildpalmsrc`

`KSharedConfig` default location. Schema:

```ini
[General]
lastActiveProfileId = profile1

[profile-profile1]
name = "Palm m505"
path = /home/clinton/.wildpalms/profile1
lastOpened = 2026-05-21T18:42:00Z

[profile-profile2]
name = "Backup Palm"
path = /mnt/external/wp/test-profile
lastOpened = 2026-05-20T11:15:00Z
```

- `[General]/lastActiveProfileId` empty (or absent) means "no
  active profile" — first-run signal.
- Each `[profile-<id>]` group holds display name, absolute path,
  and last-opened timestamp.
- `lastOpened` is ISO-8601 UTC; used for sorting and stale-entry
  recovery.

### 4.2 Per-profile layout — `<path>/`

```
profile1/
├── profile.conf      # identity + device + sync settings
├── accounts.conf     # one INI group per account
├── mappings.conf     # one INI group per mapping
├── .state/           # runtime state — UNCHANGED
│   ├── baselines.sqlite
│   └── <username>/<plugin>/...
├── rawfiles/         # default PC-side sink — UNCHANGED
└── backup/           # raw .pdb dumps — UNCHANGED
```

#### 4.2.1 `profile.conf`

```ini
[meta]
schemaVersion = 1

[profile]
id = profile1
name = "Palm m505"

[device]
path = /dev/ttyUSB1
baudRate = 115200
connectionMode = keepalive       # or "disconnect"
autoSyncOnConnect = false
defaultSyncType = hotsync        # or "fullsync"
userId = 12345
userName = "clinton"
usbSerialNumber = "..."
modelName = "Palm m505"
manufacturer = "Palm Inc"
romVersion = 1234
productId = "..."
romSize = 8388608
ramSize = 8388608
ramFree = 4194304

[sync]
lastSyncTime = 2026-05-21T18:30:00Z
conflictPolicy = AskUser
conflictAutoResolve = none
conflictFallback = defer
conflictPromptStrategy = always_ask
conflictConnectionBehavior = keep_alive
conflictTimeoutSeconds = 60
```

#### 4.2.2 `accounts.conf`

One INI group per account; `params` becomes a nested subgroup
instead of a JSON string:

```ini
[meta]
schemaVersion = 1

[account-fastmail-cal]
type = caldav
displayName = "Fastmail (CalDAV)"
enabled = true

[account-fastmail-cal/params]
url = https://caldav.fastmail.com/dav/calendars/user/clinton@example.com/
username = clinton@example.com
password = secret-or-kwallet-reference
```

Empty file (only `[meta]` group) is valid — represents "no
accounts configured." Group ids are URL-safe (alphanum + `-` + `_`).

#### 4.2.3 `mappings.conf`

One INI group per mapping:

```ini
[meta]
schemaVersion = 1

[mapping-default-wildpalms.calendar-Unfiled]
sourceBackend = wildpalms.calendar
sourceCalendar = Unfiled
targetBackend = rawfiles-wildpalms.calendar-Unfiled
enabled = true
conflictPolicy = AskUser
```

Empty file (only `[meta]` group) is valid. Group ids are URL-safe.

### 4.3 Dropped sections

The old `[conduits/<id>]` and `[databaseHandlers]` INI sections are
not written. Old files containing them are not read. The
corresponding `Profile` accessors are removed (see §6.1).

## 5. ProfileRegistry — API

`src/runtime/profileregistry.h`:

```cpp
namespace WildPalms::Runtime {

struct ProfileEntry {
    QString   id;          // matches directory basename; sticky
    QString   name;        // display name (user-editable)
    QString   path;        // absolute path to the profile directory
    QDateTime lastOpened;  // for sorting / last-active recovery

    bool isValid() const { return !id.isEmpty(); }
};

class ProfileRegistry : public QObject {
    Q_OBJECT
public:
    explicit ProfileRegistry(QObject *parent = nullptr);
    ~ProfileRegistry() override;

    QList<ProfileEntry> entries() const;
    ProfileEntry        entry(const QString &id) const;
    QString             lastActiveId() const;

    ProfileEntry registerNew(const QString &name,
                             const QString &customPath = QString());
    ProfileEntry registerExisting(const QString &path);
    bool         unregister(const QString &id);
    void         setLastActive(const QString &id);

    QString defaultRoot() const;
    void    setDefaultRoot(const QString &root);

    QString allocateNewId() const;

signals:
    void registryChanged();
    void entryUpdated(QString id);

private:
    QString               m_defaultRoot;
    KSharedConfig::Ptr    m_config;
    QList<ProfileEntry>   m_cache;

    void load();
    void save() const;
};

} // namespace WildPalms::Runtime
```

### 5.1 Semantics

- **`entries()`** returns the cached list sorted by `lastOpened`
  descending (most recent first). Cache is populated by `load()` at
  ctor time.

- **`lastActiveId()`** returns the `[General]/lastActiveProfileId`
  string. Empty (`""`) if absent — first-run signal.

- **`registerNew(name)`** with no `customPath`:
  1. Compute new id via `allocateNewId()` — first integer `N` such
     that `profileN` is not in the cache.
  2. Compute path = `defaultRoot()/profileN`.
  3. If that path exists already (orphan from a prior run), use it
     (assume the user wants to recycle it); otherwise create the
     directory. Failure to create → return invalid entry.
  4. Construct a `ProfileEntry` with id / name / path /
     lastOpened=now.
  5. Append to cache, persist via `save()`, emit `registryChanged()`,
     return the entry.

- **`registerNew(name, customPath)`** with a custom path:
  1. Compute id as the basename of `customPath`. If it doesn't
     match the pattern `[A-Za-z0-9_-]+`, return invalid entry.
  2. If that id is already registered, return invalid entry.
  3. Create the directory (or accept it if it already exists).
  4. Otherwise same as above.

- **`registerExisting(path)`** for the F.1b Import flow:
  1. Verify `<path>/profile.conf` exists.
  2. Read it (just enough to extract `[profile]/id`); if missing or
     mismatched against the directory basename, return invalid
     entry.
  3. If the id is already registered with a different path, return
     invalid entry (registry can't hold two entries with the same
     id).
  4. Otherwise insert into cache, save, emit.

- **`unregister(id)`**: removes the entry from cache + registry
  file. **Does not touch on-disk profile files.** Returns `true` if
  the entry existed.

- **`setLastActive(id)`**: updates `[General]/lastActiveProfileId`
  AND updates the entry's `lastOpened = QDateTime::currentDateTimeUtc()`.
  Saves and emits `entryUpdated(id)`.

- **`allocateNewId()`**: linear scan from `1` upwards until
  `profileN` is not in cache. Ids are never recycled within one
  process lifetime; after unregister, the gap remains (so users can
  audit the sequence).

- **`defaultRoot()` / `setDefaultRoot()`**: default is
  `QDir::homePath() + "/.wildpalms"`. `setDefaultRoot` exists for
  tests (lets ctest run with `QTemporaryDir`).

### 5.2 Persistence

`m_config = KSharedConfig::openConfig()` (default ctor — uses
`<appname>rc` under `XDG_CONFIG_HOME`; on Linux, that's
`~/.config/wildpalms/wildpalmsrc`).

`load()`:
1. Iterate `m_config->groupList()`, picking groups matching
   `^profile-.+$`.
2. For each, construct a `ProfileEntry`; append to `m_cache`.
3. Sort `m_cache` by `lastOpened` descending.

`save() const`:
1. Clear all `profile-*` groups in the config.
2. Re-write each cache entry as a fresh `[profile-<id>]` group.
3. Update `[General]/lastActiveProfileId` from whatever
   `setLastActive` last set (kept in a separate cached field).
4. Call `m_config->sync()`.

### 5.3 Signals

- `registryChanged()` fires on `registerNew` / `registerExisting` /
  `unregister`.
- `entryUpdated(id)` fires on `setLastActive`. (Future F.1b uses
  for name renames.)

## 6. Profile — changes

### 6.1 Removed API + storage

From `src/profile.h`:

```cpp
// Removed — no production consumer post-Phase E.
bool        conduitEnabled(const QString &conduitId) const;
void        setConduitEnabled(const QString &conduitId, bool enabled);
QStringList enabledConduits() const;
QJsonObject conduitSettings(const QString &conduitId) const;
void        setConduitSettings(const QString &id, const QJsonObject &s);

QString     activeDatabaseHandler(const QString &dbName) const;
void        setActiveDatabaseHandler(const QString &dbName,
                                     const QString &conduitId);
QMap<QString, QString> databaseHandlers() const;

// Removed members:
QMap<QString, bool>        m_conduitEnabled;
QMap<QString, QJsonObject> m_conduitSettings;
QMap<QString, QString>     m_databaseHandlers;
```

### 6.2 Added API

```cpp
class Profile {
public:
    QString id() const;
    static QString defaultPathForId(const QString &id);
    int schemaVersion() const;
};
```

- **`id()`** — returns `m_id`, populated by `load()` from
  `profile.conf:[profile]/id`. Empty for an uninitialised Profile.
- **`defaultPathForId(id)`** — static helper returning
  `QDir::homePath() + "/.wildpalms/" + id`. Used by
  `ProfileRegistry::registerNew` for the no-custom-path path and by
  tests.
- **`schemaVersion()`** — returns `m_schemaVersion`, populated from
  `profile.conf:[meta]/schemaVersion`. `1` is the only valid value
  for F.1a; future migrations bump it.

### 6.3 Rewritten `load()` and `save()`

```cpp
bool Profile::load()
{
    if (m_syncFolderPath.isEmpty()) return false;
    QDir dir(m_syncFolderPath);
    if (!dir.exists()) return false;

    if (!loadProfileConf())  return false;   // required
    if (!loadAccountsConf()) return false;   // may be empty
    if (!loadMappingsConf()) return false;   // may be empty
    return true;
}

bool Profile::save()
{
    if (m_syncFolderPath.isEmpty()) return false;
    QDir dir(m_syncFolderPath);
    if (!dir.exists() && !dir.mkpath(".")) return false;

    if (!saveProfileConf())  return false;
    if (!saveAccountsConf()) return false;
    if (!saveMappingsConf()) return false;
    return true;
}
```

Six new private helpers in `src/profile.cpp`:

- `bool loadProfileConf();`
- `bool loadAccountsConf();`
- `bool loadMappingsConf();`
- `bool saveProfileConf() const;`
- `bool saveAccountsConf() const;`
- `bool saveMappingsConf() const;`

Each helper opens its file via `QSettings(path, QSettings::IniFormat)`
and reads/writes the groups described in §4.2. Two failures the
helpers handle:

- File missing entirely: `loadProfileConf` fails (required);
  `loadAccountsConf` and `loadMappingsConf` succeed with empty
  state.
- File present but `schemaVersion` is unknown / wrong: log a
  warning and refuse to load (return false from the helper, which
  bubbles up from `Profile::load`).

### 6.4 Mappings serialisation

The public accessors stay:

```cpp
QJsonArray syncMappingsJson() const;
void       setSyncMappingsJson(const QJsonArray &json);
```

But the persisted shape changes. `saveMappingsConf()` walks the
`QJsonArray`, emits one INI group per mapping. `loadMappingsConf()`
walks the group list, reconstructs the `QJsonArray`. The shape of
each mapping object on the way in / out is identical to today's
(consumers downstream unaffected); only the on-disk schema differs.

Mapping group naming: `mapping-<id>` where `<id>` is the mapping's
`id` field with `/` and `[` `]` characters replaced with `_`
(KConfig group syntax characters). Dots and dashes are preserved
so `mapping-default-wildpalms.calendar-Unfiled` is a valid group
name verbatim. Collisions are resolved by appending `-2`, `-3`, ...

### 6.5 Accounts serialisation

Same pattern. `Profile::accounts()` and `saveAccount` /
`removeAccount` / `setAccounts` keep their existing
`QList<Kalburator::Sync::BackendConfiguration>` signature. The
persistence helper rewrites `BackendConfiguration::connectionParams`
(a `QHash<QString, QVariant>`) as a nested `[account-<id>/params]`
subgroup, with one INI key per param.

## 7. KF6MainWindow integration

### 7.1 New member + ctor

```cpp
// kf6mainwindow.h
private:
    std::unique_ptr<WildPalms::Runtime::ProfileRegistry> m_profileRegistry;

// kf6mainwindow.cpp ctor (added after existing member init):
m_profileRegistry =
    std::make_unique<WildPalms::Runtime::ProfileRegistry>(this);
```

### 7.2 Startup

Replace the existing "open last directory from QSettings" logic
with a registry-driven startup. Pseudocode:

```cpp
const QString lastId = m_profileRegistry->lastActiveId();
if (!lastId.isEmpty()) {
    const auto e = m_profileRegistry->entry(lastId);
    if (e.isValid() && QDir(e.path).exists()) {
        loadProfile(e.path);
        return;
    }
}
showProfilePickerStopgap();
```

`showProfilePickerStopgap()` is the F.1a stopgap helper:

- Empty registry: `QMessageBox::information(this, "No profile",
  "Let's create one to get started.")`, then call the
  `onNewProfile` stopgap.
- Registry has at least one entry but last-active is missing /
  stale: `QInputDialog::getItem(this, "Select Profile", "Pick a
  profile:", listOfNames)`, then `loadProfile(matchingEntry.path)`.
  Cancel quits.

### 7.3 `onNewProfile` stopgap

```cpp
void KF6MainWindow::onNewProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        tr("New Profile"),
        tr("Profile name:"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const auto entry = m_profileRegistry->registerNew(name);
    if (!entry.isValid()) {
        QMessageBox::critical(this, tr("New Profile"),
            tr("Could not create profile."));
        return;
    }
    loadProfile(entry.path);
}
```

F.1c replaces this method with the wizard.

### 7.4 Removed: `onOpenProfile` + its menu action

`KF6MainWindow::onOpenProfile()` is removed; the corresponding
`openProfileAction` QAction in `ActionManager` is removed; the
`openProfileRequested` signal is removed. There is no separate `.rc`
file (the menu is constructed programmatically in
`ActionManager::createActions`); the cleanup is purely C++.

### 7.5 `loadProfile` calls `setLastActive`

When `loadProfile(path)` succeeds, append:

```cpp
const QString loadedId = m_currentProfile->id();
if (!loadedId.isEmpty())
    m_profileRegistry->setLastActive(loadedId);
```

So the registry's last-opened tracking stays accurate.

### 7.6 Test seam

`showProfilePickerStopgap()` is virtual on `KF6MainWindow` so the
startup regression test can stub it. The production override calls
the real `QMessageBox` / `QInputDialog`.

## 8. AccountController, PalmRuntime, plugins — unchanged

All three consume `Profile` via the same accessors as today. The
persistence rewrite happens entirely inside `Profile::load`/`save`;
the in-memory shape of accounts and mappings doesn't change. So:

- `AccountController` constructor still takes `Profile *`. No
  change.
- `PalmRuntime` doesn't touch Profile directly; reads mappings
  through `KF6MainWindow`. No change.
- All five plugins are unaffected.

## 9. Error handling

| Failure | Surfacing | Recovery |
| --- | --- | --- |
| `~/.wildpalms/` not writable on `registerNew` | Returns invalid entry; KF6MainWindow shows critical message box. | User picks custom path, or fixes filesystem. |
| `profile.conf` missing on `Profile::load` | `load()` returns false; KF6MainWindow logs error and falls back to stopgap picker. | User picks another profile or creates one. |
| `accounts.conf` / `mappings.conf` missing | Logged at info; `load()` succeeds with empty state. | Normal — happens for brand-new profiles. |
| Unknown `schemaVersion` in any file | `load()` returns false; user-visible message via log. | None automated in F.1a; future schema-2 ships migration. |
| Registry entry's `path` no longer exists | `lastActiveId()` falls through; stopgap picker fires. | User picks a different entry or unregisters via F.1b. |
| `ProfileRegistry::registerExisting` against a dir without `profile.conf` | Returns invalid entry; F.1b's Import flow shows an error. | User picks a different directory or runs the New Profile wizard. |
| Concurrent `wildpalmsrc` edit (two WildPalms instances) | KSharedConfig handles last-writer-wins; the older instance sees stale `m_cache` until next `load()` call. | Documented as out-of-scope; running two WildPalms simultaneously isn't a supported workflow. |

## 10. Testing

### 10.1 `tests/runtime/tst_profileregistry.cpp` (new)

Uses `WILDPALMS_QTEST_GUILESS_MAIN`. Each test that touches the
filesystem uses a `QTemporaryDir` + `ProfileRegistry::setDefaultRoot`.

| Test | Verifies |
| --- | --- |
| `emptyRegistry` | `lastActiveId() == ""`, `entries().isEmpty()`. |
| `registerNewNoPath` | Allocates `profile1`, creates directory, entry returned. |
| `allocateNewIdAfterUnregister` | Register profile1+profile2, unregister profile1, next register allocates profile3. |
| `registerNewCustomPath` | `registerNew("X", "/tmp/foo")` uses basename `foo` as id. |
| `registerExisting` | Pre-write a `profile.conf` with `id=foo`; `registerExisting(path)` picks it up. |
| `registerExistingMismatch` | `profile.conf` has `id=bar` but dir is `foo` → returns invalid entry. |
| `registerExistingMissingConf` | Empty dir → returns invalid entry. |
| `registerExistingIdConflict` | Register a profile; try `registerExisting` of a *different* path with the same id → invalid entry. |
| `unregisterDoesNotDelete` | `unregister(id)` removes from registry but on-disk dir is untouched (assert via `QDir(path).exists()`). |
| `setLastActiveRoundTrip` | `setLastActive(id)` updates `lastActiveId()` and bumps `entry(id).lastOpened`. |
| `persistenceRoundTrip` | Write via one instance, construct a second instance pointing at the same `KSharedConfig`, assert entries match. |
| `setDefaultRootSeam` | `setDefaultRoot(tempdir)` makes `registerNew` use the tempdir. |
| `entriesSortedByLastOpenedDesc` | Register A, B, C with explicit lastOpened timestamps via `setLastActive`; assert `entries()` order. |

### 10.2 `tests/test_profile.cpp` (rewritten)

Uses `WILDPALMS_QTEST_GUILESS_MAIN`. Each test that writes uses
`QTemporaryDir`.

| Test | Verifies |
| --- | --- |
| `saveCreatesThreeFiles` | After `save()`, `profile.conf` + `accounts.conf` + `mappings.conf` all exist; `[meta]/schemaVersion=1` in each. |
| `roundTripBasic` | Set name + id + device fingerprint, save, load into fresh Profile, assert equality. |
| `roundTripAccounts` | Set two accounts (one CalDAV one CardDAV) with `connectionParams`, save, load, assert `Profile::accounts()` returns the same list (modulo order — sort by id). |
| `roundTripMappings` | Set two mappings via `setSyncMappingsJson`, save, load, assert `syncMappingsJson()` returns equivalent content. |
| `loadFailsIfProfileConfMissing` | Empty dir → `load()` returns false. |
| `loadSucceedsIfAccountsConfMissing` | Write only `profile.conf` → `load()` returns true with empty accounts. |
| `loadSucceedsIfMappingsConfMissing` | Write only `profile.conf` → `load()` returns true with empty mappings. |
| `unknownSchemaVersionRefuses` | Write `profile.conf` with `schemaVersion=999` → `load()` returns false. |
| `paramsPersistAsSubgroup` | After `save()`, `accounts.conf` contains `[account-X/params]` group with one key per `connectionParams` entry; no JSON-string-in-INI. |
| `mappingPersistAsGroup` | After `save()`, `mappings.conf` contains one `[mapping-<id>]` group per mapping; no JSON-string-in-INI. |
| `idAccessor` | `Profile::id()` returns the value persisted in `profile.conf:[profile]/id`. |
| `defaultPathForIdHelper` | `Profile::defaultPathForId("profile5") == "~/.wildpalms/profile5"`. |

**Deleted tests:** every test calling `conduitEnabled`,
`setConduitEnabled`, `setActiveDatabaseHandler`, or asserting on
`[conduits]` / `[databaseHandlers]` sections.

### 10.3 `tests/runtime/tst_profile_registry_integration.cpp` (new)

End-to-end flow:

```
1. Construct ProfileRegistry with QTemporaryDir as defaultRoot.
2. registerNew("Test") → returns entry with id=profile1.
3. Construct Profile p; p.setSyncFolderPath(entry.path); p.setName("Test"); set id to "profile1" via direct member access (or have load() pre-populate from registry → spec point: registerNew also writes a stub profile.conf with [profile]/id+name set, so Profile::load works immediately).
4. p.setSyncMappingsJson(<a fixture JsonArray>); p.save();
5. Construct Profile p2; p2.setSyncFolderPath(entry.path); p2.load();
6. Assert p2.id() == "profile1", p2.name() == "Test", p2.syncMappingsJson() == fixture.
```

Open detail above: should `ProfileRegistry::registerNew` write a
stub `profile.conf` so a freshly-registered profile is immediately
loadable? **Yes** — see §11 (open implementation points).

### 10.4 `tests/kf6/tst_kf6mainwindow_startup.cpp` (extended)

Uses `WILDPALMS_QTEST_MAIN`. Three new cases:

- `emptyRegistryShowsStopgap` — registry empty; startup invokes
  `showProfilePickerStopgap()` (overridden in the test fixture to
  record invocation). No profile loaded.
- `validLastActiveAutoLoads` — registry has profile1 with valid
  path; startup calls `loadProfile(path)`. `m_currentProfile->id()`
  ends up `profile1`.
- `staleLastActiveFallsBackToStopgap` — registry has profile1 but
  the path's parent dir is missing; startup invokes the stopgap.

## 11. Open implementation points (for the plan to resolve)

- **`ProfileRegistry::registerNew` writes a stub `profile.conf`?**
  The integration test (§10.3) wants a freshly-registered profile to
  be immediately loadable. Two choices:
  - (a) `registerNew` writes a minimal `profile.conf` (just
    `[meta]/schemaVersion=1` and `[profile]/id=<id>` +
    `[profile]/name=<name>`). Subsequent `Profile::save()` rewrites
    fully. **Preferred.**
  - (b) `registerNew` writes nothing on disk; caller is responsible
    for `Profile::save()` before `Profile::load()` makes sense. More
    surfaces to coordinate.

  Plan picks (a) unless implementation reveals a reason against.

- **`Profile::id()` source of truth.** Today there's no `id`
  concept. Three options for where it gets initialised:
  - (a) Read from `profile.conf:[profile]/id` on `load()`; default
    to the directory basename if the key is missing. **Preferred.**
  - (b) Always derive from directory basename; the `[profile]/id`
    key is informational only.
  - (c) Add a setter that the registry uses to inject the id when
    constructing a Profile from an entry.

  Plan picks (a). It survives the directory being renamed (the
  basename is the registered id; if a user moves the dir to a
  differently-named location, the registry breaks first and we
  surface that error).

- **Removal of `KF6MainWindow::onOpenProfile`.** Verify no tests
  reference `openProfileRequested` or `onOpenProfile` directly;
  remove the QAction in `ActionManager` and any `.rc` menu entry.
  Plan does the grep + cleanup.

- **`KSharedConfig` test seam.** `ProfileRegistry`'s ctor calls
  `KSharedConfig::openConfig()` which uses the per-user config dir.
  Tests need to point at a tempdir. Two routes: (a) pass an
  explicit config path / `KSharedConfig::Ptr` into the ctor for
  tests, or (b) override the `XDG_CONFIG_HOME` env var in the test
  fixture. (a) is cleaner; (b) is more invasive.

  Plan picks (a) — add a second constructor
  `ProfileRegistry(KSharedConfig::Ptr config, QObject *parent)` for
  tests.

- **What happens to `m_currentProfile` if the registry's last
  entry is gone?** Today, when `loadProfile` fails, the app stays
  in a "no profile loaded" state. F.1a stopgap picker fires only on
  startup; mid-session, if `loadProfile` fails after a
  registry-driven attempt, KF6MainWindow logs an error and stays
  profile-less (same as today). F.1b refines this.

## 12. Success criteria

1. **Persistence parity.** A profile that loads + saves + loads
   with no edits is byte-identical between rounds (modulo
   timestamp updates).
2. **No regressions.** All existing per-plugin e2e tests
   (`tst_calendar_v2`, `tst_contacts_v2`, `tst_memo_v2`,
   `tst_todo_v2`, `tst_install_v2_e2e`) pass unchanged.
3. **Empty registry first-run works.** Fresh `~/.config/wildpalms/`
   + fresh `~/.wildpalms/` → app launches → stopgap shows → user
   creates a profile named "Test" → profile loads → `~/.wildpalms/profile1/`
   contains `profile.conf` with `id=profile1`, name=Test.
4. **Registry survives restart.** After (3), close the app and
   relaunch; profile1 auto-loads via `lastActiveId()`.
5. **Dropped legacy doesn't break the build.** No references to
   `conduitEnabled`, `setConduitEnabled`,
   `activeDatabaseHandler` remain anywhere in the codebase outside
   of `docs/archived/`.

## 13. References

- `docs/PLUGIN_ABI.md` — plugin contract (unchanged by F.1a).
- `docs/ARCHITECTURE_2026.md` — surrounding architecture
  (`PalmRuntime`, `KF6MainWindow`, `AccountController`). F.1a
  touches only `KF6MainWindow` from this set.
- `docs/plans/2026-04-20-libkalburator-integration.md` — Phase F
  umbrella plan.
- `docs/superpowers/specs/2026-05-21-f1-new-profile-wizard-design.md`
  — superseded; preserved as historical reference for F.1c.
- `src/profile.{h,cpp}` — the file being rewritten.
- `src/kf6/kf6mainwindow.{h,cpp}` — the startup path being
  rewired.
- `src/kf6/actionmanager.{h,cpp}` — the QAction holder; the Open
  Profile action lives here.
- `src/runtime/accountcontroller.{h,cpp}` — unchanged but read by
  the implementation to confirm Profile* dependency stays
  satisfied.
