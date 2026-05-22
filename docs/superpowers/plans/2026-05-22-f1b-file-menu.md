# F.1b — New File Menu (Switch / Import / Forget) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace F.1a's stopgap menu items with a real `File → Profile ▸` submenu (New / Switch ▸ / Import / Forget ▸ / Close / Settings), plus profile rename in the Profile Settings dialog.

**Architecture:** Three building blocks: a new `ProfileMenuController` that owns the dynamic `Switch ▸` and `Forget ▸` `KActionMenu` instances and keeps them synced with `ProfileRegistry` signals; a new `ProfileRegistry::rename()` method that updates both `wildpalmsrc` and the per-profile `profile.conf`; and new `KF6MainWindow` slots for import / switch / forget with a confirm dialog that optionally `removeRecursively`s profile files. `ProfileRegistry` keeps F.1a's "never touches profile files" invariant — deletion lives in `KF6MainWindow`.

**Tech Stack:** Qt6, KF6 (KXmlGui, KActionCollection, KActionMenu, KSharedConfig, KPageDialog), C++17, ctest.

**Spec:** `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md`

**Build:** `cmake --build build-dev -j$(nproc)`
**Run all tests:** `ctest --test-dir build-dev --output-on-failure`
**Run one test:** `ctest --test-dir build-dev -R <test_name> --output-on-failure`
**Run app:** `./build-dev/src/wildpalms`

---

## File inventory

**New files (5):**
- `src/kf6/profilemenucontroller.h` — `ProfileMenuController` class declaration
- `src/kf6/profilemenucontroller.cpp` — implementation
- `tests/runtime/tst_profileregistry_rename.cpp` — registry rename tests
- `tests/runtime/tst_profilemenucontroller.cpp` — controller tests
- `tests/runtime/tst_kf6mainwindow_forget_profile.cpp` — forget-slot integration tests
- `tests/runtime/tst_profilepropertiesdialog_rename.cpp` — dialog rename tests

**Modified files (8):**
- `src/runtime/profileregistry.h` / `.cpp` — add `rename(id, newName)`
- `src/kf6/actionmanager.h` / `.cpp` — add `file_import_profile` action + `importProfileRequested` signal
- `src/kf6/kf6mainwindow.h` / `.cpp` — new slots, ctor wiring, startup rewrite, rename handler
- `src/widgets/dialogs/profilepropertiesdialog.h` / `.cpp` — add General page with Name field + `renameRequested` signal
- `data/wildpalmsui.rc` — restructure File menu
- `src/CMakeLists.txt` — register new source files
- `tests/runtime/CMakeLists.txt` — register new test executables
- `tests/runtime/tst_kf6mainwindow_startup.cpp` — extend / replace stale-last-active test

**Documentation:**
- `docs/plans/2026-04-20-libkalburator-integration.md` — mark F.1b ✅ at end

---

## Task 1: ProfileRegistry::rename — failing tests

**Files:**
- Create: `tests/runtime/tst_profileregistry_rename.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

```cpp
// tests/runtime/tst_profileregistry_rename.cpp
#include <QtTest/QtTest>

#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KSharedConfig>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileRegistryRename : public QObject
{
    Q_OBJECT
private slots:
    void renameUpdatesEntryName();
    void renamePersistsToRegistryFile();
    void renamePersistsToProfileConf();
    void renameEmitsEntryUpdated();
    void renameEmptyReturnsFalse();
    void renameWhitespaceOnlyReturnsFalse();
    void renameUnknownIdReturnsFalse();
};

namespace {
ProfileEntry registerOne(ProfileRegistry &reg, const QString &name)
{
    return reg.registerNew(name);
}
} // namespace

void TstProfileRegistryRename::renameUpdatesEntryName()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());

    const auto e = registerOne(reg, QStringLiteral("Alpha"));
    QVERIFY(e.isValid());

    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renamePersistsToRegistryFile()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    const QString cfgPath = tmp.path() + QStringLiteral("/wprc");
    QString id;
    {
        auto cfg = KSharedConfig::openConfig(cfgPath);
        ProfileRegistry reg(cfg);
        reg.setDefaultRoot(tmp.path());
        const auto e = registerOne(reg, QStringLiteral("Alpha"));
        id = e.id;
        QVERIFY(reg.rename(id, QStringLiteral("Bravo")));
    }
    // Reopen the registry from disk.
    auto cfg2 = KSharedConfig::openConfig(cfgPath);
    ProfileRegistry reg2(cfg2);
    QCOMPARE(reg2.entry(id).name, QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renamePersistsToProfileConf()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));

    QSettings s(e.path + QStringLiteral("/profile.conf"), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("profile"));
    QCOMPARE(s.value(QStringLiteral("name")).toString(), QStringLiteral("Bravo"));
}

void TstProfileRegistryRename::renameEmitsEntryUpdated()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QSignalSpy spy(&reg, &ProfileRegistry::entryUpdated);
    QVERIFY(reg.rename(e.id, QStringLiteral("Bravo")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), e.id);
}

void TstProfileRegistryRename::renameEmptyReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(!reg.rename(e.id, QString()));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Alpha"));
}

void TstProfileRegistryRename::renameWhitespaceOnlyReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());
    const auto e = registerOne(reg, QStringLiteral("Alpha"));

    QVERIFY(!reg.rename(e.id, QStringLiteral("   ")));
    QCOMPARE(reg.entry(e.id).name, QStringLiteral("Alpha"));
}

void TstProfileRegistryRename::renameUnknownIdReturnsFalse()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc"));
    ProfileRegistry reg(cfg);
    reg.setDefaultRoot(tmp.path());

    QVERIFY(!reg.rename(QStringLiteral("nonexistent"), QStringLiteral("X")));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstProfileRegistryRename)
#include "tst_profileregistry_rename.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

In `tests/runtime/CMakeLists.txt`, after the existing
`add_wildpalms_runtime_test(tst_profileregistry tst_profileregistry.cpp)` line, add:

```cmake
add_wildpalms_runtime_test(tst_profileregistry_rename tst_profileregistry_rename.cpp)
```

- [ ] **Step 3: Configure + build the test, confirm it fails to compile**

Run: `cmake --build build-dev --target tst_profileregistry_rename 2>&1 | tail -20`
Expected: compile error — `'rename' is not a member of 'WildPalms::Runtime::ProfileRegistry'`.

- [ ] **Step 4: Commit the failing test**

```bash
git add tests/runtime/tst_profileregistry_rename.cpp tests/runtime/CMakeLists.txt
git commit -m "F.1b T1: failing tests for ProfileRegistry::rename"
```

---

## Task 2: ProfileRegistry::rename — implementation

**Files:**
- Modify: `src/runtime/profileregistry.h`
- Modify: `src/runtime/profileregistry.cpp`

- [ ] **Step 1: Add rename declaration**

In `src/runtime/profileregistry.h`, in the `public:` section after `setLastActive`, add:

```cpp
    /// Rename a registered profile. Updates both the registry
    /// (wildpalmsrc) and the per-profile profile.conf's [profile]/name.
    /// Trims newName; returns false if id is unknown, newName is empty
    /// after trimming, or the profile.conf write fails (in-memory
    /// cache is rolled back on disk failure). Emits entryUpdated(id)
    /// on success.
    bool rename(const QString &id, const QString &newName);
```

- [ ] **Step 2: Implement rename**

In `src/runtime/profileregistry.cpp`, add an `#include <QSettings>` near the
other Qt includes if not present, then add this method (place it after
the existing `setLastActive` definition):

```cpp
bool ProfileRegistry::rename(const QString &id, const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (id.isEmpty() || trimmed.isEmpty()) return false;

    // Locate the entry in the cache.
    int idx = -1;
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i].id == id) { idx = i; break; }
    }
    if (idx < 0) return false;

    const QString oldName = m_cache[idx].name;
    m_cache[idx].name = trimmed;

    // Write [profile]/name to <path>/profile.conf.
    const QString confPath =
        m_cache[idx].path + QStringLiteral("/profile.conf");
    {
        QSettings s(confPath, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("profile"));
        s.setValue(QStringLiteral("name"), trimmed);
        s.endGroup();
        s.sync();
        if (s.status() != QSettings::NoError) {
            // Roll back the in-memory cache.
            m_cache[idx].name = oldName;
            return false;
        }
    }

    save();
    emit entryUpdated(id);
    return true;
}
```

- [ ] **Step 3: Run the tests, verify all pass**

Run: `cmake --build build-dev --target tst_profileregistry_rename && ctest --test-dir build-dev -R tst_profileregistry_rename --output-on-failure`

Expected: all 7 cases PASS.

- [ ] **Step 4: Confirm nothing else broke**

Run: `ctest --test-dir build-dev -R tst_profileregistry --output-on-failure`

Expected: both `tst_profileregistry` and `tst_profileregistry_rename` PASS.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/profileregistry.h src/runtime/profileregistry.cpp
git commit -m "F.1b T2: ProfileRegistry::rename — update wildpalmsrc + profile.conf"
```

---

## Task 3: ProfileMenuController — skeleton + empty/non-empty enable tests

**Files:**
- Create: `src/kf6/profilemenucontroller.h`
- Create: `src/kf6/profilemenucontroller.cpp`
- Create: `tests/runtime/tst_profilemenucontroller.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test file (minimal coverage for this task)**

```cpp
// tests/runtime/tst_profilemenucontroller.cpp
#include <QtTest/QtTest>

#include "../../src/kf6/profilemenucontroller.h"
#include "../../src/runtime/profileregistry.h"
#include "../wildpalms_qtest_main.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KSharedConfig>
#include <QSignalSpy>
#include <QTemporaryDir>

using namespace WildPalms::Runtime;

class TstProfileMenuController : public QObject
{
    Q_OBJECT
private slots:
    void emptyRegistryDisablesBothMenus();
    void nonEmptyEnablesMenus();
};

namespace {
struct Fixture {
    QTemporaryDir tmp;
    KSharedConfig::Ptr cfg;
    ProfileRegistry registry;
    KActionCollection actions;

    Fixture()
        : cfg(KSharedConfig::openConfig(tmp.path() + QStringLiteral("/wprc")))
        , registry(cfg)
        , actions(nullptr) {
        registry.setDefaultRoot(tmp.path());
    }
};
} // namespace

void TstProfileMenuController::emptyRegistryDisablesBothMenus()
{
    Fixture f;
    ProfileMenuController ctrl(&f.registry, &f.actions);

    QVERIFY(ctrl.switchMenu() != nullptr);
    QVERIFY(ctrl.forgetMenu() != nullptr);
    QVERIFY(!ctrl.switchMenu()->isEnabled());
    QVERIFY(!ctrl.forgetMenu()->isEnabled());
}

void TstProfileMenuController::nonEmptyEnablesMenus()
{
    Fixture f;
    f.registry.registerNew(QStringLiteral("Alpha"));
    ProfileMenuController ctrl(&f.registry, &f.actions);

    QVERIFY(ctrl.switchMenu()->isEnabled());
    QVERIFY(ctrl.forgetMenu()->isEnabled());
}

WILDPALMS_QTEST_MAIN(TstProfileMenuController)
#include "tst_profilemenucontroller.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

In `tests/runtime/CMakeLists.txt`, after the
`add_wildpalms_runtime_test(tst_profileregistry_rename ...)` line, add a
heavyweight test (controller needs `QApplication` + KF6 widgets):

```cmake
add_executable(tst_profilemenucontroller tst_profilemenucontroller.cpp)
target_link_libraries(tst_profilemenucontroller
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        KF6::I18n
        WildPalmsCore
        WildPalmsRuntime
)
add_test(NAME tst_profilemenucontroller COMMAND tst_profilemenucontroller)
set_tests_properties(tst_profilemenucontroller PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

(`WildPalmsCore` is the static lib that contains `src/kf6/...` per the
existing CMakeLists wiring.)

- [ ] **Step 3: Write the header**

`src/kf6/profilemenucontroller.h`:

```cpp
#ifndef WILDPALMS_KF6_PROFILEMENUCONTROLLER_H
#define WILDPALMS_KF6_PROFILEMENUCONTROLLER_H

#include <QObject>
#include <QString>

namespace WildPalms::Runtime { class ProfileRegistry; }
class KActionCollection;
class KActionMenu;

/// Owns the Switch ▸ and Forget ▸ KActionMenu instances under
/// File → Profile, and keeps their contents synced with
/// ProfileRegistry. F.1b §5 / spec
/// docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md
class ProfileMenuController : public QObject {
    Q_OBJECT
public:
    ProfileMenuController(WildPalms::Runtime::ProfileRegistry *registry,
                          KActionCollection *actionCollection,
                          QObject *parent = nullptr);
    ~ProfileMenuController() override;

    KActionMenu *switchMenu() const { return m_switchMenu; }
    KActionMenu *forgetMenu() const { return m_forgetMenu; }

    /// Marks one entry as the currently-loaded profile. Causes its
    /// row in Switch ▸ to gain a checkmark + be disabled, and its
    /// row in Forget ▸ to be disabled. Pass an empty string when no
    /// profile is loaded.
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

#endif // WILDPALMS_KF6_PROFILEMENUCONTROLLER_H
```

- [ ] **Step 4: Write minimal implementation (skeleton only — populate later tasks)**

`src/kf6/profilemenucontroller.cpp`:

```cpp
#include "profilemenucontroller.h"
#include "../runtime/profileregistry.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <QAction>
#include <QIcon>
#include <QMenu>

using namespace WildPalms::Runtime;

ProfileMenuController::ProfileMenuController(ProfileRegistry *registry,
                                              KActionCollection *actionCollection,
                                              QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_actionCollection(actionCollection)
{
    m_switchMenu = new KActionMenu(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        i18n("&Switch Profile"), this);
    m_actionCollection->addAction(
        QStringLiteral("file_switch_profile"), m_switchMenu);

    m_forgetMenu = new KActionMenu(
        QIcon::fromTheme(QStringLiteral("edit-delete")),
        i18n("&Forget Profile"), this);
    m_actionCollection->addAction(
        QStringLiteral("file_forget_profile"), m_forgetMenu);

    connect(m_registry, &ProfileRegistry::registryChanged,
            this, &ProfileMenuController::rebuild);
    connect(m_registry, &ProfileRegistry::entryUpdated,
            this, [this](QString) { rebuild(); });

    rebuild();
}

ProfileMenuController::~ProfileMenuController() = default;

void ProfileMenuController::setActiveProfileId(const QString &id)
{
    if (m_activeId == id) return;
    m_activeId = id;
    rebuild();
}

void ProfileMenuController::rebuild()
{
    m_switchMenu->menu()->clear();
    m_forgetMenu->menu()->clear();

    const auto entries = m_registry->entries();
    const bool any = !entries.isEmpty();
    m_switchMenu->setEnabled(any);
    m_forgetMenu->setEnabled(any);
    if (!any) return;

    for (const auto &e : entries) {
        const bool isActive = (e.id == m_activeId);

        QAction *sw = new QAction(e.name, m_switchMenu);
        sw->setCheckable(true);
        sw->setChecked(isActive);
        sw->setEnabled(!isActive);
        sw->setData(e.id);
        connect(sw, &QAction::triggered, this,
                [this, id = e.id]() { emit switchRequested(id); });
        m_switchMenu->addAction(sw);

        QAction *fg = new QAction(e.name, m_forgetMenu);
        fg->setEnabled(!isActive);
        fg->setData(e.id);
        connect(fg, &QAction::triggered, this,
                [this, id = e.id]() { emit forgetRequested(id); });
        m_forgetMenu->addAction(fg);
    }
}
```

- [ ] **Step 5: Register source files in src/CMakeLists.txt**

In `src/CMakeLists.txt`, after the `kf6/actionmanager.h` line (~line 66), add:

```cmake
    kf6/profilemenucontroller.cpp
    kf6/profilemenucontroller.h
```

- [ ] **Step 6: Build + run the test**

Run: `cmake --build build-dev --target tst_profilemenucontroller && ctest --test-dir build-dev -R tst_profilemenucontroller --output-on-failure`

Expected: both `emptyRegistryDisablesBothMenus` and `nonEmptyEnablesMenus` PASS.

- [ ] **Step 7: Commit**

```bash
git add src/kf6/profilemenucontroller.h src/kf6/profilemenucontroller.cpp \
    src/CMakeLists.txt tests/runtime/tst_profilemenucontroller.cpp \
    tests/runtime/CMakeLists.txt
git commit -m "F.1b T3: ProfileMenuController skeleton + empty/non-empty tests"
```

---

## Task 4: ProfileMenuController — populate + sort + active marker

**Files:**
- Modify: `tests/runtime/tst_profilemenucontroller.cpp`

The implementation already handles populate / sort / active marker
(see T3 Step 4 — the `rebuild()` body is already complete). This task
just expands the test suite to cover these behaviors.

- [ ] **Step 1: Append new test slot declarations**

In the existing `class TstProfileMenuController`'s `private slots:`
section, after `nonEmptyEnablesMenus`, add:

```cpp
    void submenusPopulatedSorted();
    void activeProfileCheckedAndDisabledInSwitch();
    void activeProfileDisabledInForget();
    void clearingActiveIdReenablesAll();
```

- [ ] **Step 2: Add the test bodies**

Append to the bottom of the file (above the `WILDPALMS_QTEST_MAIN` line):

```cpp
void TstProfileMenuController::submenusPopulatedSorted()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));
    const auto c = f.registry.registerNew(QStringLiteral("Charlie"));
    // Touch Bravo last so it sorts first by lastOpened desc.
    f.registry.setLastActive(a.id);
    f.registry.setLastActive(c.id);
    f.registry.setLastActive(b.id);

    ProfileMenuController ctrl(&f.registry, &f.actions);

    auto swActs = ctrl.switchMenu()->menu()->actions();
    QCOMPARE(swActs.size(), 3);
    QCOMPARE(swActs[0]->text(), QStringLiteral("Bravo"));
    QCOMPARE(swActs[1]->text(), QStringLiteral("Charlie"));
    QCOMPARE(swActs[2]->text(), QStringLiteral("Alpha"));

    auto fgActs = ctrl.forgetMenu()->menu()->actions();
    QCOMPARE(fgActs.size(), 3);
    QCOMPARE(fgActs[0]->text(), QStringLiteral("Bravo"));
}

void TstProfileMenuController::activeProfileCheckedAndDisabledInSwitch()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);

    for (auto *act : ctrl.switchMenu()->menu()->actions()) {
        const bool isB = (act->data().toString() == b.id);
        QCOMPARE(act->isChecked(), isB);
        QCOMPARE(act->isEnabled(), !isB);
    }
    Q_UNUSED(a);
}

void TstProfileMenuController::activeProfileDisabledInForget()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);

    for (auto *act : ctrl.forgetMenu()->menu()->actions()) {
        const bool isB = (act->data().toString() == b.id);
        QCOMPARE(act->isEnabled(), !isB);
    }
    Q_UNUSED(a);
}

void TstProfileMenuController::clearingActiveIdReenablesAll()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    ctrl.setActiveProfileId(b.id);
    ctrl.setActiveProfileId(QString());

    for (auto *act : ctrl.switchMenu()->menu()->actions()) {
        QVERIFY(!act->isChecked());
        QVERIFY(act->isEnabled());
    }
    for (auto *act : ctrl.forgetMenu()->menu()->actions()) {
        QVERIFY(act->isEnabled());
    }
    Q_UNUSED(a);
}
```

- [ ] **Step 3: Build + run the expanded tests**

Run: `cmake --build build-dev --target tst_profilemenucontroller && ctest --test-dir build-dev -R tst_profilemenucontroller --output-on-failure`

Expected: 6 cases PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/runtime/tst_profilemenucontroller.cpp
git commit -m "F.1b T4: tests for populate/sort/active-marker behavior"
```

---

## Task 5: ProfileMenuController — signal emission + registry sync tests

**Files:**
- Modify: `tests/runtime/tst_profilemenucontroller.cpp`

- [ ] **Step 1: Append new test slot declarations**

In `private slots:`, after `clearingActiveIdReenablesAll`, add:

```cpp
    void switchRequestedSignalFiresWithId();
    void forgetRequestedSignalFiresWithId();
    void registryChangedRebuilds();
    void entryUpdatedRebuilds();
    void unregisterRemovesFromBoth();
```

- [ ] **Step 2: Add the test bodies**

Append before the `WILDPALMS_QTEST_MAIN` line:

```cpp
void TstProfileMenuController::switchRequestedSignalFiresWithId()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    QSignalSpy spy(&ctrl, &ProfileMenuController::switchRequested);

    QAction *bAction = nullptr;
    for (auto *act : ctrl.switchMenu()->menu()->actions())
        if (act->data().toString() == b.id) bAction = act;
    QVERIFY(bAction);
    bAction->trigger();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), b.id);
    Q_UNUSED(a);
}

void TstProfileMenuController::forgetRequestedSignalFiresWithId()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    QSignalSpy spy(&ctrl, &ProfileMenuController::forgetRequested);

    QAction *bAction = nullptr;
    for (auto *act : ctrl.forgetMenu()->menu()->actions())
        if (act->data().toString() == b.id) bAction = act;
    QVERIFY(bAction);
    bAction->trigger();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), b.id);
    Q_UNUSED(a);
}

void TstProfileMenuController::registryChangedRebuilds()
{
    Fixture f;
    f.registry.registerNew(QStringLiteral("Alpha"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    QCOMPARE(ctrl.switchMenu()->menu()->actions().size(), 1);

    f.registry.registerNew(QStringLiteral("Bravo"));
    QCOMPARE(ctrl.switchMenu()->menu()->actions().size(), 2);
    QCOMPARE(ctrl.forgetMenu()->menu()->actions().size(), 2);
}

void TstProfileMenuController::entryUpdatedRebuilds()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    // Rename Alpha; submenu labels should update.
    QVERIFY(f.registry.rename(a.id, QStringLiteral("Alpha2")));

    QStringList swNames;
    for (auto *act : ctrl.switchMenu()->menu()->actions())
        swNames << act->text();
    QVERIFY(swNames.contains(QStringLiteral("Alpha2")));
    QVERIFY(!swNames.contains(QStringLiteral("Alpha")));
    Q_UNUSED(b);
}

void TstProfileMenuController::unregisterRemovesFromBoth()
{
    Fixture f;
    const auto a = f.registry.registerNew(QStringLiteral("Alpha"));
    const auto b = f.registry.registerNew(QStringLiteral("Bravo"));

    ProfileMenuController ctrl(&f.registry, &f.actions);
    QVERIFY(f.registry.unregister(b.id));

    QCOMPARE(ctrl.switchMenu()->menu()->actions().size(), 1);
    QCOMPARE(ctrl.forgetMenu()->menu()->actions().size(), 1);
    QCOMPARE(ctrl.switchMenu()->menu()->actions().first()->data().toString(), a.id);
}
```

- [ ] **Step 3: Build + run**

Run: `cmake --build build-dev --target tst_profilemenucontroller && ctest --test-dir build-dev -R tst_profilemenucontroller --output-on-failure`

Expected: 11 cases PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/runtime/tst_profilemenucontroller.cpp
git commit -m "F.1b T5: ProfileMenuController signal + registry-sync tests"
```

---

## Task 6: ActionManager — Import Profile action

**Files:**
- Modify: `src/kf6/actionmanager.h`
- Modify: `src/kf6/actionmanager.cpp`

- [ ] **Step 1: Add accessor + signal to the header**

In `src/kf6/actionmanager.h`, in the File Actions accessors block,
after `newProfileAction()`, add:

```cpp
    QAction* importProfileAction() const { return action(QStringLiteral("file_import_profile")); }
```

In the `Q_SIGNALS:` File operations block, after `newProfileRequested();`, add:

```cpp
    void importProfileRequested();
```

- [ ] **Step 2: Register the action in setupFileActions**

In `src/kf6/actionmanager.cpp`, in `setupFileActions()`, after the
"New Profile" block (the one that ends with `connect(newProfile, ...)`),
insert:

```cpp
    // Import Profile (F.1b)
    QAction *importProfile = new QAction(
        QIcon::fromTheme(QStringLiteral("document-open-folder")),
        i18n("&Import Profile..."), this);
    m_actionCollection->addAction(
        QStringLiteral("file_import_profile"), importProfile);
    connect(importProfile, &QAction::triggered,
            this, &ActionManager::importProfileRequested);
```

- [ ] **Step 3: Build**

Run: `cmake --build build-dev -j$(nproc) 2>&1 | tail -10`

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add src/kf6/actionmanager.h src/kf6/actionmanager.cpp
git commit -m "F.1b T6: ActionManager — file_import_profile action + signal"
```

---

## Task 7: KF6MainWindow — construct ProfileMenuController + Import slot

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Add include + member + slot declarations to the header**

In `src/kf6/kf6mainwindow.h`, near the other includes for forward-decl-only classes (e.g. after `#include <memory>` if present, or near `class ProfileRegistry;`), add:

```cpp
class ProfileMenuController;
```

(If a forward declaration of `ProfileMenuController` is awkward, instead add `#include "profilemenucontroller.h"` near the other `#include` lines.)

In the private member section (alongside `m_profileRegistry`), add:

```cpp
    std::unique_ptr<ProfileMenuController> m_profileMenuController;
```

In the `private slots:` section (alongside `onNewProfile`, `onCloseProfile`), add:

```cpp
    void onImportProfile();
    void onSwitchProfile(const QString &id);
    void onForgetProfile(const QString &id);
```

- [ ] **Step 2: Construct the controller + wire signals in the ctor**

In `src/kf6/kf6mainwindow.cpp`, find the line:

```cpp
    m_profileRegistry = std::make_unique<WildPalms::Runtime::ProfileRegistry>(this);
```

Insert immediately after it:

```cpp
    m_profileMenuController = std::make_unique<ProfileMenuController>(
        m_profileRegistry.get(),
        actionCollection(),
        this);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::switchRequested,
            this, &KF6MainWindow::onSwitchProfile);
    connect(m_profileMenuController.get(),
            &ProfileMenuController::forgetRequested,
            this, &KF6MainWindow::onForgetProfile);
```

Also add at the top of the file (with the other includes):

```cpp
#include "profilemenucontroller.h"
```

- [ ] **Step 3: Wire the ActionManager import signal**

In `src/kf6/kf6mainwindow.cpp`, in the ctor where other `connect(m_actionManager, ...)` calls live (around the existing `connect(m_actionManager, &ActionManager::newProfileRequested, this, &KF6MainWindow::onNewProfile)` line), add:

```cpp
    connect(m_actionManager, &ActionManager::importProfileRequested,
            this, &KF6MainWindow::onImportProfile);
```

- [ ] **Step 4: Implement onImportProfile**

In `src/kf6/kf6mainwindow.cpp`, add a new method in the "Profile Slots" section (near `onCloseProfile`):

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

- [ ] **Step 5: Build, confirm it compiles**

Run: `cmake --build build-dev -j$(nproc) 2>&1 | tail -20`

Expected: success.

- [ ] **Step 6: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "F.1b T7: KF6MainWindow — construct ProfileMenuController + Import slot"
```

---

## Task 8: KF6MainWindow — Switch slot + active-id propagation

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Implement onSwitchProfile**

Add to `src/kf6/kf6mainwindow.cpp` in the Profile Slots section, after
`onImportProfile`:

```cpp
void KF6MainWindow::onSwitchProfile(const QString &id)
{
    if (id.isEmpty()) return;
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) {
        m_logWidget->logError(
            i18n("Cannot switch: profile not found"));
        return;
    }
    if (!QDir(e.path).exists()) {
        QMessageBox::warning(this, i18n("Switch Profile"),
            i18n("Profile directory no longer exists: %1\n"
                 "Use File → Profile → Forget to remove it from "
                 "the registry.", e.path));
        return;
    }
    loadProfile(e.path);
}
```

- [ ] **Step 2: Propagate active-id on loadProfile success**

In `src/kf6/kf6mainwindow.cpp`, find the existing block (added in F.1a):

```cpp
    if (m_profileRegistry && !m_currentProfile->id().isEmpty())
        m_profileRegistry->setLastActive(m_currentProfile->id());
```

Replace with:

```cpp
    if (m_profileRegistry && !m_currentProfile->id().isEmpty()) {
        m_profileRegistry->setLastActive(m_currentProfile->id());
        if (m_profileMenuController)
            m_profileMenuController->setActiveProfileId(
                m_currentProfile->id());
    }
```

- [ ] **Step 3: Clear active-id on closeProfile**

Find the existing `KF6MainWindow::closeProfile()` method. Inside it,
after the line that resets `m_currentProfile` (typically
`m_currentProfile.reset();`), add:

```cpp
    if (m_profileMenuController)
        m_profileMenuController->setActiveProfileId(QString());
```

If you can't immediately spot where `m_currentProfile` is reset, search
with: `grep -n "closeProfile\|m_currentProfile.reset" src/kf6/kf6mainwindow.cpp`

- [ ] **Step 4: Build + run all existing tests**

Run: `cmake --build build-dev -j$(nproc) && ctest --test-dir build-dev -R "tst_kf6mainwindow|tst_profileregistry|tst_profilemenucontroller" --output-on-failure`

Expected: all PASS (no regressions in F.1a tests).

- [ ] **Step 5: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp
git commit -m "F.1b T8: KF6MainWindow — onSwitchProfile + active-id propagation"
```

---

## Task 9: KF6MainWindow — Forget slot + confirm dialog seam

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Add a test-seam virtual method to the header**

In `src/kf6/kf6mainwindow.h`, in the `protected:` section (creating one
if it doesn't exist; place it before `private slots:`):

```cpp
protected:
    /// Test seam: pops the Forget confirm dialog. Production override
    /// runs the real QDialog (see kf6mainwindow.cpp); tests override
    /// to return preset values. Returns true if user clicked Forget;
    /// outDeleteFiles is set to the checkbox state.
    virtual bool confirmForgetProfile(const WildPalms::Runtime::ProfileEntry &entry,
                                       bool *outDeleteFiles);
```

(`#include "../runtime/profileregistry.h"` should already be present
in kf6mainwindow.h for `m_profileRegistry`. If `ProfileEntry` isn't
visible, add the include.)

- [ ] **Step 2: Implement the production confirmForgetProfile and onForgetProfile**

In `src/kf6/kf6mainwindow.cpp`, add to the Profile Slots section after
`onSwitchProfile`:

```cpp
bool KF6MainWindow::confirmForgetProfile(
    const WildPalms::Runtime::ProfileEntry &entry,
    bool *outDeleteFiles)
{
    QDialog dlg(this);
    dlg.setWindowTitle(i18n("Forget Profile"));
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        i18n("Remove profile \"%1\" from the registry?\n\n"
             "Folder: %2", entry.name, entry.path), &dlg));
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

    const bool ok = (dlg.exec() == QDialog::Accepted);
    if (outDeleteFiles) *outDeleteFiles = deleteCheck->isChecked();
    return ok;
}

void KF6MainWindow::onForgetProfile(const QString &id)
{
    if (id.isEmpty()) return;
    if (m_currentProfile && m_currentProfile->id() == id) {
        m_logWidget->logError(i18n(
            "Cannot forget the currently-loaded profile. "
            "Close it first."));
        return;
    }
    const auto e = m_profileRegistry->entry(id);
    if (!e.isValid()) return;

    bool wantDelete = false;
    if (!confirmForgetProfile(e, &wantDelete)) return;

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

Add any missing includes at the top of `kf6mainwindow.cpp`:

```cpp
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
```

(QDialog, QDialogButtonBox, QPushButton, QMessageBox, QDir, QFileDialog,
QInputDialog should already be included from F.1a.)

- [ ] **Step 3: Build, confirm it compiles**

Run: `cmake --build build-dev -j$(nproc) 2>&1 | tail -20`

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "F.1b T9: KF6MainWindow — onForgetProfile + confirm dialog seam"
```

---

## Task 10: KF6MainWindow Forget slot — integration tests

**Files:**
- Create: `tests/runtime/tst_kf6mainwindow_forget_profile.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

```cpp
// tests/runtime/tst_kf6mainwindow_forget_profile.cpp
#include <QtTest/QtTest>
#include <QDir>
#include <QTemporaryDir>

#include "../../src/kf6/kf6mainwindow.h"
#include "../../src/runtime/profileregistry.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

#include <KSharedConfig>

using namespace WildPalms::Runtime;

class TestableForgetWindow : public KF6MainWindow {
public:
    using KF6MainWindow::KF6MainWindow;

    bool nextConfirmReturn = false;
    bool nextDeleteFiles   = false;
    int  confirmInvocations = 0;
    ProfileEntry lastEntry;

protected:
    bool confirmForgetProfile(const ProfileEntry &entry,
                               bool *outDeleteFiles) override {
        ++confirmInvocations;
        lastEntry = entry;
        if (outDeleteFiles) *outDeleteFiles = nextDeleteFiles;
        return nextConfirmReturn;
    }
};

class TstKf6MainWindowForgetProfile : public QObject
{
    Q_OBJECT
private slots:
    void forgetWithoutDeleteKeepsFiles();
    void forgetWithDeleteRemovesFiles();
    void forgetCancelDoesNothing();
    void forgetActiveProfileIsRejected();

private:
    QString registerProfile(ProfileRegistry &reg, const QString &name);
};

QString TstKf6MainWindowForgetProfile::registerProfile(
    ProfileRegistry &reg, const QString &name)
{
    const auto e = reg.registerNew(name);
    return e.id;
}

void TstKf6MainWindowForgetProfile::forgetWithoutDeleteKeepsFiles()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    QVERIFY(entry.isValid());
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = false;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(QDir(path).exists());
    QVERIFY(!win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetWithDeleteRemovesFiles()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    QVERIFY(entry.isValid());
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = true;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(!QDir(path).exists());
    QVERIFY(!win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetCancelDoesNothing()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));
    const QString path = entry.path;

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    win.nextConfirmReturn = false;
    win.nextDeleteFiles   = true;  // ignored when cancel

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    QVERIFY(QDir(path).exists());
    QVERIFY(win.profileRegistryForTest()->entry(entry.id).isValid());
}

void TstKf6MainWindowForgetProfile::forgetActiveProfileIsRejected()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto entry = reg->registerNew(QStringLiteral("Alpha"));

    TestableForgetWindow win;
    win.setProfileRegistryForTest(std::move(reg));
    // Simulate the profile being loaded: directly trigger loadProfile.
    win.runLoadProfileForTest(entry.path);
    QVERIFY(win.currentProfileIdForTest() == entry.id);

    win.nextConfirmReturn = true;
    win.nextDeleteFiles   = true;
    win.confirmInvocations = 0;

    QMetaObject::invokeMethod(&win, "onForgetProfile",
        Qt::DirectConnection,
        Q_ARG(QString, entry.id));

    // Confirm dialog should not even have fired.
    QCOMPARE(win.confirmInvocations, 0);
    QVERIFY(win.profileRegistryForTest()->entry(entry.id).isValid());
}

WILDPALMS_QTEST_MAIN(TstKf6MainWindowForgetProfile)
#include "tst_kf6mainwindow_forget_profile.moc"
```

- [ ] **Step 2: Add test seams to KF6MainWindow if not present**

The test uses:
- `setProfileRegistryForTest` — already exists per F.1a T14.
- `profileRegistryForTest()` — NEW accessor.
- `runLoadProfileForTest(path)` — NEW; calls `loadProfile(path)` from public API for tests.
- `currentProfileIdForTest()` — NEW.

In `src/kf6/kf6mainwindow.h` `public:` section (after `setProfileRegistryForTest`):

```cpp
    // Test seams (F.1b T10): read-only accessors used by Forget tests.
    WildPalms::Runtime::ProfileRegistry *profileRegistryForTest() const {
        return m_profileRegistry.get();
    }
    void runLoadProfileForTest(const QString &path) { loadProfile(path); }
    QString currentProfileIdForTest() const {
        return m_currentProfile ? m_currentProfile->id() : QString();
    }
```

- [ ] **Step 3: Register the test in tests/runtime/CMakeLists.txt**

After the `tst_kf6mainwindow_startup` block, add:

```cmake
add_executable(tst_kf6mainwindow_forget_profile
    tst_kf6mainwindow_forget_profile.cpp)
target_link_libraries(tst_kf6mainwindow_forget_profile
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::XmlGui
        KF6::ConfigCore
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        PalmDeviceAccessLib
        WildPalmsRuntime
        WildPalmsPalmDevice
        WildPalmsCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_kf6mainwindow_forget_profile
         COMMAND tst_kf6mainwindow_forget_profile)
set_tests_properties(tst_kf6mainwindow_forget_profile PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 4: Build + run**

Run: `cmake --build build-dev --target tst_kf6mainwindow_forget_profile && ctest --test-dir build-dev -R tst_kf6mainwindow_forget_profile --output-on-failure`

Expected: 4 cases PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/runtime/tst_kf6mainwindow_forget_profile.cpp \
    tests/runtime/CMakeLists.txt \
    src/kf6/kf6mainwindow.h
git commit -m "F.1b T10: KF6MainWindow forget — integration tests + seams"
```

---

## Task 11: wildpalmsui.rc — restructure File menu

**Files:**
- Modify: `data/wildpalmsui.rc`

- [ ] **Step 1: Replace the existing File Menu block**

In `data/wildpalmsui.rc`, replace the entire `<Menu name="file">` block with:

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

Also bump the version attribute on the `<gui>` element so KXmlGui reloads
the merged file rather than using a cached older version:

```xml
<gui name="wildpalms" version="5">
```

(Existing line was `version="4"`.)

- [ ] **Step 2: Build (rc is installed into the build tree by CMake)**

Run: `cmake --build build-dev -j$(nproc) 2>&1 | tail -10`

Expected: success.

- [ ] **Step 3: Run all existing KF6 tests for regressions**

Run: `ctest --test-dir build-dev -R "tst_kf6mainwindow|tst_main_window" --output-on-failure`

Expected: PASS (rc change is metadata only; runtime startup test should not be affected).

- [ ] **Step 4: Commit**

```bash
git add data/wildpalmsui.rc
git commit -m "F.1b T11: wildpalmsui.rc — restructure File menu under Profile submenu"
```

---

## Task 12: Startup — replace stale-last-active stopgap with auto-load-most-recent

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp`
- Modify: `tests/runtime/tst_kf6mainwindow_startup.cpp`

- [ ] **Step 1: Rewrite the failing test first**

In `tests/runtime/tst_kf6mainwindow_startup.cpp`, locate the
`staleLastActiveFallsBackToStopgap` test and **replace it** with:

```cpp
void TstKf6MainWindowStartup::staleLastActiveAutoLoadsMostRecent()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto cfg = KSharedConfig::openConfig(
        tmp.path() + QStringLiteral("/wprc"));
    auto reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());
    const auto a = reg->registerNew(QStringLiteral("Alpha"));
    const auto b = reg->registerNew(QStringLiteral("Bravo"));
    // Make Bravo more recent.
    reg->setLastActive(a.id);
    reg->setLastActive(b.id);
    // Then set lastActive to a bogus id (stale).
    {
        KConfigGroup g(cfg, QStringLiteral("General"));
        g.writeEntry("lastActiveProfileId", QStringLiteral("does-not-exist"));
        cfg->sync();
    }
    // Reload the registry to pick up the on-disk stale id.
    reg = std::make_unique<ProfileRegistry>(cfg);
    reg->setDefaultRoot(tmp.path());

    TestableMainWindow win;
    win.stopgapReturn = QString();   // should NOT be reached
    win.setProfileRegistryForTest(std::move(reg));

    const QString picked = win.runStartupForTest();

    QCOMPARE(win.stopgapInvocations, 0);
    QCOMPARE(picked, b.path);
}
```

Also update the test slot declaration in `private slots:` (replace
`staleLastActiveFallsBackToStopgap` with `staleLastActiveAutoLoadsMostRecent`).

- [ ] **Step 2: Update test slot declaration in the header section of the test class**

```cpp
    void staleLastActiveAutoLoadsMostRecent();   // was staleLastActiveFallsBackToStopgap
```

- [ ] **Step 3: Build + run the test — it should FAIL**

Run: `cmake --build build-dev --target tst_kf6mainwindow_startup && ctest --test-dir build-dev -R tst_kf6mainwindow_startup --output-on-failure`

Expected: `staleLastActiveAutoLoadsMostRecent` FAILS (current behavior is to call the stopgap).

- [ ] **Step 4: Rewrite resolveStartupProfile**

In `src/kf6/kf6mainwindow.cpp`, find `KF6MainWindow::resolveStartupProfile()`
and replace its body with:

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
    // F.1b: stale or missing last-active falls through to
    // auto-load-most-recent.
    const auto entries = m_profileRegistry->entries();
    for (const auto &e : entries) {
        if (QDir(e.path).exists()) {
            loadProfile(e.path);
            return e.path;
        }
    }
    // Empty registry (or every entry stale) — F.1a name-prompt stopgap.
    const QString picked = showProfilePickerStopgap();
    if (!picked.isEmpty()) {
        loadProfile(picked);
        return picked;
    }
    return QString();
}
```

- [ ] **Step 5: Simplify showProfilePickerStopgap — remove non-empty branch**

In `src/kf6/kf6mainwindow.cpp`, find `KF6MainWindow::showProfilePickerStopgap()`
and replace its body with the empty-registry path only:

```cpp
QString KF6MainWindow::showProfilePickerStopgap()
{
    // F.1b: only invoked when the registry is empty (the
    // non-empty-with-stale branch now auto-loads the most-recent
    // entry from resolveStartupProfile).
    QMessageBox::information(this,
        i18n("No Profile"),
        i18n("No WildPalms profile has been created yet.\n"
             "Let's create one to get started."));

    bool ok = false;
    const QString name = QInputDialog::getText(this,
        i18n("New Profile"),
        i18n("Profile name:"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return QString();

    const auto e = m_profileRegistry->registerNew(name.trimmed());
    if (!e.isValid()) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not create profile."));
        return QString();
    }
    return e.path;
}
```

- [ ] **Step 6: Build + re-run startup tests — all should PASS now**

Run: `cmake --build build-dev --target tst_kf6mainwindow_startup && ctest --test-dir build-dev -R tst_kf6mainwindow_startup --output-on-failure`

Expected: all cases PASS, including the new `staleLastActiveAutoLoadsMostRecent`.

- [ ] **Step 7: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp tests/runtime/tst_kf6mainwindow_startup.cpp
git commit -m "F.1b T12: startup — stale last-active auto-loads most recent"
```

---

## Task 13: ProfilePropertiesDialog — General page with rename — failing tests

**Files:**
- Create: `tests/runtime/tst_profilepropertiesdialog_rename.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

```cpp
// tests/runtime/tst_profilepropertiesdialog_rename.cpp
#include <QtTest/QtTest>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "../../src/widgets/dialogs/profilepropertiesdialog.h"
#include "../../src/profile.h"
#include "../wildpalms_qtest_main.h"

class TstProfilePropertiesDialogRename : public QObject
{
    Q_OBJECT
private slots:
    void renameEmitsSignal();
    void noChangeNoSignal();
    void whitespaceOnlyIgnored();
    void nameFieldPrefilledFromProfile();
    void dialogDoesNotMutateProfileName();

private:
    QLineEdit *findNameEdit(ProfilePropertiesDialog *dlg);
};

QLineEdit *TstProfilePropertiesDialogRename::findNameEdit(
    ProfilePropertiesDialog *dlg)
{
    // The General page is the first KPageWidgetItem; its widget
    // contains a single QLineEdit named "profileName".
    auto edits = dlg->findChildren<QLineEdit *>(
        QStringLiteral("profileName"));
    return edits.isEmpty() ? nullptr : edits.first();
}

namespace {
// Construct a fully-initialised in-memory Profile for the dialog.
std::unique_ptr<Profile> makeProfile(const QString &name, const QString &path)
{
    auto p = std::make_unique<Profile>();
    p->setName(name);
    p->setSyncFolderPath(path);
    return p;
}
} // namespace

void TstProfilePropertiesDialogRename::nameFieldPrefilledFromProfile()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());

    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    QCOMPARE(edit->text(), QStringLiteral("Original"));
}

void TstProfilePropertiesDialogRename::renameEmitsSignal()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Renamed"));

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("Renamed"));
}

void TstProfilePropertiesDialogRename::noChangeNoSignal()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(spy.count(), 0);
}

void TstProfilePropertiesDialogRename::whitespaceOnlyIgnored()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("   "));

    QSignalSpy spy(&dlg, &ProfilePropertiesDialog::renameRequested);
    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(spy.count(), 0);
}

void TstProfilePropertiesDialogRename::dialogDoesNotMutateProfileName()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto profile = makeProfile(QStringLiteral("Original"), tmp.path());
    ProfilePropertiesDialog dlg(profile.get());
    auto *edit = findNameEdit(&dlg);
    QVERIFY(edit);
    edit->setText(QStringLiteral("Renamed"));

    QMetaObject::invokeMethod(&dlg, "onApply", Qt::DirectConnection);
    QCOMPARE(profile->name(), QStringLiteral("Original"));
}

WILDPALMS_QTEST_MAIN(TstProfilePropertiesDialogRename)
#include "tst_profilepropertiesdialog_rename.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

In `tests/runtime/CMakeLists.txt`, after the
`tst_kf6mainwindow_forget_profile` block:

```cmake
add_executable(tst_profilepropertiesdialog_rename
    tst_profilepropertiesdialog_rename.cpp)
target_link_libraries(tst_profilepropertiesdialog_rename
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        KF6::I18n
        KF6::ConfigCore
        KF6::WidgetsAddons
        WildPalmsCore
)
add_test(NAME tst_profilepropertiesdialog_rename
         COMMAND tst_profilepropertiesdialog_rename)
set_tests_properties(tst_profilepropertiesdialog_rename PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

`KF6::WidgetsAddons` provides `KPageDialog`, which `ProfilePropertiesDialog`
inherits from. `WildPalmsCore` links it PRIVATE-ly, so consumers
must add it explicitly. `KF6::ConfigCore` is needed because `Profile`
uses `KSharedConfig`.

- [ ] **Step 3: Build + run — expect failures (no signal yet)**

Run: `cmake --build build-dev --target tst_profilepropertiesdialog_rename 2>&1 | tail -20`

Expected: compile error — `renameRequested` is not a member of
`ProfilePropertiesDialog`.

- [ ] **Step 4: Commit the failing tests**

```bash
git add tests/runtime/tst_profilepropertiesdialog_rename.cpp \
    tests/runtime/CMakeLists.txt
git commit -m "F.1b T13: failing rename tests for ProfilePropertiesDialog"
```

---

## Task 14: ProfilePropertiesDialog — General page implementation

**Files:**
- Modify: `src/widgets/dialogs/profilepropertiesdialog.h`
- Modify: `src/widgets/dialogs/profilepropertiesdialog.cpp`

- [ ] **Step 1: Update the header**

In `src/widgets/dialogs/profilepropertiesdialog.h`, add the General-page
helper and the rename signal:

In the `Q_SIGNALS:` block, after `settingsChanged();`:

```cpp
    void renameRequested(QString id, QString newName);
```

In the `private:` member-functions section, after `createConflictPage()`:

```cpp
    QWidget* createGeneralPage();
```

In the data-member section, add at the top (before `// Device page`):

```cpp
    // General page (F.1b)
    QLineEdit *m_nameEdit;
```

- [ ] **Step 2: Implement createGeneralPage and add it as the first page**

In `src/widgets/dialogs/profilepropertiesdialog.cpp`, near the top
where the existing `createDevicePage` is defined, add:

```cpp
QWidget *ProfilePropertiesDialog::createGeneralPage()
{
    auto *page = new QWidget;
    auto *layout = new QFormLayout(page);

    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setObjectName(QStringLiteral("profileName"));
    m_nameEdit->setText(m_profile->name());
    layout->addRow(i18n("Profile name:"), m_nameEdit);

    return page;
}
```

Make sure `#include <QFormLayout>` and `#include <QLineEdit>` are
present at the top of the file (QLineEdit usually already is).

- [ ] **Step 3: Wire the General page into the ctor**

Find the existing code in the ctor that adds the Device and Conflict
pages, e.g.:

```cpp
    addPage(createDevicePage(), i18n("Device"));
    addPage(createConflictPage(), i18n("Conflict Handling"));
```

Insert the General page first:

```cpp
    addPage(createGeneralPage(), i18n("General"));
    addPage(createDevicePage(), i18n("Device"));
    addPage(createConflictPage(), i18n("Conflict Handling"));
```

- [ ] **Step 4: Emit renameRequested from onApply**

Find the existing `ProfilePropertiesDialog::onApply()` method. Add at
the **top** of the method body (before any other save logic):

```cpp
    const QString trimmedName = m_nameEdit->text().trimmed();
    if (!trimmedName.isEmpty() && trimmedName != m_profile->name()) {
        emit renameRequested(m_profile->id(), trimmedName);
    }
```

Note: the dialog deliberately does NOT call `m_profile->setName` itself
— that's the handler's job after `ProfileRegistry::rename` succeeds.

- [ ] **Step 5: Build + run the rename tests — all should PASS**

Run: `cmake --build build-dev --target tst_profilepropertiesdialog_rename && ctest --test-dir build-dev -R tst_profilepropertiesdialog_rename --output-on-failure`

Expected: 5 cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/widgets/dialogs/profilepropertiesdialog.h \
    src/widgets/dialogs/profilepropertiesdialog.cpp
git commit -m "F.1b T14: ProfilePropertiesDialog — General page with Name field"
```

---

## Task 15: KF6MainWindow — handle renameRequested from the dialog

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp`

- [ ] **Step 1: Connect renameRequested in onProfileSettings**

In `src/kf6/kf6mainwindow.cpp`, find `KF6MainWindow::onProfileSettings()`.
After the existing `connect(dlg, &ProfilePropertiesDialog::settingsChanged, ...)`
line, add:

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

- [ ] **Step 2: Build, run all relevant tests**

Run: `cmake --build build-dev -j$(nproc) && ctest --test-dir build-dev -R "tst_kf6mainwindow|tst_profileregistry|tst_profilemenucontroller|tst_profileproperties" --output-on-failure`

Expected: all PASS.

- [ ] **Step 3: Commit**

```bash
git add src/kf6/kf6mainwindow.cpp
git commit -m "F.1b T15: KF6MainWindow — handle ProfilePropertiesDialog::renameRequested"
```

---

## Task 16: Full test suite + manual smoke test

**Files:** none modified

- [ ] **Step 1: Run the full test suite**

Run: `ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -30`

Expected: 100% pass rate (or no regressions vs. pre-F.1b). Note any
pre-existing failures that don't relate to F.1b.

- [ ] **Step 2: Launch the app and verify menu structure**

Run: `./build-dev/src/wildpalms &`

Manually verify:
1. **Menu structure.** File → Profile shows: New Profile / Switch Profile ▸ / Import Profile / Forget Profile ▸ / Close Profile / — / Profile Settings. File → Configure Mappings sits below.
2. **Empty-registry first run.** If the registry is empty (use a fresh `XDG_CONFIG_HOME` if needed: `XDG_CONFIG_HOME=$(mktemp -d) ./build-dev/src/wildpalms`), the name-prompt stopgap fires.
3. **Create + switch.** Create "Test1" via File → Profile → New Profile. Quit. Relaunch — Test1 auto-loads. Use File → Profile → New Profile again to create "Test2". Test2 is now active. File → Profile → Switch Profile shows both with Test2 checkmarked/disabled. Click Test1 — switches.
4. **Import.** Forget Test2 (without delete) — entry vanishes from Switch ▸ and Forget ▸. Use File → Profile → Import Profile, pick the Test2 folder. Test2 reappears in Switch ▸ and is loaded.
5. **Forget-with-delete.** Forget Test2 (with delete checkbox) — entry vanishes; folder is gone from disk.
6. **Rename.** With Test1 active, File → Profile → Profile Settings → General page → change name to "Test1Renamed" → Apply / OK. Window title updates. Switch ▸ submenu now lists "Test1Renamed".
7. **No QInputDialog::getItem.** With Test1Renamed registered, edit `wildpalmsrc` to set `lastActiveProfileId=bogus`. Relaunch — Test1Renamed auto-loads silently (no picker dialog).

Close the app when done.

- [ ] **Step 3: Commit (no changes; this is a verification step)**

No commit needed.

---

## Task 17: Docs — mark F.1b ✅ in integration plan

**Files:**
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`

- [ ] **Step 1: Update the Phase F status row**

In `docs/plans/2026-04-20-libkalburator-integration.md`, find the row
in the phase table for Phase F (look for "F.1a ✅"). Update it from:

```
| F | Full Sync Mode UI polish + profile-creation wizard (the mode collapse itself landed in E.16 per Phase-E spec decision #3) | WP | **In progress.** F.1a ✅ done 2026-05-22 (profile persistence + app registry). F.1b / F.1c / F.2 / F.3 / F.4 pending. | E |
```

to:

```
| F | Full Sync Mode UI polish + profile-creation wizard (the mode collapse itself landed in E.16 per Phase-E spec decision #3) | WP | **In progress.** F.1a ✅ + F.1b ✅ done 2026-05-22. F.1c / F.2 / F.3 / F.4 pending. | E |
```

Also update the explanatory paragraph (around line 49):

```
  F.1b (new File menu: Switch / Import / Forget) ✅ landed 2026-05-22
  in `docs/superpowers/specs/2026-05-22-f1b-file-menu-design.md` +
  `docs/superpowers/plans/2026-05-22-f1b-file-menu.md`. F.1c (the
  multi-page wizard) — next.
```

(Old line said "F.1b … — next, brainstorm pending.")

- [ ] **Step 2: Commit**

```bash
git add docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "F.1b T17: docs — mark F.1b ✅ in integration plan"
```

---

## Verification checklist (use after all tasks complete)

- [ ] `ctest --test-dir build-dev --output-on-failure -j$(nproc)` is green (or no new failures vs. pre-F.1b).
- [ ] `grep -E "removeRecursively|QDir::rmdir|QFile::remove" src/runtime/profileregistry.cpp` returns nothing (F.1a invariant preserved).
- [ ] `grep -n "QInputDialog::getItem" src/kf6/kf6mainwindow.cpp` returns nothing (stopgap fully replaced).
- [ ] App launches and all 7 manual smoke-test items in Task 16 Step 2 pass.
- [ ] No `[gone]` branches; `git status` clean; `git log --oneline -20` shows the F.1b T1..T17 series.
