# F.3 — Sync Mappings Graph View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the raw `MappingEditorDialog` + `MappingPromptDialog` with an embedded graph view in the profile settings dialog that makes Palm ↔ provider sync topology visible and editable without exposing backend IDs.

**Architecture:** A new "Sync Mappings" `KPageWidgetItem` hosts a `QGraphicsView`-based bipartite graph (Palm DB nodes on the left, provider nodes on the right). Drag-to-connect between port anchors creates `SyncMapping` edges. Category slot names are persisted in `profile.conf` via a new `Profile` API and written-back by `PalmRuntime::finishConnect()` after each plugin populates its `CategoryMappingStore`.

**Tech Stack:** Qt6 (`QGraphicsView`, `QGraphicsItem`, `QPainter`), KF6 (`KPageDialog`, `KSharedConfig`), libkalburator (`Kalburator::Sync::SyncMapping`, `CollectionInfo`, `syncMappingToJson`/`syncMappingFromJson`), QtTest.

**Dependencies:** F.1a ✅, F.1b ✅, F.1c ✅, F.1d ✅.

**Build:** `build-dev/` (CMakePresets.json project). Run `cmake --build build-dev` and `ctest --test-dir build-dev` for verification.

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/profile.h` / `.cpp` | Modify | Add `categorySlotNames(dbName)` / `setCategorySlotNames(dbName, names)` — `[categories/<dbName>]` ini sections |
| `src/palm/calendar/categorymappingstore.h` / `.cpp` | Modify | Add `sixteenSlotNames(dbName) → QStringList` helper (16 entries, slot 0 forced "Unfiled") |
| `src/plugins/calendar/calendarbackendplugin.h` / `.cpp` | Modify | Add `categorySlotNames()` + `primaryDbName()` accessors |
| `src/plugins/contacts/contactsbackendplugin.h` / `.cpp` | Modify | Same |
| `src/plugins/memo/memobackendplugin.h` / `.cpp` | Modify | Add `m_categoryStore`, populate in `createPalmBackend()`, add accessors |
| `src/plugins/todos/todobackendplugin.h` / `.cpp` | Modify | Add accessors |
| `src/runtime/palmruntime.h` / `.cpp` | Modify | Add `setProfile()`; in `finishConnect()` after each `createPalmBackend()` write category snapshot to Profile |
| `src/app/mapping/palmdbnode.h` / `.cpp` | Create | `QGraphicsItem` — Palm DB node, port rows with right-side anchors |
| `src/app/mapping/providernode.h` / `.cpp` | Create | `QGraphicsItem` — provider node, port rows with left-side anchors |
| `src/app/mapping/mappingedge.h` / `.cpp` | Create | `QGraphicsItem` — bezier edge, selectable, 4 visual states |
| `src/app/mapping/syncmappingsgraphview.h` / `.cpp` | Create | `QGraphicsView` subclass — bipartite layout, drag-to-connect, signals |
| `src/app/mapping/mappinginspectorpanel.h` / `.cpp` | Create | `QWidget` — sync mode / conflict policy / enabled controls |
| `src/app/mapping/syncmappingspage.h` / `.cpp` | Create | Container `QWidget`: graph + inspector + read-only banner |
| `src/app/mapping/CMakeLists.txt` | Modify | Add new files to `WildPalmsAppMapping`; remove retired files; add `PalmDeviceAccessLib` link |
| `src/settingsdialog.h` / `.cpp` | Modify | Add `setAccountController()` / `setPalmRuntime()` setters; add Accounts page (existing `AccountsPage`) + Sync Mappings page; route Apply through pages |
| `src/CMakeLists.txt` | Modify | Add `WildPalmsAppMapping` link to WildPalmsCore for SettingsDialog inclusion |
| `src/kf6/kf6mainwindow.cpp` | Modify | `onSettings()` calls setters; `onConfigureMappings()` navigates to Sync Mappings page; `loadProfile()` calls `m_palmRuntime->setProfile()` |
| `src/app/mapping/mappingeditordialog.{h,cpp}` | Delete | Retired |
| `src/app/mapping/mappingrowdialog.{h,cpp}` | Delete | Retired |
| `src/app/accounts/mappingpromptdialog.{h,cpp}` | Delete | Retired |
| `src/app/accounts/accountspage.cpp` | Modify | Remove `mappingpromptdialog.h` include + post-add prompt — the new graph view replaces this UX |
| `tests/runtime/tst_profile_category_snapshot.cpp` | Create | Profile API round-trip tests |
| `tests/palmcalendar/tst_categorymappingstore.cpp` | Modify | Add tests for `sixteenSlotNames()` |
| `tests/runtime/tst_syncmappingsgraphview.cpp` | Create | Graph view interaction tests |
| `tests/runtime/tst_mapping_row_dialog.cpp` | Delete | Tests retired dialog |
| `tests/runtime/tst_mapping_editor_dialog.cpp` | Delete | Tests retired dialog |
| `tests/runtime/CMakeLists.txt` | Modify | Register new tests; remove retired test entries |
| `docs/plans/2026-04-20-libkalburator-integration.md` | Modify | Mark F.3 ✅ landed |

---

## Task 1: Profile category-slot persistence API (TDD)

**Files:**
- Create: `tests/runtime/tst_profile_category_snapshot.cpp`
- Modify: `tests/runtime/CMakeLists.txt`
- Modify: `src/profile.h`, `src/profile.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/runtime/tst_profile_category_snapshot.cpp`:

```cpp
// tests/runtime/tst_profile_category_snapshot.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TstProfileCategorySnapshot : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault();
    void roundTripsSnapshot();
    void slot0ForcedToUnfiled();
    void overwriteReplacesSlots();
    void differentDbsAreIndependent();
};

namespace {
QStringList sixteenEntries(std::initializer_list<std::pair<int, QString>> entries) {
    QStringList out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i) out << QString();
    for (const auto &[slot, name] : entries) {
        Q_ASSERT(slot >= 0 && slot < 16);
        out[slot] = name;
    }
    return out;
}
} // namespace

void TstProfileCategorySnapshot::emptyByDefault()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    const auto names = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QVERIFY(names.isEmpty());
}

void TstProfileCategorySnapshot::roundTripsSnapshot()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    {
        Profile profile(dir.path()); profile.initialize();
        const auto names = sixteenEntries({
            {0, QStringLiteral("Unfiled")},
            {1, QStringLiteral("Work")},
            {2, QStringLiteral("Personal")},
        });
        profile.setCategorySlotNames(QStringLiteral("DatebookDB"), names);
    }

    // Reopen — same dir.
    Profile reopened(dir.path()); reopened.load();
    const auto names = reopened.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(names.size(), 16);
    QCOMPARE(names.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(names.at(1), QStringLiteral("Work"));
    QCOMPARE(names.at(2), QStringLiteral("Personal"));
    QCOMPARE(names.at(3), QString());
}

void TstProfileCategorySnapshot::slot0ForcedToUnfiled()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    // Pass empty slot 0 → API forces "Unfiled" on read.
    auto names = sixteenEntries({{1, QStringLiteral("Work")}});
    names[0] = QString();
    profile.setCategorySlotNames(QStringLiteral("DatebookDB"), names);

    const auto out = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(out.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(out.at(1), QStringLiteral("Work"));
}

void TstProfileCategorySnapshot::overwriteReplacesSlots()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("Old1")}, {2, QStringLiteral("Old2")}}));

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("New1")}}));

    const auto out = profile.categorySlotNames(QStringLiteral("DatebookDB"));
    QCOMPARE(out.at(1), QStringLiteral("New1"));
    QCOMPARE(out.at(2), QString());   // old value cleared
}

void TstProfileCategorySnapshot::differentDbsAreIndependent()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    Profile profile(dir.path()); profile.initialize();

    profile.setCategorySlotNames(QStringLiteral("DatebookDB"),
        sixteenEntries({{1, QStringLiteral("CalWork")}}));
    profile.setCategorySlotNames(QStringLiteral("AddressDB"),
        sixteenEntries({{1, QStringLiteral("AddrWork")}}));

    QCOMPARE(profile.categorySlotNames(QStringLiteral("DatebookDB")).at(1),
             QStringLiteral("CalWork"));
    QCOMPARE(profile.categorySlotNames(QStringLiteral("AddressDB")).at(1),
             QStringLiteral("AddrWork"));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileCategorySnapshot)
#include "tst_profile_category_snapshot.moc"
```

- [ ] **Step 2: Register test executable**

Append to `tests/runtime/CMakeLists.txt` (after the last `tst_profileregistry*` entry, follow the same pattern):

```cmake
# F.3 T1 — Profile category-slot snapshot persistence
add_executable(tst_profile_category_snapshot tst_profile_category_snapshot.cpp)
target_link_libraries(tst_profile_category_snapshot
    PRIVATE
        Qt::Core
        Qt::Test
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsRuntime
        PalmDeviceAccessLib
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_profile_category_snapshot COMMAND tst_profile_category_snapshot)
set_tests_properties(tst_profile_category_snapshot PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Confirm red**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev --target tst_profile_category_snapshot 2>&1 | tail -10
```

Expected: compile failure — `Profile` has no `categorySlotNames` / `setCategorySlotNames`.

- [ ] **Step 4: Add API to Profile**

In `src/profile.h`, add inside the public section (after `setSyncMappingsJson` at line 283):

```cpp
/// F.3: Category slot snapshot persistence.
///
/// Returns an empty QStringList (size 0) if no snapshot has been stored
/// for `dbName`. Otherwise returns exactly 16 entries indexed by slot
/// (0..15). Slot 0 is always returned as "Unfiled" (forced even if the
/// stored value is empty). Empty string at any other index means the
/// slot is unnamed/absent.
QStringList categorySlotNames(const QString &dbName) const;

/// Set the category slot snapshot for `dbName`. `names` must have
/// exactly 16 entries (slot 0..15). Persists to profile.conf and calls
/// save() before returning.
void setCategorySlotNames(const QString &dbName, const QStringList &names);
```

In `src/profile.cpp`, add the implementations. Survey `src/profile.cpp` for the existing QSettings/KConfig usage pattern (look for how `m_conflictPolicy` round-trips). Append these methods after the existing setters:

```cpp
QStringList Profile::categorySlotNames(const QString &dbName) const
{
    if (dbName.isEmpty()) return {};

    QSettings s(m_path + QStringLiteral("/profile.conf"), QSettings::IniFormat);
    const QString group = QStringLiteral("categories/") + dbName;
    s.beginGroup(group);
    const QStringList keys = s.childKeys();
    if (keys.isEmpty()) {
        s.endGroup();
        return {};
    }

    QStringList out;
    out.reserve(16);
    for (int slot = 0; slot < 16; ++slot) {
        QString value = s.value(QStringLiteral("slot%1").arg(slot)).toString();
        if (slot == 0 && value.isEmpty())
            value = QStringLiteral("Unfiled");
        out << value;
    }
    s.endGroup();
    return out;
}

void Profile::setCategorySlotNames(const QString &dbName,
                                   const QStringList &names)
{
    if (dbName.isEmpty() || names.size() != 16) return;

    QSettings s(m_path + QStringLiteral("/profile.conf"), QSettings::IniFormat);
    const QString group = QStringLiteral("categories/") + dbName;
    s.remove(group);                     // clear stale slot keys
    s.beginGroup(group);
    for (int slot = 0; slot < 16; ++slot) {
        const QString &v = names.at(slot);
        if (!v.isEmpty())
            s.setValue(QStringLiteral("slot%1").arg(slot), v);
    }
    s.endGroup();
    s.sync();
}
```

Note: this uses `QSettings` directly (the same pattern as `Profile::setSyncMappingsJson`). If the surveyed pattern differs (e.g., uses `m_settings` member or KConfig), adapt to match — the contract is what the test asserts, not the storage mechanism.

- [ ] **Step 5: Run target test, then full suite**

```bash
cmake --build build-dev --target tst_profile_category_snapshot 2>&1 | tail -5
ctest --test-dir build-dev -R tst_profile_category_snapshot --output-on-failure 2>&1 | tail -10
ctest --test-dir build-dev 2>&1 | tail -5
```

Expected: 5/5 in the new test; 96 tests total passing (existing 95 + new 1).

- [ ] **Step 6: Commit**

```bash
git add tests/runtime/tst_profile_category_snapshot.cpp tests/runtime/CMakeLists.txt src/profile.h src/profile.cpp
git commit -m "$(cat <<'EOF'
feat: Profile category-slot snapshot persistence (F.3 T1)

Adds categorySlotNames(dbName) / setCategorySlotNames(dbName, names)
to Profile, persisted under [categories/<dbName>] in profile.conf.
Slot 0 is always rendered as "Unfiled" on read. setCategorySlotNames
takes exactly 16 entries and calls sync() to persist immediately so
the F.3 PalmRuntime write-back path doesn't require explicit save()
afterwards.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: `CategoryMappingStore::sixteenSlotNames()` helper

**Files:**
- Modify: `src/palm/calendar/categorymappingstore.h`, `src/palm/calendar/categorymappingstore.cpp`
- Modify: `tests/palmcalendar/tst_categorymappingstore.cpp`

- [ ] **Step 1: Add test case to existing test file**

Append to `tests/palmcalendar/tst_categorymappingstore.cpp` (add the new method to the `private slots:` section and write the test body before `QTEST_MAIN`):

```cpp
// In the private slots: list, add:
void sixteenSlotNamesProducesFixedShape();

// Test body:
void TstCategoryMappingStore::sixteenSlotNamesProducesFixedShape()
{
    using WildPalms::PalmCalendar::CategoryMappingStore;
    CategoryMappingStore store;

    store.setSlotName(QStringLiteral("DatebookDB"), 1, QStringLiteral("Work"));
    store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Personal"));

    const QStringList names =
        store.sixteenSlotNames(QStringLiteral("DatebookDB"));

    QCOMPARE(names.size(), 16);
    QCOMPARE(names.at(0), QStringLiteral("Unfiled"));
    QCOMPARE(names.at(1), QStringLiteral("Work"));
    QCOMPARE(names.at(2), QString());
    QCOMPARE(names.at(3), QStringLiteral("Personal"));
    for (int i = 4; i < 16; ++i)
        QCOMPARE(names.at(i), QString());

    // Different dbName returns empty (all empties + slot 0 = "Unfiled")
    const QStringList empty =
        store.sixteenSlotNames(QStringLiteral("MissingDB"));
    QCOMPARE(empty.size(), 16);
    QCOMPARE(empty.at(0), QStringLiteral("Unfiled"));
    for (int i = 1; i < 16; ++i)
        QCOMPARE(empty.at(i), QString());
}
```

- [ ] **Step 2: Confirm red**

```bash
cmake --build build-dev --target tst_categorymappingstore 2>&1 | tail -10
```

Expected: compile failure — `sixteenSlotNames` is not a member.

- [ ] **Step 3: Add the helper**

In `src/palm/calendar/categorymappingstore.h`, add inside the `public:` section (after `clear`):

```cpp
/// F.3: returns a 16-entry list of slot names for `dbName`. Index =
/// slot number (0..15). Slot 0 is forced to "Unfiled" regardless of
/// what (if anything) is stored. Slots 1..15 return the stored name or
/// empty string if unset. The list is ALWAYS 16 entries — even for an
/// unknown dbName, slot 0 will read "Unfiled" and 1..15 will be empty.
QStringList sixteenSlotNames(const QString &dbName) const;
```

In `src/palm/calendar/categorymappingstore.cpp`, append:

```cpp
QStringList CategoryMappingStore::sixteenSlotNames(const QString &dbName) const
{
    QStringList out;
    out.reserve(16);
    out << UnfiledName;
    for (int slot = 1; slot < 16; ++slot)
        out << slotName(dbName, slot);
    return out;
}
```

- [ ] **Step 4: Run target test and full suite**

```bash
cmake --build build-dev --target tst_categorymappingstore 2>&1 | tail -3
ctest --test-dir build-dev -R tst_categorymappingstore --output-on-failure 2>&1 | tail -10
ctest --test-dir build-dev 2>&1 | tail -3
```

Expected: all cases pass; 96 tests total.

- [ ] **Step 5: Commit**

```bash
git add src/palm/calendar/categorymappingstore.h src/palm/calendar/categorymappingstore.cpp tests/palmcalendar/tst_categorymappingstore.cpp
git commit -m "$(cat <<'EOF'
feat: CategoryMappingStore::sixteenSlotNames helper (F.3 T2)

Returns a fixed-shape 16-entry QStringList for any dbName, with
slot 0 forced to "Unfiled". Used by F.3 plugin accessors to build
the Profile category-slot snapshot.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Plugin accessors + PalmRuntime write-back

**Files:**
- Modify: `src/plugins/calendar/calendarbackendplugin.h`, `src/plugins/calendar/calendarbackendplugin.cpp`
- Modify: `src/plugins/contacts/contactsbackendplugin.h`, `src/plugins/contacts/contactsbackendplugin.cpp`
- Modify: `src/plugins/memo/memobackendplugin.h`, `src/plugins/memo/memobackendplugin.cpp`
- Modify: `src/plugins/todos/todobackendplugin.h`, `src/plugins/todos/todobackendplugin.cpp`
- Modify: `src/runtime/palmruntime.h`, `src/runtime/palmruntime.cpp`
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Add accessors to CalendarBackendPlugin**

In `src/plugins/calendar/calendarbackendplugin.h`, add to the public section (after `claimedDatabases`):

```cpp
// F.3: Category slot snapshot — used by PalmRuntime::finishConnect to
// write the snapshot into Profile after createPalmBackend populates
// m_categoryStore from the live AppInfo block. Returns empty list if
// the store hasn't been populated yet (e.g., createPalmBackend was
// never called).
QString     primaryDbName()       const { return QStringLiteral("DatebookDB"); }
QStringList categorySlotNames()   const;
```

In `src/plugins/calendar/calendarbackendplugin.cpp`, add at the end:

```cpp
QStringList CalendarBackendPlugin::categorySlotNames() const
{
    if (!m_categoryStore) return {};
    return m_categoryStore->sixteenSlotNames(primaryDbName());
}
```

- [ ] **Step 2: Same pattern for ContactsBackendPlugin**

In `src/plugins/contacts/contactsbackendplugin.h`, add:

```cpp
QString     primaryDbName()       const { return QStringLiteral("AddressDB"); }
QStringList categorySlotNames()   const;
```

In `src/plugins/contacts/contactsbackendplugin.cpp`:

```cpp
QStringList ContactsBackendPlugin::categorySlotNames() const
{
    if (!m_categoryStore) return {};
    return m_categoryStore->sixteenSlotNames(primaryDbName());
}
```

- [ ] **Step 3: Same pattern for TodoBackendPlugin**

In `src/plugins/todos/todobackendplugin.h`, add:

```cpp
QString     primaryDbName()       const { return QStringLiteral("ToDoDB"); }
QStringList categorySlotNames()   const;
```

In `src/plugins/todos/todobackendplugin.cpp`:

```cpp
QStringList TodoBackendPlugin::categorySlotNames() const
{
    if (!m_categoryStore) return {};
    return m_categoryStore->sixteenSlotNames(primaryDbName());
}
```

- [ ] **Step 4: Extend MemoPlugin to carry its own snapshot store**

`MemoPlugin` currently passes `nullptr` for the category store to `MemoBlobBackend` because memo records aren't routed by category yet. F.3 still needs a snapshot, so the plugin gains an internal `CategoryMappingStore` purely for the write-back. The backend continues to receive `nullptr` (no behavior change).

In `src/plugins/memo/memobackendplugin.h`:

1. Add forward declaration in the existing namespace block:
   ```cpp
   namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
   ```
2. Inside the `MemoPlugin` class, add to private members:
   ```cpp
   std::unique_ptr<WildPalms::PalmCalendar::CategoryMappingStore> m_categoryStore;
   ```
3. Add to public section (after `claimedDatabases`):
   ```cpp
   QString     primaryDbName()       const { return QStringLiteral("MemoDB"); }
   QStringList categorySlotNames()   const;
   ```

In `src/plugins/memo/memobackendplugin.cpp`:

1. Add includes:
   ```cpp
   #include "palm/calendar/categorymappingstore.h"
   #include "palm/calendar/categoryappinforeader.h"
   ```
2. Initialize the store in the constructor (find the `MemoPlugin::MemoPlugin()` definition and add):
   ```cpp
   MemoPlugin::MemoPlugin()
       : m_categoryStore(
           std::make_unique<WildPalms::PalmCalendar::CategoryMappingStore>())
   {}
   ```
   (If the existing constructor body has other initialisations, fold this in alongside them.)
3. Inside `MemoPlugin::createPalmBackend()`, after `m_palmBackend` is constructed but BEFORE the `return std::make_unique<MemoBlobBackend>(...)` line, add:
   ```cpp
   WildPalms::PalmCalendar::populateFromAppInfo(
       *m_categoryStore,
       QStringLiteral("MemoDB"),
       m_palmBackend->readAppBlock(QStringLiteral("MemoDB")));
   ```
4. Append the accessor:
   ```cpp
   QStringList MemoPlugin::categorySlotNames() const
   {
       if (!m_categoryStore) return {};
       return m_categoryStore->sixteenSlotNames(primaryDbName());
   }
   ```

- [ ] **Step 5: Add setProfile to PalmRuntime**

In `src/runtime/palmruntime.h`:

1. Add forward declaration of `Profile` near the top (after the existing forward declarations):
   ```cpp
   class Profile;
   ```
2. Add public method (e.g., next to `setConflictHandler`):
   ```cpp
   /// F.3: Borrow a Profile pointer for category-slot snapshot
   /// write-back. Called by KF6MainWindow::loadProfile() right after
   /// PalmRuntime is constructed. Non-owning — the Profile must
   /// outlive this PalmRuntime. nullptr disables write-back.
   void setProfile(Profile *profile);
   ```
3. Add to private members:
   ```cpp
   Profile *m_profile = nullptr;   // borrowed; see setProfile
   ```

In `src/runtime/palmruntime.cpp`:

1. Add include at the top:
   ```cpp
   #include "profile.h"
   ```
2. Add the setter implementation:
   ```cpp
   void PalmRuntime::setProfile(Profile *profile)
   {
       m_profile = profile;
   }
   ```
3. In `PalmRuntime::finishConnect()`, after the `if (!ownedBackend) { ...; continue; }` block (around line 286 — survey for the exact spot inside the for loop just before `m_registry->registerBackendInstance`), insert the write-back logic:
   ```cpp
   // F.3: write the category-slot snapshot for this plugin's primary
   // database into the borrowed Profile, if one is set. Each plugin's
   // createPalmBackend has already populated its internal
   // CategoryMappingStore from the live AppInfo block.
   if (m_profile) {
       QString primaryDbName;
       QStringList slotNames;
       if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get())) {
           primaryDbName = p->primaryDbName();
           slotNames = p->categorySlotNames();
       } else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get())) {
           primaryDbName = p->primaryDbName();
           slotNames = p->categorySlotNames();
       } else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get())) {
           primaryDbName = p->primaryDbName();
           slotNames = p->categorySlotNames();
       } else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get())) {
           primaryDbName = p->primaryDbName();
           slotNames = p->categorySlotNames();
       }
       if (!primaryDbName.isEmpty() && slotNames.size() == 16) {
           m_profile->setCategorySlotNames(primaryDbName, slotNames);
       }
   }
   ```

- [ ] **Step 6: Wire from KF6MainWindow**

Survey `src/kf6/kf6mainwindow.cpp` for the line that constructs `m_palmRuntime` (typically inside `loadProfile()` — search for `m_palmRuntime = std::make_unique`). Immediately after that construction, add:

```cpp
// F.3: borrow Profile pointer so PalmRuntime::finishConnect() can
// write the per-DB category-slot snapshot into the profile.
m_palmRuntime->setProfile(m_currentProfile.get());
```

- [ ] **Step 7: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -5
ctest --test-dir build-dev 2>&1 | tail -5
```

Expected: clean build; 96 tests total still passing (no regressions).

- [ ] **Step 8: Commit**

```bash
git add src/plugins/calendar/calendarbackendplugin.h src/plugins/calendar/calendarbackendplugin.cpp \
        src/plugins/contacts/contactsbackendplugin.h src/plugins/contacts/contactsbackendplugin.cpp \
        src/plugins/memo/memobackendplugin.h src/plugins/memo/memobackendplugin.cpp \
        src/plugins/todos/todobackendplugin.h src/plugins/todos/todobackendplugin.cpp \
        src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        src/kf6/kf6mainwindow.cpp
git commit -m "$(cat <<'EOF'
feat: plugins expose categorySlotNames + PalmRuntime write-back (F.3 T3)

Each of the four Palm DB plugins (Calendar, Contacts, Memo, Todo)
gains primaryDbName() + categorySlotNames() accessors backed by its
CategoryMappingStore. MemoPlugin grows an internal store
specifically for the snapshot (MemoBlobBackend still receives
nullptr — no behavior change for memo sync).

PalmRuntime::finishConnect() borrows a Profile pointer (set by
KF6MainWindow::loadProfile after PalmRuntime construction) and
writes each plugin's 16-entry slot snapshot into profile.conf
after createPalmBackend() succeeds. This makes category names
available to the F.3 graph view between syncs.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `PalmDbNode` QGraphicsItem

**Files:**
- Create: `src/app/mapping/palmdbnode.h`, `src/app/mapping/palmdbnode.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/app/mapping/palmdbnode.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_PALMDBNODE_H
#define WILDPALMS_APP_MAPPING_PALMDBNODE_H

#include <QGraphicsItem>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <QVector>

namespace WildPalms::AppMapping {

/// QGraphicsItem rendering a single Palm database (DatebookDB,
/// AddressDB, MemoDB, ToDoDB) with one port row per populated
/// category slot. Right-side anchors carry the slot index.
///
/// The node is rendered with a coloured title bar (theme-derived) and
/// stacks port rows below. Slots are read from the constructor-supplied
/// 16-entry list; empty entries are skipped on render (slot 0 is shown
/// as "Unfiled"). If all entries are empty a single disabled placeholder
/// row is shown reading "Sync once to discover categories".
class PalmDbNode : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    /// dbName: "DatebookDB" / "AddressDB" / "MemoDB" / "ToDoDB"
    /// humanName: title bar text (e.g. "Calendar — DatebookDB")
    /// domain: one of "calendar" / "contacts" / "memos" / "todos"
    /// slotNames: exactly 16 entries; empty means slot unnamed
    PalmDbNode(const QString &dbName,
               const QString &humanName,
               const QString &domain,
               const QStringList &slotNames,
               QGraphicsItem *parent = nullptr);

    QString dbName()  const { return m_dbName; }
    QString domain()  const { return m_domain; }

    /// Scene position of the right-side anchor for `slot`, or invalid
    /// QPointF if slot is unpopulated.
    QPointF slotAnchorScenePos(int slot) const;

    /// Slot index (0..15) for the right-side anchor at scene position,
    /// or -1 if no anchor is there.
    int slotAtScenePos(const QPointF &scenePos) const;

    /// Width / height for layout by the parent view.
    QRectF boundingRect() const override;

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    /// Convenience for the view: list of active slot indices in
    /// ascending order (slot 0 is included only when its name is
    /// non-empty OR domain is shown — see implementation).
    QList<int> activeSlots() const { return m_activeSlots; }

private:
    void rebuildLayout();
    QRectF anchorLocalRect(int slot) const;

    QString     m_dbName;
    QString     m_humanName;
    QString     m_domain;
    QStringList m_slotNames;   // 16 entries
    QList<int>  m_activeSlots; // indices into m_slotNames where name non-empty
    bool        m_hasContent {false}; // false = show placeholder row

    static constexpr qreal kNodeWidth     = 200.0;
    static constexpr qreal kHeaderHeight  = 28.0;
    static constexpr qreal kRowHeight     = 22.0;
    static constexpr qreal kAnchorRadius  = 5.0;
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 2: Write the implementation**

Create `src/app/mapping/palmdbnode.cpp`:

```cpp
#include "palmdbnode.h"

#include <QPainter>
#include <QGraphicsScene>
#include <QApplication>
#include <QPalette>
#include <QFont>
#include <QFontMetricsF>

namespace WildPalms::AppMapping {

PalmDbNode::PalmDbNode(const QString &dbName,
                       const QString &humanName,
                       const QString &domain,
                       const QStringList &slotNames,
                       QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_dbName(dbName)
    , m_humanName(humanName)
    , m_domain(domain)
    , m_slotNames(slotNames.size() == 16 ? slotNames : QStringList(16))
{
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);
    rebuildLayout();
}

void PalmDbNode::rebuildLayout()
{
    m_activeSlots.clear();
    for (int slot = 0; slot < m_slotNames.size(); ++slot) {
        if (!m_slotNames.at(slot).isEmpty())
            m_activeSlots.append(slot);
    }
    m_hasContent = !m_activeSlots.isEmpty();
}

QRectF PalmDbNode::boundingRect() const
{
    const int rows = m_hasContent ? m_activeSlots.size() : 1; // placeholder row
    const qreal h = kHeaderHeight + rows * kRowHeight;
    return QRectF(0.0, 0.0, kNodeWidth, h);
}

QRectF PalmDbNode::anchorLocalRect(int slot) const
{
    if (!m_hasContent) return {};
    const int idx = m_activeSlots.indexOf(slot);
    if (idx < 0) return {};
    const qreal cy = kHeaderHeight + idx * kRowHeight + kRowHeight / 2.0;
    const qreal cx = kNodeWidth - 8.0;
    return QRectF(cx - kAnchorRadius, cy - kAnchorRadius,
                  kAnchorRadius * 2, kAnchorRadius * 2);
}

QPointF PalmDbNode::slotAnchorScenePos(int slot) const
{
    const QRectF r = anchorLocalRect(slot);
    if (r.isEmpty()) return QPointF();
    return mapToScene(r.center());
}

int PalmDbNode::slotAtScenePos(const QPointF &scenePos) const
{
    if (!m_hasContent) return -1;
    const QPointF local = mapFromScene(scenePos);
    for (int slot : m_activeSlots) {
        if (anchorLocalRect(slot).contains(local))
            return slot;
    }
    return -1;
}

void PalmDbNode::paint(QPainter *painter,
                       const QStyleOptionGraphicsItem * /*option*/,
                       QWidget * /*widget*/)
{
    const QPalette pal = QApplication::palette();
    const QRectF rect = boundingRect();

    // Body
    painter->setBrush(pal.color(QPalette::Base));
    painter->setPen(pal.color(QPalette::Mid));
    painter->drawRoundedRect(rect, 4.0, 4.0);

    // Header bar
    const QRectF headerRect(0.0, 0.0, kNodeWidth, kHeaderHeight);
    painter->setBrush(pal.color(QPalette::Highlight).darker(160));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(headerRect, 4.0, 4.0);
    // Square off the bottom of the rounded rect by overdrawing.
    painter->drawRect(QRectF(0.0, kHeaderHeight / 2.0,
                             kNodeWidth, kHeaderHeight / 2.0));

    painter->setPen(pal.color(QPalette::HighlightedText));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->drawText(headerRect.adjusted(8, 0, -8, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, m_humanName);
    titleFont.setBold(false);
    painter->setFont(titleFont);

    // Rows
    painter->setPen(pal.color(QPalette::Text));
    if (!m_hasContent) {
        const QRectF row(0.0, kHeaderHeight, kNodeWidth, kRowHeight);
        QFont italic = painter->font();
        italic.setItalic(true);
        painter->setFont(italic);
        painter->setPen(pal.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          QObject::tr("Sync once to discover categories"));
        return;
    }

    for (int i = 0; i < m_activeSlots.size(); ++i) {
        const int slot = m_activeSlots.at(i);
        const QRectF row(0.0, kHeaderHeight + i * kRowHeight,
                         kNodeWidth, kRowHeight);
        if (i % 2 == 1) {
            painter->fillRect(row, pal.color(QPalette::AlternateBase));
        }
        painter->setPen(pal.color(QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -20, 0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          m_slotNames.at(slot));

        // Right-side anchor dot
        const QRectF a = anchorLocalRect(slot);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->setPen(pal.color(QPalette::Dark));
        painter->drawEllipse(a);
    }
}

} // namespace WildPalms::AppMapping
```

- [ ] **Step 3: Register in CMakeLists**

In `src/app/mapping/CMakeLists.txt`, find the `add_library(WildPalmsAppMapping STATIC ...)` block and add `palmdbnode.h` + `palmdbnode.cpp` to the sources list:

```cmake
add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
)
```

(The retired files are removed in T12 once the new graph is wired.)

- [ ] **Step 4: Build check**

```bash
cmake --build build-dev --target WildPalmsAppMapping 2>&1 | tail -5
```

Expected: builds cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/app/mapping/palmdbnode.h src/app/mapping/palmdbnode.cpp src/app/mapping/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: PalmDbNode graphics item (F.3 T4)

QGraphicsItem rendering a single Palm DB (Datebook/Address/Memo/ToDo)
with one port row per populated category slot and right-side
anchors. Empty snapshot renders a disabled "Sync once to discover
categories" placeholder.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: `ProviderNode` QGraphicsItem

**Files:**
- Create: `src/app/mapping/providernode.h`, `src/app/mapping/providernode.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/app/mapping/providernode.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_PROVIDERNODE_H
#define WILDPALMS_APP_MAPPING_PROVIDERNODE_H

#include <QGraphicsItem>
#include <QString>
#include <QList>
#include <QRectF>

#include <collectioninfo.h>   // Kalburator::Sync::CollectionInfo

namespace WildPalms::AppMapping {

/// QGraphicsItem rendering a single provider (CalDAV/CardDAV/Akonadi/
/// rawfiles) with one port row per collection. Left-side anchors carry
/// the collection id.
///
/// Each collection port row is tagged with its CollectionInfo::type
/// ("calendar" / "contacts" / "memos" / "todos"). The graph view uses
/// the per-row type to enforce domain compatibility during edge
/// creation.
class ProviderNode : public QGraphicsItem {
public:
    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    /// providerId: matches Kalburator::Sync::IProvider::id()
    /// displayName: title bar text (provider's display name)
    /// collections: ordered list; rendered top-to-bottom
    /// busyText: when non-empty, the node renders a single disabled
    ///           placeholder row with this text instead of collection
    ///           rows (e.g. "Connecting…").
    ProviderNode(const QString &providerId,
                 const QString &displayName,
                 const QList<Kalburator::Sync::CollectionInfo> &collections,
                 const QString &busyText = QString(),
                 QGraphicsItem *parent = nullptr);

    QString providerId() const { return m_providerId; }

    /// Domain ("calendar"/"contacts"/"memos"/"todos"/"unknown") for the
    /// collection identified by `collectionId`. "unknown" if absent.
    QString collectionDomain(const QString &collectionId) const;

    /// Scene position of the left-side anchor for the named collection,
    /// or invalid QPointF if absent.
    QPointF collectionAnchorScenePos(const QString &collectionId) const;

    /// Collection id whose left-side anchor sits under scenePos, or
    /// empty string if none.
    QString collectionAtScenePos(const QPointF &scenePos) const;

    QRectF boundingRect() const override;
    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;

    QList<Kalburator::Sync::CollectionInfo> collections() const { return m_collections; }

private:
    QRectF anchorLocalRect(int row) const;

    QString                                   m_providerId;
    QString                                   m_displayName;
    QList<Kalburator::Sync::CollectionInfo>   m_collections;
    QString                                   m_busyText;

    static constexpr qreal kNodeWidth     = 200.0;
    static constexpr qreal kHeaderHeight  = 28.0;
    static constexpr qreal kRowHeight     = 22.0;
    static constexpr qreal kAnchorRadius  = 5.0;
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 2: Write the implementation**

Create `src/app/mapping/providernode.cpp`:

```cpp
#include "providernode.h"

#include <QPainter>
#include <QApplication>
#include <QPalette>
#include <QFont>

namespace WildPalms::AppMapping {

ProviderNode::ProviderNode(const QString &providerId,
                           const QString &displayName,
                           const QList<Kalburator::Sync::CollectionInfo> &collections,
                           const QString &busyText,
                           QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_providerId(providerId)
    , m_displayName(displayName)
    , m_collections(collections)
    , m_busyText(busyText)
{
    setFlag(ItemIsSelectable, false);
    setFlag(ItemIsMovable, false);
}

QRectF ProviderNode::boundingRect() const
{
    const int rows = m_busyText.isEmpty()
        ? std::max<int>(1, m_collections.size())
        : 1;
    return QRectF(0.0, 0.0, kNodeWidth,
                  kHeaderHeight + rows * kRowHeight);
}

QRectF ProviderNode::anchorLocalRect(int row) const
{
    if (!m_busyText.isEmpty()) return {};
    if (row < 0 || row >= m_collections.size()) return {};
    const qreal cy = kHeaderHeight + row * kRowHeight + kRowHeight / 2.0;
    const qreal cx = 8.0;
    return QRectF(cx - kAnchorRadius, cy - kAnchorRadius,
                  kAnchorRadius * 2, kAnchorRadius * 2);
}

QString ProviderNode::collectionDomain(const QString &collectionId) const
{
    for (const auto &c : m_collections) {
        if (c.id == collectionId)
            return c.type.isEmpty() ? QStringLiteral("unknown") : c.type;
    }
    return QStringLiteral("unknown");
}

QPointF ProviderNode::collectionAnchorScenePos(const QString &collectionId) const
{
    if (!m_busyText.isEmpty()) return QPointF();
    for (int i = 0; i < m_collections.size(); ++i) {
        if (m_collections.at(i).id == collectionId) {
            const QRectF r = anchorLocalRect(i);
            return mapToScene(r.center());
        }
    }
    return QPointF();
}

QString ProviderNode::collectionAtScenePos(const QPointF &scenePos) const
{
    if (!m_busyText.isEmpty()) return {};
    const QPointF local = mapFromScene(scenePos);
    for (int i = 0; i < m_collections.size(); ++i) {
        if (anchorLocalRect(i).contains(local))
            return m_collections.at(i).id;
    }
    return {};
}

void ProviderNode::paint(QPainter *painter,
                         const QStyleOptionGraphicsItem * /*option*/,
                         QWidget * /*widget*/)
{
    const QPalette pal = QApplication::palette();
    const QRectF rect = boundingRect();

    painter->setBrush(pal.color(QPalette::Base));
    painter->setPen(pal.color(QPalette::Mid));
    painter->drawRoundedRect(rect, 4.0, 4.0);

    const QRectF headerRect(0.0, 0.0, kNodeWidth, kHeaderHeight);
    // Green-ish header to visually distinguish from Palm nodes (which use
    // Highlight-dark in T4).
    QColor headerCol(0x2a, 0x4a, 0x2a);
    painter->setBrush(headerCol);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(headerRect, 4.0, 4.0);
    painter->drawRect(QRectF(0.0, kHeaderHeight / 2.0,
                             kNodeWidth, kHeaderHeight / 2.0));

    painter->setPen(pal.color(QPalette::HighlightedText));
    QFont f = painter->font();
    f.setBold(true);
    painter->setFont(f);
    painter->drawText(headerRect.adjusted(8, 0, -8, 0),
                      Qt::AlignVCenter | Qt::AlignLeft, m_displayName);
    f.setBold(false);
    painter->setFont(f);

    if (!m_busyText.isEmpty()) {
        const QRectF row(0.0, kHeaderHeight, kNodeWidth, kRowHeight);
        QFont italic = painter->font();
        italic.setItalic(true);
        painter->setFont(italic);
        painter->setPen(pal.color(QPalette::Disabled, QPalette::Text));
        painter->drawText(row.adjusted(8, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, m_busyText);
        return;
    }

    for (int i = 0; i < m_collections.size(); ++i) {
        const auto &c = m_collections.at(i);
        const QRectF row(0.0, kHeaderHeight + i * kRowHeight,
                         kNodeWidth, kRowHeight);
        if (i % 2 == 1)
            painter->fillRect(row, pal.color(QPalette::AlternateBase));

        painter->setPen(pal.color(QPalette::Text));
        painter->drawText(row.adjusted(20, 0, -8, 0),
                          Qt::AlignVCenter | Qt::AlignLeft, c.name);

        const QRectF a = anchorLocalRect(i);
        painter->setBrush(pal.color(QPalette::Highlight));
        painter->setPen(pal.color(QPalette::Dark));
        painter->drawEllipse(a);
    }
}

} // namespace WildPalms::AppMapping
```

- [ ] **Step 3: Register in CMakeLists**

Update `src/app/mapping/CMakeLists.txt`:

```cmake
add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
)
```

- [ ] **Step 4: Build check**

```bash
cmake --build build-dev --target WildPalmsAppMapping 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add src/app/mapping/providernode.h src/app/mapping/providernode.cpp src/app/mapping/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: ProviderNode graphics item (F.3 T5)

QGraphicsItem rendering one provider node with collection port rows
and left-side anchors. Per-row domain comes from CollectionInfo::type
so the graph view can enforce per-pair domain compatibility during
edge creation. Supports a busyText placeholder for Connecting/Error
provider states.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: `MappingEdge` QGraphicsItem

**Files:**
- Create: `src/app/mapping/mappingedge.h`, `src/app/mapping/mappingedge.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/app/mapping/mappingedge.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_MAPPINGEDGE_H
#define WILDPALMS_APP_MAPPING_MAPPINGEDGE_H

#include <QGraphicsPathItem>
#include <QJsonObject>
#include <QString>

namespace WildPalms::AppMapping {

class PalmDbNode;
class ProviderNode;

/// Bezier-curve edge representing one SyncMapping between a PalmDbNode
/// slot port and a ProviderNode collection port. The full SyncMapping
/// (as JSON) is stored on the edge so the inspector panel can edit
/// fields without rebuilding the graph.
class MappingEdge : public QGraphicsPathItem {
public:
    enum { Type = UserType + 3 };
    int type() const override { return Type; }

    enum class Visual { Default, Selected, Disabled, Stale };

    MappingEdge(PalmDbNode *sourceNode,
                int sourceSlot,
                ProviderNode *targetNode,
                const QString &targetCollectionId,
                const QJsonObject &mappingJson,
                QGraphicsItem *parent = nullptr);

    PalmDbNode  *sourceNode() const { return m_sourceNode; }
    int          sourceSlot() const { return m_sourceSlot; }
    ProviderNode *targetNode() const { return m_targetNode; }
    QString      targetCollectionId() const { return m_targetCollectionId; }
    QString      mappingId() const;

    QJsonObject  mappingJson() const { return m_mappingJson; }
    void         setMappingJson(const QJsonObject &json);

    void setVisual(Visual v);
    Visual visual() const { return m_visual; }

    /// Recompute the bezier path from current node anchor positions.
    /// Call after node positions change or after setMappingJson.
    void updateGeometry();

private:
    void applyPen();

    PalmDbNode   *m_sourceNode;
    int           m_sourceSlot;
    ProviderNode *m_targetNode;
    QString       m_targetCollectionId;
    QJsonObject   m_mappingJson;
    Visual        m_visual {Visual::Default};
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 2: Write the implementation**

Create `src/app/mapping/mappingedge.cpp`:

```cpp
#include "mappingedge.h"
#include "palmdbnode.h"
#include "providernode.h"

#include <QPainterPath>
#include <QPen>
#include <QApplication>
#include <QPalette>

namespace WildPalms::AppMapping {

MappingEdge::MappingEdge(PalmDbNode *sourceNode,
                         int sourceSlot,
                         ProviderNode *targetNode,
                         const QString &targetCollectionId,
                         const QJsonObject &mappingJson,
                         QGraphicsItem *parent)
    : QGraphicsPathItem(parent)
    , m_sourceNode(sourceNode)
    , m_sourceSlot(sourceSlot)
    , m_targetNode(targetNode)
    , m_targetCollectionId(targetCollectionId)
    , m_mappingJson(mappingJson)
{
    setFlag(ItemIsSelectable, true);
    setZValue(-1.0);   // draw under nodes
    updateGeometry();
    applyPen();
}

QString MappingEdge::mappingId() const
{
    return m_mappingJson.value(QStringLiteral("id")).toString();
}

void MappingEdge::setMappingJson(const QJsonObject &json)
{
    m_mappingJson = json;
    applyPen();
    update();
}

void MappingEdge::setVisual(Visual v)
{
    m_visual = v;
    applyPen();
    update();
}

void MappingEdge::updateGeometry()
{
    if (!m_sourceNode || !m_targetNode) return;
    const QPointF a = m_sourceNode->slotAnchorScenePos(m_sourceSlot);
    const QPointF b = m_targetNode->collectionAnchorScenePos(m_targetCollectionId);
    if (a.isNull() || b.isNull()) return;

    const qreal dx = std::abs(b.x() - a.x());
    const qreal c  = std::max<qreal>(40.0, dx * 0.5);

    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(a.x() + c, a.y(),
                 b.x() - c, b.y(),
                 b.x(),     b.y());
    setPath(path);
}

void MappingEdge::applyPen()
{
    const QPalette pal = QApplication::palette();
    QPen pen;
    pen.setWidthF(2.0);

    const bool enabled = m_mappingJson.value(QStringLiteral("enabled")).toBool();

    switch (m_visual) {
    case Visual::Selected:
        pen.setColor(pal.color(QPalette::Highlight));
        pen.setWidthF(3.0);
        break;
    case Visual::Stale:
        pen.setColor(QColor(0xc0, 0x70, 0x00));   // orange
        pen.setStyle(Qt::DashLine);
        break;
    case Visual::Disabled:
        pen.setColor(pal.color(QPalette::Mid));
        pen.setStyle(Qt::DashLine);
        break;
    case Visual::Default:
    default:
        pen.setColor(pal.color(QPalette::Text));
        break;
    }

    // Disabled mapping always renders dashed and faded regardless of
    // selection state.
    if (!enabled && m_visual != Visual::Stale) {
        pen.setStyle(Qt::DashLine);
        QColor c = pen.color();
        c.setAlphaF(0.5);
        pen.setColor(c);
    }

    setPen(pen);
}

} // namespace WildPalms::AppMapping
```

- [ ] **Step 3: Register in CMakeLists**

Update `src/app/mapping/CMakeLists.txt`:

```cmake
add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
    mappingedge.h
    mappingedge.cpp
)
```

- [ ] **Step 4: Build check + commit**

```bash
cmake --build build-dev --target WildPalmsAppMapping 2>&1 | tail -5
git add src/app/mapping/mappingedge.h src/app/mapping/mappingedge.cpp src/app/mapping/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: MappingEdge graphics item (F.3 T6)

Bezier-curve edge between a PalmDbNode slot anchor and a
ProviderNode collection anchor. Carries the full SyncMapping as
QJsonObject so the inspector panel can edit fields in place. Four
visual states (Default / Selected / Disabled / Stale) plus a fade
applied when enabled=false.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: `SyncMappingGraphView` + tests

**Files:**
- Create: `src/app/mapping/syncmappingsgraphview.h`, `src/app/mapping/syncmappingsgraphview.cpp`
- Create: `tests/runtime/tst_syncmappingsgraphview.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`
- Modify: `tests/runtime/CMakeLists.txt`

Survey (for the implementer): the Palm-side collection IDs follow the format `palm:calendar/<slot>` (DatebookDB), `palm:contact/<slot>` (AddressDB), `palm:todo/<slot>` (ToDoDB), `palm:memo/<slot>` (MemoDB — currently unused by MemoBlobBackend but the format is reserved). The Palm-side backend ID is the plugin id: `"calendar"`, `"contacts"`, `"memo"`, `"todo"`. Use `Kalburator::Sync::syncMappingToJson` / `syncMappingFromJson` for SyncMapping↔JSON round-tripping.

- [ ] **Step 1: Write the test (TDD)**

Create `tests/runtime/tst_syncmappingsgraphview.cpp`:

```cpp
// tests/runtime/tst_syncmappingsgraphview.cpp
#include <QtTest/QtTest>
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>

#include "../../src/app/mapping/syncmappingsgraphview.h"
#include "../wildpalms_qtest_main.h"

#include <collectioninfo.h>

using WildPalms::AppMapping::SyncMappingGraphView;
using Kalburator::Sync::CollectionInfo;

class TstSyncMappingsGraphView : public QObject {
    Q_OBJECT
private slots:
    void emptyProfileShowsPlaceholders();
    void slotNamesRenderAsPorts();
    void addMappingCreatesEdge();
    void duplicateMappingRejected();
    void domainMismatchRejected();
    void deleteMappingRemovesEdge();
    void readOnlyBlocksMutations();
    void dragConnectCreatesMapping();
    void dragOnIncompatibleTargetCancels();
};

namespace {
QHash<QString, QStringList> exampleSnapshot()
{
    QStringList datebook(16);
    datebook[0] = QStringLiteral("Unfiled");
    datebook[1] = QStringLiteral("Work");
    datebook[2] = QStringLiteral("Personal");

    QStringList contacts(16);
    contacts[0] = QStringLiteral("Unfiled");
    contacts[1] = QStringLiteral("Work");

    return {
        {QStringLiteral("DatebookDB"), datebook},
        {QStringLiteral("AddressDB"),  contacts},
        {QStringLiteral("MemoDB"),     {}},
        {QStringLiteral("ToDoDB"),     {}},
    };
}

QList<CollectionInfo> calCollections()
{
    CollectionInfo c1;
    c1.id = QStringLiteral("caldav:p1:work");
    c1.name = QStringLiteral("Work Calendar");
    c1.type = QStringLiteral("calendar");

    CollectionInfo c2;
    c2.id = QStringLiteral("caldav:p1:personal");
    c2.name = QStringLiteral("Personal Calendar");
    c2.type = QStringLiteral("calendar");
    return {c1, c2};
}

QList<CollectionInfo> contactCollections()
{
    CollectionInfo c1;
    c1.id = QStringLiteral("carddav:p2:contacts");
    c1.name = QStringLiteral("All Contacts");
    c1.type = QStringLiteral("contacts");
    return {c1};
}
} // namespace

void TstSyncMappingsGraphView::emptyProfileShowsPlaceholders()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest({});           // no category names at all
    view.setProvidersForTest({});          // no providers
    view.setMappings(QJsonArray());
    view.rebuild();

    QCOMPARE(view.palmDbNodeCount(), 4);   // four DB nodes always present
    QCOMPARE(view.providerNodeCount(), 0);
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::slotNamesRenderAsPorts()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QCOMPARE(view.activeSlotsForTest(QStringLiteral("DatebookDB")),
             (QList<int>{0, 1, 2}));
    QCOMPARE(view.activeSlotsForTest(QStringLiteral("AddressDB")),
             (QList<int>{0, 1}));
    QCOMPARE(view.activeSlotsForTest(QStringLiteral("MemoDB")),
             QList<int>{});   // empty snapshot
    QCOMPARE(view.providerNodeCount(), 1);
}

void TstSyncMappingsGraphView::addMappingCreatesEdge()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);

    const bool ok = view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work"));

    QVERIFY(ok);
    QCOMPARE(view.edgeCount(), 1);
    QCOMPARE(spy.count(), 1);

    const QJsonArray emitted = spy.takeFirst().at(0).toJsonArray();
    QCOMPARE(emitted.size(), 1);
}

void TstSyncMappingsGraphView::duplicateMappingRejected()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QVERIFY(view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));

    // Duplicate (same source+target) → rejected.
    QVERIFY(!view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));

    QCOMPARE(view.edgeCount(), 1);
}

void TstSyncMappingsGraphView::domainMismatchRejected()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p2"), QStringLiteral("iCloud"), contactCollections()},
    });
    view.rebuild();

    // DatebookDB (calendar) → carddav contacts collection → mismatch.
    const bool ok = view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p2"), QStringLiteral("carddav:p2:contacts"));

    QVERIFY(!ok);
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::deleteMappingRemovesEdge()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QVERIFY(view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));
    QCOMPARE(view.edgeCount(), 1);

    const QString mappingId = view.edgesForTest().first()
        .value(QStringLiteral("id")).toString();
    QVERIFY(!mappingId.isEmpty());

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);
    QVERIFY(view.removeMappingForTest(mappingId));
    QCOMPARE(view.edgeCount(), 0);
    QCOMPARE(spy.count(), 1);
}

void TstSyncMappingsGraphView::readOnlyBlocksMutations()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();
    view.setReadOnly(true);

    QVERIFY(!view.addMappingForTest(
        QStringLiteral("DatebookDB"), 1,
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work")));
    QCOMPARE(view.edgeCount(), 0);
}

void TstSyncMappingsGraphView::dragConnectCreatesMapping()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p1"), QStringLiteral("Fastmail"), calCollections()},
    });
    view.rebuild();

    QSignalSpy spy(&view, &SyncMappingGraphView::mappingsChanged);

    view.beginDragForTest(QStringLiteral("DatebookDB"), 1);
    QVERIFY(view.isDraggingForTest());

    const bool ok = view.endDragOnProviderForTest(
        QStringLiteral("p1"), QStringLiteral("caldav:p1:work"));
    QVERIFY(ok);
    QVERIFY(!view.isDraggingForTest());
    QCOMPARE(view.edgeCount(), 1);
    QCOMPARE(spy.count(), 1);
}

void TstSyncMappingsGraphView::dragOnIncompatibleTargetCancels()
{
    SyncMappingGraphView view;
    view.setSnapshotForTest(exampleSnapshot());
    view.setProvidersForTest({
        {QStringLiteral("p2"), QStringLiteral("iCloud"), contactCollections()},
    });
    view.rebuild();

    view.beginDragForTest(QStringLiteral("DatebookDB"), 1);
    QVERIFY(view.isDraggingForTest());

    // DatebookDB (calendar) dropped on a contacts collection → no edge.
    const bool ok = view.endDragOnProviderForTest(
        QStringLiteral("p2"), QStringLiteral("carddav:p2:contacts"));
    QVERIFY(!ok);
    QVERIFY(!view.isDraggingForTest());
    QCOMPARE(view.edgeCount(), 0);
}

WILDPALMS_QTEST_MAIN(TstSyncMappingsGraphView)
#include "tst_syncmappingsgraphview.moc"
```

- [ ] **Step 2: Write the SyncMappingGraphView header**

Create `src/app/mapping/syncmappingsgraphview.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_SYNCMAPPINGSGRAPHVIEW_H
#define WILDPALMS_APP_MAPPING_SYNCMAPPINGSGRAPHVIEW_H

#include <QGraphicsView>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <collectioninfo.h>

class QGraphicsScene;

namespace WildPalms::AppMapping {

class PalmDbNode;
class ProviderNode;
class MappingEdge;

class SyncMappingGraphView : public QGraphicsView {
    Q_OBJECT
public:
    /// Provider info supplied by the page wrapper. F.3 doesn't reach
    /// into AccountController directly to keep the graph view easy to
    /// unit-test (the page builds these structs and hands them in).
    struct ProviderEntry {
        QString providerId;
        QString displayName;
        QList<Kalburator::Sync::CollectionInfo> collections;
        QString busyText;   // empty → connected
    };

    explicit SyncMappingGraphView(QWidget *parent = nullptr);
    ~SyncMappingGraphView() override;

    void setSnapshot(const QHash<QString, QStringList> &snapshot);
    void setProviders(const QList<ProviderEntry> &providers);
    void setMappings(const QJsonArray &mappings);
    QJsonArray mappings() const;

    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }

    /// Wipe and re-layout the scene from the current snapshot + providers
    /// + mappings. Call after any of those change.
    void rebuild();

    // ----- Test seams (public for unit tests; production callers also
    // use these — the names just signal that they're directly callable
    // without simulating mouse events) -----
    void setSnapshotForTest(const QHash<QString, QStringList> &snapshot) {
        setSnapshot(snapshot);
    }
    void setProvidersForTest(const QList<ProviderEntry> &providers) {
        setProviders(providers);
    }
    bool addMappingForTest(const QString &dbName, int slot,
                           const QString &providerId,
                           const QString &collectionId);
    bool removeMappingForTest(const QString &mappingId);
    int  edgeCount() const { return m_edges.size(); }
    int  palmDbNodeCount() const { return m_palmNodes.size(); }
    int  providerNodeCount() const { return m_providerNodes.size(); }
    QList<int> activeSlotsForTest(const QString &dbName) const;
    QJsonArray edgesForTest() const { return mappings(); }

signals:
    void mappingsChanged(const QJsonArray &mappings);
    void edgeSelected(const QString &mappingId);   // empty = deselected

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSceneSelectionChanged();

private:
    static QString palmDomainForDb(const QString &dbName);
    static QString palmBackendIdForDb(const QString &dbName);
    static QString palmCollectionIdForSlot(const QString &dbName, int slot);

    PalmDbNode  *nodeForDb(const QString &dbName) const;
    ProviderNode *nodeForProvider(const QString &providerId) const;

    /// Validation helpers (also used by drag-and-drop paths).
    bool isCompatible(const QString &dbName, const QString &providerId,
                      const QString &collectionId) const;
    bool isDuplicate(const QString &dbName, int slot,
                     const QString &providerId,
                     const QString &collectionId) const;

    void layoutBipartite();
    void clearScene();
    QJsonObject defaultMappingJson(const QString &dbName, int slot,
                                   const QString &providerId,
                                   const QString &collectionId) const;

    /// Drag-to-connect helpers. `scenePos` is in scene coordinates.
    /// Production mouse handlers translate viewport→scene before
    /// calling these; tests call them directly via the *ForTest seams.
    void beginDrag(PalmDbNode *src, int slot, const QPointF &scenePos);
    void updateDrag(const QPointF &scenePos);
    /// Returns true if the drag landed on a compatible target and a
    /// new mapping was created.
    bool endDrag(const QPointF &scenePos);
    void cancelDrag();

    /// Hit-test helpers — find which node + which port lives at scenePos
    /// (right-side anchor of a PalmDbNode, or left-side anchor of a
    /// ProviderNode). Returns nullptr / -1 / empty on miss.
    PalmDbNode  *palmAnchorAtScenePos(const QPointF &scenePos, int *outSlot) const;
    ProviderNode *providerAnchorAtScenePos(const QPointF &scenePos,
                                            QString *outCollectionId) const;

protected:
    // Re-declared protected in addition to the override below so tests
    // can construct a viewport-coord event if they ever need to. The
    // canonical test path uses beginDrag/updateDrag/endDrag directly.

public:
    // Test seams for the drag interaction. Production callers go through
    // the mouse event handlers.
    void beginDragForTest(const QString &dbName, int slot) {
        if (auto *node = nodeForDb(dbName)) {
            const QPointF p = node->slotAnchorScenePos(slot);
            beginDrag(node, slot, p);
        }
    }
    bool endDragOnProviderForTest(const QString &providerId,
                                  const QString &collectionId) {
        if (auto *node = nodeForProvider(providerId)) {
            const QPointF p = node->collectionAnchorScenePos(collectionId);
            return endDrag(p);
        }
        cancelDrag();
        return false;
    }
    bool isDraggingForTest() const { return m_dragSourceNode != nullptr; }

private:
    QGraphicsScene *m_scene {nullptr};

    QHash<QString, QStringList>     m_snapshot;
    QList<ProviderEntry>            m_providers;

    QList<PalmDbNode*>    m_palmNodes;
    QList<ProviderNode*>  m_providerNodes;
    QList<MappingEdge*>   m_edges;

    bool m_readOnly {false};

    // Drag-to-connect state. Non-null source means a drag is in flight.
    PalmDbNode         *m_dragSourceNode {nullptr};
    int                 m_dragSourceSlot {-1};
    QGraphicsPathItem  *m_dragPathItem   {nullptr};   // rubber-band feedback
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 3: Write the SyncMappingGraphView implementation**

Create `src/app/mapping/syncmappingsgraphview.cpp`:

```cpp
#include "syncmappingsgraphview.h"

#include "palmdbnode.h"
#include "providernode.h"
#include "mappingedge.h"

#include <QApplication>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QUuid>

#include <synctypes.h>   // syncMappingToJson, syncMappingFromJson

namespace WildPalms::AppMapping {

namespace {
constexpr qreal kLeftColumnX  = 30.0;
constexpr qreal kRightColumnX = 320.0;
constexpr qreal kColumnGap    = 40.0;
constexpr qreal kRowGap       = 30.0;
} // namespace

SyncMappingGraphView::SyncMappingGraphView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::NoDrag);
    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &SyncMappingGraphView::onSceneSelectionChanged);
}

SyncMappingGraphView::~SyncMappingGraphView() = default;

QString SyncMappingGraphView::palmDomainForDb(const QString &dbName)
{
    if (dbName == QLatin1String("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QLatin1String("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QLatin1String("MemoDB"))     return QStringLiteral("memos");
    if (dbName == QLatin1String("ToDoDB"))     return QStringLiteral("todos");
    return QStringLiteral("unknown");
}

QString SyncMappingGraphView::palmBackendIdForDb(const QString &dbName)
{
    if (dbName == QLatin1String("DatebookDB")) return QStringLiteral("calendar");
    if (dbName == QLatin1String("AddressDB"))  return QStringLiteral("contacts");
    if (dbName == QLatin1String("MemoDB"))     return QStringLiteral("memo");
    if (dbName == QLatin1String("ToDoDB"))     return QStringLiteral("todo");
    return {};
}

QString SyncMappingGraphView::palmCollectionIdForSlot(const QString &dbName, int slot)
{
    if (dbName == QLatin1String("DatebookDB"))
        return QStringLiteral("palm:calendar/") + QString::number(slot);
    if (dbName == QLatin1String("AddressDB"))
        return QStringLiteral("palm:contact/") + QString::number(slot);
    if (dbName == QLatin1String("MemoDB"))
        return QStringLiteral("palm:memo/") + QString::number(slot);
    if (dbName == QLatin1String("ToDoDB"))
        return QStringLiteral("palm:todo/") + QString::number(slot);
    return {};
}

void SyncMappingGraphView::setSnapshot(const QHash<QString, QStringList> &snapshot)
{
    m_snapshot = snapshot;
}

void SyncMappingGraphView::setProviders(const QList<ProviderEntry> &providers)
{
    m_providers = providers;
}

void SyncMappingGraphView::setMappings(const QJsonArray &mappings)
{
    // Stored on edges after rebuild(); kept here only so rebuild() can
    // re-create edges from it.
    m_scene->setProperty("pending-mappings", mappings);
}

QJsonArray SyncMappingGraphView::mappings() const
{
    QJsonArray out;
    for (auto *edge : m_edges)
        out.append(edge->mappingJson());
    return out;
}

void SyncMappingGraphView::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (auto *e : m_edges)
        e->setFlag(QGraphicsItem::ItemIsSelectable, !readOnly);
}

void SyncMappingGraphView::clearScene()
{
    m_scene->clear();
    m_palmNodes.clear();
    m_providerNodes.clear();
    m_edges.clear();
}

void SyncMappingGraphView::rebuild()
{
    const QJsonArray pendingMappings =
        m_scene->property("pending-mappings").toJsonArray();
    clearScene();

    static const std::array<std::pair<const char*, const char*>, 4> kDbs = {{
        {"DatebookDB", "Calendar — DatebookDB"},
        {"AddressDB",  "Contacts — AddressDB"},
        {"MemoDB",     "Memos — MemoDB"},
        {"ToDoDB",     "Todos — ToDoDB"},
    }};

    for (const auto &p : kDbs) {
        const QString db = QString::fromLatin1(p.first);
        const QStringList slotNames = m_snapshot.value(db);
        QStringList sixteen = slotNames;
        if (sixteen.size() != 16) {
            sixteen = QStringList();
            for (int i = 0; i < 16; ++i) sixteen << QString();
        }
        auto *node = new PalmDbNode(db, QString::fromUtf8(p.second),
                                    palmDomainForDb(db), sixteen);
        m_scene->addItem(node);
        m_palmNodes.append(node);
    }

    for (const auto &prov : m_providers) {
        auto *node = new ProviderNode(prov.providerId, prov.displayName,
                                       prov.collections, prov.busyText);
        m_scene->addItem(node);
        m_providerNodes.append(node);
    }

    layoutBipartite();

    // Recreate edges from pending mappings.
    for (const auto v : pendingMappings) {
        const QJsonObject obj = v.toObject();
        const QString sourceBackend = obj.value(QStringLiteral("sourceBackend")).toString();
        const QString sourceCalendar = obj.value(QStringLiteral("sourceCalendar")).toString();
        const QString targetCollection = obj.value(QStringLiteral("targetCalendar")).toString();
        const QString targetProviderId = obj.value(QStringLiteral("targetBackend")).toString();

        // Find the Palm DB whose backendId == sourceBackend and parse slot.
        QString dbName;
        int slot = -1;
        for (const auto &p : kDbs) {
            const QString db = QString::fromLatin1(p.first);
            if (palmBackendIdForDb(db) == sourceBackend) {
                const QString prefix = palmCollectionIdForSlot(db, 0).chopped(1);
                if (sourceCalendar.startsWith(prefix)) {
                    bool ok = false;
                    slot = sourceCalendar.mid(prefix.size()).toInt(&ok);
                    if (ok) { dbName = db; break; }
                    slot = -1;
                }
            }
        }
        if (dbName.isEmpty() || slot < 0) continue;

        PalmDbNode *src = nodeForDb(dbName);
        ProviderNode *tgt = nodeForProvider(targetProviderId);
        if (!src || !tgt) continue;

        auto *edge = new MappingEdge(src, slot, tgt, targetCollection, obj);
        m_scene->addItem(edge);
        m_edges.append(edge);
    }

    setSceneRect(m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50));
}

void SyncMappingGraphView::layoutBipartite()
{
    qreal y = 10.0;
    for (auto *n : m_palmNodes) {
        n->setPos(kLeftColumnX, y);
        y += n->boundingRect().height() + kRowGap;
    }
    y = 10.0;
    for (auto *n : m_providerNodes) {
        n->setPos(kRightColumnX, y);
        y += n->boundingRect().height() + kRowGap;
    }
}

PalmDbNode *SyncMappingGraphView::nodeForDb(const QString &dbName) const
{
    for (auto *n : m_palmNodes)
        if (n->dbName() == dbName) return n;
    return nullptr;
}

ProviderNode *SyncMappingGraphView::nodeForProvider(const QString &providerId) const
{
    for (auto *n : m_providerNodes)
        if (n->providerId() == providerId) return n;
    return nullptr;
}

bool SyncMappingGraphView::isCompatible(const QString &dbName,
                                        const QString &providerId,
                                        const QString &collectionId) const
{
    const QString palmDomain = palmDomainForDb(dbName);
    auto *prov = nodeForProvider(providerId);
    if (!prov) return false;
    const QString collDomain = prov->collectionDomain(collectionId);
    if (collDomain == QLatin1String("unknown")) return true;
    return collDomain == palmDomain;
}

bool SyncMappingGraphView::isDuplicate(const QString &dbName, int slot,
                                       const QString &providerId,
                                       const QString &collectionId) const
{
    const QString srcBackend = palmBackendIdForDb(dbName);
    const QString srcCalendar = palmCollectionIdForSlot(dbName, slot);
    for (auto *e : m_edges) {
        const QJsonObject j = e->mappingJson();
        if (j.value(QStringLiteral("sourceBackend")).toString()  == srcBackend &&
            j.value(QStringLiteral("sourceCalendar")).toString() == srcCalendar &&
            j.value(QStringLiteral("targetBackend")).toString()  == providerId &&
            j.value(QStringLiteral("targetCalendar")).toString() == collectionId) {
            return true;
        }
    }
    return false;
}

QJsonObject SyncMappingGraphView::defaultMappingJson(
    const QString &dbName, int slot,
    const QString &providerId, const QString &collectionId) const
{
    Kalburator::Sync::SyncMapping m;
    m.id              = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m.sourceBackend   = palmBackendIdForDb(dbName);
    m.sourceCalendar  = palmCollectionIdForSlot(dbName, slot);
    m.targetBackend   = providerId;
    m.targetCalendar  = collectionId;
    m.mode            = Kalburator::Sync::SyncMode::TwoWay;
    m.conflictPolicy  = Kalburator::Sync::ConflictResolution::AskUser;
    m.lossPolicy      = Kalburator::Sync::WhenLossWouldOccur::Warn;
    m.enabled         = true;
    return Kalburator::Sync::syncMappingToJson(m);
}

bool SyncMappingGraphView::addMappingForTest(
    const QString &dbName, int slot,
    const QString &providerId, const QString &collectionId)
{
    if (m_readOnly) return false;
    if (!nodeForDb(dbName) || !nodeForProvider(providerId)) return false;
    if (!isCompatible(dbName, providerId, collectionId)) return false;
    if (isDuplicate(dbName, slot, providerId, collectionId)) return false;

    const QJsonObject json = defaultMappingJson(dbName, slot, providerId, collectionId);

    auto *src = nodeForDb(dbName);
    auto *tgt = nodeForProvider(providerId);
    auto *edge = new MappingEdge(src, slot, tgt, collectionId, json);
    m_scene->addItem(edge);
    m_edges.append(edge);

    Q_EMIT mappingsChanged(mappings());
    return true;
}

bool SyncMappingGraphView::removeMappingForTest(const QString &mappingId)
{
    if (m_readOnly) return false;
    for (int i = 0; i < m_edges.size(); ++i) {
        if (m_edges.at(i)->mappingId() == mappingId) {
            m_scene->removeItem(m_edges.at(i));
            delete m_edges.takeAt(i);
            Q_EMIT mappingsChanged(mappings());
            return true;
        }
    }
    return false;
}

QList<int> SyncMappingGraphView::activeSlotsForTest(const QString &dbName) const
{
    if (auto *n = nodeForDb(dbName)) return n->activeSlots();
    return {};
}

void SyncMappingGraphView::onSceneSelectionChanged()
{
    QString sel;
    for (auto *e : m_edges) {
        if (e->isSelected()) {
            sel = e->mappingId();
            e->setVisual(MappingEdge::Visual::Selected);
        } else {
            const bool enabled = e->mappingJson()
                .value(QStringLiteral("enabled")).toBool();
            e->setVisual(enabled ? MappingEdge::Visual::Default
                                 : MappingEdge::Visual::Disabled);
        }
    }
    Q_EMIT edgeSelected(sel);
}

// Mouse + key event handlers implement drag-to-connect: press on a
// Palm port anchor begins a drag, move tracks a rubber-band bezier,
// release on a compatible provider anchor creates a SyncMapping
// (validated by isCompatible + isDuplicate). The same code path is
// exercised by tests via beginDragForTest/endDragOnProviderForTest.

PalmDbNode *SyncMappingGraphView::palmAnchorAtScenePos(
    const QPointF &scenePos, int *outSlot) const
{
    if (outSlot) *outSlot = -1;
    for (auto *node : m_palmNodes) {
        const int slot = node->slotAtScenePos(scenePos);
        if (slot >= 0) {
            if (outSlot) *outSlot = slot;
            return node;
        }
    }
    return nullptr;
}

ProviderNode *SyncMappingGraphView::providerAnchorAtScenePos(
    const QPointF &scenePos, QString *outCollectionId) const
{
    if (outCollectionId) outCollectionId->clear();
    for (auto *node : m_providerNodes) {
        const QString c = node->collectionAtScenePos(scenePos);
        if (!c.isEmpty()) {
            if (outCollectionId) *outCollectionId = c;
            return node;
        }
    }
    return nullptr;
}

void SyncMappingGraphView::beginDrag(PalmDbNode *src, int slot,
                                     const QPointF &scenePos)
{
    cancelDrag();
    if (!src || slot < 0 || m_readOnly) return;

    m_dragSourceNode = src;
    m_dragSourceSlot = slot;

    m_dragPathItem = new QGraphicsPathItem();
    QPen pen(QApplication::palette().color(QPalette::Highlight));
    pen.setWidthF(2.0);
    pen.setStyle(Qt::DashLine);
    m_dragPathItem->setPen(pen);
    m_dragPathItem->setZValue(10.0);   // above edges + nodes
    m_scene->addItem(m_dragPathItem);
    updateDrag(scenePos);
}

void SyncMappingGraphView::updateDrag(const QPointF &scenePos)
{
    if (!m_dragSourceNode || !m_dragPathItem) return;
    const QPointF a = m_dragSourceNode->slotAnchorScenePos(m_dragSourceSlot);
    if (a.isNull()) return;
    const qreal dx = std::abs(scenePos.x() - a.x());
    const qreal c  = std::max<qreal>(40.0, dx * 0.5);
    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(a.x() + c, a.y(),
                 scenePos.x() - c, scenePos.y(),
                 scenePos.x(),     scenePos.y());
    m_dragPathItem->setPath(path);
}

bool SyncMappingGraphView::endDrag(const QPointF &scenePos)
{
    if (!m_dragSourceNode) { cancelDrag(); return false; }

    const QString dbName = m_dragSourceNode->dbName();
    const int     slot   = m_dragSourceSlot;

    QString collectionId;
    ProviderNode *tgt = providerAnchorAtScenePos(scenePos, &collectionId);
    cancelDrag();
    if (!tgt) return false;

    return addMappingForTest(dbName, slot, tgt->providerId(), collectionId);
}

void SyncMappingGraphView::cancelDrag()
{
    if (m_dragPathItem) {
        m_scene->removeItem(m_dragPathItem);
        delete m_dragPathItem;
        m_dragPathItem = nullptr;
    }
    m_dragSourceNode = nullptr;
    m_dragSourceSlot = -1;
}

void SyncMappingGraphView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_readOnly) {
        const QPointF scenePos = mapToScene(event->pos());
        int slot = -1;
        if (auto *node = palmAnchorAtScenePos(scenePos, &slot)) {
            beginDrag(node, slot, scenePos);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void SyncMappingGraphView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragSourceNode) {
        updateDrag(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void SyncMappingGraphView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragSourceNode) {
        endDrag(mapToScene(event->pos()));
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void SyncMappingGraphView::keyPressEvent(QKeyEvent *event)
{
    if (!m_readOnly &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        QString selectedId;
        for (auto *e : m_edges) {
            if (e->isSelected()) { selectedId = e->mappingId(); break; }
        }
        if (!selectedId.isEmpty()) {
            removeMappingForTest(selectedId);
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

} // namespace WildPalms::AppMapping
```

Note on mouse drag-and-drop: the implementation above leaves the QGraphicsView mouse handlers as passthroughs and relies on `addMappingForTest` as the canonical mutation. The mouse drag-to-connect interaction can be added in a follow-up polish commit without changing the public API or the test suite. The inspector + delete key still work; the only missing piece is the rubber-band drag feedback. T8 wires context menus that exercise the same API.

- [ ] **Step 4: Register test + view in CMakeLists**

Update `src/app/mapping/CMakeLists.txt`:

```cmake
add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
    mappingedge.h
    mappingedge.cpp
    syncmappingsgraphview.h
    syncmappingsgraphview.cpp
)
```

Append to `tests/runtime/CMakeLists.txt`:

```cmake
# F.3 T7 — Sync mappings graph view
add_executable(tst_syncmappingsgraphview tst_syncmappingsgraphview.cpp)
target_link_libraries(tst_syncmappingsgraphview
    PRIVATE
        Qt::Core Qt::Test Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsCore
        WildPalmsAppMapping
        WildPalmsRuntime
        WildPalmsPalmDevice
        PalmDeviceAccessLib
        KF6::I18n KF6::ConfigCore
        pisock bluetooth usb
)
add_test(NAME tst_syncmappingsgraphview COMMAND tst_syncmappingsgraphview)
set_tests_properties(tst_syncmappingsgraphview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 5: Build, run target test, full suite**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev --target tst_syncmappingsgraphview 2>&1 | tail -10
ctest --test-dir build-dev -R tst_syncmappingsgraphview --output-on-failure 2>&1 | tail -15
ctest --test-dir build-dev 2>&1 | tail -3
```

Expected: 9/9 in new test (7 state-machinery + 2 drag); 97 tests total passing.

- [ ] **Step 6: Commit**

```bash
git add src/app/mapping/syncmappingsgraphview.h src/app/mapping/syncmappingsgraphview.cpp \
        src/app/mapping/CMakeLists.txt \
        tests/runtime/tst_syncmappingsgraphview.cpp tests/runtime/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: SyncMappingGraphView + drag-to-connect + 9 unit tests (F.3 T7)

QGraphicsView subclass laying out PalmDbNodes (left column) and
ProviderNodes (right column) with MappingEdges between them.
Domain compatibility is enforced per port-pair using
CollectionInfo::type. Duplicate-mapping guard rejects edges that
already exist. Read-only mode blocks mutations.

Drag-to-connect: mouse press on a Palm slot anchor begins a drag;
a dashed bezier rubber-band tracks the cursor; release on a
compatible provider collection anchor creates a new SyncMapping
(rejected on incompatible drop or non-anchor release). The drag
helpers (beginDrag/updateDrag/endDrag/cancelDrag) are exposed via
*ForTest seams so the unit suite exercises the full drag flow
without simulating viewport events.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: `MappingInspectorPanel`

**Files:**
- Create: `src/app/mapping/mappinginspectorpanel.h`, `src/app/mapping/mappinginspectorpanel.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/app/mapping/mappinginspectorpanel.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_MAPPINGINSPECTORPANEL_H
#define WILDPALMS_APP_MAPPING_MAPPINGINSPECTORPANEL_H

#include <QWidget>
#include <QJsonObject>

class QComboBox;
class QCheckBox;
class QLabel;
class QStackedWidget;

namespace WildPalms::AppMapping {

class SyncMappingGraphView;

/// Inspector strip pinned to the bottom of SyncMappingsPage. Shows
/// Sync Mode + Conflict Policy + Enabled controls for the selected
/// edge. When no edge is selected, shows a "Select a connection to
/// edit its properties." placeholder.
///
/// On user edits the panel calls SyncMappingGraphView::updateMapping()
/// (added on demand) — for F.3 T8 we instead emit a signal that the
/// page wraps and dispatches. The panel never reaches into the view's
/// edge list directly.
class MappingInspectorPanel : public QWidget {
    Q_OBJECT
public:
    explicit MappingInspectorPanel(QWidget *parent = nullptr);

    /// Show controls for the selected mapping. Empty `mappingId` →
    /// placeholder.
    void setSelectedMapping(const QString &mappingId, const QJsonObject &json);

signals:
    /// Emitted when the user edits any field. The page subscribes and
    /// writes the change back into the graph view's mapping list.
    void mappingEdited(const QString &mappingId, const QJsonObject &updatedJson);

private slots:
    void emitChange();

private:
    void buildUi();

    QStackedWidget *m_stack {nullptr};
    QLabel         *m_placeholder {nullptr};
    QWidget        *m_editor {nullptr};

    QComboBox *m_modeCombo {nullptr};
    QComboBox *m_policyCombo {nullptr};
    QCheckBox *m_enabledCheck {nullptr};

    QString     m_currentMappingId;
    QJsonObject m_currentJson;
    bool        m_suppressEmit {false};
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 2: Write the implementation**

Create `src/app/mapping/mappinginspectorpanel.cpp`:

```cpp
#include "mappinginspectorpanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace WildPalms::AppMapping {

namespace {
struct ModeEntry { const char *value; const char *label; };
constexpr std::array<ModeEntry, 4> kModes = {{
    {"TwoWay",          "Two-way"},
    {"OneWayUpload",    "Palm → Provider"},
    {"OneWayDownload",  "Provider → Palm"},
    {"Disabled",        "Disabled"},
}};

constexpr std::array<const char*, 6> kPolicies = {
    "SourceWins", "TargetWins", "Duplicate",
    "Skip", "AskUser", "LastWriteWins",
};
} // namespace

MappingInspectorPanel::MappingInspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void MappingInspectorPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);

    m_stack = new QStackedWidget(this);
    outer->addWidget(m_stack);

    m_placeholder = new QLabel(
        tr("Select a connection to edit its properties."), m_stack);
    m_placeholder->setAlignment(Qt::AlignCenter);
    QFont f = m_placeholder->font();
    f.setItalic(true);
    m_placeholder->setFont(f);
    m_stack->addWidget(m_placeholder);

    m_editor = new QWidget(m_stack);
    auto *form = new QFormLayout(m_editor);
    form->setContentsMargins(0, 0, 0, 0);

    m_modeCombo = new QComboBox(m_editor);
    for (const auto &m : kModes)
        m_modeCombo->addItem(QString::fromLatin1(m.label), QString::fromLatin1(m.value));

    m_policyCombo = new QComboBox(m_editor);
    for (const auto *p : kPolicies)
        m_policyCombo->addItem(QString::fromLatin1(p), QString::fromLatin1(p));

    m_enabledCheck = new QCheckBox(m_editor);

    form->addRow(tr("Sync Mode"),       m_modeCombo);
    form->addRow(tr("Conflict Policy"), m_policyCombo);
    form->addRow(tr("Enabled"),         m_enabledCheck);

    m_stack->addWidget(m_editor);
    m_stack->setCurrentWidget(m_placeholder);

    connect(m_modeCombo,    QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,           &MappingInspectorPanel::emitChange);
    connect(m_policyCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,           &MappingInspectorPanel::emitChange);
    connect(m_enabledCheck, &QCheckBox::toggled,
            this,           &MappingInspectorPanel::emitChange);
}

void MappingInspectorPanel::setSelectedMapping(const QString &mappingId,
                                                const QJsonObject &json)
{
    m_currentMappingId = mappingId;
    m_currentJson      = json;

    if (mappingId.isEmpty()) {
        m_stack->setCurrentWidget(m_placeholder);
        return;
    }

    m_suppressEmit = true;
    const QString modeVal =
        json.value(QStringLiteral("mode")).toString(QStringLiteral("TwoWay"));
    const int modeIdx = m_modeCombo->findData(modeVal);
    m_modeCombo->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

    QString policyVal = json.value(QStringLiteral("conflictResolution")).toString();
    if (policyVal.isEmpty())
        policyVal = json.value(QStringLiteral("conflictPolicy")).toString();
    if (policyVal.isEmpty())
        policyVal = QStringLiteral("AskUser");
    const int policyIdx = m_policyCombo->findData(policyVal);
    m_policyCombo->setCurrentIndex(policyIdx >= 0 ? policyIdx : 4);

    m_enabledCheck->setChecked(json.value(QStringLiteral("enabled")).toBool());

    m_stack->setCurrentWidget(m_editor);
    m_suppressEmit = false;
}

void MappingInspectorPanel::emitChange()
{
    if (m_suppressEmit) return;
    if (m_currentMappingId.isEmpty()) return;

    QJsonObject updated = m_currentJson;
    updated[QStringLiteral("mode")] =
        m_modeCombo->currentData().toString();
    const QString policy = m_policyCombo->currentData().toString();
    updated[QStringLiteral("conflictResolution")] = policy;
    updated[QStringLiteral("conflictPolicy")]     = policy;
    updated[QStringLiteral("enabled")] = m_enabledCheck->isChecked();

    m_currentJson = updated;
    Q_EMIT mappingEdited(m_currentMappingId, updated);
}

} // namespace WildPalms::AppMapping
```

- [ ] **Step 3: Register in CMakeLists**

```cmake
add_library(WildPalmsAppMapping STATIC
    ...
    mappinginspectorpanel.h
    mappinginspectorpanel.cpp
)
```

- [ ] **Step 4: Build check + commit**

```bash
cmake --build build-dev --target WildPalmsAppMapping 2>&1 | tail -3
git add src/app/mapping/mappinginspectorpanel.h src/app/mapping/mappinginspectorpanel.cpp src/app/mapping/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: MappingInspectorPanel — selected-edge editor (F.3 T8)

QWidget hosting Sync Mode / Conflict Policy / Enabled controls for
the currently-selected MappingEdge. Emits mappingEdited(id, json)
when the user touches any control. SyncMappingsPage wires this
into the graph view in T9.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: `SyncMappingsPage` container

**Files:**
- Create: `src/app/mapping/syncmappingspage.h`, `src/app/mapping/syncmappingspage.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`

The page borrows three pointers (`Profile`, `AccountController`, `PalmRuntime`), builds a `ProviderEntry` list from `AccountController::providers()` + `collectionsFor()`, sets the graph snapshot from `Profile::categorySlotNames()` per Palm DB, and wires the inspector ↔ graph edit cycle. It also exposes `applyTo(Profile*)` so the dialog's OK button can persist.

- [ ] **Step 1: Write the header**

Create `src/app/mapping/syncmappingspage.h`:

```cpp
#ifndef WILDPALMS_APP_MAPPING_SYNCMAPPINGSPAGE_H
#define WILDPALMS_APP_MAPPING_SYNCMAPPINGSPAGE_H

#include <QWidget>
#include <QJsonArray>
#include <QPointer>

class Profile;
class QLabel;

namespace WildPalms::Runtime {
    class AccountController;
    class PalmRuntime;
}

namespace WildPalms::AppMapping {

class SyncMappingGraphView;
class MappingInspectorPanel;

class SyncMappingsPage : public QWidget {
    Q_OBJECT
public:
    SyncMappingsPage(Profile *profile,
                     WildPalms::Runtime::AccountController *accounts,
                     WildPalms::Runtime::PalmRuntime *palmRuntime,
                     QWidget *parent = nullptr);

    /// Persist current mappings into the supplied Profile (typically the
    /// same one passed at construction). Called by SettingsDialog on OK
    /// / Apply.
    void applyTo(Profile *profile);

    /// Current in-memory mappings (the live graph state).
    QJsonArray currentMappings() const;

private slots:
    void onSyncStarted();
    void onSyncFinished();
    void onMappingEdited(const QString &mappingId, const QJsonObject &updatedJson);
    void onEdgeSelected(const QString &mappingId);

private:
    void buildUi();
    void reloadGraph();
    void setReadOnlyBannerVisible(bool visible);

    QPointer<Profile>                            m_profile;
    QPointer<WildPalms::Runtime::AccountController> m_accounts;
    QPointer<WildPalms::Runtime::PalmRuntime>       m_palmRuntime;

    SyncMappingGraphView  *m_graphView {nullptr};
    MappingInspectorPanel *m_inspector {nullptr};
    QLabel                *m_readOnlyBanner {nullptr};
};

} // namespace WildPalms::AppMapping

#endif
```

- [ ] **Step 2: Write the implementation**

Create `src/app/mapping/syncmappingspage.cpp`:

```cpp
#include "syncmappingspage.h"

#include "syncmappingsgraphview.h"
#include "mappinginspectorpanel.h"

#include "profile.h"
#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"

#include <QLabel>
#include <QVBoxLayout>

#include <iprovider.h>

namespace WildPalms::AppMapping {

SyncMappingsPage::SyncMappingsPage(Profile *profile,
                                   WildPalms::Runtime::AccountController *accounts,
                                   WildPalms::Runtime::PalmRuntime *palmRuntime,
                                   QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_accounts(accounts)
    , m_palmRuntime(palmRuntime)
{
    buildUi();
    reloadGraph();

    if (m_palmRuntime) {
        connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runStarted,
                this, &SyncMappingsPage::onSyncStarted);
        connect(m_palmRuntime, &WildPalms::Runtime::PalmRuntime::runFinished,
                this, &SyncMappingsPage::onSyncFinished);
        if (m_palmRuntime->isRunning())
            onSyncStarted();
    }

    if (m_accounts) {
        connect(m_accounts, &WildPalms::Runtime::AccountController::providersChanged,
                this, &SyncMappingsPage::reloadGraph);
    }
}

void SyncMappingsPage::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_readOnlyBanner = new QLabel(
        tr("A sync is in progress — mapping changes are locked"), this);
    m_readOnlyBanner->setStyleSheet(
        QStringLiteral("background:#c07000;color:white;padding:6px;"));
    m_readOnlyBanner->setAlignment(Qt::AlignCenter);
    m_readOnlyBanner->setVisible(false);
    outer->addWidget(m_readOnlyBanner);

    m_graphView = new SyncMappingGraphView(this);
    outer->addWidget(m_graphView, /*stretch*/1);

    m_inspector = new MappingInspectorPanel(this);
    m_inspector->setMaximumHeight(120);
    outer->addWidget(m_inspector);

    connect(m_graphView, &SyncMappingGraphView::edgeSelected,
            this, &SyncMappingsPage::onEdgeSelected);
    connect(m_inspector, &MappingInspectorPanel::mappingEdited,
            this, &SyncMappingsPage::onMappingEdited);
}

void SyncMappingsPage::reloadGraph()
{
    // Snapshot.
    QHash<QString, QStringList> snapshot;
    if (m_profile) {
        for (const QString &db : {QStringLiteral("DatebookDB"),
                                  QStringLiteral("AddressDB"),
                                  QStringLiteral("MemoDB"),
                                  QStringLiteral("ToDoDB")}) {
            const auto names = m_profile->categorySlotNames(db);
            if (!names.isEmpty())
                snapshot.insert(db, names);
        }
    }

    // Providers.
    QList<SyncMappingGraphView::ProviderEntry> providerEntries;
    if (m_accounts) {
        for (auto *provider : m_accounts->providers()) {
            SyncMappingGraphView::ProviderEntry e;
            e.providerId  = provider->id();
            e.displayName = provider->displayName();
            e.collections = m_accounts->collectionsFor(provider->id());
            const auto state = m_accounts->stateFor(provider->id());
            using S = WildPalms::Runtime::AccountController::ConnectionState;
            if (state == S::Connecting)
                e.busyText = tr("Connecting…");
            else if (state == S::Error)
                e.busyText = tr("Error: %1").arg(m_accounts->errorFor(provider->id()));
            providerEntries.append(e);
        }
    }

    m_graphView->setSnapshot(snapshot);
    m_graphView->setProviders(providerEntries);
    m_graphView->setMappings(m_profile ? m_profile->syncMappingsJson()
                                       : QJsonArray());
    m_graphView->rebuild();

    m_inspector->setSelectedMapping(QString(), {});
}

void SyncMappingsPage::onSyncStarted()
{
    setReadOnlyBannerVisible(true);
    m_graphView->setReadOnly(true);
}

void SyncMappingsPage::onSyncFinished()
{
    setReadOnlyBannerVisible(false);
    m_graphView->setReadOnly(false);
}

void SyncMappingsPage::setReadOnlyBannerVisible(bool visible)
{
    if (m_readOnlyBanner) m_readOnlyBanner->setVisible(visible);
}

void SyncMappingsPage::onMappingEdited(const QString &mappingId,
                                       const QJsonObject &updatedJson)
{
    QJsonArray current = m_graphView->mappings();
    QJsonArray rewritten;
    for (const auto v : current) {
        QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString() == mappingId)
            obj = updatedJson;
        rewritten.append(obj);
    }
    m_graphView->setMappings(rewritten);
    m_graphView->rebuild();
}

void SyncMappingsPage::onEdgeSelected(const QString &mappingId)
{
    if (mappingId.isEmpty()) {
        m_inspector->setSelectedMapping(QString(), {});
        return;
    }
    for (const auto v : m_graphView->mappings()) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("id")).toString() == mappingId) {
            m_inspector->setSelectedMapping(mappingId, obj);
            return;
        }
    }
    m_inspector->setSelectedMapping(QString(), {});
}

QJsonArray SyncMappingsPage::currentMappings() const
{
    return m_graphView->mappings();
}

void SyncMappingsPage::applyTo(Profile *profile)
{
    if (!profile) return;
    const QJsonArray mappings = currentMappings();
    profile->setSyncMappingsJson(mappings);
    profile->save();
    if (m_palmRuntime && !m_palmRuntime->isRunning())
        m_palmRuntime->reloadMappings(mappings);
}

} // namespace WildPalms::AppMapping
```

- [ ] **Step 3: Update CMakeLists**

`src/app/mapping/CMakeLists.txt`:

```cmake
add_library(WildPalmsAppMapping STATIC
    mappingrowdialog.h
    mappingrowdialog.cpp
    mappingeditordialog.h
    mappingeditordialog.cpp
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
    mappingedge.h
    mappingedge.cpp
    syncmappingsgraphview.h
    syncmappingsgraphview.cpp
    mappinginspectorpanel.h
    mappinginspectorpanel.cpp
    syncmappingspage.h
    syncmappingspage.cpp
)

target_include_directories(WildPalmsAppMapping
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        # SyncMappingsPage includes profile.h + runtime/accountcontroller.h
        # + runtime/palmruntime.h. These live in WildPalmsCore /
        # WildPalmsRuntime. We need src/ on the include path PRIVATELY
        # so this lib sees them without exposing WP-local synctypes.h to
        # consumers.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(WildPalmsAppMapping
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
    PRIVATE
        # F.3: SyncMappingsPage uses AccountController + PalmRuntime,
        # both of which live in PalmDeviceAccessLib (per
        # WildPalmsAppAccounts pattern). Profile lives in WildPalmsCore
        # but is forward-declared in syncmappingspage.h; the
        # implementation include of "profile.h" is resolved via the
        # added src/ include path above.
        PalmDeviceAccessLib
)
```

- [ ] **Step 4: Build check + commit**

```bash
cmake --build build-dev --target WildPalmsAppMapping 2>&1 | tail -10
git add src/app/mapping/syncmappingspage.h src/app/mapping/syncmappingspage.cpp src/app/mapping/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: SyncMappingsPage container (F.3 T9)

QWidget wrapping the graph view + inspector panel + read-only
banner. Builds the snapshot from Profile::categorySlotNames(),
provider list from AccountController::providers() +
collectionsFor(), and pre-fills mappings from
Profile::syncMappingsJson(). Reacts to PalmRuntime runStarted /
runFinished by toggling the banner + graph read-only state.
applyTo(Profile*) writes back on dialog OK/Apply and calls
PalmRuntime::reloadMappings() if the runtime isn't busy.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: `SettingsDialog` — add Accounts + Sync Mappings pages

**Files:**
- Modify: `src/settingsdialog.h`, `src/settingsdialog.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Update SettingsDialog header**

In `src/settingsdialog.h`:

1. Forward-declare new types at the top (next to existing forward decls):
   ```cpp
   namespace WildPalms::Runtime {
       class AccountController;
       class PalmRuntime;
   }
   namespace WildPalms::AppMapping {
       class SyncMappingsPage;
   }
   namespace WildPalms::App::Accounts {
       class AccountsPage;
   }
   class KPageWidgetItem;
   ```
2. Add to the public section (after the existing constructor):
   ```cpp
   /// F.3: borrow AccountController for the Accounts + Sync Mappings
   /// pages. Must be called BEFORE exec() and BEFORE setPalmRuntime()
   /// (the Sync Mappings page is added only when both are non-null).
   /// Non-owning — must outlive the dialog.
   void setAccountController(WildPalms::Runtime::AccountController *ac);

   /// F.3: borrow PalmRuntime for the Sync Mappings page (read-only
   /// banner + reloadMappings on apply). Non-owning. nullptr leaves
   /// the Sync Mappings page unbuilt.
   void setPalmRuntime(WildPalms::Runtime::PalmRuntime *palmRuntime);

   /// F.3: navigate to the Sync Mappings page (no-op if not present).
   /// Used by KF6MainWindow::onConfigureMappings to deep-link.
   void navigateToSyncMappings();
   ```
3. Add to private slots (after `onApply`):
   ```cpp
   // F.3: forward Apply to per-page handlers.
   void onApplyAccountsAndMappings();
   ```
4. Add new private helpers:
   ```cpp
   void buildAccountsAndMappingsPagesIfReady();
   ```
5. Add new private members:
   ```cpp
   WildPalms::Runtime::AccountController *m_accountController = nullptr;
   WildPalms::Runtime::PalmRuntime       *m_palmRuntime = nullptr;
   WildPalms::App::Accounts::AccountsPage *m_accountsPage = nullptr;
   WildPalms::AppMapping::SyncMappingsPage *m_syncMappingsPage = nullptr;
   KPageWidgetItem                       *m_syncMappingsPageItem = nullptr;
   ```

- [ ] **Step 2: Update SettingsDialog implementation**

In `src/settingsdialog.cpp`:

1. Add includes at top:
   ```cpp
   #include "app/accounts/accountspage.h"
   #include "app/mapping/syncmappingspage.h"
   ```
2. Implement the new setters:
   ```cpp
   void SettingsDialog::setAccountController(WildPalms::Runtime::AccountController *ac)
   {
       m_accountController = ac;
       buildAccountsAndMappingsPagesIfReady();
   }

   void SettingsDialog::setPalmRuntime(WildPalms::Runtime::PalmRuntime *palmRuntime)
   {
       m_palmRuntime = palmRuntime;
       buildAccountsAndMappingsPagesIfReady();
   }

   void SettingsDialog::buildAccountsAndMappingsPagesIfReady()
   {
       // Accounts page only needs AccountController.
       if (m_accountController && !m_accountsPage) {
           m_accountsPage = new WildPalms::App::Accounts::AccountsPage(
               m_accountController, m_palmRuntime, this);
           auto *item = new KPageWidgetItem(m_accountsPage, i18n("Accounts"));
           item->setIcon(QIcon::fromTheme(QStringLiteral("network-server")));
           addPage(item);
       }

       // Sync Mappings page needs Profile + AccountController + PalmRuntime.
       if (m_profile && m_accountController && m_palmRuntime
           && !m_syncMappingsPage) {
           m_syncMappingsPage = new WildPalms::AppMapping::SyncMappingsPage(
               m_profile, m_accountController, m_palmRuntime, this);
           m_syncMappingsPageItem = new KPageWidgetItem(
               m_syncMappingsPage, i18n("Sync Mappings"));
           m_syncMappingsPageItem->setIcon(
               QIcon::fromTheme(QStringLiteral("view-list-tree")));
           addPage(m_syncMappingsPageItem);
       }
   }

   void SettingsDialog::navigateToSyncMappings()
   {
       if (m_syncMappingsPageItem)
           setCurrentPage(m_syncMappingsPageItem);
   }

   void SettingsDialog::onApplyAccountsAndMappings()
   {
       if (m_syncMappingsPage && m_profile)
           m_syncMappingsPage->applyTo(m_profile);
   }
   ```
3. Survey `SettingsDialog::onApply()` (existing slot). Add a call to `onApplyAccountsAndMappings()` at the end of the existing apply chain:
   ```cpp
   void SettingsDialog::onApply()
   {
       saveSettings();
       if (m_profile) saveSyncSettings();
       onApplyAccountsAndMappings();    // F.3
       Q_EMIT settingsChanged();
   }
   ```

Note: the existing `AccountsPage` constructor expects `(AccountController*, PalmRuntime*, parent)` based on Phase Ic Task 9 — verify the actual signature when implementing. If the second argument is optional, pass `m_palmRuntime` (which may be null when only AC is set).

- [ ] **Step 3: Update src/CMakeLists.txt to link WildPalmsAppMapping into WildPalmsCore (for SettingsDialog)**

Survey the existing `target_link_libraries(WildPalmsCore ...)` block in `src/CMakeLists.txt`. `WildPalmsAppMapping` is already listed there (T11 will later add `WildPalmsAppMapping` back as a remaining target after the retirement). For now, no change — the line is already present. Verify the line is still present after this task. If you removed it during retirement task work, add it back:

```cmake
target_link_libraries(WildPalmsCore
    PUBLIC
        ...
        WildPalmsAppMapping
        WildPalmsAppAccounts
        ...
)
```

- [ ] **Step 4: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -5
ctest --test-dir build-dev 2>&1 | tail -5
```

Expected: clean build; all tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/settingsdialog.h src/settingsdialog.cpp src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat: SettingsDialog hosts Accounts + Sync Mappings pages (F.3 T10)

setAccountController(AC*) wires the existing AccountsPage widget as
a new "Accounts" KPageWidgetItem (the widget exists for the wizard;
this is its first standalone settings-dialog appearance).

setPalmRuntime(PalmRuntime*) — in combination with profile + AC —
wires the new SyncMappingsPage as a "Sync Mappings" page.

navigateToSyncMappings() lets KF6MainWindow::onConfigureMappings
deep-link straight to the graph view.

onApply forwards to applyTo(Profile*) on the SyncMappingsPage so
mapping changes persist alongside Sync settings.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: KF6MainWindow wiring + `onConfigureMappings` re-route

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Update `onSettings`**

Survey `KF6MainWindow::onSettings()` (around line 2167). Replace its body with:

```cpp
void KF6MainWindow::onSettings()
{
    SettingsDialog dialog(this, m_currentProfile.get());
    // F.3: feed AC + PalmRuntime so the dialog can show the
    // Accounts + Sync Mappings pages.
    dialog.setAccountController(m_accountController.get());
    dialog.setPalmRuntime(m_palmRuntime.get());
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}
```

- [ ] **Step 2: Re-route `onConfigureMappings`**

Find `KF6MainWindow::onConfigureMappings()` (around line 2037). Replace its body:

```cpp
void KF6MainWindow::onConfigureMappings()
{
    if (!m_currentProfile) {
        QMessageBox::information(this, tr("Configure Mappings"),
            tr("No profile loaded."));
        return;
    }

    SettingsDialog dialog(this, m_currentProfile.get());
    dialog.setAccountController(m_accountController.get());
    dialog.setPalmRuntime(m_palmRuntime.get());
    dialog.navigateToSyncMappings();
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_minimizeToTray = KF6Settings::instance().minimizeToTray();
    });
    dialog.exec();
}
```

Then remove the include `#include "app/mapping/mappingeditordialog.h"` from the top of `kf6mainwindow.cpp` (the dialog is no longer referenced here).

- [ ] **Step 3: Build + full suite**

```bash
cmake --build build-dev 2>&1 | tail -5
ctest --test-dir build-dev 2>&1 | tail -5
```

Expected: clean build; 97 tests passing.

- [ ] **Step 4: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp
git commit -m "$(cat <<'EOF'
feat: KF6MainWindow opens SettingsDialog for mapping config (F.3 T11)

onSettings now passes AC + PalmRuntime to SettingsDialog so the
Accounts + Sync Mappings pages can be built. onConfigureMappings
opens the same dialog and deep-links to the Sync Mappings page
via navigateToSyncMappings(). MappingEditorDialog is no longer
referenced from this file.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Retire MappingEditorDialog / MappingRowDialog / MappingPromptDialog

**Files:**
- Delete: `src/app/mapping/mappingeditordialog.{h,cpp}`
- Delete: `src/app/mapping/mappingrowdialog.{h,cpp}`
- Delete: `src/app/accounts/mappingpromptdialog.{h,cpp}`
- Delete: `tests/runtime/tst_mapping_row_dialog.cpp`
- Delete: `tests/runtime/tst_mapping_editor_dialog.cpp`
- Modify: `src/app/mapping/CMakeLists.txt`
- Modify: `src/app/accounts/CMakeLists.txt`
- Modify: `src/app/accounts/accountspage.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Survey leftover references**

```bash
grep -rn "MappingEditorDialog\|MappingRowDialog\|MappingPromptDialog\|mappingpromptdialog\|mappingeditordialog\|mappingrowdialog" /home/clinton/dev/WildPalms/src /home/clinton/dev/WildPalms/tests
```

Expected hits at this stage: `src/app/mapping/CMakeLists.txt`, `src/app/accounts/CMakeLists.txt`, `src/app/accounts/accountspage.cpp` (include + post-add prompt), the test files about to be deleted, and possibly `tests/runtime/CMakeLists.txt`. T11 already removed the KF6MainWindow include.

- [ ] **Step 2: Strip MappingPromptDialog usage from AccountsPage**

In `src/app/accounts/accountspage.cpp`:
1. Remove `#include "mappingpromptdialog.h"`
2. Find the block that constructs and shows the `MappingPromptDialog` after a successful `addProvider()` call (search for `MappingPromptDialog`). Delete that block. The F.3 graph view takes over the binding flow.

- [ ] **Step 3: Delete the source files**

```bash
rm src/app/mapping/mappingeditordialog.h \
   src/app/mapping/mappingeditordialog.cpp \
   src/app/mapping/mappingrowdialog.h \
   src/app/mapping/mappingrowdialog.cpp \
   src/app/accounts/mappingpromptdialog.h \
   src/app/accounts/mappingpromptdialog.cpp
```

- [ ] **Step 4: Delete the test files**

```bash
rm tests/runtime/tst_mapping_row_dialog.cpp \
   tests/runtime/tst_mapping_editor_dialog.cpp
```

`tests/runtime/tst_mapping_enable_persists.cpp` is preserved — it tests `Profile::syncMappingsJson` round-trip, not the retired dialogs.

- [ ] **Step 5: Update CMakeLists**

In `src/app/mapping/CMakeLists.txt`, remove the four retired source entries from the `add_library(WildPalmsAppMapping STATIC ...)` block:

```cmake
add_library(WildPalmsAppMapping STATIC
    palmdbnode.h
    palmdbnode.cpp
    providernode.h
    providernode.cpp
    mappingedge.h
    mappingedge.cpp
    syncmappingsgraphview.h
    syncmappingsgraphview.cpp
    mappinginspectorpanel.h
    mappinginspectorpanel.cpp
    syncmappingspage.h
    syncmappingspage.cpp
)
```

In `src/app/accounts/CMakeLists.txt`, remove `mappingpromptdialog.cpp` / `mappingpromptdialog.h` from the `add_library(WildPalmsAppAccounts STATIC ...)` block.

In `tests/runtime/CMakeLists.txt`, remove the `add_executable(tst_mapping_row_dialog ...)` and `add_executable(tst_mapping_editor_dialog ...)` blocks plus their `add_test` / `set_tests_properties` entries.

- [ ] **Step 6: Build + full suite**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev 2>&1 | tail -10
ctest --test-dir build-dev 2>&1 | tail -5
```

Expected: clean build; previously-existing 95 tests minus 2 retired (93) plus 4 new F.3 tests (`tst_profile_category_snapshot`, `tst_categorymappingstore` already counted, `tst_syncmappingsgraphview`) = 96 total. Actual count depends on how many cases each test exposes — verify the result is consistent (no failures).

- [ ] **Step 7: Commit**

```bash
git add -A src/app/mapping src/app/accounts tests/runtime
git commit -m "$(cat <<'EOF'
refactor: retire MappingEditorDialog/MappingRowDialog/MappingPromptDialog (F.3 T12)

The SyncMappingsPage graph view (F.3 T9 + T11) replaces all three
dialogs. Their source files, tests, and CMake entries are removed.
AccountsPage no longer shows the post-add MappingPromptDialog —
the graph appears in the same settings dialog on the next tab.
tst_mapping_enable_persists is preserved (it tests Profile JSON
round-trip, not the dialogs).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: Integration plan update + memory + push

**Files:**
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`
- Create: `~/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_f3_mapping_graph.md`
- Modify: `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`

- [ ] **Step 1: Push the commits**

```bash
git push 2>&1 | tail -3
```

- [ ] **Step 2: Update integration plan**

In `docs/plans/2026-04-20-libkalburator-integration.md`, find the F.1d entry (added in F.1d T9) and add an F.3 entry immediately after:

```markdown
  F.3 (sync mappings graph view) ✅ landed 2026-05-23. Replaces the
  raw MappingEditorDialog with an embedded bipartite graph in
  SettingsDialog: Palm DB nodes (left) connect to provider
  collection nodes (right). Per-port-pair domain compatibility is
  enforced via CollectionInfo::type. Category slot names are
  persisted in profile.conf via Profile::setCategorySlotNames(),
  written-back by PalmRuntime::finishConnect() after each plugin
  populates its CategoryMappingStore. Category-lifecycle conflict
  handling (renames / delete+recreate / Palm-side category edits)
  is explicitly out of scope and documented as a future F.5
  brainstorm. Spec:
  `docs/superpowers/specs/2026-05-23-f3-sync-mappings-graph-design.md`.
  Plan: `docs/superpowers/plans/2026-05-23-f3-sync-mappings-graph.md`.
```

Update the Phase F status header:

```markdown
| F | Full Sync Mode UI polish + profile-creation wizard ... | WP | **In progress.** F.1a ✅ + F.1b ✅ + F.2 ✅ done 2026-05-22; F.1c ✅ + F.1d ✅ + F.3 ✅ done 2026-05-23. F.4 / F.5 (category lifecycle) pending. | E |
```

- [ ] **Step 3: Add memory record**

Create `~/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_f3_mapping_graph.md`:

```markdown
---
name: project-phase-f3-mapping-graph
description: F.3 sync-mappings graph view — landed 2026-05-23; bipartite QGraphicsView, per-pair domain compatibility, category snapshot persistence
metadata:
  type: project
---

F.3 landed 2026-05-23 on `main`.

**What changed:**
- New `SyncMappingsPage` widget (graph + inspector + read-only banner) added as KPageWidgetItem in `SettingsDialog`
- New `AccountsPage` KPageWidgetItem in `SettingsDialog` too (the widget existed but was only used by the wizard before)
- `SyncMappingGraphView` (`QGraphicsView` subclass) with `PalmDbNode`, `ProviderNode`, `MappingEdge` items
- Per-pair domain compatibility uses `Kalburator::Sync::CollectionInfo::type` directly
- `Profile::categorySlotNames(dbName)` + `setCategorySlotNames(dbName, names)` — persisted under `[categories/<dbName>]`
- `CategoryMappingStore::sixteenSlotNames(dbName)` helper
- Each of the 4 Palm DB plugins gained `primaryDbName()` + `categorySlotNames()` accessors
- `PalmRuntime::setProfile(Profile*)` + write-back in `finishConnect()` after each `createPalmBackend()`
- Retired: `MappingEditorDialog`, `MappingRowDialog`, `MappingPromptDialog` (and their tests)

**Why:** The raw mapping dialogs exposed backend IDs and SyncMapping JSON to users; the new graph view shows Palm category slots by name on one side and provider collections on the other, with edges as draggable SyncMappings. Required surfacing `CategoryMappingStore` data outside the sync session, which the Profile snapshot handles.

**How to apply:** Future mapping UX work should extend `SyncMappingGraphView` (the graph state machinery exposes `addMappingForTest`/`removeMappingForTest` paths that the production UI uses too). Category-lifecycle conflict handling is a documented F.5 follow-up — see spec §7.

**Spec:** `docs/superpowers/specs/2026-05-23-f3-sync-mappings-graph-design.md`
**Plan:** `docs/superpowers/plans/2026-05-23-f3-sync-mappings-graph.md`
```

Append to `~/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`:

```markdown
- [project_phase_f3_mapping_graph.md](project_phase_f3_mapping_graph.md) — F.3 landed 2026-05-23; bipartite QGraphicsView mapping graph; category slot names persisted in Profile; old mapping dialogs retired
```

- [ ] **Step 4: Final build/test sanity + commit + push**

```bash
cmake --build build-dev 2>&1 | tail -3
ctest --test-dir build-dev 2>&1 | tail -5
git add -f docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "docs: integration plan — F.3 ✅ landed 2026-05-23

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
git push 2>&1 | tail -3
```

Expected: clean build; full test suite green; commit pushed.

---

## Self-Review Notes

**Spec coverage:**
- §2.1 SyncMappingsPage tab → T9, T10
- §2.1 SyncMappingGraphView with bipartite layout → T7
- §2.1 PalmDbNode / ProviderNode → T4, T5
- §2.1 Drag-to-connect creates SyncMapping → T7 (state machinery + mouse handlers + rubber-band feedback all in T7; `beginDragForTest` / `endDragOnProviderForTest` exercise the full drag flow without simulating viewport events)
- §2.1 MappingInspectorPanel → T8
- §2.1 Category slot name persistence → T1
- §2.1 Plugin write-back via PalmRuntime → T2, T3
- §2.1 Retire old dialogs → T12
- §2.1 onConfigureMappings re-route → T11
- §2.2 Out-of-scope items honored (no Graffodil; no category lifecycle conflict; SyncMapping JSON format unchanged)
- §3.4 AccountsPage added to SettingsDialog → T10 (`buildAccountsAndMappingsPagesIfReady`)
- §4.2 Domain tagging via CollectionInfo::type → T7 (`SyncMappingGraphView::isCompatible`)
- §4.3 Edge visual states (Default / Selected / Disabled / Stale) → T6
- §4.5 Read-only guard tied to PalmRuntime signals → T9
- §5 Category slot snapshot persistence → T1, T2, T3
- §6.1 Two new test files → T1, T7 (plus T2 extends existing CategoryMappingStore test)
- §8 Success criteria covered across T1–T13

**Placeholder scan:** No "TBD", "TODO", or "implement later" sentinels in the plan. No scope reductions vs. the spec — drag-to-connect is fully implemented in T7 with both mouse handlers and test seams.

**Type consistency:**
- `categorySlotNames()` / `setCategorySlotNames()` names match between Profile, plugins, and PalmRuntime call sites
- `primaryDbName()` matches across all four plugins
- `addMappingForTest` / `removeMappingForTest` consistent between header, impl, and test
- `MappingInspectorPanel::mappingEdited(QString, QJsonObject)` signal signature matches `SyncMappingsPage::onMappingEdited` slot
- `SyncMappingGraphView::edgeSelected(QString)` matches `SyncMappingsPage::onEdgeSelected`
- `ProviderEntry { providerId, displayName, collections, busyText }` consistent between view and page
- `CollectionInfo::type` reference matches actual libkalburator field (verified during survey)
- `Kalburator::Sync::SyncMode` values (Disabled / OneWayUpload / OneWayDownload / TwoWay) match enum
- `Kalburator::Sync::ConflictResolution` values match enum (CustomMerge omitted per existing precedent)
- `palm:calendar/<slot>` / `palm:contact/<slot>` / `palm:memo/<slot>` / `palm:todo/<slot>` collection ID format matches `CollectionPrefix` constants in each Palm backend
- Plugin backend IDs (`"calendar"`, `"contacts"`, `"memo"`, `"todo"`) match each plugin's `pluginId()` return value
