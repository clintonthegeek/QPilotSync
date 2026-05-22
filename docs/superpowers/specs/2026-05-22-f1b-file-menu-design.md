# F.1b — New File menu: Switch / Import / Forget — design

**Date:** 2026-05-22
**Status:** Design approved through brainstorming. Spec ready for plan.
**Phase:** F.1b (second of three Phase F.1 sub-projects; follows F.1a ✅ 2026-05-22, precedes F.1c wizard).
**Predecessor:** F.1a `2026-05-21-f1a-profile-registry-design.md` ✅ landed 2026-05-22.

---

## 1. Why this exists

F.1a delivered the structural plumbing: `ProfileRegistry`, a three-file
per-profile layout, and stopgap menu/UI items just sufficient to keep
the app launchable. What it did **not** deliver is a real
profile-management UX:

- "New Profile" is still a one-field `QInputDialog::getText` name prompt.
- "Switch profiles" is impossible from a running app — you have to
  close, edit `wildpalmsrc`, or restart.
- There's no way to register a profile that already exists on disk
  (e.g. one copied over from another machine).
- There's no way to remove a registry entry without hand-editing INI.
- The startup picker for the stale-last-active case is a
  `QInputDialog::getItem` list.
- The profile's display name can only be set at creation; no rename UI.

F.1b ships the real File menu. After F.1b, all common profile-management
operations are reachable from `File → Profile ▸`. F.1c will replace the
"New Profile" name-prompt with the full wizard; F.1b leaves it alone.

## 2. Scope

In scope:

- Restructure the File menu into a `File → Profile ▸` submenu containing
  New / Switch ▸ / Import / Forget ▸ / Close / Settings (per §4).
- New `ProfileMenuController` class (`src/kf6/profilemenucontroller.{h,cpp}`)
  owning the dynamic `Switch ▸` and `Forget ▸` `KActionMenu` instances
  and keeping them in sync with `ProfileRegistry`.
- New slot `KF6MainWindow::onImportProfile()` — `QFileDialog` → 
  `ProfileRegistry::registerExisting` → load.
- New slot `KF6MainWindow::onSwitchProfile(QString id)` — close +
  load by id.
- New slot `KF6MainWindow::onForgetProfile(QString id)` — confirm
  dialog with "Also delete files at \<path>" checkbox; unregister
  + optionally `QDir::removeRecursively()`.
- Replace F.1a's startup `QInputDialog::getItem` picker with
  auto-load-most-recent. Empty-registry still triggers the F.1a
  one-field name prompt (unchanged).
- Extend `ProfilePropertiesDialog` with an editable Name field.
- Add `ProfileRegistry::rename(id, newName)` and a corresponding
  `Profile` rename path that updates both `wildpalmsrc` and
  `<path>/profile.conf:[profile]/name`.
- Update `data/wildpalmsui.rc` to the new menu structure.
- Tests: `tst_profilemenucontroller`, `tst_profileregistry_rename`,
  `tst_kf6mainwindow_forget_profile`, updated `tst_kf6mainwindow_startup`.

Out of scope (separate sub-projects):

- **F.1c** — the full multi-page profile-creation wizard. F.1b's
  "New Profile" stays as the F.1a one-field stopgap.
- **F.2** — real `IConflictPresenter`.
- **F.3** — per-Palm-category routing UX.
- **F.4** — Radicale E2E + user docs.
- Multi-select in Switch/Forget submenus (one at a time).
- Drag-reorder of profiles in the submenu.
- Importing a profile *without* loading it after.
- Profile renaming from inside the Switch/Forget submenus (rename
  lives in Profile Settings only).
- Migration of pre-F.1a `.wildpalms.conf` files (already declared
  out by F.1a §2).
- Hardening account password storage to KWallet (deferred to F.1c
  per F.1a §2).

## 3. Confirmed design choices

Established during 2026-05-22 brainstorming:

1. **Menu structure:** grouped under `File → Profile ▸`. Configure
   Mappings stays at the top level of File. (Rejected: flat
   File-menu layout; collapsing Close into Switch.)
2. **Switch:** dynamic submenu of registered profiles, sorted by
   `lastOpened` desc (matches `ProfileRegistry::entries()`). Active
   profile gets a checkmark and is disabled. Click switches
   immediately (close current + load picked). (Rejected: modal
   picker dialog; combined manager dialog.)
3. **Import:** plain `QFileDialog::getExistingDirectory` →
   `registerExisting` → auto-switch on success. (Rejected:
   register-without-loading; wizard-lite folder + rename two-step.)
4. **Forget:** dynamic submenu of registered profiles. Active
   profile is disabled (user must Close it first). Clicking a
   non-active entry opens a confirm dialog with checkbox "Also
   delete files at \<path>" (default off). (Rejected: registry-only
   without delete option; always-forget-current-profile.)
5. **Startup picker replacement:** stale-last-active falls through
   to auto-load-most-recent. Empty registry still uses the F.1a
   one-field name-prompt stopgap. (Rejected: real "Select Profile"
   dialog; keeping `QInputDialog::getItem`.)
6. **Rename UX:** add a Name field to the existing
   `ProfilePropertiesDialog` (Profile Settings). No standalone Rename
   menu item. (Rejected: separate Rename menu item; deferring rename
   to F.1c.)
7. **Delete-files ownership:** `KF6MainWindow::onForgetProfile`
   performs `QDir::removeRecursively`. `ProfileRegistry` keeps its
   F.1a "registry never touches profile files" invariant — no
   `unregisterAndDelete` method on the registry.
8. **Dynamic submenu sync:** dedicated `ProfileMenuController` class
   listens to `ProfileRegistry::registryChanged()` and an active-id
   signal from `KF6MainWindow`, rebuilds the `Switch ▸` and `Forget ▸`
   contents in place. (Rejected: rebuild on `QMenu::aboutToShow`.)

## 4. Menu structure

### 4.1 `data/wildpalmsui.rc` change

The existing File menu becomes:

```xml
<Menu name="file">
    <text>&amp;File</text>
    <Menu name="file_profile">
        <text>&amp;Profile</text>
        <Action name="file_new_profile"/>
        <Action name="file_switch_profile"/>
        <Action name="file_import_profile"/>
        <Action name="file_forget_profile"/>
        <Action name="file_close_profile"/>
        <Separator/>
        <Action name="file_profile_settings"/>
    </Menu>
    <Action name="file_configure_mappings"/>
    <Separator/>
    <Action name="file_quit"/>
</Menu>
```

The `file_recent_profiles` placeholder (currently in the .rc file
but never wired) is removed in the same edit.

### 4.2 Action inventory

| Action name (KActionCollection) | Type | Class | Notes |
| --- | --- | --- | --- |
| `file_new_profile` | `QAction` | `ActionManager` | Unchanged. F.1a stopgap; F.1c replaces handler. |
| `file_switch_profile` | `KActionMenu` | `ProfileMenuController` | NEW. Dynamic submenu of profiles. |
| `file_import_profile` | `QAction` | `ActionManager` | NEW. Triggers `onImportProfile`. |
| `file_forget_profile` | `KActionMenu` | `ProfileMenuController` | NEW. Dynamic submenu of profiles. |
| `file_close_profile` | `QAction` | `ActionManager` | Unchanged. |
| `file_profile_settings` | `QAction` | `ActionManager` | Unchanged. Handler extended to support rename. |
| `file_configure_mappings` | `QAction` | `ActionManager` | Unchanged. |

The two `KActionMenu` actions are constructed by `ProfileMenuController`
and registered into the action collection during its construction. The
.rc XML references them by name like any other action; KXmlGui
substitutes a submenu at the position of the `<Action>` element.

### 4.3 ToolBar

The existing toolbar `Action name="file_new_profile"` entry stays
as-is. No new toolbar items in F.1b.

## 5. ProfileMenuController

`src/kf6/profilemenucontroller.h`:

```cpp
namespace WildPalms::Runtime { class ProfileRegistry; }
class KActionCollection;
class KActionMenu;
class QAction;

class ProfileMenuController : public QObject {
    Q_OBJECT
public:
    ProfileMenuController(WildPalms::Runtime::ProfileRegistry *registry,
                          KActionCollection *actionCollection,
                          QObject *parent = nullptr);
    ~ProfileMenuController() override;

    KActionMenu *switchMenu() const;
    KActionMenu *forgetMenu() const;

    void setActiveProfileId(const QString &id);

signals:
    void switchRequested(QString id);
    void forgetRequested(QString id);

private:
    void rebuild();

    WildPalms::Runtime::ProfileRegistry *m_registry;
    KActionCollection *m_actionCollection;
    KActionMenu       *m_switchMenu = nullptr;
    KActionMenu       *m_forgetMenu = nullptr;
    QString            m_activeId;
};
```

### 5.1 Construction

The controller takes a non-owning `ProfileRegistry *` (borrowed from
`KF6MainWindow`) and a `KActionCollection *` to register its two
`KActionMenu` instances under the names `file_switch_profile` and
`file_forget_profile`. The two menus carry the text `&Switch Profile`
and `&Forget Profile` respectively, plus standard icons
(`view-refresh` and `edit-delete`).

The ctor:
1. Creates `m_switchMenu = new KActionMenu(...)` and
   `m_forgetMenu = new KActionMenu(...)`.
2. Adds them to `m_actionCollection` under the action names above.
3. Connects `m_registry->registryChanged()` to `this->rebuild()`.
4. Calls `rebuild()` once to populate the initial state.

The controller is constructed by `KF6MainWindow` **before**
`setupGUI()` so KXmlGui finds the actions when it parses the .rc file.

### 5.2 `rebuild()` algorithm

Pseudocode:

```cpp
void ProfileMenuController::rebuild()
{
    m_switchMenu->menu()->clear();
    m_forgetMenu->menu()->clear();

    const auto entries = m_registry->entries();
    if (entries.isEmpty()) {
        m_switchMenu->setEnabled(false);
        m_forgetMenu->setEnabled(false);
        return;
    }
    m_switchMenu->setEnabled(true);
    m_forgetMenu->setEnabled(true);

    for (const auto &e : entries) {
        const bool isActive = (e.id == m_activeId);

        QAction *sw = new QAction(e.name, m_switchMenu);
        sw->setCheckable(true);
        sw->setChecked(isActive);
        sw->setEnabled(!isActive);
        sw->setData(e.id);
        connect(sw, &QAction::triggered, this, [this, id = e.id]() {
            emit switchRequested(id);
        });
        m_switchMenu->addAction(sw);

        QAction *fg = new QAction(e.name, m_forgetMenu);
        fg->setEnabled(!isActive);
        fg->setData(e.id);
        connect(fg, &QAction::triggered, this, [this, id = e.id]() {
            emit forgetRequested(id);
        });
        m_forgetMenu->addAction(fg);
    }
}
```

The dynamic `QAction` children are parented to the `KActionMenu`,
so `menu()->clear()` deletes them on the next rebuild. No leaks.

### 5.3 `setActiveProfileId`

Called by `KF6MainWindow` whenever `m_currentProfile` changes
(after `loadProfile` success and after `closeProfile`). Stores the
id and calls `rebuild()` so the checkmark/disabled state updates.

When no profile is loaded, `setActiveProfileId("")` is called;
all entries become enabled in both submenus.

### 5.4 Empty-registry behavior

Both submenus are disabled (greyed out in the menu bar) when the
registry has zero entries. This is visually distinct from "loaded
but no entries to switch to"; KXmlGui shows a disabled submenu
indicator. Acceptable for first-run; the user reaches New Profile
via the sibling menu item.

## 6. ProfileRegistry::rename

### 6.1 API addition

`src/runtime/profileregistry.h`:

```cpp
bool rename(const QString &id, const QString &newName);
```

### 6.2 Behavior

1. Look up the entry in `m_cache`. If not found, return `false`.
2. Trim `newName`. If empty, return `false`.
3. Update `entry.name` in the cache.
4. Open `<entry.path>/profile.conf` via `QSettings(..., QSettings::IniFormat)`
   and write `[profile]/name = <newName>`; call `sync()`. If the
   write fails (read-only filesystem, missing file, sync returns
   `NoError`-not), return `false` and roll back the cache change.
5. Call `save()` to flush the registry.
6. Emit `entryUpdated(id)`.
7. Return `true`.

The on-disk profile.conf write is best-effort: if the user later
reloads the profile, `Profile::load()` will pick up the new name
from `[profile]/name`, so registry and profile.conf stay in sync.

### 6.3 Why not have `Profile::setName` write back?

`Profile::setName` already exists and writes to the in-memory
member. The registry's name *must* match what's on disk in
`profile.conf` because `Profile::load` reads name from there. So
rename has to touch *both* surfaces atomically (registry +
profile.conf). Doing it in `ProfileRegistry::rename` keeps the
two-surface update co-located, and it works whether or not the
profile is currently loaded.

When the currently-loaded profile is renamed, `KF6MainWindow`
additionally calls `m_currentProfile->setName(newName)` to keep
the in-memory `Profile` consistent with what's about to be on disk
once the user next triggers `Profile::save()`. This avoids the
in-memory state going stale.

## 7. KF6MainWindow integration

### 7.1 New members

```cpp
// kf6mainwindow.h
private:
    std::unique_ptr<ProfileMenuController> m_profileMenuController;

private slots:
    void onSwitchProfile(const QString &id);
    void onImportProfile();
    void onForgetProfile(const QString &id);
```

### 7.2 Construction order

`KF6MainWindow` ctor (current order around `setupGUI`):

```cpp
// existing:
m_profileRegistry = std::make_unique<ProfileRegistry>(this);

// NEW:
m_profileMenuController = std::make_unique<ProfileMenuController>(
    m_profileRegistry.get(),
    actionCollection(),    // provided by KXmlGuiWindow
    this);
connect(m_profileMenuController.get(),
        &ProfileMenuController::switchRequested,
        this, &KF6MainWindow::onSwitchProfile);
connect(m_profileMenuController.get(),
        &ProfileMenuController::forgetRequested,
        this, &KF6MainWindow::onForgetProfile);

// existing:
setupGUI(Default, ...);
```

The controller must construct *before* `setupGUI()` so the
`file_switch_profile` and `file_forget_profile` actions exist in
the action collection when KXmlGui resolves the .rc file.

`ActionManager` also gains `setupFileActions()` registration for
`file_import_profile`; it's a plain `QAction` (no dynamic content)
and follows the same pattern as `file_new_profile`. The signal
`importProfileRequested()` is added; `KF6MainWindow` connects it
to `onImportProfile()`.

### 7.3 `onSwitchProfile`

```cpp
void KF6MainWindow::onSwitchProfile(const QString &id)
{
    if (id.isEmpty()) return;
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) {
        m_logWidget->logError(i18n("Cannot switch: profile not found"));
        return;
    }
    if (!QDir(e.path).exists()) {
        QMessageBox::warning(this, i18n("Switch Profile"),
            i18n("Profile directory no longer exists: %1\n"
                 "Use File → Profile → Forget to remove it from "
                 "the registry.", e.path));
        return;
    }
    loadProfile(e.path);  // existing helper; calls closeProfile first if needed
}
```

`loadProfile` already calls `setLastActive` on success (per F.1a
§7.5); the registry signal then fires, the menu controller
rebuilds, and the new active profile picks up its checkmark
automatically. `KF6MainWindow` also calls
`m_profileMenuController->setActiveProfileId(loadedId)` at the end
of `loadProfile` so the active-id stays explicit (the registry
signal alone doesn't tell the controller which entry is now
active).

### 7.4 `onImportProfile`

```cpp
void KF6MainWindow::onImportProfile()
{
    const QString path = QFileDialog::getExistingDirectory(this,
        i18n("Import Profile"),
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.isEmpty()) return;

    const auto entry = m_profileRegistry->registerExisting(path);
    if (!entry.isValid()) {
        QMessageBox::warning(this, i18n("Import Profile"),
            i18n("Could not import \"%1\".\n\n"
                 "The folder must contain a valid profile.conf with "
                 "an id matching the folder name, and the id must "
                 "not already be registered.", path));
        return;
    }
    loadProfile(entry.path);
}
```

### 7.5 `onForgetProfile`

```cpp
void KF6MainWindow::onForgetProfile(const QString &id)
{
    if (id.isEmpty()) return;
    if (m_currentProfile && m_currentProfile->id() == id) {
        // Defensive — the menu disables this entry, but a stale
        // signal could still arrive.
        m_logWidget->logError(i18n(
            "Cannot forget the currently-loaded profile. "
            "Close it first."));
        return;
    }
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) return;

    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Forget Profile"));
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        i18n("Remove profile \"%1\" from the registry?\n\n"
             "Folder: %2", e.name, e.path), &dlg));
    auto *deleteCheck = new QCheckBox(
        i18n("Also delete files at the folder above"), &dlg);
    deleteCheck->setChecked(false);
    layout->addWidget(deleteCheck);
    auto *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(i18n("Forget"));
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(box);

    if (dlg.exec() != QDialog::Accepted) return;

    const bool wantDelete = deleteCheck->isChecked();
    const QString pathCopy = e.path;

    if (!m_profileRegistry->unregister(id)) {
        m_logWidget->logError(i18n(
            "Failed to remove profile from registry"));
        return;
    }
    if (wantDelete) {
        QDir d(pathCopy);
        if (d.exists() && !d.removeRecursively()) {
            QMessageBox::warning(this, i18n("Forget Profile"),
                i18n("Removed from registry, but could not delete "
                     "files at: %1", pathCopy));
        }
    }
}
```

The `pathCopy` is taken before `unregister` because the entry is
invalidated by that call (the registry's cache no longer contains it).

### 7.6 Startup picker — replace stopgap

`KF6MainWindow::resolveStartupProfile` (F.1a §7.2) becomes:

```cpp
QString KF6MainWindow::resolveStartupProfile()
{
    const QString lastId = m_profileRegistry->lastActiveId();
    if (!lastId.isEmpty()) {
        const auto e = m_profileRegistry->entry(lastId);
        if (e.isValid() && QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    // Stale or missing last-active: auto-load the most-recently-opened.
    const auto entries = m_profileRegistry->entries();
    if (!entries.isEmpty()) {
        // entries() is sorted lastOpened desc per F.1a §5.1.
        const auto &e = entries.first();
        if (QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    // Empty registry (or every entry stale): F.1a name-prompt stopgap.
    const QString picked = showProfilePickerStopgap();
    if (!picked.isEmpty()) {
        loadProfile(picked);
        return picked;
    }
    return QString();
}
```

`showProfilePickerStopgap()` is simplified: the non-empty-with-stale
branch is removed (auto-load-most-recent handles it). What remains
is just the empty-registry name-prompt path. The method keeps its
signature (still virtual for the test seam) and is now invoked
only when the registry is empty.

### 7.7 Active-id propagation

`KF6MainWindow::loadProfile(path)` already calls
`m_profileRegistry->setLastActive(id)` (F.1a §7.5). After that, add:

```cpp
m_profileMenuController->setActiveProfileId(m_currentProfile->id());
```

In `KF6MainWindow::closeProfile()`:

```cpp
m_profileMenuController->setActiveProfileId(QString());
```

so the active-profile state is always in sync.

### 7.8 Profile Settings — rename support

`ProfilePropertiesDialog` gains a `QLineEdit *m_nameEdit` as the
first form row ("Profile name"). Initialised from
`m_profile->name()`. On Apply / OK:

```cpp
const QString newName = m_nameEdit->text().trimmed();
if (!newName.isEmpty() && newName != m_profile->name()) {
    // Hand off to KF6MainWindow's handler; it updates the registry
    // first and only updates m_currentProfile if rename succeeds.
    emit renameRequested(m_profile->id(), newName);
}
```

The dialog emits a new signal `renameRequested(QString id, QString newName)`.
`KF6MainWindow::onProfileSettings` connects it:

```cpp
connect(dlg, &ProfilePropertiesDialog::renameRequested,
        this, [this](const QString &id, const QString &newName) {
    if (m_profileRegistry->rename(id, newName)) {
        if (m_currentProfile && m_currentProfile->id() == id)
            m_currentProfile->setName(newName);
        updateWindowTitle();
    } else {
        m_logWidget->logError(i18n("Failed to rename profile"));
    }
});
```

**In-memory update ordering:** the dialog does NOT call
`m_profile->setName` itself. The dialog merely emits
`renameRequested`; the handler above updates `m_currentProfile` only
after `registry->rename` succeeds. This way a rename failure
(filesystem permission, etc.) doesn't leave the in-memory `Profile`
out of sync with the registry and disk. The dialog's sample code
in this section reflects that:

```cpp
// In ProfilePropertiesDialog::accept() (or equivalent):
const QString newName = m_nameEdit->text().trimmed();
if (!newName.isEmpty() && newName != m_profile->name()) {
    emit renameRequested(m_profile->id(), newName);
}
// All other settings are applied to m_profile directly as today.
```

The registry's `entryUpdated(id)` signal causes `ProfileMenuController`
to refresh the submenu labels (via the existing `registryChanged`
→ `rebuild` path — see §8.1 for why we widen the wiring).

## 8. Signal wiring

### 8.1 ProfileRegistry → ProfileMenuController

The controller connects to **two** registry signals:

- `registryChanged()` — fires on registerNew / registerExisting /
  unregister. Triggers `rebuild()`.
- `entryUpdated(QString id)` — fires on setLastActive and on
  rename (per §6.2). Also triggers `rebuild()`. (Cheap; the only
  cost is rebuilding ~N QActions for a list that's typically
  fewer than a dozen entries.)

Both connections live in `ProfileMenuController`'s ctor.

### 8.2 KF6MainWindow ↔ controller

- `KF6MainWindow` → `controller->setActiveProfileId(id)` on
  `loadProfile` success and on `closeProfile`.
- `controller` → `KF6MainWindow::onSwitchProfile(id)` via
  `switchRequested` signal.
- `controller` → `KF6MainWindow::onForgetProfile(id)` via
  `forgetRequested` signal.

### 8.3 ActionManager → KF6MainWindow

- New signal `ActionManager::importProfileRequested()`.
- Connected in `KF6MainWindow` ctor to `onImportProfile`.

## 9. Error handling

| Failure | Surfacing | Recovery |
| --- | --- | --- |
| Switch target dir gone | `QMessageBox::warning` directing user to Forget | User picks Forget for the stale entry |
| Switch target's `Profile::load` fails | Existing `loadProfile` error surfacing (log widget); the previously-loaded profile is left closed (loadProfile's existing behavior) | User picks a different profile or fixes the source |
| Import: folder lacks `profile.conf` | `QMessageBox::warning` with all three reasons listed (per §7.4 message) | User picks a different folder |
| Import: id collision | Same message (same warning copy covers all `registerExisting` invalid-entry cases) | User picks a different folder, or unregisters the existing entry first |
| Forget: confirm dismissed | No-op | None |
| Forget: `unregister` returns false | Logged as error | User retries; likely benign race (entry already gone) |
| Forget: `removeRecursively` fails | `QMessageBox::warning` "Removed from registry, but could not delete files" | User deletes files manually with a file manager |
| Rename: empty name | Silently ignored (no save) | User types a name |
| Rename: `ProfileRegistry::rename` returns false | `m_logWidget->logError`; cache rollback already happened inside `rename` | User retries (maybe filesystem permission); window title not updated |
| Stale signal: forget arrives for currently-loaded profile | Logged as error; no destructive action | User closes profile first |

**No partial state.** Every operation either fully commits (registry
+ profile.conf for rename; registry + optional file delete for
forget; load completes for switch/import) or leaves the registry
untouched.

## 10. Testing

### 10.1 `tests/kf6/tst_profilemenucontroller.cpp` (new)

Uses `WILDPALMS_QTEST_MAIN` (controller constructs `KActionMenu`s
which need a `QApplication`). Each test uses a real
`ProfileRegistry` over a `QTemporaryDir`, populated via
`registerNew`. A real `KActionCollection` instance is constructed
without an associated window.

| Test | Verifies |
| --- | --- |
| `emptyRegistryDisablesBothMenus` | Both `switchMenu()` and `forgetMenu()` are disabled when `entries().isEmpty()`. |
| `nonEmptyEnablesMenus` | After `registerNew`, both menus are enabled. |
| `submenusPopulatedSorted` | Register three profiles with explicit `setLastActive` timestamps; submenus contain three QActions in lastOpened-desc order, with the entry name as text. |
| `activeProfileCheckedAndDisabledInSwitch` | `setActiveProfileId("profile2")` → the QAction matching profile2 in `switchMenu()` is `checked=true` and `enabled=false`; others are unchecked + enabled. |
| `activeProfileDisabledInForget` | Same setup → forget submenu's profile2 entry is `enabled=false`; others enabled. |
| `clearingActiveIdReenablesAll` | After `setActiveProfileId("")`, no checkmarks; everything enabled in both submenus. |
| `switchRequestedSignalFiresWithId` | Trigger profile2's switch QAction → `switchRequested("profile2")` emitted exactly once. |
| `forgetRequestedSignalFiresWithId` | Trigger profile2's forget QAction → `forgetRequested("profile2")` emitted exactly once. |
| `registryChangedRebuilds` | Register profile3 after construction → submenus rebuild; new entry present in both. |
| `entryUpdatedRebuilds` | Call `setLastActive("profile3")` → submenus rebuild; order reflects new lastOpened. |
| `unregisterRemovesFromBoth` | Call `unregister("profile2")` → entry vanishes from both submenus. |

### 10.2 `tests/runtime/tst_profileregistry_rename.cpp` (new)

Uses `WILDPALMS_QTEST_GUILESS_MAIN`. Each test uses a
`QTemporaryDir`.

| Test | Verifies |
| --- | --- |
| `renameUpdatesEntryName` | `registerNew("A")` → `rename(id, "B")` returns true; `entry(id).name == "B"`. |
| `renamePersistsToRegistryFile` | `rename` + construct a second registry → `entry(id).name == "B"` (loaded fresh from `wildpalmsrc`). |
| `renamePersistsToProfileConf` | After `rename`, read `<path>/profile.conf` directly → `[profile]/name == "B"`. |
| `renameEmitsEntryUpdated` | `qSignalSpy` on `entryUpdated`; `rename` fires exactly one signal with the correct id. |
| `renameEmptyReturnsFalse` | `rename(id, "")` and `rename(id, "   ")` both return false; entry name unchanged. |
| `renameUnknownIdReturnsFalse` | `rename("nonexistent", "X")` returns false. |
| `renameRollsBackOnDiskFailure` | Make profile dir read-only after `registerNew`; `rename` returns false; in-memory cache and registry file unchanged. |

### 10.3 `tests/kf6/tst_kf6mainwindow_forget_profile.cpp` (new)

Uses `WILDPALMS_QTEST_MAIN`. A `KF6MainWindow` fixture with the
real registry, but the forget confirm dialog is suppressed via a
test seam: a virtual `bool confirmForgetProfile(const ProfileEntry &, bool *deleteFiles)`
method that production overrides with the real dialog and tests
override to return preset values.

| Test | Verifies |
| --- | --- |
| `forgetWithoutDeleteKeepsFiles` | Register profile via stub seam; `confirmForgetProfile` returns true, `*deleteFiles = false`; trigger `onForgetProfile`; assert entry gone from registry; `QDir(path).exists() == true`. |
| `forgetWithDeleteRemovesFiles` | Same but `*deleteFiles = true`; assert entry gone AND directory gone. |
| `forgetActiveProfileIsRejected` | Load profile1; trigger `onForgetProfile("profile1")` directly (simulating stale signal); assert log error; registry still contains profile1. |
| `forgetCancelDoesNothing` | `confirmForgetProfile` returns false; registry unchanged; directory exists. |

### 10.4 `tests/kf6/tst_kf6mainwindow_startup.cpp` (extend)

Add two cases to the existing F.1a startup test:

| Test | Verifies |
| --- | --- |
| `staleLastActiveAutoLoadsMostRecent` | Register profile1 (lastOpened older) + profile2 (newer); set lastActiveId to "deleted-profile" (stale); construct window; assert profile2 is loaded, not profile1, not the stopgap. |
| `emptyRegistryStillTriggersStopgap` | Empty registry → `showProfilePickerStopgap` is called exactly once (existing F.1a fixture covers this; F.1b just verifies the behavior is preserved). |

The existing F.1a `staleLastActiveFallsBackToStopgap` test is
**replaced** by `staleLastActiveAutoLoadsMostRecent` because the
fallback behavior changes in F.1b.

### 10.5 `tests/widgets/tst_profilepropertiesdialog_rename.cpp` (new)

Uses `WILDPALMS_QTEST_MAIN`. Construct a Profile with a known
name, open `ProfilePropertiesDialog`, simulate user editing the
name field and clicking OK.

| Test | Verifies |
| --- | --- |
| `renameEmitsSignal` | `QSignalSpy` on `renameRequested`; edit name to "X", click OK; signal fires with `(profile.id(), "X")`. |
| `noChangeNoSignal` | Don't edit name; click OK; `renameRequested` not emitted. |
| `whitespaceOnlyIgnored` | Edit name to "   "; click OK; `renameRequested` not emitted. |
| `nameFieldPrefilledFromProfile` | Construct dialog with `profile.name() == "Original"`; `m_nameEdit->text() == "Original"`. |
| `dialogDoesNotMutateProfileName` | After OK with new name "X", `profile.name()` is unchanged (still "Original") — the dialog only emits; mutation is the handler's job. |

### 10.6 Manual smoke test (post-implementation)

Documented in the implementation plan as a final step: build, run
`./build-dev/src/wildpalms`, exercise:

- Empty registry → name prompt → "Test 1" created → loaded.
- File → Profile → New Profile → "Test 2" created → loaded.
- File → Profile → Switch Profile ▸ → "Test 1" → switches.
- File → Profile → Forget Profile ▸ → "Test 2" → confirm without delete → entry gone, dir present.
- File → Profile → Import Profile → pick the "Test 2" dir → re-registered, loaded.
- File → Profile → Forget Profile ▸ → "Test 2" → confirm WITH delete → entry gone, dir gone.
- File → Profile → Profile Settings → change name → window title updates.

### 10.7 What's NOT tested

- KXmlGui's submenu-substitution behavior. Qt/KF6 ship it; we
  don't test it.
- The visual appearance of checkmarks / disabled items in
  `KActionMenu`. Qt6 handles rendering.
- POSE64-driven Palm-device interactions. Out of scope.
- Real CalDAV/CardDAV/Akonadi backends. F.4.

## 11. Open implementation points (for the plan to resolve)

- **KActionMenu construction without window.** `KActionMenu` ctor
  doesn't require a parent `KXmlGuiWindow`, but the action collection
  it lives in does need to be attached to a window for setupGUI
  substitution. Plan verifies that constructing `ProfileMenuController`
  in `KF6MainWindow`'s ctor (after `actionCollection()` returns a
  valid pointer but before `setupGUI`) works. Reference: KDevelop's
  similar dynamic-menu pattern.

- **`confirmForgetProfile` test seam name.** §10.3 proposes a
  virtual method on `KF6MainWindow`. Plan picks a name that
  doesn't clash with existing virtuals (`onForgetProfile` is the
  slot; the seam is the dialog-running helper, e.g.
  `runForgetProfileConfirm` or `confirmForgetProfile`). Plan grep
  + decide.

- **Registry rename + active profile in-memory.** Resolved: the
  dialog emits `renameRequested` only; `KF6MainWindow`'s handler
  calls `m_currentProfile->setName(newName)` only after
  `registry->rename` returns true. Cleanly avoids the in-memory /
  registry skew if rename fails. Plan implements this exact
  ordering.

- **`KActionMenu` icon choice.** Spec proposes `view-refresh` for
  Switch and `edit-delete` for Forget. Plan double-checks these
  resolve in Breeze/Oxygen; if not, fall back to `document-open`
  and `edit-clear`.

- **Action shortcuts.** F.1a kept `Ctrl+N` on New Profile. F.1b
  doesn't add shortcuts; Switch/Forget/Import don't get defaults.
  Plan confirms and leaves customization to KStandardAction's
  keybindings dialog.

- **`KActionCollection` ownership of submenu QActions.** The
  `KActionMenu` lives in the collection; the per-profile QActions
  inside it are parented to the menu (so they don't pollute the
  collection's action list). Plan verifies that
  `KActionCollection::actions()` callers (e.g. shortcut config)
  don't unexpectedly enumerate the dynamic children.

## 12. Success criteria

1. **Discoverability.** A user with two registered profiles can
   switch between them in two clicks (File menu → Profile → Switch
   → name).
2. **Import works end-to-end.** Copying a profile directory from
   another machine, then File → Profile → Import, then picking the
   directory, results in the profile loading. No manual edits to
   `wildpalmsrc` required.
3. **Forget is reversible-by-import.** Forgetting a profile without
   the delete checkbox leaves the directory intact; immediately
   importing it again succeeds and produces the same registered
   entry (same id, same name).
4. **Rename is round-trippable.** Rename via Profile Settings →
   close app → relaunch → registry shows new name; profile.conf's
   `[profile]/name` matches.
5. **No stopgap UI for non-empty registry.** From a startup with a
   registered (but stale-last-active) profile, the user sees no
   `QInputDialog::getItem`; the most-recent profile loads
   automatically.
6. **No regressions.** All F.1a tests still pass. Every plugin's
   e2e suite (`tst_calendar_v2`, `tst_contacts_v2`, `tst_memo_v2`,
   `tst_todo_v2`, `tst_install_v2_e2e`) passes unchanged.
7. **F.1a invariant preserved.** `ProfileRegistry` still never
   touches profile files (verified by grep:
   `removeRecursively`, `QDir::rmdir`, `QFile::remove` should not
   appear in `src/runtime/profileregistry.cpp`).
8. **Test coverage.** All five new test files land green:
   `tst_profilemenucontroller`, `tst_profileregistry_rename`,
   `tst_kf6mainwindow_forget_profile`,
   `tst_profilepropertiesdialog_rename`, plus the extended
   `tst_kf6mainwindow_startup`.

## 13. References

- `docs/superpowers/specs/2026-05-21-f1a-profile-registry-design.md`
  — predecessor; defines `ProfileRegistry` API and the stopgaps
  this spec replaces.
- `docs/superpowers/specs/2026-05-21-f1-new-profile-wizard-design.md`
  — superseded; preserved as reference for F.1c.
- `docs/plans/2026-04-20-libkalburator-integration.md` §Phase F —
  umbrella plan.
- `data/wildpalmsui.rc` — KXmlGui menu definition being restructured.
- `src/kf6/kf6mainwindow.{h,cpp}` — slots / startup wiring.
- `src/kf6/actionmanager.{h,cpp}` — File action registration.
- `src/runtime/profileregistry.{h,cpp}` — rename addition.
- `src/widgets/dialogs/profilepropertiesdialog.{h,cpp}` — Name
  field addition.
- KDE Frameworks `KActionMenu` — dynamic submenu pattern reference.
