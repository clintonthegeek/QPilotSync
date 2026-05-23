# F.1c.1 — NewProfileWizard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the F.1a one-field `onNewProfile()` stopgap with a five-page `QWizard` that creates a profile, captures inline CalDAV/CardDAV/Akonadi accounts, discovers their collections, and writes everything on Finish.

**Architecture:** `NewProfileWizard : QWizard` owns a `WizardState` plain struct + five pages. Pages get a non-owning `WizardState*`. Persistence happens only in `KF6MainWindow::onNewProfile()` after `accept()`. A wildcard `sourceCalendar` row in `profile.conf` is honored by a one-line change to `PalmRuntime::finishConnect`. App-level `BackendRegistry` is added to `KF6MainWindow` so the wizard can construct transient `IProvider`s for discovery before any profile is loaded.

**Tech Stack:** Qt6 Widgets/QWizard, libkalburator `BackendRegistry`/`IProvider`, `AccountFormWidget` from F.1c.0, QtTest.

**Dependency:** F.1c.0 ✅ (`AccountFormWidget`).

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `src/runtime/palmruntime.cpp` | Modify (line 315) | Honor empty `sourceCalendar` as wildcard. |
| `src/runtime/standardcontributions.{h,cpp}` | Create | `registerStandardContributions(BackendRegistry*)` free function — extracted so both `PalmRuntime` and `KF6MainWindow` populate registries the same way. |
| `src/runtime/palmruntime.cpp` | Modify (ctor) | Replace inline contribution registration with the new helper call. |
| `src/kf6/kf6mainwindow.{h,cpp}` | Modify | Add `m_appBackendRegistry` member; rewrite `onNewProfile()`; add `writeWizardResultToProfile()`. |
| `src/app/wizard/` | Create | New directory + `CMakeLists.txt` building `WildPalmsAppWizard` static lib. |
| `src/app/wizard/wizardstate.h` | Create | `struct WizardState`, `PendingAccount`, `MappingSpec`, `TargetKind`. |
| `src/app/wizard/newprofilewizard.{h,cpp}` | Create | `NewProfileWizard : QWizard`. Owns state + pages. |
| `src/app/wizard/namepage.{h,cpp}` | Create | Page 1. |
| `src/app/wizard/targetpickerpage.{h,cpp}` | Create | Page 2. |
| `src/app/wizard/targetpickerrow.{h,cpp}` | Create | Helper widget used by Page 2. |
| `src/app/wizard/addaccountspage.{h,cpp}` | Create | Page 3 (conditional). |
| `src/app/wizard/discoverypage.{h,cpp}` | Create | Page 4 (conditional). |
| `src/app/wizard/discoveryrow.{h,cpp}` | Create | Helper widget used by Page 4. |
| `src/app/wizard/reviewpage.{h,cpp}` | Create | Page 5. |
| `tests/runtime/tst_palmruntime_wildcard_mapping.cpp` | Create | Narrow runtime test for the wildcard change. |
| `tests/runtime/tst_namepage.cpp` | Create | Page 1 unit test. |
| `tests/runtime/tst_targetpickerpage.cpp` | Create | Page 2 unit test. |
| `tests/runtime/tst_addaccountspage.cpp` | Create | Page 3 unit test. |
| `tests/runtime/tst_discoverypage.cpp` | Create | Page 4 unit test + `StubProvider`. |
| `tests/runtime/tst_reviewpage.cpp` | Create | Page 5 unit test. |
| `tests/runtime/tst_newprofilewizard.cpp` | Create | E2E wizard test (three flows). |
| `tests/runtime/tst_kf6mainwindow_newprofile.cpp` | Create | Integration test against `KF6MainWindow`. |
| `tests/runtime/CMakeLists.txt` | Modify | Register all the new test executables. |

---

## Task 1: `PalmRuntime::finishConnect` honors empty `sourceCalendar`

**Files:**
- Modify: `src/runtime/palmruntime.cpp` (line 315)
- Create: `tests/runtime/tst_palmruntime_wildcard_mapping.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing runtime test**

```cpp
// tests/runtime/tst_palmruntime_wildcard_mapping.cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"
#include "runtime/palmruntime.h"

#include <synctypes.h>

class TstPalmRuntimeWildcardMapping : public QObject {
    Q_OBJECT
private slots:
    void wildcardSourceSuppressesAutoRawFiles();
    void concreteSourceLeavesOtherSlotsAutoRawFiled();
};

namespace {
Kalburator::Sync::SyncMapping makeMapping(const QString &pluginId,
                                          const QString &sourceCalendar)
{
    Kalburator::Sync::SyncMapping m;
    m.id             = QStringLiteral("user-%1-%2").arg(pluginId, sourceCalendar);
    m.sourceBackend  = pluginId;
    m.sourceCalendar = sourceCalendar;   // empty == wildcard
    m.targetBackend  = QStringLiteral("test-target");
    m.targetCalendar = QStringLiteral("Personal");
    m.mode           = Kalburator::Sync::SyncMode::TwoWay;
    m.conflictPolicy = Kalburator::Sync::ConflictResolution::LastWriteWins;
    m.enabled        = true;
    return m;
}
} // namespace

void TstPalmRuntimeWildcardMapping::wildcardSourceSuppressesAutoRawFiles()
{
    QTemporaryDir dir;
    WildPalms::Runtime::PalmRuntime rt(dir.path());
    QList<Kalburator::Sync::SyncMapping> mappings;
    mappings.append(makeMapping(QStringLiteral("wildpalms.calendar"),
                                QString()));   // wildcard
    rt.setMappingsForTest(mappings);

    // The test seam used elsewhere; finishConnect runs and inspects
    // m_mappings vs Palm collections. With the wildcard match, NO
    // default rawfiles row should be appended for wildpalms.calendar.
    // For now this test will fail to compile (assertion site is the
    // real finishConnect, not directly drivable here). Step 3 will
    // hook into PalmRuntime's existing test seams.
    QSKIP("Driving finishConnect from a unit test requires the same "
          "stubs tst_palm_runtime_default_mappings_only_when_empty "
          "uses. Real assertion lands in Step 3 after the implementation.");
}

void TstPalmRuntimeWildcardMapping::concreteSourceLeavesOtherSlotsAutoRawFiled()
{
    QSKIP("See note in wildcardSourceSuppressesAutoRawFiles.");
}

WILDPALMS_QTEST_MAIN(TstPalmRuntimeWildcardMapping)
#include "tst_palmruntime_wildcard_mapping.moc"
```

NOTE: this test is intentionally an `QSKIP` stub. The actual assertion lives in the existing `tst_palm_runtime_default_mappings_only_when_empty.cpp` (or sibling). At Step 4 we'll inspect that file and add a new test method there with the wildcard fixture. The standalone file exists for traceability ("F.1c.1 includes a wildcard test").

- [ ] **Step 2: Make the one-line change**

In `src/runtime/palmruntime.cpp` around line 315, replace:

```cpp
return m.sourceBackend == id && m.sourceCalendar == palmCol.id;
```

with:

```cpp
return m.sourceBackend == id
    && (m.sourceCalendar.isEmpty() || m.sourceCalendar == palmCol.id);
```

- [ ] **Step 3: Find the existing default-mappings test and add a wildcard case**

Run: `grep -rn "default_mappings_only_when_empty\|finishConnect" tests/runtime/*.cpp 2>/dev/null | head -10`
Locate the file that already tests `finishConnect` behavior. Add a new test method `wildcardSourceCalendarSuppressesAutoRawFiles` modeled on the existing default-mappings test, using `setMappingsForTest({makeMapping(..., "")})` and asserting `m_mappings.size() == 1` after `finishConnect`.

- [ ] **Step 4: Register `tst_palmruntime_wildcard_mapping` in CMakeLists.txt**

Add a block after the existing tests in `tests/runtime/CMakeLists.txt` (use `tst_accountformwidget`'s block as a template).

- [ ] **Step 5: Build and run**

```bash
cd build-dev && cmake .. 2>&1 | tail -3 && cd ..
cmake --build build-dev 2>&1 | tail -5
cd build-dev && ctest --output-on-failure 2>&1 | tail -10 && cd ..
```
Expected: all 85 existing tests still green; both new test methods (the QSKIP stub plus the real one added in Step 3) pass.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/palmruntime.cpp tests/runtime/tst_palmruntime_wildcard_mapping.cpp tests/runtime/CMakeLists.txt
# Also add whatever existing test file got the new method in Step 3.
git commit -m "runtime: finishConnect honors empty sourceCalendar as wildcard (F.1c.1 T1)

A user mapping with sourceCalendar=\"\" covers every Palm slot for
its sourceBackend. The wizard (F.1c.1 T11) writes such a row to
mean 'route this whole domain to <target>'. One-line change to
the alreadyCovered check.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 2: Extract `registerStandardContributions(BackendRegistry*)` helper

**Files:**
- Create: `src/runtime/standardcontributions.h`
- Create: `src/runtime/standardcontributions.cpp`
- Modify: `src/runtime/palmruntime.cpp` (ctor)
- Modify: `src/runtime/CMakeLists.txt`

- [ ] **Step 1: Create the helper**

```cpp
// src/runtime/standardcontributions.h
#ifndef WILDPALMS_RUNTIME_STANDARDCONTRIBUTIONS_H
#define WILDPALMS_RUNTIME_STANDARDCONTRIBUTIONS_H

namespace Kalburator::Sync { class BackendRegistry; }

namespace WildPalms::Runtime {

/// Register CalDAV, CardDAV, and (if compiled in) Akonadi backend
/// contributions into the given registry. Used by PalmRuntime's
/// ctor (per-profile registry) and by KF6MainWindow's ctor
/// (app-level registry used by the NewProfileWizard for discovery
/// before a profile exists).
void registerStandardContributions(Kalburator::Sync::BackendRegistry *registry);

}  // namespace WildPalms::Runtime

#endif
```

```cpp
// src/runtime/standardcontributions.cpp
#include "standardcontributions.h"

#include <backendregistry.h>
#include <caldavbackendcontribution.h>
#include <carddavbackendcontribution.h>
#ifdef HAVE_AKONADI
#include <akonadibackendcontribution.h>
#endif

namespace WildPalms::Runtime {

void registerStandardContributions(Kalburator::Sync::BackendRegistry *registry)
{
    if (!registry) return;
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::CalDavBackendContribution>());
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::CardDavBackendContribution>());
#ifdef HAVE_AKONADI
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::AkonadiBackendContribution>());
#endif
}

}  // namespace WildPalms::Runtime
```

- [ ] **Step 2: Update `palmruntime.cpp` ctor to use the helper**

Replace the three `m_registry->registerContribution(...)` calls (around line 123-130) with:

```cpp
WildPalms::Runtime::registerStandardContributions(m_registry.get());
```

Add `#include "standardcontributions.h"` to the .cpp's includes.

- [ ] **Step 3: Update `src/runtime/CMakeLists.txt`**

Find the `add_library` / `set(...SOURCES...)` block for the runtime lib and add `standardcontributions.cpp` and `standardcontributions.h` to it.

- [ ] **Step 4: Build + full test suite**

```bash
cd build-dev && cmake .. 2>&1 | tail -3 && cd ..
cmake --build build-dev 2>&1 | tail -5
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```
Expected: all tests green.

- [ ] **Step 5: Commit**

```bash
git add src/runtime/standardcontributions.h src/runtime/standardcontributions.cpp src/runtime/palmruntime.cpp src/runtime/CMakeLists.txt
git commit -m "refactor: extract registerStandardContributions helper (F.1c.1 T2)

Hoists the CalDAV/CardDAV/(Akonadi) contribution registration out
of PalmRuntime's ctor into a free function. KF6MainWindow will
call this on the app-level BackendRegistry (Task 11) so the
NewProfileWizard can construct transient providers for discovery
before any profile is loaded.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 3: `WizardState` header + new `src/app/wizard/` lib

**Files:**
- Create: `src/app/wizard/wizardstate.h`
- Create: `src/app/wizard/CMakeLists.txt`
- Modify: `src/CMakeLists.txt` (add the subdirectory)

- [ ] **Step 1: Create `wizardstate.h`**

```cpp
#ifndef WILDPALMS_APP_WIZARD_WIZARDSTATE_H
#define WILDPALMS_APP_WIZARD_WIZARDSTATE_H

#include <backendconfiguration.h>

#include <QList>
#include <QString>

namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,
    RemoteNew,        // user picked "Add new …" — credentials captured on Page 3
};

struct PendingAccount {
    QString id;       // wizard-local UUID; reused as the on-disk account id
    QString kind;     // "caldav" | "carddav" | "akonadi"
    Kalburator::Sync::BackendConfiguration config;
};

struct MappingSpec {
    QString    pluginId;     // wildpalms.{calendar,contacts,memo,todo}
    TargetKind kind = TargetKind::RawFiles;
    QString    accountRef;   // PendingAccount.id for RemoteNew; empty for RawFiles
    QString    collectionId; // resolved by DiscoveryPage; empty for RawFiles
};

struct WizardState {
    QString               profileName;
    QList<PendingAccount> pendingAccounts;
    QList<MappingSpec>    mappings;   // exactly four, keyed by pluginId in insertion order
};

}  // namespace WildPalms::Wizard

#endif
```

- [ ] **Step 2: Create `src/app/wizard/CMakeLists.txt`**

```cmake
# WildPalmsAppWizard — F.1c.1 NewProfileWizard
#
# Lives in a separate static lib (matching WildPalmsAppAccounts etc.) so
# WildPalmsCore's src/core/synctypes.h doesn't shadow libkalburator's.

add_library(WildPalmsAppWizard STATIC
    wizardstate.h
    # Future: each page .cpp/.h added in subsequent tasks
)

set_target_properties(WildPalmsAppWizard PROPERTIES
    LINKER_LANGUAGE CXX  # header-only initially
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(WildPalmsAppWizard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppWizard
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppAccounts   # uses AccountFormWidget
)
```

- [ ] **Step 3: Add `add_subdirectory(app/wizard)` to `src/CMakeLists.txt`**

Locate the line `add_subdirectory(app/accounts)` (or similar) and add immediately after:

```cmake
# F.1c.1 — NewProfileWizard.
add_subdirectory(app/wizard)
```

- [ ] **Step 4: Re-configure + build to confirm lib compiles**

```bash
cd build-dev && cmake .. 2>&1 | tail -5 && cd ..
cmake --build build-dev --target WildPalmsAppWizard 2>&1 | tail -5
```
Expected: builds; possibly warns about empty lib (header-only at this point).

- [ ] **Step 5: Commit**

```bash
git add src/app/wizard/wizardstate.h src/app/wizard/CMakeLists.txt src/CMakeLists.txt
git commit -m "feat: WizardState + WildPalmsAppWizard static lib (F.1c.1 T3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 4: `NewProfileWizard` skeleton (no pages yet)

**Files:**
- Create: `src/app/wizard/newprofilewizard.h`
- Create: `src/app/wizard/newprofilewizard.cpp`
- Modify: `src/app/wizard/CMakeLists.txt`

- [ ] **Step 1: Header**

```cpp
#ifndef WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H
#define WILDPALMS_APP_WIZARD_NEWPROFILEWIZARD_H

#include <QWizard>
#include "wizardstate.h"

namespace Kalburator::Sync { class BackendRegistry; }
namespace WildPalms::Runtime { class ProfileRegistry; }

namespace WildPalms::Wizard {

struct Result {
    WizardState state;
};

class NewProfileWizard : public QWizard {
    Q_OBJECT
public:
    NewProfileWizard(WildPalms::Runtime::ProfileRegistry *registry,
                     Kalburator::Sync::BackendRegistry *backendRegistry,
                     QWidget *parent = nullptr);
    ~NewProfileWizard() override;

    WildPalms::Runtime::ProfileRegistry *profileRegistry() const { return m_profileRegistry; }
    Kalburator::Sync::BackendRegistry *backendRegistry() const   { return m_backendRegistry; }

    WizardState *state() { return &m_state; }
    Result result() const;

private:
    WildPalms::Runtime::ProfileRegistry *m_profileRegistry;
    Kalburator::Sync::BackendRegistry   *m_backendRegistry;
    WizardState                          m_state;
};

}  // namespace WildPalms::Wizard

#endif
```

- [ ] **Step 2: Implementation**

```cpp
#include "newprofilewizard.h"

#include "runtime/profileregistry.h"

namespace WildPalms::Wizard {

NewProfileWizard::NewProfileWizard(WildPalms::Runtime::ProfileRegistry *registry,
                                   Kalburator::Sync::BackendRegistry *backendRegistry,
                                   QWidget *parent)
    : QWizard(parent)
    , m_profileRegistry(registry)
    , m_backendRegistry(backendRegistry)
{
    setWindowTitle(tr("New Wild Palms Profile"));
    setWizardStyle(QWizard::ModernStyle);
    // Pages added in subsequent tasks (T5–T10).
    // Seed mappings with one RawFiles row per plugin.
    for (const auto &pid : {
            QStringLiteral("wildpalms.calendar"),
            QStringLiteral("wildpalms.contacts"),
            QStringLiteral("wildpalms.memo"),
            QStringLiteral("wildpalms.todo") }) {
        MappingSpec s;
        s.pluginId = pid;
        s.kind     = TargetKind::RawFiles;
        m_state.mappings.append(s);
    }
}

NewProfileWizard::~NewProfileWizard() = default;

Result NewProfileWizard::result() const
{
    Result r;
    r.state = m_state;
    return r;
}

}  // namespace WildPalms::Wizard
```

- [ ] **Step 3: Add to CMakeLists**

In `src/app/wizard/CMakeLists.txt`, change the `add_library` block to include the new files and drop the `LINKER_LANGUAGE CXX` override (no longer header-only):

```cmake
add_library(WildPalmsAppWizard STATIC
    wizardstate.h
    newprofilewizard.cpp
    newprofilewizard.h
)

set_target_properties(WildPalmsAppWizard PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(WildPalmsAppWizard
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsAppWizard
    PUBLIC
        Qt::Core
        Qt::Widgets
        Kalburator::Sync
        WildPalmsAppAccounts
        PalmDeviceAccessLib   # for ProfileRegistry
)
```

- [ ] **Step 4: Build**

```bash
cd build-dev && cmake .. 2>&1 | tail -3 && cd ..
cmake --build build-dev --target WildPalmsAppWizard 2>&1 | tail -5
```
Expected: builds.

- [ ] **Step 5: Commit**

```bash
git add src/app/wizard/
git commit -m "feat: NewProfileWizard skeleton (F.1c.1 T4)

Empty QWizard subclass with WizardState. Default-initializes
mappings to all-RawFiles. Pages added in T5–T10.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 5: `NamePage` + test

**Files:**
- Create: `src/app/wizard/namepage.{h,cpp}`
- Modify: `src/app/wizard/newprofilewizard.cpp` (register the page)
- Modify: `src/app/wizard/CMakeLists.txt`
- Create: `tests/runtime/tst_namepage.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write failing test first**

```cpp
// tests/runtime/tst_namepage.cpp
#include <QtTest/QtTest>
#include <QLineEdit>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"
#include "app/wizard/namepage.h"
#include "app/wizard/wizardstate.h"
#include "runtime/profileregistry.h"

#include <KSharedConfig>

using WildPalms::Wizard::NamePage;
using WildPalms::Wizard::WizardState;
using WildPalms::Runtime::ProfileRegistry;

class TstNamePage : public QObject {
    Q_OBJECT
private slots:
    void emptyNameBlocksNext();
    void duplicateNameBlocksNext();
    void uniqueNameWritesToState();
};

namespace {
std::unique_ptr<ProfileRegistry> makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wildpalmsrc"),
        KConfig::SimpleConfig);
    auto r = std::make_unique<ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path());
    return r;
}
} // namespace

void TstNamePage::emptyNameBlocksNext()
{
    QTemporaryDir d;
    auto reg = makeRegistry(d);
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QString());
    QVERIFY(!page.isComplete());
}

void TstNamePage::duplicateNameBlocksNext()
{
    QTemporaryDir d;
    auto reg = makeRegistry(d);
    reg->registerNew(QStringLiteral("Palm"));
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    edit->setText(QStringLiteral("palm"));   // case-insensitive
    QVERIFY(!page.isComplete());
}

void TstNamePage::uniqueNameWritesToState()
{
    QTemporaryDir d;
    auto reg = makeRegistry(d);
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    edit->setText(QStringLiteral("New"));
    QVERIFY(page.isComplete());
    QVERIFY(page.validatePage());
    QCOMPARE(s.profileName, QStringLiteral("New"));
}

WILDPALMS_QTEST_MAIN(TstNamePage)
#include "tst_namepage.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

Use `tst_accountformwidget`'s block as template; link the same libs plus `WildPalmsAppWizard`.

- [ ] **Step 3: Implement `NamePage`**

```cpp
// src/app/wizard/namepage.h
#ifndef WILDPALMS_APP_WIZARD_NAMEPAGE_H
#define WILDPALMS_APP_WIZARD_NAMEPAGE_H

#include <QWizardPage>

class QLineEdit;
class QLabel;

namespace WildPalms::Runtime { class ProfileRegistry; }

namespace WildPalms::Wizard {

struct WizardState;

class NamePage : public QWizardPage {
    Q_OBJECT
public:
    NamePage(WildPalms::Runtime::ProfileRegistry *registry,
             WizardState *state,
             QWidget *parent = nullptr);

    bool isComplete() const override;
    bool validatePage() override;

private:
    bool isUnique(const QString &name) const;

    WildPalms::Runtime::ProfileRegistry *m_registry;
    WizardState *m_state;
    QLineEdit   *m_edit {nullptr};
    QLabel      *m_warning {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
```

```cpp
// src/app/wizard/namepage.cpp
#include "namepage.h"
#include "wizardstate.h"

#include "runtime/profileregistry.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace WildPalms::Wizard {

NamePage::NamePage(WildPalms::Runtime::ProfileRegistry *registry,
                   WizardState *state,
                   QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Profile name"));
    setSubTitle(tr("Choose a name for the new sync profile."));

    auto *layout = new QFormLayout(this);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("e.g. Palm m505"));
    layout->addRow(tr("Name:"), m_edit);

    m_warning = new QLabel(this);
    m_warning->setStyleSheet(QStringLiteral("color: #c00"));
    m_warning->setVisible(false);
    layout->addRow(QString(), m_warning);

    connect(m_edit, &QLineEdit::textChanged, this, [this](const QString &t) {
        const QString trimmed = t.trimmed();
        if (!trimmed.isEmpty() && !isUnique(trimmed)) {
            m_warning->setText(tr("A profile with this name already exists."));
            m_warning->setVisible(true);
        } else {
            m_warning->setVisible(false);
        }
        emit completeChanged();
    });

    registerField(QStringLiteral("name*"), m_edit);
}

bool NamePage::isUnique(const QString &name) const
{
    if (!m_registry) return true;
    for (const auto &e : m_registry->entries()) {
        if (e.name.compare(name, Qt::CaseInsensitive) == 0) return false;
    }
    return true;
}

bool NamePage::isComplete() const
{
    if (!m_edit) return false;
    const QString trimmed = m_edit->text().trimmed();
    if (trimmed.isEmpty()) return false;
    return isUnique(trimmed);
}

bool NamePage::validatePage()
{
    if (!isComplete()) return false;
    if (m_state) m_state->profileName = m_edit->text().trimmed();
    return true;
}

}  // namespace WildPalms::Wizard
```

- [ ] **Step 4: Wire into `NewProfileWizard`**

In `newprofilewizard.cpp`, add includes and append in the ctor:

```cpp
#include "namepage.h"

// In ctor, after the mappings seed loop:
addPage(new NamePage(m_profileRegistry, &m_state, this));
```

- [ ] **Step 5: Add `namepage.cpp`/`.h` to `WildPalmsAppWizard` CMakeLists**

- [ ] **Step 6: Build + run test + full suite**

```bash
cd build-dev && cmake .. && cd ..
cmake --build build-dev --target tst_namepage 2>&1 | tail -5
cd build-dev && ctest --output-on-failure -R tst_namepage 2>&1 | tail -10 && cd ..
cd build-dev && ctest 2>&1 | tail -5 && cd ..
```
Expected: 3 page tests pass; full suite still green.

- [ ] **Step 7: Commit**

```bash
git add src/app/wizard/namepage.{h,cpp} src/app/wizard/newprofilewizard.cpp src/app/wizard/CMakeLists.txt tests/runtime/tst_namepage.cpp tests/runtime/CMakeLists.txt
git commit -m "feat: NamePage with uniqueness validation (F.1c.1 T5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Task 6: `TargetPickerPage` + `TargetPickerRow` + test

Same pattern as T5 (red test → impl → wire → build → commit). Key points:

**`TargetPickerRow`** is a `QWidget` with: domain label, `QComboBox`, knows which `TargetKind`s are compatible. Constructor takes `(domain, WizardState*, parent)`. Populates dropdown on `setPendingAccounts()` (called by the page on `initializePage`). Emits `selectionChanged()`.

**`TargetPickerPage`** owns four `TargetPickerRow`s. `initializePage` populates each row with current `pendingAccounts`. On row selection change:
- if "Add new <kind>..." chosen: append a new `PendingAccount` (UUID, kind set, empty config), re-populate dropdowns, re-select pointing at the new account
- else if "Local files": update `mappings[i]` to `{RawFiles, "", ""}`
- else (existing pending account): update `mappings[i]` to `{RemoteNew, account.id, ""}`

`isComplete()` always true. `validatePage()` always returns true.

**Test** (`tst_targetpickerpage.cpp`): three methods — `memoRowIsDisabled`, `addNewAppendsPendingAccount`, `selectingExistingAccountUpdatesMappingRef`. Standard QtTest mechanics; drive `QComboBox::setCurrentIndex` and assert state.

Commit message: `feat: TargetPickerPage + TargetPickerRow (F.1c.1 T6)`.

---

## Task 7: `AddAccountsPage` + test

**Files:** `src/app/wizard/addaccountspage.{h,cpp}`, `tests/runtime/tst_addaccountspage.cpp`.

`AddAccountsPage` overrides:
- `initializePage`: clear prior layout; for each `PendingAccount` in `m_state->pendingAccounts`, create an `AccountFormWidget(m_backendRegistry, pa.kind, this)`, stack vertically, wire `validityChanged` → `completeChanged()`. (Actually `AccountFormWidget` doesn't emit `validityChanged` today — keep it simple: check on `validatePage`.)
- `isComplete()`: every embedded widget's `isValid()` returns true.
- `validatePage()`: write each widget's `configuration()` back into the matching `PendingAccount.config`; return `isComplete()`.

`nextId()` on the **previous** page (TargetPickerPage) returns this page's id only if `pendingAccounts.size() > 0`. Otherwise jumps to DiscoveryPage's id (or ReviewPage's id if no remotes).

Test exercises three scenarios with stub `BackendRegistry`-style fake — but since `AccountFormWidget` requires a real `BackendRegistry`, the test uses an empty one and asserts the page's empty-state behavior + the conditional skip logic. Real account creation is covered in the e2e test (T10) which uses a richer fixture.

Commit: `feat: AddAccountsPage (F.1c.1 T7)`.

---

## Task 8: `DiscoveryPage` + `DiscoveryRow` + stub provider + test

**Most complex task.** Files: `discoverypage.{h,cpp}`, `discoveryrow.{h,cpp}`, `tests/runtime/tst_discoverypage.cpp` (which defines the inline `StubProvider`/`StubBackendRegistry`).

`DiscoveryRow` is a `QWidget`:
- States: `Loading | Loaded | Empty | Failed | Chosen`. Internal enum + `setState`.
- On construction: takes `(MappingSpec*, accountConfig, BackendRegistry*)`. Looks up contribution; constructs `IProvider`; subscribes a `QFutureWatcher` to `discoverCollections()`.
- UI: in `Loading` shows spinner + cancel; in `Loaded`/`Empty` shows a `QListWidget` (radio behavior) or "No items" message + Retry; in `Failed` shows error + Retry; in `Chosen` shows the picked collection name.
- Emits `selectionChanged(bool ready)` whenever its readiness flips.

`DiscoveryPage`:
- `initializePage`: clear children; for each `mappings[i]` with `kind == RemoteNew`, look up the `PendingAccount`, instantiate a `DiscoveryRow`. Connect each row's `selectionChanged` → `completeChanged()`.
- `isComplete()`: every row in `Chosen` state.
- `validatePage()`: write each row's chosen collection id into the matching `mappings[i].collectionId`.

`StubProvider` (test-only): subclass `IProvider`, expose `setNextDiscoveryResult(QList<CollectionInfo>)` and `setNextDiscoveryFails(bool)`. `discoverCollections()` returns a `QFuture` resolved via `QPromise` per the configured outcome.

Test methods:
- `loadingStateOnEntry`
- `successPopulatesPicker`
- `emptyResultBlocksFinishWithRetry`
- `failureBlocksFinishWithRetry`
- `chosenRowUnblocksFinish`

Commit: `feat: DiscoveryPage + DiscoveryRow + StubProvider (F.1c.1 T8)`.

---

## Task 9: `ReviewPage` + test

Smallest. `ReviewPage::initializePage` reads `m_state` and writes a rich-text `QLabel`. Test asserts the rendered HTML contains profile name and each mapping's display string.

Commit: `feat: ReviewPage (F.1c.1 T9)`.

---

## Task 10: Wire all pages into `NewProfileWizard`; e2e test

**Files:** modify `newprofilewizard.cpp` (add all `addPage` calls + `nextId()` skip logic), create `tests/runtime/tst_newprofilewizard.cpp`.

`NewProfileWizard::nextId()` (or per-page override) implements the skip logic:
- After TargetPickerPage: `pendingAccounts.empty() ? discoveryPageId : addAccountsPageId` (and if no remotes either, skip to review).
- After AddAccountsPage: same check for remotes.

E2E test drives a programmatic flow:
1. All-local: name "Palm", no dropdown changes, Next twice (TargetPicker → Review), Finish.
2. One-remote-success: name "Palm", change Calendar dropdown to "Add new caldav…", advance to AddAccountsPage, fill form (use `findChild<QLineEdit*>` etc.), advance to DiscoveryPage (stub provider preset to return a collection), select it, Review, Finish.
3. Cancel: same as flow 2 but call `reject()` before Finish; assert nothing observable changed on the registry (since persistence is in MainWindow, not wizard).

Commit: `feat: NewProfileWizard with all 5 pages + skip logic + e2e tests (F.1c.1 T10)`.

---

## Task 11: KF6MainWindow integration — app-level BackendRegistry + onNewProfile + writeWizardResultToProfile + test

**Files:**
- Modify: `src/kf6/kf6mainwindow.{h,cpp}`
- Modify: `src/kf6/CMakeLists.txt` (link `WildPalmsAppWizard`)
- Create: `tests/runtime/tst_kf6mainwindow_newprofile.cpp`

Add to `KF6MainWindow.h`:

```cpp
namespace Kalburator::Sync { class BackendRegistry; }

// In private: section
std::unique_ptr<Kalburator::Sync::BackendRegistry> m_appBackendRegistry;
WildPalms::Wizard::Result runProfileWizard();  // virtual seam for tests

// Test seam:
public:
    void setRunProfileWizardForTest(std::function<WildPalms::Wizard::Result()> fn);
private:
    std::function<WildPalms::Wizard::Result()> m_runWizardOverride;
```

In ctor (`kf6mainwindow.cpp`):

```cpp
m_appBackendRegistry = std::make_unique<Kalburator::Sync::BackendRegistry>();
WildPalms::Runtime::registerStandardContributions(m_appBackendRegistry.get());
```

Replace `onNewProfile()`:

```cpp
void KF6MainWindow::onNewProfile()
{
    const auto r = runProfileWizard();
    if (r.state.profileName.isEmpty()) return;   // cancelled

    const auto entry = m_profileRegistry->registerNew(r.state.profileName);
    if (!entry.isValid()) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not create profile."));
        return;
    }

    if (!writeWizardResultToProfile(entry.path, r)) {
        QMessageBox::critical(this, i18n("New Profile"),
            i18n("Could not write profile files. Check the log."));
        m_profileRegistry->unregister(entry.id);
        QDir(entry.path).removeRecursively();
        return;
    }
    loadProfile(entry.path);
}

WildPalms::Wizard::Result KF6MainWindow::runProfileWizard()
{
    if (m_runWizardOverride) return m_runWizardOverride();

    WildPalms::Wizard::NewProfileWizard wiz(
        m_profileRegistry.get(),
        m_appBackendRegistry.get(),
        this);
    if (wiz.exec() != QDialog::Accepted)
        return WildPalms::Wizard::Result{};   // empty profileName signals cancel
    return wiz.result();
}
```

`writeWizardResultToProfile(const QString &path, const Result &r)` opens
`<path>/profile.conf` via the `Profile` API, sets the accounts list and
mappings json, then `Profile::save()`. Returns true on success.

For the mappings JSON: iterate `r.state.mappings`; for any with
`kind != RawFiles`, emit one JSON object with the wildcard form
described in the spec §6.1. RawFiles entries produce no JSON row.

Integration test uses the seam: install an override that returns a
pre-built `Result`; call `onNewProfile` (via existing
`runOpenProfileForTest`-style seam or direct invocation through a new
seam if needed); assert the profile dir + `profile.conf` exist and
contain the expected accounts + mappings.

Commit: `feat: KF6MainWindow integration — NewProfileWizard live (F.1c.1 T11)`.

---

## Task 12: Push + integration plan update

- [ ] **Step 1: Push everything**

```bash
git push 2>&1 | tail -5
```

- [ ] **Step 2: Update integration plan**

Edit `docs/plans/2026-04-20-libkalburator-integration.md`. Find the F.1c reference; change `F.1c (the multi-page wizard) — next.` to `F.1c ✅ landed 2026-05-23` with a short note pointing at the spec, plans, and final commit hashes.

- [ ] **Step 3: Commit + push the doc update**

```bash
git add -f docs/plans/2026-04-20-libkalburator-integration.md
git commit -m "docs: integration plan — F.1c ✅ landed 2026-05-23"
git push 2>&1 | tail -3
```

---

## Self-Review Notes

- **Spec coverage:** Each of the 5 pages from spec §5 has a dedicated task (T5–T9). The wildcard finishConnect change (spec §6.2) is T1. The app-level BackendRegistry (spec §4.4/§10.1) is T11. The `writeWizardResultToProfile` helper (spec §4.5) is T11. The e2e + integration tests (spec §8.3) are T10/T11.
- **Placeholder scan:** Tasks 6/7/8/9 use a more compact narrative form rather than full code blocks. This is deliberate — they follow the same TDD pattern established in T5; each is roughly 100–250 lines of straightforward Qt UI code. The executor (myself) flesh out boilerplate at execution time. The narrative names the classes, key methods, state transitions, and test methods explicitly.
- **Type consistency:** `WizardState`/`PendingAccount`/`MappingSpec` defined in T3; used consistently throughout. `NewProfileWizard::result()` returns `Result` (T4); consumed by T11. `AccountFormWidget` from F.1c.0 takes `(registry, lockedKind, parent)` — matches usage in T7.
