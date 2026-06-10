# Accounts-First Profile Wizard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dropdown-as-account-factory wizard flow with an accounts-first flow (Name → Accounts → Bindings → Review), driven by libkalburator's registry/provider model, with Akonadi compiled in.

**Architecture:** Page 2 creates accounts via the existing `AddAccountDialog` and connects each provider immediately (collections discovered once per account). Page 3's per-conduit dropdowns list only real `(account, collection)` pairs filtered by domain. `PendingAccount` staging, `AddAccountsPage`, `DiscoveryPage`/`DiscoveryRow`, and the hardcoded `{domain → kinds}` table are deleted. Persistence (`Profile::setAccounts` + mappings JSON) is unchanged, so `PalmRuntime` needs no changes.

**Tech Stack:** Qt6 Widgets/QWizard, libkalburator v0.66 (`IProvider`, `BackendContribution`, `BackendRegistry`, `CollectionInfo`), QtTest with `QT_QPA_PLATFORM=offscreen`.

**Spec:** `docs/superpowers/specs/2026-06-09-accounts-first-wizard-design.md`

**Build/test commands** (legacy `build/` dir, no presets):
```bash
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.66 -DKALBURATOR_HAVE_AKONADI=ON
cmake --build build -j 8
ctest --test-dir build -j 8
```
Baseline before this plan: **120/120 pass.** Every task ends with a green build + green ctest.

---

## File structure (end state)

```
src/app/wizard/
  wizardstate.h            REWRITTEN  — WizardAccount replaces PendingAccount; TargetKind::Account
  domainfilter.{h,cpp}     NEW        — collectionMatchesDomain(CollectionInfo, pluginId)
  accountssetuppage.{h,cpp} NEW       — Page 2: account list + AddAccountDialog + async connect
  targetpickerrow.{h,cpp}  REWRITTEN  — dropdown of (account ▸ collection) bindings
  targetpickerpage.{h,cpp} REWRITTEN  — Page 3: four binding rows
  newprofilewizard.{h,cpp} MODIFIED   — new PageId enum, sequential flow
  reviewpage.cpp           MODIFIED   — renames + collection-name lookup
  namepage.{h,cpp}         UNCHANGED
  addaccountspage.{h,cpp}  DELETED
  discoverypage.{h,cpp}    DELETED
  discoveryrow.{h,cpp}     DELETED
src/app/accounts/
  accountformwidget.{h,cpp} MODIFIED  — +setConfiguration(); −locked-kind ctor; Akonadi label
  addaccountdialog.{h,cpp}  MODIFIED  — +setConfiguration() pass-through
src/kf6/kf6mainwindow.cpp   MODIFIED  — mechanical renames in writeWizardResultToProfile
CMakeLists.txt              MODIFIED  — KALBURATOR_HAVE_AKONADI default ON
tests/runtime/
  tst_domainfilter.cpp      NEW
  tst_accountssetuppage.cpp NEW
  tst_targetpickerpage.cpp  REWRITTEN
  tst_discoverypage.cpp     DELETED
  tst_addaccountspage.cpp   DELETED
  tst_accountformwidget.cpp MODIFIED
  tst_newprofilewizard.cpp  MODIFIED
  tst_kf6mainwindow_newprofile.cpp  MODIFIED (renames only)
  tst_reviewpage.cpp        MODIFIED (renames only)
```

Key libkalburator facts (v0.66, no lib changes needed):
- `HAVE_AKONADI` is a PUBLIC compile definition on the `kalburator` target (their CMakeLists:753), so WP's `#ifdef HAVE_AKONADI` in `src/runtime/standardcontributions.cpp` activates automatically when `KALBURATOR_HAVE_AKONADI=ON`.
- `CollectionInfo` (`src/types/collectioninfo.h`): `id`, `name`, `type` ("memos"|"contacts"|"calendar"|"todos"), `readOnly`, `contentTypes` ("VEVENT"/"VTODO"/"VCARD").
- `IProvider::connect()` returns `QFuture<bool>`; `collections()` valid after; `error(QString)` signal carries failure reason.
- `BackendRegistry::contributionFor(type)` → `BackendContribution::createProvider()`.

---

### Task 1: Enable Akonadi in the build

**Files:**
- Modify: `CMakeLists.txt:59`

- [ ] **Step 1: Flip the option default**

In `CMakeLists.txt`, change line 59:

```cmake
# Phase L.8: allow KALBURATOR_HAVE_AKONADI to be set by the caller; default OFF
option(KALBURATOR_HAVE_AKONADI "Build with KPim6 Akonadi support" OFF)
```
to:
```cmake
# Accounts-first wizard (2026-06-09): Akonadi is a first-class local target.
option(KALBURATOR_HAVE_AKONADI "Build with KPim6 Akonadi support" ON)
```

- [ ] **Step 2: Reconfigure (cache override needed once — the old build dir caches OFF)**

Run:
```bash
cmake -S . -B build -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.66 -DKALBURATOR_HAVE_AKONADI=ON
```
Expected: configure succeeds; libkalburator status line prints `akonadi=ON`. The lib runs `find_package(KPim6Akonadi REQUIRED)` — the `akonadi` 26.04.2 package is installed and provides the CMake config. If configure fails on a missing KPim6 package, install it (`sudo pacman -S --needed akonadi akonadi-contacts`) and re-run.

- [ ] **Step 3: Build**

Run: `cmake --build build -j 8`
Expected: clean build. `AkonadiBackendContribution` now compiles into the lib and `registerStandardContributions()` registers it (the `#ifdef HAVE_AKONADI` block in `src/runtime/standardcontributions.cpp:21-24` activates via the PUBLIC compile definition).

- [ ] **Step 4: Baseline ctest**

Run: `ctest --test-dir build -j 8`
Expected: 120/120 pass. If an Akonadi-related test misbehaves because no Akonadi daemon runs, note it — but none of the existing 120 touch Akonadi providers, so this is not expected.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: enable KALBURATOR_HAVE_AKONADI by default"
```

---

### Task 2: Domain filter helper (TDD)

**Files:**
- Create: `src/app/wizard/domainfilter.h`
- Create: `src/app/wizard/domainfilter.cpp`
- Create: `tests/runtime/tst_domainfilter.cpp`
- Modify: `src/app/wizard/CMakeLists.txt`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/runtime/tst_domainfilter.cpp`:

```cpp
// tests/runtime/tst_domainfilter.cpp
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "app/wizard/domainfilter.h"
#include <collectioninfo.h>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Wizard::collectionMatchesDomain;

class TstDomainFilter : public QObject {
    Q_OBJECT
private slots:
    void matchesByCollectionType();
    void matchesByContentTypesFallback();
    void vtodoCalendarServesBothCalendarAndTodo();
    void unknownPluginMatchesNothing();
};

void TstDomainFilter::matchesByCollectionType()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("contacts")));

    c.type = QStringLiteral("contacts");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("contacts")));

    c.type = QStringLiteral("todos");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("todo")));

    c.type = QStringLiteral("memos");
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("memo")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("calendar")));
}

void TstDomainFilter::matchesByContentTypesFallback()
{
    CollectionInfo c;   // no type set — DAV servers may only report contentTypes
    c.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("todo")));

    c.contentTypes = { QStringLiteral("VCARD") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("contacts")));
}

void TstDomainFilter::vtodoCalendarServesBothCalendarAndTodo()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    c.contentTypes = { QStringLiteral("VEVENT"), QStringLiteral("VTODO") };
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("calendar")));
    QVERIFY(collectionMatchesDomain(c, QStringLiteral("todo")));
}

void TstDomainFilter::unknownPluginMatchesNothing()
{
    CollectionInfo c;
    c.type = QStringLiteral("calendar");
    QVERIFY(!collectionMatchesDomain(c, QStringLiteral("plucker")));
}

WILDPALMS_QTEST_MAIN(TstDomainFilter)
#include "tst_domainfilter.moc"
```

- [ ] **Step 2: Register the test in `tests/runtime/CMakeLists.txt`**

Append (copy the exact block style used by `tst_targetpickerpage` at lines 455-476):

```cmake
# Accounts-first wizard — domain filter unit test
add_executable(tst_domainfilter tst_domainfilter.cpp)
target_link_libraries(tst_domainfilter
    PRIVATE
        Qt::Core
        Qt::Test
        Qt::Widgets
        $<LINK_LIBRARY:WHOLE_ARCHIVE,Kalburator::Sync>
        WildPalmsAppWizard
        WildPalmsAppAccounts
        WildPalmsCore
        WildPalmsPalmDevice
        KF6::I18n
        KF6::ConfigCore
        pisock
        bluetooth
        usb
)
add_test(NAME tst_domainfilter COMMAND tst_domainfilter)
set_tests_properties(tst_domainfilter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build build -j 8 --target tst_domainfilter`
Expected: FAIL to compile — `app/wizard/domainfilter.h` not found.

- [ ] **Step 4: Write the implementation**

Create `src/app/wizard/domainfilter.h`:

```cpp
#ifndef WILDPALMS_APP_WIZARD_DOMAINFILTER_H
#define WILDPALMS_APP_WIZARD_DOMAINFILTER_H

#include <QString>

namespace Kalburator::Sync { struct CollectionInfo; }

namespace WildPalms::Wizard {

/// True when a discovered collection can serve as the sync target for the
/// given Palm conduit pluginId (calendar|contacts|memo|todo). Matches on
/// CollectionInfo::type with a contentTypes fallback (DAV servers report
/// VEVENT/VTODO/VCARD).
bool collectionMatchesDomain(const Kalburator::Sync::CollectionInfo &c,
                             const QString &pluginId);

}  // namespace WildPalms::Wizard

#endif
```

Create `src/app/wizard/domainfilter.cpp`:

```cpp
#include "domainfilter.h"

#include <collectioninfo.h>

namespace WildPalms::Wizard {

bool collectionMatchesDomain(const Kalburator::Sync::CollectionInfo &c,
                             const QString &pluginId)
{
    if (pluginId == QStringLiteral("calendar"))
        return c.type == QStringLiteral("calendar")
            || c.contentTypes.contains(QStringLiteral("VEVENT"));
    if (pluginId == QStringLiteral("todo"))
        return c.type == QStringLiteral("todos")
            || c.contentTypes.contains(QStringLiteral("VTODO"));
    if (pluginId == QStringLiteral("contacts"))
        return c.type == QStringLiteral("contacts")
            || c.contentTypes.contains(QStringLiteral("VCARD"));
    if (pluginId == QStringLiteral("memo"))
        return c.type == QStringLiteral("memos");
    return false;
}

}  // namespace WildPalms::Wizard
```

Add both files to `src/app/wizard/CMakeLists.txt` in the `add_library(WildPalmsAppWizard STATIC ...)` source list, after `namepage.cpp`:

```cmake
    domainfilter.h
    domainfilter.cpp
```

- [ ] **Step 5: Run the test to verify it passes**

Run:
```bash
cmake --build build -j 8 --target tst_domainfilter && ctest --test-dir build -R tst_domainfilter
```
Expected: PASS (4 test functions).

- [ ] **Step 6: Commit**

```bash
git add src/app/wizard/domainfilter.h src/app/wizard/domainfilter.cpp src/app/wizard/CMakeLists.txt tests/runtime/tst_domainfilter.cpp tests/runtime/CMakeLists.txt
git commit -m "feat(wizard): add collectionMatchesDomain helper"
```

---

### Task 3: AccountFormWidget/AddAccountDialog setConfiguration + Akonadi label (TDD)

The locked-kind ctor stays for now (still used by `AddAccountsPage`, which dies in Task 6); it is removed in Task 7.

**Files:**
- Modify: `src/app/accounts/accountformwidget.h`
- Modify: `src/app/accounts/accountformwidget.cpp`
- Modify: `src/app/accounts/addaccountdialog.h`
- Modify: `src/app/accounts/addaccountdialog.cpp`
- Test: `tests/runtime/tst_accountformwidget.cpp`

- [ ] **Step 1: Write the failing tests**

In `tests/runtime/tst_accountformwidget.cpp`, add three slot declarations to the class:

```cpp
    void setConfigurationOnEmptyRegistryIsSafeNoOp();
    void setConfigurationSelectsKindByType();
    void kindComboListsOnlyRegisteredContributions();
```

and the implementations before `WILDPALMS_QTEST_MAIN`:

```cpp
void TstAccountFormWidget::setConfigurationOnEmptyRegistryIsSafeNoOp()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    Kalburator::Sync::BackendConfiguration cfg;
    cfg.type = QStringLiteral("caldav");
    w.setConfiguration(cfg);          // must not crash
    QCOMPARE(w.selectedKind(), QString());
}

void TstAccountFormWidget::setConfigurationSelectsKindByType()
{
    BackendRegistry reg;
    // Stub contribution registered under type "stub".
    reg.registerContribution(std::make_shared<StubContribution>());
    AccountFormWidget w(&reg);
    Kalburator::Sync::BackendConfiguration cfg;
    cfg.type = QStringLiteral("stub");
    cfg.displayName = QStringLiteral("Edited");
    w.setConfiguration(cfg);
    QCOMPARE(w.selectedKind(), QStringLiteral("stub"));
}

void TstAccountFormWidget::kindComboListsOnlyRegisteredContributions()
{
    // Spec §8(a): offered kinds are exactly the registered contributions —
    // a build without Akonadi never offers Akonadi.
    BackendRegistry reg;
    reg.registerContribution(std::make_shared<StubContribution>());
    AccountFormWidget w(&reg);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->count(), 1);
    QCOMPARE(combo->itemData(0).toString(), QStringLiteral("stub"));
}
```

This file has no stub yet — add one (same pattern as `tests/runtime/tst_discoverypage.cpp:35-86`) in an anonymous namespace after the `using` declarations:

```cpp
#include <backendcontribution.h>
#include <iprovider.h>
#include <collectioninfo.h>
#include <QPromise>

namespace {

class StubProvider : public Kalburator::Sync::IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr)
        : Kalburator::Sync::IProvider(parent) {}
    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    void load(const Kalburator::Sync::BackendConfiguration &) override {}
    Kalburator::Sync::BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; p.start(); p.addResult(true); p.finish();
        return p.future();
    }
    void disconnect() override {}
    bool isConnected() const override { return false; }
    QList<Kalburator::Sync::CollectionInfo> collections() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IBlobBackend> createBackend(const QString &) override {
        return nullptr;
    }
};

class StubContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
    createProvider(QObject *parent = nullptr) const override {
        return std::make_unique<StubProvider>(parent);
    }
};

} // namespace
```

(If `#include "tst_accountformwidget.moc"` is already at the bottom, the new `Q_OBJECT` stub is picked up by the same moc include. `#include <memory>` if not already transitively present.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build -j 8 --target tst_accountformwidget`
Expected: FAIL — `setConfiguration` is not a member of `AccountFormWidget`.

- [ ] **Step 3: Implement**

In `src/app/accounts/accountformwidget.h`, after `bool isValid() const;` add:

```cpp
    /// Populate the form from a persisted account (Edit flows). Selects the
    /// kind matching cfg.type and forwards cfg to that kind's config widget.
    /// No-op when cfg.type isn't among the registered contributions.
    void setConfiguration(const Kalburator::Sync::BackendConfiguration &cfg);
```

In `src/app/accounts/accountformwidget.cpp`:

1. Add the Akonadi label branch in `buildUi()` — the `if/else` chain at lines 51-59 becomes:

```cpp
        if (label == QStringLiteral("caldav"))
            label = tr("CalDAV (calendar)");
        else if (label == QStringLiteral("carddav"))
            label = tr("CardDAV (contacts)");
        else if (label == QStringLiteral("akonadi"))
            label = tr("Akonadi (local)");
        else if (label == QStringLiteral("multiproto-dav"))
            label = tr("Multi-protocol DAV (calendar + contacts)");
        else {
            label[0] = label[0].toUpper();
        }
```

2. Add the method (after `configuration()`):

```cpp
void AccountFormWidget::setConfiguration(const BackendConfiguration &cfg) {
    const int idx = m_kindCombo->findData(cfg.type);
    if (idx < 0) return;
    m_kindCombo->setCurrentIndex(idx);
    if (auto *cw = dynamic_cast<Kalburator::Sync::IProviderConfigWidget *>(
                       m_configStack->widget(idx)))
        cw->setConfiguration(cfg);
}
```

In `src/app/accounts/addaccountdialog.h`, after `configuration()` add:

```cpp
    /// Pre-fill the form from an existing account (Edit flows).
    void setConfiguration(const Kalburator::Sync::BackendConfiguration &cfg);
```

In `src/app/accounts/addaccountdialog.cpp`, after `configuration()` add:

```cpp
void AddAccountDialog::setConfiguration(const BackendConfiguration &cfg) {
    if (m_form) m_form->setConfiguration(cfg);
}
```

- [ ] **Step 4: Run to verify pass**

Run:
```bash
cmake --build build -j 8 --target tst_accountformwidget && ctest --test-dir build -R tst_accountformwidget
```
Expected: PASS (all 6 test functions, including the 4 pre-existing).

- [ ] **Step 5: Commit**

```bash
git add src/app/accounts/ tests/runtime/tst_accountformwidget.cpp
git commit -m "feat(accounts): setConfiguration on form+dialog; Akonadi (local) label"
```

---

### Task 4: WizardState rework + mechanical renames (no behavior change)

`PendingAccount` → `WizardAccount` (gaining discovery-result fields), `pendingAccounts` → `accounts`, `TargetKind::RemoteNew` → `TargetKind::Account`. All current consumers (including the pages deleted in Task 6) are renamed in lockstep so this commit stays green.

**Files:**
- Rewrite: `src/app/wizard/wizardstate.h`
- Modify (renames): `src/app/wizard/targetpickerrow.cpp`, `targetpickerpage.cpp`, `addaccountspage.{h,cpp}`, `discoverypage.{h,cpp}`, `discoveryrow.{h,cpp}`, `reviewpage.cpp`, `src/kf6/kf6mainwindow.cpp`
- Modify (renames): `tests/runtime/tst_targetpickerpage.cpp`, `tst_discoverypage.cpp`, `tst_addaccountspage.cpp`, `tst_kf6mainwindow_newprofile.cpp`, `tst_reviewpage.cpp`, `tst_newprofilewizard.cpp`

- [ ] **Step 1: Rewrite `src/app/wizard/wizardstate.h`** with this exact content:

```cpp
#ifndef WILDPALMS_APP_WIZARD_WIZARDSTATE_H
#define WILDPALMS_APP_WIZARD_WIZARDSTATE_H

#include <backendconfiguration.h>
#include <collectioninfo.h>

#include <QList>
#include <QString>

namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,
    Account,          // bound to a WizardAccount's collection
};

/// An account created on the wizard's Accounts page. The page owns the
/// transient IProvider; discovery results are value-copied here so later
/// pages (Bindings, Review) need no live provider.
struct WizardAccount {
    QString id;       // wizard-local UUID; reused as the on-disk account id
    QString kind;     // contribution backendType(): "caldav" | "carddav" | "akonadi" | ...
    Kalburator::Sync::BackendConfiguration config;

    // Discovery results, filled by AccountsSetupPage when connect() resolves.
    bool    connected = false;
    QString error;
    QList<Kalburator::Sync::CollectionInfo> collections;
};

struct MappingSpec {
    QString    pluginId;     // calendar | contacts | memo | todo (matches plugin->pluginId())
    TargetKind kind = TargetKind::RawFiles;
    QString    accountRef;   // WizardAccount.id for Account; empty for RawFiles
    QString    collectionId; // chosen collection for Account; empty for RawFiles
};

struct WizardState {
    QString              profileName;
    QList<WizardAccount> accounts;
    QList<MappingSpec>   mappings;   // exactly four, keyed by pluginId in insertion order

    const WizardAccount *accountById(const QString &id) const {
        for (const auto &a : accounts)
            if (a.id == id) return &a;
        return nullptr;
    }
};

}  // namespace WildPalms::Wizard

#endif
```

- [ ] **Step 2: Apply the mechanical renames everywhere else**

```bash
cd /home/clinton/dev/WildPalms
sed -i 's/TargetKind::RemoteNew/TargetKind::Account/g; s/RemoteNew/Account/g; s/PendingAccount/WizardAccount/g; s/pendingAccounts/accounts/g' \
  src/app/wizard/targetpickerrow.cpp \
  src/app/wizard/targetpickerpage.cpp \
  src/app/wizard/addaccountspage.h \
  src/app/wizard/addaccountspage.cpp \
  src/app/wizard/discoverypage.h \
  src/app/wizard/discoverypage.cpp \
  src/app/wizard/discoveryrow.h \
  src/app/wizard/discoveryrow.cpp \
  src/app/wizard/reviewpage.cpp \
  src/kf6/kf6mainwindow.cpp \
  tests/runtime/tst_targetpickerpage.cpp \
  tests/runtime/tst_discoverypage.cpp \
  tests/runtime/tst_addaccountspage.cpp \
  tests/runtime/tst_kf6mainwindow_newprofile.cpp \
  tests/runtime/tst_reviewpage.cpp \
  tests/runtime/tst_newprofilewizard.cpp
```

Then verify nothing is left:
```bash
grep -rn "PendingAccount\|RemoteNew\|pendingAccounts" src/ tests/
```
Expected: zero hits.

Note: `kf6mainwindow.cpp`'s `writeWizardResultToProfile` only needs these renames — `r.state.accounts` iteration and `TargetKind::Account` skip-check; the JSON written is identical.

- [ ] **Step 3: ReviewPage collection-name lookup**

In `src/app/wizard/reviewpage.cpp` (post-rename), replace the account-lookup loop inside `initializePage()`'s `else` branch (originally lines 38-53) with:

```cpp
            // Look up the referenced WizardAccount.
            QString accountDisplay = m.accountRef;
            QString accountKind;
            QString collectionLabel = m.collectionId;
            if (const auto *a = m_state->accountById(m.accountRef)) {
                accountDisplay = a->config.displayName.isEmpty()
                    ? a->id : a->config.displayName;
                accountKind = a->kind;
                for (const auto &c : a->collections)
                    if (c.id == m.collectionId) { collectionLabel = c.name; break; }
            }
            line = tr("%1 → %2 / \"%3\" (%4)")
                       .arg(m.pluginId.toHtmlEscaped(),
                            accountDisplay.toHtmlEscaped(),
                            collectionLabel.toHtmlEscaped(),
                            accountKind.toHtmlEscaped());
```

(Falls back to the raw collection id when `collections` is empty, so existing `tst_reviewpage` assertions keep passing.)

Also update the "New accounts" heading loop a few lines below — after the sed it already iterates `m_state->accounts`; just confirm the heading string still reads `"<p><b>New accounts to be created:</b></p><ul>"` (unchanged).

- [ ] **Step 4: Build and run full ctest**

```bash
cmake --build build -j 8 && ctest --test-dir build -j 8
```
Expected: everything passes (pure rename + additive fields + label fallback keeps behavior identical).

- [ ] **Step 5: Commit**

```bash
git add -A src/ tests/
git commit -m "refactor(wizard): WizardAccount with discovery fields; TargetKind::Account"
```

---

### Task 5: AccountsSetupPage (TDD, standalone — not yet wired into the wizard)

**Files:**
- Create: `src/app/wizard/accountssetuppage.h`
- Create: `src/app/wizard/accountssetuppage.cpp`
- Create: `tests/runtime/tst_accountssetuppage.cpp`
- Modify: `src/app/wizard/CMakeLists.txt`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/runtime/tst_accountssetuppage.cpp`:

```cpp
// tests/runtime/tst_accountssetuppage.cpp
#include <QtTest/QtTest>
#include <QLabel>
#include <QPromise>
#include <QPushButton>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/accountssetuppage.h"
#include "app/wizard/wizardstate.h"

#include <backendregistry.h>
#include <backendcontribution.h>
#include <iprovider.h>
#include <collectioninfo.h>

using WildPalms::Wizard::AccountsSetupPage;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::TargetKind;
using Kalburator::Sync::BackendRegistry;
using Kalburator::Sync::BackendContribution;
using Kalburator::Sync::IProvider;
using Kalburator::Sync::BackendConfiguration;
using Kalburator::Sync::CollectionInfo;

namespace {

class StubProvider : public IProvider {
    Q_OBJECT
public:
    explicit StubProvider(QObject *parent = nullptr) : IProvider(parent) {}
    void setConnectResult(bool ok) { m_connectResult = ok; }
    void setCollections(const QList<CollectionInfo> &c) { m_collections = c; }

    QString id() const override { return QStringLiteral("stub-id"); }
    QString kind() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    void load(const BackendConfiguration &) override {}
    BackendConfiguration save() const override { return {}; }
    QWidget *createConfigWidget(QWidget *) override { return nullptr; }
    QFuture<bool> connect() override {
        QPromise<bool> p; p.start();
        p.addResult(m_connectResult); p.finish();
        m_connected = m_connectResult;
        return p.future();
    }
    void disconnect() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }
    QList<CollectionInfo> collections() const override { return m_collections; }
    std::unique_ptr<Kalburator::Sync::IBlobBackend> createBackend(const QString &) override {
        return nullptr;
    }

private:
    bool m_connectResult = true;
    bool m_connected = false;
    QList<CollectionInfo> m_collections;
};

class StubContribution : public BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("stub"); }
    QString displayName() const override { return QStringLiteral("Stub"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<IProvider> createProvider(QObject *parent = nullptr) const override {
        auto p = std::make_unique<StubProvider>(parent);
        p->setConnectResult(s_nextConnectResult);
        p->setCollections(s_nextCollections);
        return p;
    }
    static bool                  s_nextConnectResult;
    static QList<CollectionInfo> s_nextCollections;
};
bool                  StubContribution::s_nextConnectResult = true;
QList<CollectionInfo> StubContribution::s_nextCollections;

WizardState seedState() {
    WizardState s;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m;
        m.pluginId = pid;
        s.mappings.append(m);
    }
    return s;
}

BackendConfiguration stubConfig(const QString &name) {
    BackendConfiguration cfg;
    cfg.type        = QStringLiteral("stub");
    cfg.displayName = name;
    return cfg;
}

} // namespace

class TstAccountsSetupPage : public QObject {
    Q_OBJECT
private slots:
    void init();
    void addConnectsAndStoresCollections();
    void failedConnectKeepsAccountAndDoesNotBlock();
    void removeResetsMappingsReferencingAccount();
    void editClearsChosenCollectionsButKeepsAccountRef();

private:
    BackendRegistry m_registry;
};

void TstAccountsSetupPage::init()
{
    m_registry.unregisterContribution(QStringLiteral("stub"));
    m_registry.registerContribution(std::make_shared<StubContribution>());
    StubContribution::s_nextConnectResult = true;
    StubContribution::s_nextCollections.clear();
}

void TstAccountsSetupPage::addConnectsAndStoresCollections()
{
    CollectionInfo ci;
    ci.id = QStringLiteral("personal"); ci.name = QStringLiteral("Personal");
    ci.type = QStringLiteral("calendar");
    StubContribution::s_nextCollections = { ci };

    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();

    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("My Stub")));
    QVERIFY(!id.isEmpty());
    QTest::qWait(50);   // let the QFutureWatcher resolve

    QCOMPARE(s.accounts.size(), 1);
    QCOMPARE(s.accounts.first().id, id);
    QCOMPARE(s.accounts.first().config.id, id);   // on-disk id == wizard id
    QVERIFY(s.accounts.first().connected);
    QCOMPARE(s.accounts.first().collections.size(), 1);
    QVERIFY(page.isComplete());
}

void TstAccountsSetupPage::failedConnectKeepsAccountAndDoesNotBlock()
{
    StubContribution::s_nextConnectResult = false;

    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    page.addAccountFromConfig(QStringLiteral("stub"),
                              stubConfig(QStringLiteral("Broken")));
    QTest::qWait(50);

    QCOMPARE(s.accounts.size(), 1);
    QVERIFY(!s.accounts.first().connected);
    QVERIFY(page.isComplete());   // failures never block Next (spec §7)
}

void TstAccountsSetupPage::removeResetsMappingsReferencingAccount()
{
    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("Doomed")));
    QTest::qWait(50);

    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = id;
    s.mappings[0].collectionId = QStringLiteral("personal");

    page.removeAccount(id);
    QCOMPARE(s.accounts.size(), 0);
    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    QVERIFY(s.mappings[0].accountRef.isEmpty());
    QVERIFY(s.mappings[0].collectionId.isEmpty());
}

void TstAccountsSetupPage::editClearsChosenCollectionsButKeepsAccountRef()
{
    auto s = seedState();
    AccountsSetupPage page(&m_registry, &s);
    page.initializePage();
    const QString id = page.addAccountFromConfig(
        QStringLiteral("stub"), stubConfig(QStringLiteral("Original")));
    QTest::qWait(50);

    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = id;
    s.mappings[0].collectionId = QStringLiteral("personal");

    page.editAccountConfig(id, QStringLiteral("stub"),
                           stubConfig(QStringLiteral("Renamed")));
    QTest::qWait(50);

    QCOMPARE(s.accounts.size(), 1);
    QCOMPARE(s.accounts.first().id, id);   // id stable across edits
    QCOMPARE(s.accounts.first().config.displayName, QStringLiteral("Renamed"));
    QCOMPARE(s.mappings[0].accountRef, id);          // binding survives...
    QVERIFY(s.mappings[0].collectionId.isEmpty());   // ...but must be re-picked
}

WILDPALMS_QTEST_MAIN(TstAccountsSetupPage)
#include "tst_accountssetuppage.moc"
```

Register in `tests/runtime/CMakeLists.txt` (same block template as Task 2 Step 2, names `tst_accountssetuppage`).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build -j 8 --target tst_accountssetuppage`
Expected: FAIL to compile — `accountssetuppage.h` not found.

- [ ] **Step 3: Implement the page**

Create `src/app/wizard/accountssetuppage.h`:

```cpp
#ifndef WILDPALMS_APP_WIZARD_ACCOUNTSSETUPPAGE_H
#define WILDPALMS_APP_WIZARD_ACCOUNTSSETUPPAGE_H

#include <QHash>
#include <QSet>
#include <QWizardPage>
#include <memory>

class QPushButton;
class QVBoxLayout;

namespace Kalburator::Sync {
    class BackendRegistry;
    class IProvider;
    struct BackendConfiguration;
}

namespace WildPalms::Wizard {

struct WizardState;

/// Page 2 — create the profile's accounts up front. Each account is
/// configured via AddAccountDialog, then its provider connect()s
/// immediately so collections are known before the Bindings page. The
/// page owns the transient providers (wizard lifetime); discovery
/// results are value-copied into WizardState::accounts. Skippable:
/// zero accounts == local-files-only profile.
class AccountsSetupPage : public QWizardPage {
    Q_OBJECT
public:
    AccountsSetupPage(Kalburator::Sync::BackendRegistry *registry,
                      WizardState *state,
                      QWidget *parent = nullptr);
    ~AccountsSetupPage() override;

    void initializePage() override;
    bool isComplete() const override;   // false only while a connect() is in flight

    // Programmatic seams: the Add/Edit/Remove buttons route through these;
    // tests call them directly to bypass the modal dialog.
    QString addAccountFromConfig(
        const QString &kind, const Kalburator::Sync::BackendConfiguration &cfg);
    void editAccountConfig(
        const QString &id, const QString &kind,
        const Kalburator::Sync::BackendConfiguration &cfg);
    void removeAccount(const QString &id);

private slots:
    void onAddClicked();

private:
    void onEditClicked(const QString &id);
    void connectAccount(const QString &id);
    void rebuildList();
    int  accountIndex(const QString &id) const;

    Kalburator::Sync::BackendRegistry *m_registry;
    WizardState *m_state;
    QVBoxLayout *m_listLayout {nullptr};
    QPushButton *m_addButton {nullptr};

    QHash<QString, std::unique_ptr<Kalburator::Sync::IProvider>> m_providers;
    QHash<QString, QString> m_lastError;
    QSet<QString> m_inFlightIds;
};

}  // namespace WildPalms::Wizard

#endif
```

Create `src/app/wizard/accountssetuppage.cpp`:

```cpp
#include "accountssetuppage.h"
#include "wizardstate.h"

#include "addaccountdialog.h"   // WildPalmsAppAccounts (PUBLIC dep of this lib)

#include <backendregistry.h>
#include <backendcontribution.h>
#include <collectioninfo.h>
#include <iprovider.h>

#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

namespace {
QString kindFriendly(const QString &kind) {
    if (kind == QStringLiteral("caldav"))  return QStringLiteral("CalDAV");
    if (kind == QStringLiteral("carddav")) return QStringLiteral("CardDAV");
    if (kind == QStringLiteral("akonadi")) return QObject::tr("Akonadi (local)");
    return kind.toUpper();
}
} // namespace

AccountsSetupPage::AccountsSetupPage(Kalburator::Sync::BackendRegistry *registry,
                                     WizardState *state,
                                     QWidget *parent)
    : QWizardPage(parent)
    , m_registry(registry)
    , m_state(state)
{
    setTitle(tr("Accounts"));
    setSubTitle(tr("Add the accounts this profile syncs with. Skip this "
                   "page to keep everything in local files."));

    auto *outer = new QVBoxLayout(this);
    auto *listHost = new QWidget(this);
    m_listLayout = new QVBoxLayout(listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(listHost);

    m_addButton = new QPushButton(tr("Add Account…"), this);
    m_addButton->setObjectName(QStringLiteral("addAccount"));
    connect(m_addButton, &QPushButton::clicked,
            this, &AccountsSetupPage::onAddClicked);
    outer->addWidget(m_addButton, 0, Qt::AlignLeft);
    outer->addStretch(1);
}

AccountsSetupPage::~AccountsSetupPage() = default;

void AccountsSetupPage::initializePage()
{
    rebuildList();
}

bool AccountsSetupPage::isComplete() const
{
    return m_inFlightIds.isEmpty();
}

int AccountsSetupPage::accountIndex(const QString &id) const
{
    for (int i = 0; i < m_state->accounts.size(); ++i)
        if (m_state->accounts[i].id == id) return i;
    return -1;
}

QString AccountsSetupPage::addAccountFromConfig(
    const QString &kind, const Kalburator::Sync::BackendConfiguration &cfg)
{
    WizardAccount acc;
    acc.id     = QUuid::createUuid().toString(QUuid::WithoutBraces);
    acc.kind   = kind;
    acc.config = cfg;
    acc.config.id = acc.id;   // on-disk account id == wizard id (F.1c §10.2)
    if (acc.config.type.isEmpty()) acc.config.type = kind;
    m_state->accounts.append(acc);
    connectAccount(acc.id);
    return acc.id;
}

void AccountsSetupPage::editAccountConfig(
    const QString &id, const QString &kind,
    const Kalburator::Sync::BackendConfiguration &cfg)
{
    const int i = accountIndex(id);
    if (i < 0 || m_inFlightIds.contains(id)) return;
    auto &acc = m_state->accounts[i];
    acc.kind   = kind;
    acc.config = cfg;
    acc.config.id = id;       // id is stable across edits
    if (acc.config.type.isEmpty()) acc.config.type = kind;
    acc.connected = false;
    acc.error.clear();
    acc.collections.clear();
    // Collections may have changed; bindings on this account must re-pick.
    for (auto &m : m_state->mappings)
        if (m.kind == TargetKind::Account && m.accountRef == id)
            m.collectionId.clear();
    m_providers.remove(id);
    connectAccount(id);
}

void AccountsSetupPage::removeAccount(const QString &id)
{
    if (m_inFlightIds.contains(id)) return;
    const int i = accountIndex(id);
    if (i < 0) return;
    m_providers.remove(id);
    m_state->accounts.removeAt(i);
    for (auto &m : m_state->mappings) {
        if (m.accountRef == id) {
            m.kind = TargetKind::RawFiles;
            m.accountRef.clear();
            m.collectionId.clear();
        }
    }
    rebuildList();
}

void AccountsSetupPage::connectAccount(const QString &id)
{
    const int i = accountIndex(id);
    if (i < 0) return;
    auto &acc = m_state->accounts[i];

    Kalburator::Sync::BackendContribution *contribution =
        m_registry ? m_registry->contributionFor(acc.kind) : nullptr;
    std::unique_ptr<Kalburator::Sync::IProvider> provider =
        contribution ? contribution->createProvider(this) : nullptr;
    if (!provider) {
        acc.connected = false;
        acc.error = tr("No provider for account kind: %1").arg(acc.kind);
        rebuildList();
        return;
    }
    provider->load(acc.config);

    auto *p = provider.get();
    m_providers[id] = std::move(provider);
    m_lastError.remove(id);
    QObject::connect(p, &Kalburator::Sync::IProvider::error, this,
                     [this, id](const QString &msg) { m_lastError[id] = msg; });

    m_inFlightIds.insert(id);
    emit completeChanged();

    auto *w = new QFutureWatcher<bool>(this);
    QObject::connect(w, &QFutureWatcher<bool>::finished, this, [this, w, id]() {
        const bool ok = w->future().resultCount() > 0 && w->result();
        const int i = accountIndex(id);
        if (i >= 0) {
            auto &acc = m_state->accounts[i];
            acc.connected = ok;
            acc.error = ok ? QString()
                           : m_lastError.value(id, tr("Couldn't connect."));
            acc.collections = (ok && m_providers.contains(id))
                ? m_providers[id]->collections()
                : QList<Kalburator::Sync::CollectionInfo>{};
        }
        m_inFlightIds.remove(id);
        rebuildList();
        emit completeChanged();
        w->deleteLater();
    });
    w->setFuture(p->connect());
    rebuildList();
}

void AccountsSetupPage::rebuildList()
{
    while (auto *item = m_listLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto &acc : m_state->accounts) {
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);

        const QString name = acc.config.displayName.isEmpty()
            ? tr("Unnamed account") : acc.config.displayName;
        QString status;
        if (m_inFlightIds.contains(acc.id))
            status = tr("Connecting…");
        else if (acc.connected)
            status = tr("Connected — %n collection(s)", "",
                        int(acc.collections.size()));
        else
            status = acc.error.isEmpty()
                ? tr("Not connected") : tr("Failed: %1").arg(acc.error);

        auto *label = new QLabel(QStringLiteral("%1 (%2) — %3")
                                     .arg(name, kindFriendly(acc.kind), status),
                                 row);
        h->addWidget(label, 1);

        auto *edit = new QPushButton(tr("Edit"), row);
        edit->setObjectName(QStringLiteral("edit:%1").arg(acc.id));
        edit->setEnabled(!m_inFlightIds.contains(acc.id));
        connect(edit, &QPushButton::clicked, this,
                [this, id = acc.id]() { onEditClicked(id); });
        h->addWidget(edit);

        auto *remove = new QPushButton(tr("Remove"), row);
        remove->setObjectName(QStringLiteral("remove:%1").arg(acc.id));
        remove->setEnabled(!m_inFlightIds.contains(acc.id));
        connect(remove, &QPushButton::clicked, this,
                [this, id = acc.id]() { removeAccount(id); });
        h->addWidget(remove);

        m_listLayout->addWidget(row);
    }
}

void AccountsSetupPage::onAddClicked()
{
    WildPalms::App::Accounts::AddAccountDialog dlg(m_registry, this);
    if (dlg.exec() != QDialog::Accepted) return;
    addAccountFromConfig(dlg.selectedKind(), dlg.configuration());
}

void AccountsSetupPage::onEditClicked(const QString &id)
{
    const int i = accountIndex(id);
    if (i < 0) return;
    WildPalms::App::Accounts::AddAccountDialog dlg(m_registry, this);
    dlg.setWindowTitle(tr("Edit Account"));
    dlg.setConfiguration(m_state->accounts[i].config);
    if (dlg.exec() != QDialog::Accepted) return;
    editAccountConfig(id, dlg.selectedKind(), dlg.configuration());
}

}  // namespace WildPalms::Wizard
```

Include-path note: `addaccountdialog.h` resolves because `WildPalmsAppWizard` links `WildPalmsAppAccounts` PUBLIC (see `src/app/wizard/CMakeLists.txt`) and that target exports its source dir. Verify with `grep '#include' src/app/wizard/addaccountspage.cpp` — match whatever include form that file uses for `accountformwidget.h`; use the same form for `addaccountdialog.h`.

Add to `src/app/wizard/CMakeLists.txt` source list:

```cmake
    accountssetuppage.h
    accountssetuppage.cpp
```

- [ ] **Step 4: Run to verify pass**

```bash
cmake --build build -j 8 --target tst_accountssetuppage && ctest --test-dir build -R tst_accountssetuppage
```
Expected: PASS (4 test functions).

- [ ] **Step 5: Run full ctest (page not wired yet — nothing else should move)**

Run: `ctest --test-dir build -j 8`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/app/wizard/accountssetuppage.h src/app/wizard/accountssetuppage.cpp src/app/wizard/CMakeLists.txt tests/runtime/tst_accountssetuppage.cpp tests/runtime/CMakeLists.txt
git commit -m "feat(wizard): AccountsSetupPage — accounts created+connected up front"
```

---

### Task 6: Bindings rework, wizard rewire, delete the old pages

One commit: the dropdown staging dies, the new flow goes live.

**Files:**
- Rewrite: `src/app/wizard/targetpickerrow.h`, `targetpickerrow.cpp`
- Rewrite: `src/app/wizard/targetpickerpage.h`, `targetpickerpage.cpp`
- Modify: `src/app/wizard/newprofilewizard.h`, `newprofilewizard.cpp`
- Delete: `src/app/wizard/addaccountspage.{h,cpp}`, `discoverypage.{h,cpp}`, `discoveryrow.{h,cpp}`
- Delete: `tests/runtime/tst_addaccountspage.cpp`, `tests/runtime/tst_discoverypage.cpp`
- Rewrite: `tests/runtime/tst_targetpickerpage.cpp`
- Modify: `tests/runtime/tst_newprofilewizard.cpp`
- Modify: `src/app/wizard/CMakeLists.txt`, `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Rewrite the failing test for the bindings behavior**

Replace `tests/runtime/tst_targetpickerpage.cpp` entirely with:

```cpp
// tests/runtime/tst_targetpickerpage.cpp
#include <QtTest/QtTest>
#include <QComboBox>
#include <QLabel>
#include <QStandardItemModel>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/targetpickerpage.h"
#include "app/wizard/targetpickerrow.h"
#include "app/wizard/wizardstate.h"

#include <collectioninfo.h>

using WildPalms::Wizard::TargetPickerPage;
using WildPalms::Wizard::TargetPickerRow;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::WizardAccount;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::TargetKind;
using Kalburator::Sync::CollectionInfo;

namespace {

WizardState seedState() {
    WizardState s;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m;
        m.pluginId = pid;
        m.kind     = TargetKind::RawFiles;
        s.mappings.append(m);
    }
    return s;
}

CollectionInfo col(const QString &id, const QString &name,
                   const QString &type, bool readOnly = false) {
    CollectionInfo c;
    c.id = id; c.name = name; c.type = type; c.readOnly = readOnly;
    return c;
}

// One connected account: a writable calendar, a read-only calendar,
// and a todos collection. No contacts, no memos.
WizardState stateWithConnectedAccount() {
    auto s = seedState();
    WizardAccount acc;
    acc.id   = QStringLiteral("acc-1");
    acc.kind = QStringLiteral("caldav");
    acc.config.displayName = QStringLiteral("Fastmail");
    acc.connected = true;
    acc.collections = {
        col(QStringLiteral("cal-1"),  QStringLiteral("Personal"), QStringLiteral("calendar")),
        col(QStringLiteral("cal-ro"), QStringLiteral("Holidays"), QStringLiteral("calendar"), true),
        col(QStringLiteral("todo-1"), QStringLiteral("Tasks"),    QStringLiteral("todos")),
    };
    s.accounts.append(acc);
    return s;
}

QComboBox *comboFor(TargetPickerPage &page, const QString &pluginId) {
    for (auto *r : page.findChildren<TargetPickerRow*>())
        if (r->pluginId() == pluginId)
            return r->findChild<QComboBox*>();
    return nullptr;
}

} // namespace

class TstTargetPickerPage : public QObject {
    Q_OBJECT
private slots:
    void populatesDomainFilteredBindings();
    void readOnlyCollectionsAreNotSelectable();
    void selectingBindingWritesMapping();
    void localFilesResetsMapping();
    void staleBindingResetsToLocalOnRebuild();
    void hintShownWhenAccountsHaveNoMatchingCollections();
};

void TstTargetPickerPage::populatesDomainFilteredBindings()
{
    auto s = stateWithConnectedAccount();
    TargetPickerPage page(&s);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QVERIFY(cal);
    QCOMPARE(cal->count(), 3);   // Local files + Personal + Holidays(ro)
    QCOMPARE(cal->itemText(1), QStringLiteral("Fastmail ▸ Personal"));

    auto *todo = comboFor(page, QStringLiteral("todo"));
    QVERIFY(todo);
    QCOMPARE(todo->count(), 2);  // Local files + Tasks

    auto *contacts = comboFor(page, QStringLiteral("contacts"));
    QVERIFY(contacts);
    QCOMPARE(contacts->count(), 1);  // Local files only

    auto *memo = comboFor(page, QStringLiteral("memo"));
    QVERIFY(memo);
    QCOMPARE(memo->count(), 1);
    QVERIFY(memo->isEnabled());      // no more hardcoded memo disable
}

void TstTargetPickerPage::readOnlyCollectionsAreNotSelectable()
{
    auto s = stateWithConnectedAccount();
    TargetPickerPage page(&s);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QVERIFY(cal);
    auto *model = qobject_cast<QStandardItemModel*>(cal->model());
    QVERIFY(model);
    QVERIFY(cal->itemText(2).contains(QStringLiteral("read-only")));
    QVERIFY(!(model->item(2)->flags() & Qt::ItemIsEnabled));
    QVERIFY(model->item(1)->flags() & Qt::ItemIsEnabled);
}

void TstTargetPickerPage::selectingBindingWritesMapping()
{
    auto s = stateWithConnectedAccount();
    TargetPickerPage page(&s);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    cal->setCurrentIndex(1);   // Fastmail ▸ Personal

    QCOMPARE(s.mappings[0].kind, TargetKind::Account);
    QCOMPARE(s.mappings[0].accountRef, QStringLiteral("acc-1"));
    QCOMPARE(s.mappings[0].collectionId, QStringLiteral("cal-1"));
}

void TstTargetPickerPage::localFilesResetsMapping()
{
    auto s = stateWithConnectedAccount();
    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = QStringLiteral("acc-1");
    s.mappings[0].collectionId = QStringLiteral("cal-1");

    TargetPickerPage page(&s);
    page.initializePage();

    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QCOMPARE(cal->currentIndex(), 1);   // selection restored from state
    cal->setCurrentIndex(0);            // back to Local files

    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    QVERIFY(s.mappings[0].accountRef.isEmpty());
    QVERIFY(s.mappings[0].collectionId.isEmpty());
}

void TstTargetPickerPage::staleBindingResetsToLocalOnRebuild()
{
    auto s = stateWithConnectedAccount();
    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = QStringLiteral("gone-account");
    s.mappings[0].collectionId = QStringLiteral("gone-col");

    TargetPickerPage page(&s);
    page.initializePage();

    QCOMPARE(s.mappings[0].kind, TargetKind::RawFiles);
    auto *cal = comboFor(page, QStringLiteral("calendar"));
    QCOMPARE(cal->currentIndex(), 0);
}

void TstTargetPickerPage::hintShownWhenAccountsHaveNoMatchingCollections()
{
    auto s = stateWithConnectedAccount();
    TargetPickerPage page(&s);
    page.initializePage();

    TargetPickerRow *contactsRow = nullptr;
    for (auto *r : page.findChildren<TargetPickerRow*>())
        if (r->pluginId() == QStringLiteral("contacts")) { contactsRow = r; break; }
    QVERIFY(contactsRow);
    // Account is connected but has no contacts collections -> hint visible flag.
    auto *hint = contactsRow->findChild<QLabel*>(QStringLiteral("hint"));
    QVERIFY(hint);
    QVERIFY(!hint->isHidden());
}

WILDPALMS_QTEST_MAIN(TstTargetPickerPage)
#include "tst_targetpickerpage.moc"
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build -j 8 --target tst_targetpickerpage`
Expected: FAIL to compile (`TargetPickerRow` ctor signature, missing members).

- [ ] **Step 3: Rewrite `src/app/wizard/targetpickerrow.h`**

```cpp
#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERROW_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERROW_H

#include <QWidget>

class QComboBox;
class QLabel;

namespace WildPalms::Wizard {

struct WizardState;

/// One row on the Bindings page. Renders a label + QComboBox whose items
/// are "Local files" plus every domain-matching collection across the
/// connected WizardAccounts. Item data is QStringList{accountId,
/// collectionId} — both empty for Local files. Read-only collections are
/// listed but disabled.
class TargetPickerRow : public QWidget {
    Q_OBJECT
public:
    TargetPickerRow(const QString &pluginId,
                    WizardState *state,
                    QWidget *parent = nullptr);

    QString pluginId() const { return m_pluginId; }

    /// Repopulate from state->accounts, restore the current selection from
    /// the row's MappingSpec, and reset stale bindings to RawFiles.
    void rebuild();

signals:
    /// Empty ids == user picked "Local files".
    void bindingSelected(const QString &accountId, const QString &collectionId);

private:
    void onCurrentIndexChanged(int idx);

    QString      m_pluginId;
    WizardState *m_state;
    QComboBox   *m_combo {nullptr};
    QLabel      *m_hint  {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
```

- [ ] **Step 4: Rewrite `src/app/wizard/targetpickerrow.cpp`**

```cpp
#include "targetpickerrow.h"
#include "domainfilter.h"
#include "wizardstate.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace WildPalms::Wizard {

namespace {
QString domainLabel(const QString &pluginId) {
    if (pluginId == QStringLiteral("calendar")) return QObject::tr("Calendar");
    if (pluginId == QStringLiteral("contacts")) return QObject::tr("Contacts");
    if (pluginId == QStringLiteral("memo"))     return QObject::tr("Memo");
    if (pluginId == QStringLiteral("todo"))     return QObject::tr("To-do");
    return pluginId;
}
} // namespace

TargetPickerRow::TargetPickerRow(const QString &pluginId,
                                 WizardState *state,
                                 QWidget *parent)
    : QWidget(parent)
    , m_pluginId(pluginId)
    , m_state(state)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *top = new QHBoxLayout();
    auto *label = new QLabel(domainLabel(pluginId), this);
    label->setMinimumWidth(120);
    m_combo = new QComboBox(this);
    top->addWidget(label);
    top->addWidget(m_combo, /*stretch=*/1);
    outer->addLayout(top);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("hint"));
    m_hint->setIndent(124);
    m_hint->setVisible(false);
    outer->addWidget(m_hint);

    rebuild();
    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TargetPickerRow::onCurrentIndexChanged);
}

void TargetPickerRow::rebuild()
{
    if (!m_combo || !m_state) return;

    QSignalBlocker block(m_combo);
    m_combo->clear();

    // Index 0, always present: local files.
    m_combo->addItem(tr("Local files (default)"),
                     QVariant::fromValue(QStringList{QString(), QString()}));

    bool anyConnectedAccount = false;
    for (const auto &acc : m_state->accounts) {
        if (!acc.connected) continue;
        anyConnectedAccount = true;
        const QString accName = acc.config.displayName.isEmpty()
            ? acc.id : acc.config.displayName;
        for (const auto &c : acc.collections) {
            if (!collectionMatchesDomain(c, m_pluginId)) continue;
            QString label = QStringLiteral("%1 ▸ %2").arg(accName, c.name);
            if (c.readOnly) label += tr(" (read-only)");
            m_combo->addItem(label,
                             QVariant::fromValue(QStringList{acc.id, c.id}));
            if (c.readOnly) {
                // Palm→remote writes need a writable target; list it so the
                // user sees it exists, but make it unselectable.
                auto *model = qobject_cast<QStandardItemModel*>(m_combo->model());
                if (model)
                    if (auto *it = model->item(m_combo->count() - 1))
                        it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            }
        }
    }

    // Restore the selection from state; reset stale bindings.
    int current = 0;
    int mi = -1;
    for (int i = 0; i < m_state->mappings.size(); ++i)
        if (m_state->mappings[i].pluginId == m_pluginId) { mi = i; break; }
    if (mi >= 0 && m_state->mappings[mi].kind == TargetKind::Account) {
        for (int i = 1; i < m_combo->count(); ++i) {
            const auto data = m_combo->itemData(i).toStringList();
            if (data.value(0) == m_state->mappings[mi].accountRef &&
                data.value(1) == m_state->mappings[mi].collectionId) {
                current = i;
                break;
            }
        }
        if (current == 0) {
            // Bound target no longer exists (account removed or edited).
            m_state->mappings[mi].kind = TargetKind::RawFiles;
            m_state->mappings[mi].accountRef.clear();
            m_state->mappings[mi].collectionId.clear();
        }
    }
    m_combo->setCurrentIndex(current);

    m_hint->setText(tr("No matching collections on your accounts."));
    m_hint->setVisible(anyConnectedAccount && m_combo->count() == 1);
}

void TargetPickerRow::onCurrentIndexChanged(int idx)
{
    if (idx < 0 || !m_combo) return;
    const auto data = m_combo->itemData(idx).toStringList();
    emit bindingSelected(data.value(0), data.value(1));
}

}  // namespace WildPalms::Wizard
```

- [ ] **Step 5: Rewrite `src/app/wizard/targetpickerpage.h`**

```cpp
#ifndef WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H
#define WILDPALMS_APP_WIZARD_TARGETPICKERPAGE_H

#include <QHash>
#include <QWizardPage>

namespace WildPalms::Wizard {

struct WizardState;
class TargetPickerRow;

/// Page 3 — Bindings. Four domain rows; each picks (account, collection)
/// from the accounts created on the AccountsSetupPage, or Local files.
class TargetPickerPage : public QWizardPage {
    Q_OBJECT
public:
    explicit TargetPickerPage(WizardState *state, QWidget *parent = nullptr);

    void initializePage() override;
    bool isComplete() const override { return true; }

    // Called by rows (and exposed for tests). Empty accountId == Local files.
    void selectBinding(const QString &pluginId,
                       const QString &accountId,
                       const QString &collectionId);

private:
    void buildRows();
    int  mappingIndex(const QString &pluginId) const;

    WizardState *m_state;
    QHash<QString, TargetPickerRow*> m_rows;   // pluginId -> row
};

}  // namespace WildPalms::Wizard

#endif
```

- [ ] **Step 6: Rewrite `src/app/wizard/targetpickerpage.cpp`**

```cpp
#include "targetpickerpage.h"
#include "targetpickerrow.h"
#include "wizardstate.h"

#include <QVBoxLayout>

namespace WildPalms::Wizard {

TargetPickerPage::TargetPickerPage(WizardState *state, QWidget *parent)
    : QWizardPage(parent)
    , m_state(state)
{
    setTitle(tr("Sync targets"));
    setSubTitle(tr("Pick where each Palm domain syncs. Go back to the "
                   "Accounts page if a collection you expect is missing."));
    buildRows();
}

void TargetPickerPage::buildRows()
{
    auto *layout = new QVBoxLayout(this);
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        auto *row = new TargetPickerRow(pid, m_state, this);
        layout->addWidget(row);
        m_rows.insert(pid, row);
        connect(row, &TargetPickerRow::bindingSelected, this,
                [this, pid](const QString &accountId, const QString &collectionId) {
                    selectBinding(pid, accountId, collectionId);
                });
    }
}

void TargetPickerPage::initializePage()
{
    for (auto *row : m_rows.values())
        row->rebuild();
}

int TargetPickerPage::mappingIndex(const QString &pluginId) const
{
    if (!m_state) return -1;
    for (int i = 0; i < m_state->mappings.size(); ++i)
        if (m_state->mappings[i].pluginId == pluginId) return i;
    return -1;
}

void TargetPickerPage::selectBinding(const QString &pluginId,
                                     const QString &accountId,
                                     const QString &collectionId)
{
    const int mi = mappingIndex(pluginId);
    if (mi < 0) return;
    if (accountId.isEmpty()) {
        m_state->mappings[mi].kind = TargetKind::RawFiles;
        m_state->mappings[mi].accountRef.clear();
        m_state->mappings[mi].collectionId.clear();
    } else {
        m_state->mappings[mi].kind         = TargetKind::Account;
        m_state->mappings[mi].accountRef   = accountId;
        m_state->mappings[mi].collectionId = collectionId;
    }
}

}  // namespace WildPalms::Wizard
```

- [ ] **Step 7: Rewire `NewProfileWizard`**

`src/app/wizard/newprofilewizard.h` — replace the `PageId` enum:

```cpp
    // Page ids; flow is strictly sequential (QWizard default ordering).
    enum PageId {
        NamePageId = 0,
        AccountsPageId,
        TargetPickerPageId,
        ReviewPageId,
    };
```

`src/app/wizard/newprofilewizard.cpp` — replace the includes and `setPage` block:

```cpp
#include "newprofilewizard.h"
#include "namepage.h"
#include "accountssetuppage.h"
#include "targetpickerpage.h"
#include "reviewpage.h"

#include "runtime/profileregistry.h"
```

and in the constructor:

```cpp
    setPage(NamePageId, new NamePage(m_profileRegistry, &m_state, this));
    setPage(AccountsPageId,
            new AccountsSetupPage(m_backendRegistry, &m_state, this));
    setPage(TargetPickerPageId, new TargetPickerPage(&m_state, this));
    setPage(ReviewPageId, new ReviewPage(&m_state, this));
    setStartId(NamePageId);
```

(The seeding loop for the four `MappingSpec` rows stays; update its stale comment "Pages added in T5–T10..." to "The Accounts and Bindings pages edit these in place.")

- [ ] **Step 8: Delete the dead pages and their tests**

```bash
git rm src/app/wizard/addaccountspage.h src/app/wizard/addaccountspage.cpp \
       src/app/wizard/discoverypage.h src/app/wizard/discoverypage.cpp \
       src/app/wizard/discoveryrow.h src/app/wizard/discoveryrow.cpp \
       tests/runtime/tst_addaccountspage.cpp tests/runtime/tst_discoverypage.cpp
```

In `src/app/wizard/CMakeLists.txt`, remove from the source list:
```
    addaccountspage.h
    addaccountspage.cpp
    discoveryrow.h
    discoveryrow.cpp
    discoverypage.h
    discoverypage.cpp
```

In `tests/runtime/CMakeLists.txt`, delete the complete `add_executable`/`target_link_libraries`/`add_test`/`set_tests_properties` blocks for `tst_discoverypage` (lines ~409-430) and `tst_addaccountspage` (lines ~432-453).

- [ ] **Step 9: Update `tests/runtime/tst_newprofilewizard.cpp`**

Replace the `skipsAddAccountsAndDiscoveryWhenAllLocal` slot (declaration and body) with:

```cpp
    void pageOrderIsNameAccountsBindingsReview();
```

```cpp
void TstNewProfileWizard::pageOrderIsNameAccountsBindingsReview()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    BackendRegistry backendReg;

    NewProfileWizard w(reg.get(), &backendReg);
    QVERIFY(w.page(NewProfileWizard::AccountsPageId));
    QVERIFY(w.page(NewProfileWizard::TargetPickerPageId));
    // Strictly sequential: QWizard default ordering, no nextId() overrides.
    QCOMPARE(w.page(NewProfileWizard::NamePageId)->nextId(),
             int(NewProfileWizard::AccountsPageId));
    QCOMPARE(w.page(NewProfileWizard::AccountsPageId)->nextId(),
             int(NewProfileWizard::TargetPickerPageId));
    QCOMPARE(w.page(NewProfileWizard::TargetPickerPageId)->nextId(),
             int(NewProfileWizard::ReviewPageId));
    QCOMPARE(w.page(NewProfileWizard::ReviewPageId)->nextId(), -1);
}
```

- [ ] **Step 10: Build and run the full suite**

```bash
cmake --build build -j 8 && ctest --test-dir build -j 8
```
Expected: all pass. The deleted tests are gone from the suite; `tst_targetpickerpage` runs its 6 new functions.

- [ ] **Step 11: Commit**

```bash
git add -A src/ tests/
git commit -m "feat(wizard): accounts-first flow — Bindings page, old staging pages deleted"
```

---

### Task 7: Remove the locked-kind ctor (last user died in Task 6)

**Files:**
- Modify: `src/app/accounts/accountformwidget.h`
- Modify: `src/app/accounts/accountformwidget.cpp`
- Modify: `tests/runtime/tst_accountformwidget.cpp`

- [ ] **Step 1: Verify no remaining users**

```bash
grep -rn "AccountFormWidget(" src/ tests/ | grep -v "accountformwidget"
```
Expected: only `AddAccountDialog` (2-arg form) and test constructions with `(&reg)`.

- [ ] **Step 2: Remove the ctor**

In `accountformwidget.h`: delete the `lockedKind` ctor declaration (lines 34-36) and rewrite the class comment block (lines 20-28) to:

```cpp
/// Reusable account form. Populates a kind combo from
/// BackendRegistry::contributions() and stacks each provider's
/// createConfigWidget(). Used by AddAccountDialog (Settings → Accounts
/// and the NewProfileWizard's AccountsSetupPage).
```

Change `void buildUi(const QString &lockedKind);` to `void buildUi();`.

In `accountformwidget.cpp`: delete the second ctor (lines 30-37); change the first ctor's `buildUi(QString())` to `buildUi()`; change `buildUi(const QString &lockedKind)` to `buildUi()`; delete the `lockedIndex`/`i` tracking (lines 47-48, 68-70) and replace the tail selection block (lines 91-97) with:

```cpp
    if (hasItems)
        onKindChanged(0);
```

- [ ] **Step 3: Update the test**

In `tests/runtime/tst_accountformwidget.cpp`: delete the `lockedKindOnEmptyRegistryDoesNotHideCombo` slot declaration and body (the contract it documented — silent fallback — is exactly the bug this work removes).

- [ ] **Step 4: Build + full ctest**

```bash
cmake --build build -j 8 && ctest --test-dir build -j 8
```
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/accounts/ tests/runtime/tst_accountformwidget.cpp
git commit -m "refactor(accounts): drop locked-kind ctor and its silent CalDAV fallback"
```

---

### Task 8: Final verification + handoff doc update

- [ ] **Step 1: Full clean-ish verification**

```bash
cmake --build build -j 8 && ctest --test-dir build -j 8
```
Expected: all tests pass. Record the count.

- [ ] **Step 2: Residue scan**

```bash
grep -rn "PendingAccount\|RemoteNew\|__add_new__\|AddAccountsPage\|DiscoveryPage\|DiscoveryRow" src/ tests/ docs/superpowers/plans/2026-06-09-accounts-first-wizard.md --include="*.cpp" --include="*.h"
```
Expected: zero hits in `src/` and `tests/`.

- [ ] **Step 3: Manual smoke (offscreen-incapable parts)**

Launch the app (`./build/bin/wildpalms` or the project's run target), open File → New Profile, and verify:
1. Page 2 shows "Add Account…"; the dialog's kind combo lists CalDAV (calendar), CardDAV (contacts), Akonadi (local) — Akonadi present because the build now compiles it in.
2. Adding an Akonadi account shows the Akonadi config widget — **not** a CalDAV form.
3. With no accounts, Next → Bindings shows four rows each with only "Local files (default)".
4. Finish writes a loadable profile.

(If no Akonadi daemon is running, the account row shows a connect failure — acceptable, spec §7.)

- [ ] **Step 4: Update `CLAUDE.md`**

In the project `CLAUDE.md`: update the "Current branch and state" line for the new ctest count, add a "What just landed" note for the accounts-first wizard (spec + plan paths), and remove the now-stale description of the wizard flow if any.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(claude.md): accounts-first wizard landed"
```
