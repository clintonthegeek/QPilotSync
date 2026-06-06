# Clobber-Sync Implementation Plan

> **Status (2026-06-05):** Tasks 1–11 LANDED on `feature/three-tier-sync` (tip `270bfa8`). Hardware verification (Task 12) PENDING. Consistency follow-ups noted in `CLAUDE.md`. See `CLAUDE.md` "What just landed: clobber-sync feature" for the current state; this plan stays as authored for historical reference.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Tools-menu "Clobber Palm from PC" mode that wipes selected Palm-side PIM databases and re-pushes from the hub in one operation, enabling repeatable freshen-Palm test loops and exposing a general "freshen Palm from desktop" capability to end users.

**Architecture:** WildPalms-side `PalmRuntime::clobberSync(mappingIds)` builds a `SyncRequest` with a new `ExecutionOverride::clobber=true` flag, dispatches through `SyncEngine::runSync`, and the engine — for each mapping — skips baseline+mass-delete-guard, calls `targetBackend->wipeCollection(targetCollectionId)`, then pushes source records. The four Palm-side blob backends (`PalmCalendarBackend`, `PalmContactsBackend`, `MemoBlobBackend`, `TodoBlobBackend`) override `wipeCollection` to issue `dlp_DeleteDB`+`dlp_CreateDB` against the Palm.

**Tech Stack:** Qt 6, KF6 (Calendar/Contacts), libkalburator (sync engine + IBlobBackend), pilot-link (libpisock DLP calls), QtTest for tests.

**Source design spec:** `docs/superpowers/specs/2026-06-05-clobber-sync-design.md`

---

## Cross-repo dependency

This plan has **two phases separated by a BLOCKER**:

- **Phase 1** (executable now): write the libkalburator handoff doc.
- **BLOCKER:** wait for libkalburator to land `ExecutionOverride::clobber` + `IBlobBackend::wipeCollection` + relaxed-subset-dispatch semantics, pass PlanStan-green gate, and cut a tag (likely `v0.65`).
- **Phase 2** (after blocker): bump WP pin, implement WP-side surface, test, verify on hardware.

Do NOT begin Phase 2 tasks until the BLOCKER clears.

---

## Phase 1: libkalburator handoff

### Task 1: Write the libkalburator clobber-sync handoff

**Files:**
- Create: `docs/2026-06-05-libkalburator-clobber-sync-handoff.md`

- [ ] **Step 1: Author the handoff**

The handoff must specify the three libkalburator changes precisely enough that the libkalburator agent can implement and ship without follow-up questions. Use the section structure of the previous handoffs (`docs/2026-05-28-libkalburator-sqlite-thread-safety-handoff.md` is the template).

Required sections:
- **TL;DR** — one paragraph naming the three changes and why.
- **Motivation** — point at the WildPalms spec at `docs/superpowers/specs/2026-06-05-clobber-sync-design.md` and summarize the freshen-Palm use case in 2-3 sentences.
- **Change 1: `ExecutionOverride::clobber` flag** — header diff, semantics list (skip baseline, skip mass-delete guard, call `wipeCollection`, push, write fresh baseline; `direction` ignored under clobber), test names (`tst_syncengine_clobber_single_mapping`, `tst_syncengine_clobber_multi_mapping`, `tst_syncengine_clobber_mass_delete_guard_silenced`).
- **Change 2: `SyncRequest` subset-dispatch with override** — relax rule that `executionOverride` only applies on `isSingleMapping()`: the `clobber` flag also applies on subset dispatch (multiple mappingIds). `direction` stays single-mapping-only.
- **Change 3: `IBlobBackend::wipeCollection`** — header diff with default implementation (iterate `loadRecords` + `deleteRecord`), one test `tst_iblobbackend_default_wipeCollection`. Note default impl makes this a non-breaking addition.
- **PlanStan-green gate** — explicit ask that PlanStan's full ctest passes before tag.
- **Tag** — request a tag (suggested `v0.65`); WP will bump pin to it.

Write the file with literal code blocks for the header diffs so the libkalburator implementer doesn't have to guess struct field placement.

- [ ] **Step 2: Commit**

```bash
git add docs/2026-06-05-libkalburator-clobber-sync-handoff.md
git commit -m "docs(handoff): libkalburator clobber-sync surface (RFC)

Three additions needed for the clobber-sync WP feature: an
ExecutionOverride::clobber flag, relaxed SyncRequest semantics for that
flag on subset dispatch, and IBlobBackend::wipeCollection (default impl
provided). See docs/superpowers/specs/2026-06-05-clobber-sync-design.md
for the full design context.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 3: Push**

```bash
git push origin feature/three-tier-sync
```

---

## BLOCKER: libkalburator ships v0.65

Before Phase 2 starts, confirm in `~/dev/libkalburator/`:

```bash
cd ~/dev/libkalburator
git fetch --tags
git tag --list 'v0.65*'   # expect: v0.65
git show v0.65 -- src/types/synctypes.h src/engine/syncrequest.h src/blob/iblobbackend.h | head -50
```

If `v0.65` exists and contains the three additions, the blocker is clear.

---

## Phase 2: WildPalms-side implementation

### Task 2: Bump libkalburator pin v0.64 → v0.65

**Files:**
- Modify: `CMakeLists.txt:63`

- [ ] **Step 1: Edit the pin**

Change line 63 of `CMakeLists.txt` from:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.64" CACHE STRING
```
to:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.65" CACHE STRING
```

- [ ] **Step 2: Clean reconfigure**

```bash
cmake -S /home/clinton/dev/WildPalms -B /home/clinton/dev/WildPalms/build \
      -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR= \
      -DWILDPALMS_LIBKALBURATOR_GIT_TAG=v0.65
```
Expected: ends with `Generating done`, no errors.

- [ ] **Step 3: Build to 100%**

```bash
cmake --build /home/clinton/dev/WildPalms/build -j 8
```
Expected: 100% built, no link errors. Pre-existing 3-failing test cluster (`tst_palm_runtime_route_first_sync` etc.) is unchanged; ignore it for this task.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(kalburator): bump pin v0.64 -> v0.65

Picks up ExecutionOverride::clobber, IBlobBackend::wipeCollection
(default impl), and relaxed SyncRequest subset-dispatch semantics
for the clobber flag — prerequisites for the WP clobber-sync feature.
See docs/2026-06-05-libkalburator-clobber-sync-handoff.md.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 3: Mapping classification helpers on PalmRuntime

**Files:**
- Modify: `src/runtime/palmruntime.h` (add two methods + a domain-list constant)
- Modify: `src/runtime/palmruntime.cpp` (implement)
- Create: `tests/runtime/tst_palm_mapping_classification.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (register the new test)

- [ ] **Step 1: Write the failing test**

`tests/runtime/tst_palm_mapping_classification.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include "runtime/palmruntime.h"

using namespace WildPalms::Runtime;

class TstPalmMappingClassification : public QObject { Q_OBJECT
private slots:
    void palm_direct_mapping_recognized()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m["id"]              = "test-palm-cal";
        m["sourceBackend"]   = "wp-hub";
        m["sourceCalendar"]  = "palm:calendar";
        m["targetBackend"]   = "calendar";          // a Palm-side plugin id
        m["targetCalendar"]  = "palm:calendar/0";
        m["mode"]            = "TwoWay";
        m["enabled"]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        const auto ids = runtime.palmDirectMappingsForDomain(QStringLiteral("calendar"));
        QCOMPARE(ids.size(), 1);
        QCOMPARE(ids.first(), QStringLiteral("test-palm-cal"));
    }

    void route_mapping_excluded()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m["id"]              = "test-route";
        m["sourceBackend"]   = "wp-hub";
        m["sourceCalendar"]  = "palm:calendar";
        m["targetBackend"]   = "caldav-personal";   // a remote backend, not Palm
        m["targetCalendar"]  = "personal";
        m["mode"]            = "TwoWay";
        m["enabled"]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        QCOMPARE(runtime.palmDirectMappingsForDomain(QStringLiteral("calendar")).size(), 0);
    }
};
QTEST_GUILESS_MAIN(TstPalmMappingClassification)
#include "tst_palm_mapping_classification.moc"
```

Register it in `tests/runtime/CMakeLists.txt` following the same pattern as the existing `tst_palm_runtime_modes` registration (look for `qt6_add_executable(tst_palm_runtime_modes ...)` and add an analogous block).

- [ ] **Step 2: Run the test to verify it fails to compile**

```bash
cmake --build /home/clinton/dev/WildPalms/build -j 8 -t tst_palm_mapping_classification
```
Expected: error: `palmDirectMappingsForDomain` is not a member of `WildPalms::Runtime::PalmRuntime`.

- [ ] **Step 3: Add the declarations**

In `src/runtime/palmruntime.h`, in the public section near `runAllMappings()`:

```cpp
/// Returns the IDs of all enabled mappings whose target backend is the
/// Palm-side blob backend for the given domain (e.g. "calendar",
/// "contacts", "memo", "todo"). Used by ClobberDialog to populate
/// per-conduit checkboxes; the engine never consumes this.
QList<QString> palmDirectMappingsForDomain(const QString &domain) const;

/// Returns true iff the given mapping is Palm-direct (targets one of
/// the Palm-side blob backends). Exposed mostly for testing.
bool isPalmDirectMapping(const Kalburator::Sync::SyncMapping &m) const;
```

- [ ] **Step 4: Implement**

In `src/runtime/palmruntime.cpp` near the existing mapping-iteration methods:

```cpp
namespace {
constexpr std::array<const char*, 4> kPalmBackendIds = {
    "calendar", "contacts", "memo", "todo"
};
}

bool PalmRuntime::isPalmDirectMapping(
    const Kalburator::Sync::SyncMapping &m) const
{
    for (const char *id : kPalmBackendIds) {
        if (m.targetBackend == QLatin1String(id))
            return true;
    }
    return false;
}

QList<QString> PalmRuntime::palmDirectMappingsForDomain(
    const QString &domain) const
{
    QList<QString> ids;
    for (const auto &m : m_mappings) {
        if (!m.enabled) continue;
        if (m.targetBackend != domain) continue;
        if (!isPalmDirectMapping(m)) continue;
        ids.append(m.id);
    }
    return ids;
}
```

- [ ] **Step 5: Build + run the test**

```bash
cmake --build /home/clinton/dev/WildPalms/build -j 8 -t tst_palm_mapping_classification
ctest --test-dir /home/clinton/dev/WildPalms/build -R tst_palm_mapping_classification --output-on-failure
```
Expected: 2/2 passed.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_mapping_classification.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "feat(runtime): Palm-direct mapping classification helpers

palmDirectMappingsForDomain(domain) and isPalmDirectMapping(SyncMapping)
on PalmRuntime, plus a unit test. Used by ClobberDialog (next task) to
populate per-conduit checkboxes. The engine never consumes the
classification.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: ClobberDialog

**Files:**
- Create: `src/runtime/clobberdialog.h`
- Create: `src/runtime/clobberdialog.cpp`
- Modify: `src/runtime/CMakeLists.txt` (add the new sources)
- Create: `tests/runtime/tst_clobber_dialog.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/runtime/tst_clobber_dialog.cpp`:

```cpp
#include <QtTest/QtTest>
#include "runtime/clobberdialog.h"

using namespace WildPalms::Runtime;

class TstClobberDialog : public QObject { Q_OBJECT
private slots:
    void initial_selection_empty()
    {
        ClobberDialog::DomainMappings input = {
            { "calendar", { "m-cal-1" } },
            { "contacts", { "m-con-1" } },
        };
        ClobberDialog dlg(input);
        QVERIFY(dlg.selectedMappingIds().isEmpty());
    }

    void select_one_domain_returns_its_mapping_ids()
    {
        ClobberDialog::DomainMappings input = {
            { "calendar", { "m-cal-1", "m-cal-2" } },
            { "contacts", { "m-con-1" } },
        };
        ClobberDialog dlg(input);
        dlg.setDomainChecked("calendar", true);
        const auto ids = dlg.selectedMappingIds();
        QCOMPARE(ids.size(), 2);
        QVERIFY(ids.contains("m-cal-1"));
        QVERIFY(ids.contains("m-cal-2"));
    }
};
QTEST_MAIN(TstClobberDialog)
#include "tst_clobber_dialog.moc"
```

Register in `tests/runtime/CMakeLists.txt` following the existing pattern, **without** `QTEST_GUILESS_MAIN` — the dialog needs `QApplication`.

- [ ] **Step 2: Verify it fails to compile**

```bash
cmake --build /home/clinton/dev/WildPalms/build -j 8 -t tst_clobber_dialog
```
Expected: `clobberdialog.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`src/runtime/clobberdialog.h`:

```cpp
#pragma once

#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

namespace WildPalms::Runtime {

/// Modal dialog presenting per-conduit checkboxes for selecting which
/// Palm-direct mappings to clobber. Has zero engine knowledge: receives
/// a domain→mapping-IDs map at construction, returns the selected
/// mapping IDs on accept.
class ClobberDialog : public QDialog {
    Q_OBJECT
public:
    /// Key: domain name (e.g. "calendar"). Value: enabled Palm-direct
    /// mapping IDs for that domain.
    using DomainMappings = QMap<QString, QList<QString>>;

    explicit ClobberDialog(const DomainMappings &mappings,
                           QWidget *parent = nullptr);
    ~ClobberDialog() override;

    /// Programmatic setter (for tests). Same as ticking the box.
    void setDomainChecked(const QString &domain, bool checked);

    /// Mapping IDs corresponding to currently-checked domains.
    /// Order matches insertion order of DomainMappings.
    QList<QString> selectedMappingIds() const;

public Q_SLOTS:
    void accept() override;  // shows the final warning prompt

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace WildPalms::Runtime
```

- [ ] **Step 4: Write the implementation**

`src/runtime/clobberdialog.cpp`:

```cpp
#include "clobberdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

namespace WildPalms::Runtime {

struct ClobberDialog::Impl {
    DomainMappings input;
    QMap<QString, QCheckBox*> checkboxes;
};

ClobberDialog::ClobberDialog(const DomainMappings &mappings, QWidget *parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->input = mappings;
    setWindowTitle(tr("Clobber Palm from PC"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Select which Palm conduits to wipe and re-push from desktop:"),
        this));

    for (auto it = d->input.constBegin(); it != d->input.constEnd(); ++it) {
        auto *cb = new QCheckBox(
            tr("%1 — %2 mapping(s)")
                .arg(it.key())
                .arg(it.value().size()),
            this);
        layout->addWidget(cb);
        d->checkboxes.insert(it.key(), cb);
    }

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Clobber"));
    layout->addWidget(bb);

    connect(bb, &QDialogButtonBox::accepted, this, &ClobberDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &ClobberDialog::reject);
}

ClobberDialog::~ClobberDialog() = default;

void ClobberDialog::setDomainChecked(const QString &domain, bool checked)
{
    if (auto *cb = d->checkboxes.value(domain)) cb->setChecked(checked);
}

QList<QString> ClobberDialog::selectedMappingIds() const
{
    QList<QString> result;
    for (auto it = d->input.constBegin(); it != d->input.constEnd(); ++it) {
        if (d->checkboxes.value(it.key())->isChecked())
            result.append(it.value());
    }
    return result;
}

void ClobberDialog::accept()
{
    const int n = selectedMappingIds().size();
    if (n == 0) {
        QDialog::reject();
        return;
    }
    const auto button = QMessageBox::warning(
        this, tr("Clobber Palm from PC"),
        tr("This will delete %n Palm database(s) and replace them with "
           "desktop data. The Palm-side data being deleted is NOT backed "
           "up. Continue?", "", n),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (button == QMessageBox::Yes) QDialog::accept();
}

} // namespace WildPalms::Runtime
```

Add the sources to `src/runtime/CMakeLists.txt` — find the existing PalmDeviceAccessLib (or PalmRuntimeLib) target and append `clobberdialog.h clobberdialog.cpp` to its source list. Link `Qt6::Widgets` if not already linked at that level.

- [ ] **Step 5: Build + run the test**

```bash
cmake --build /home/clinton/dev/WildPalms/build -j 8 -t tst_clobber_dialog
ctest --test-dir /home/clinton/dev/WildPalms/build -R tst_clobber_dialog --output-on-failure
```
Expected: 2/2 passed.

- [ ] **Step 6: Commit**

```bash
git add src/runtime/clobberdialog.h src/runtime/clobberdialog.cpp \
        src/runtime/CMakeLists.txt \
        tests/runtime/tst_clobber_dialog.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "feat(runtime): ClobberDialog modal with per-conduit checkboxes

Construct with a domain->mapping-IDs map; user ticks domains; selected
mapping IDs returned on accept. Final warning prompt gates accept.
Zero engine knowledge.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: PalmCalendarBackend::wipeCollection override (calendar submodule)

**Files (in `src/plugins/calendar/` submodule):**
- Modify: `palmcalendarbackend.h` (add override declaration)
- Modify: `palmcalendarbackend.cpp` (implement)
- Modify: `tests/tst_palmcalendarbackend.cpp` (or analogous existing test) — add a wipeCollection round-trip test

> **Submodule discipline:** all edits in this task happen inside `src/plugins/calendar/` (a git submodule with its own remote). Commit there first, then bump the gitlink in the WP superproject in Task 9.

- [ ] **Step 1: Enter the submodule**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git status -sb
```
Expected: shows the submodule's current branch.

- [ ] **Step 2: Write the failing test**

In the submodule's existing test for `PalmCalendarBackend` (find via `grep -rn "PalmCalendarBackend" tests/`), add:

```cpp
void wipeCollection_clears_palm_calendar_database()
{
    // Set up a mock pilot-link link providing dlp_DeleteDB + dlp_CreateDB stubs.
    // (Use the same MockPalmDatabaseAccess pattern existing tests use.)
    MockPalmDatabaseAccess mock;
    mock.createDatabase("DatebookDB");
    // pre-seed one record
    PalmRecord pr; pr.recordId = 1; pr.data = QByteArray("dummy");
    mock.createRecord("DatebookDB", pr);

    PalmCalendarBackend backend(&mock);
    QVERIFY(backend.wipeCollection("palm:calendar/0"));
    QCOMPARE(mock.recordCount("DatebookDB"), 0);
    QVERIFY(mock.databaseExists("DatebookDB"));   // recreated empty
}
```

If `MockPalmDatabaseAccess` doesn't currently expose `recordCount`/`databaseExists`, add the minimum needed. Run:
```bash
cd /home/clinton/dev/WildPalms
cmake --build build -j 8 -t tst_calendarbackendplugin
ctest --test-dir build -R wipeCollection_clears_palm --output-on-failure
```
Expected: FAIL (method not implemented).

- [ ] **Step 3: Declare the override**

In `palmcalendarbackend.h`, add to the public section:

```cpp
bool wipeCollection(const QString &collectionId) override;
```

- [ ] **Step 4: Implement**

In `palmcalendarbackend.cpp`:

```cpp
bool PalmCalendarBackend::wipeCollection(const QString &collectionId)
{
    // collectionId is "palm:calendar/<slot>"; we wipe the underlying
    // database regardless of slot (DatebookDB is shared across slots
    // via category routing).
    Q_UNUSED(collectionId);

    // Use the device-access API to issue dlp_DeleteDB then dlp_CreateDB.
    // The classic DB name is the one WP syncs against.
    constexpr const char *kClassicDb = "DatebookDB";
    constexpr const char *kEnhancedDb = "CalendarDB-PDat";

    bool ok = m_db->deleteDatabase(kClassicDb);
    // Ignore failure on the enhanced DB — most Palm OS 4 devices don't
    // have it; "not found" is expected.
    (void)m_db->deleteDatabase(kEnhancedDb);

    // Recreate the classic DB so the next push has a target. The PIM apps
    // would also do this on first open, but we can't rely on the user
    // opening the Datebook app.
    ok = ok && m_db->createDatabase(kClassicDb);

    return ok;
}
```

If `m_db->deleteDatabase` doesn't exist on `IPalmDatabaseAccess`, add it. It should wrap `dlp_DeleteDB(handle, cardno, dbname)`. Same for `createDatabase` (already exists per the test in Step 2's snippet).

**Creator IDs for `dlp_CreateDB`:** the recreated empty DB must carry the same creator/type IDs the built-in PIM apps expect, or the apps will not recognize it. The classic four-letter IDs are:

| Conduit | DB name | Creator | Type |
|---|---|---|---|
| Calendar | DatebookDB | `date` (0x64617465) | `DATA` |
| Contacts | AddressDB | `addr` (0x61646472) | `DATA` |
| Memo | MemoDB | `memo` (0x6D656D6F) | `DATA` |
| ToDo | ToDoDB | `todo` (0x746F646F) | `DATA` |

Before implementing, check whether `IPalmDatabaseAccess::createDatabase(name)` already passes these creator IDs internally (it must — existing first-sync flows depend on it). If it does, the override above is sufficient. If `createDatabase` is a stub or only works for synthetic test DB names, extend its signature to accept creator/type IDs and pass the right ones from each backend.

- [ ] **Step 5: Run the test**

```bash
cd /home/clinton/dev/WildPalms
cmake --build build -j 8 -t tst_calendarbackendplugin
ctest --test-dir build -R wipeCollection_clears_palm --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit in the submodule**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/calendar
git add -A
git commit -m "feat(calendar): PalmCalendarBackend::wipeCollection override

Implements IBlobBackend::wipeCollection by calling
IPalmDatabaseAccess::deleteDatabase('DatebookDB') (also the enhanced
'CalendarDB-PDat' best-effort) then createDatabase to leave an empty
target for the subsequent push. Round-trip test included.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
git push origin HEAD
```

The superproject gitlink bump happens in Task 9 once all four submodules are done.

---

### Task 6: PalmContactsBackend::wipeCollection override (contacts submodule)

**Files (in `src/plugins/contacts/` submodule):**
- Modify: `palmcontactsbackend.h`
- Modify: `palmcontactsbackend.cpp`
- Modify: tests file analogous to Task 5

Same shape as Task 5, with:
- Classic DB name: `AddressDB`
- Enhanced DB name: `ContactsDB-PAdd`
- Test: `wipeCollection_clears_palm_contacts_database`
- Backend class: `PalmContactsBackend`
- Test target: `tst_contactsbackendplugin`

- [ ] **Step 1: Enter the submodule**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/contacts
```

- [ ] **Step 2: Repeat steps 2–6 of Task 5** with the names above substituted. Commit and push in the submodule.

---

### Task 7: MemoBlobBackend::wipeCollection override (memo submodule)

**Files (in `src/plugins/memo/` submodule):**
- Modify: `memoblobbackend.h`
- Modify: `memoblobbackend.cpp`
- Modify: tests file analogous to Task 5

Names:
- Classic DB name: `MemoDB`
- Enhanced DB name: `MemosDB-PMem`
- Test: `wipeCollection_clears_palm_memo_database`
- Backend class: `MemoBlobBackend`
- Test target: `tst_memobackendplugin`

- [ ] **Step 1: Enter the submodule**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/memo
```

- [ ] **Step 2: Repeat steps 2–6 of Task 5** with the names above substituted. Commit and push in the submodule.

---

### Task 8: TodoBlobBackend::wipeCollection override (todos submodule)

**Files (in `src/plugins/todos/` submodule):**
- Modify: `todoblobbackend.h`
- Modify: `todoblobbackend.cpp`
- Modify: tests file analogous to Task 5

Names:
- Classic DB name: `ToDoDB`
- Enhanced DB name: `TasksDB-PTod`
- Test: `wipeCollection_clears_palm_todo_database`
- Backend class: `TodoBlobBackend`
- Test target: `tst_todobackendplugin`

- [ ] **Step 1: Enter the submodule**

```bash
cd /home/clinton/dev/WildPalms/src/plugins/todos
```

- [ ] **Step 2: Repeat steps 2–6 of Task 5** with the names above substituted. Commit and push in the submodule.

---

### Task 9: Superproject gitlink bumps (4 conduit submodules)

**Files:**
- Modify: superproject gitlinks for `src/plugins/calendar`, `src/plugins/contacts`, `src/plugins/memo`, `src/plugins/todos`

- [ ] **Step 1: Confirm the four submodules pushed new heads**

```bash
cd /home/clinton/dev/WildPalms
git submodule status | grep "calendar\|contacts\|memo\|todos"
```
Expected: each shows a new SHA prefixed with `+` (indicating the submodule HEAD is ahead of the superproject's recorded gitlink).

- [ ] **Step 2: Bump each gitlink**

```bash
git add src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
```

- [ ] **Step 3: Build the full tree from superproject root**

```bash
cmake --build build -j 8
```
Expected: 100% built; all four `tst_<conduit>backendplugin` targets build.

- [ ] **Step 4: Run the four backend test executables**

```bash
ctest --test-dir build -R "tst_calendarbackendplugin|tst_contactsbackendplugin|tst_memobackendplugin|tst_todobackendplugin" --output-on-failure
```
Expected: all four pass, including the new `wipeCollection_*` cases.

- [ ] **Step 5: Commit the gitlink bumps**

```bash
git commit -m "build(submodule): bump four conduits to wipeCollection override

calendar/contacts/memo/todos each gain an IBlobBackend::wipeCollection
override that drops + recreates the Palm-side classic database (and
best-effort drops the OS5-enhanced equivalent). Wires the Palm side of
clobber-sync. Per-backend round-trip tests landed in each submodule.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 10: PalmRuntime::clobberSync entry point

**Files:**
- Modify: `src/runtime/palmruntime.h` (declare; remove `copyPCToPalm`)
- Modify: `src/runtime/palmruntime.cpp` (implement; delete `copyPCToPalm`)
- Create: `tests/runtime/tst_palm_runtime_clobber_sync.cpp`
- Modify: `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/runtime/tst_palm_runtime_clobber_sync.cpp`:

```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"
#include "palm/calendar/datebookcodec.h"

using namespace WildPalms::Runtime;

class TstPalmRuntimeClobberSync : public QObject { Q_OBJECT
private slots:
    void clobber_wipes_palm_then_pushes_hub_data()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m["id"]              = "test-clobber-cal";
        m["sourceBackend"]   = "wp-hub";
        m["sourceCalendar"]  = "palm:calendar";
        m["targetBackend"]   = "calendar";
        m["targetCalendar"]  = "palm:calendar/0";
        m["mode"]            = "TwoWay";
        m["enabled"]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        // Pre-populate the Palm with a record we expect to be wiped.
        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        mockDb->createDatabase("DatebookDB");
        WildPalms::PalmSync::PalmRecord stalePr;
        stalePr.recordId = 999;
        stalePr.data = QByteArray("stale-palm-only");
        mockDb->createRecord("DatebookDB", stalePr);

        auto *mockDbPtr = mockDb.get();
        auto dev = std::make_unique<PalmDeviceAccess>(std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        // v1 smoke: empty hub + post-clobber assertion that the stale
        // Palm-only record is gone. Richer hub-seeding lives in the
        // device-backed verification (Task 12).
        auto fut = runtime.clobberSync({QStringLiteral("test-clobber-cal")});
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
        QVERIFY(fut.resultAt(0).success);

        QCOMPARE(mockDbPtr->recordCount(QStringLiteral("DatebookDB")), 0);
        QVERIFY(mockDbPtr->databaseExists(QStringLiteral("DatebookDB")));
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeClobberSync)
#include "tst_palm_runtime_clobber_sync.moc"
```

Register in `tests/runtime/CMakeLists.txt`.

- [ ] **Step 2: Verify the test fails to compile**

```bash
cmake --build build -j 8 -t tst_palm_runtime_clobber_sync
```
Expected: `clobberSync` is not a member of `PalmRuntime`.

- [ ] **Step 3: Add the declaration; remove copyPCToPalm**

In `src/runtime/palmruntime.h`:
- Add (near `hotSync()`):
  ```cpp
  /// Wipe selected Palm-side databases and re-push hub data in one
  /// operation. mappingIds must reference Palm-direct mappings only;
  /// callers should filter via palmDirectMappingsForDomain(). Returns
  /// per-mapping success/stats via the standard PalmRunResult shape.
  QFuture<PalmRunResult> clobberSync(const QList<QString> &mappingIds);
  ```
- Delete the line `QFuture<PalmRunResult> copyPCToPalm();`.
- Update the comment at `palmruntime.h:91` to remove `copyPCToPalm` from the cancel-target list and add `clobberSync`.
- Update the comment at `palmruntime.h:216-217` similarly.

- [ ] **Step 4: Implement; delete copyPCToPalm**

In `src/runtime/palmruntime.cpp`:
- Delete the `copyPCToPalm()` method body (currently at lines ~1053-1056).
- Add `clobberSync` implementation, modeled on `runMirror` but using `SyncRequest` directly:

```cpp
QFuture<PalmRunResult> PalmRuntime::clobberSync(const QList<QString> &mappingIds)
{
    constexpr auto kLabel = QStringLiteral("ClobberSync");
    Q_EMIT runStarted(kLabel);

    if (mappingIds.isEmpty())
        return makeSuccessFuture();

    Kalburator::Sync::SyncRequest req;
    req.mappingIds = mappingIds;
    req.behavior   = Kalburator::Sync::SyncEngine::SyncBehavior::Unmonitored;

    Kalburator::Sync::ExecutionOverride ov;
    ov.clobber = true;
    req.executionOverride = ov;

    auto engineFuture = m_engine->runSync(req);

    // Same cancellation-watcher pattern as runAllMappings.
    if (m_activeSyncWatcher) {
        m_activeSyncWatcher->cancel();
        m_activeSyncWatcher->deleteLater();
    }
    m_activeSyncWatcher = new QFutureWatcher<void>(this);
    QObject::connect(m_activeSyncWatcher,
                     &QFutureWatcher<void>::finished, this, [this]() {
        if (m_activeSyncWatcher) {
            m_activeSyncWatcher->deleteLater();
            m_activeSyncWatcher = nullptr;
        }
    });
    m_activeSyncWatcher->setFuture(engineFuture);

    return engineFuture.then(
        [this, label = kLabel](QList<Kalburator::Sync::SyncResult> results) {
            PalmRunResult r;
            r.startTime = QDateTime::currentDateTimeUtc();
            r.success = std::all_of(results.begin(), results.end(),
                [](const auto &sr){ return sr.success; });
            if (!r.success) {
                for (const auto &sr : results) {
                    if (!sr.success) {
                        r.errorMessage = sr.errorMessage;
                        break;
                    }
                }
            }
            // Multi-domain reporting: aggregate per source backend.
            for (const auto &sr : results) {
                PalmRunResult::PluginStats stats;
                stats.created   = sr.targetStats.created;
                stats.updated   = sr.targetStats.updated;
                stats.deleted   = sr.targetStats.deleted;
                stats.unchanged = sr.targetStats.unchanged;
                stats.errors    = sr.success ? 0 : 1;
                r.perPluginStats.insert(sr.targetBackendId, stats);
            }
            r.endTime = QDateTime::currentDateTimeUtc();
            QMetaObject::invokeMethod(this, [this, r]() {
                if (m_device) m_device->flushWrites();
                if (m_device) m_device->resumeTickle();
                Q_EMIT runFinished(r);
                Q_EMIT syncCompleted();
            });
            return r;
        });
}
```

(If `SyncResult` doesn't expose `targetBackendId` under that name, use the field libkalburator v0.65 actually provides — check `synctypes.h`.)

- [ ] **Step 5: Build the full tree (catches any caller of the removed copyPCToPalm)**

```bash
cmake --build build -j 8
```
Expected: compile errors at every call site of the deleted `copyPCToPalm`. Fix each (Task 11 handles the kf6 menu wiring; for any other internal caller surfaced here, replace with `clobberSync` or remove).

- [ ] **Step 6: Run the test**

```bash
ctest --test-dir build -R tst_palm_runtime_clobber_sync --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        tests/runtime/tst_palm_runtime_clobber_sync.cpp \
        tests/runtime/CMakeLists.txt
git commit -m "feat(runtime): PalmRuntime::clobberSync entry point

Builds SyncRequest{mappingIds, executionOverride={clobber=true}} and
dispatches through SyncEngine::runSync. Per-mapping result aggregation
into PalmRunResult. Deletes copyPCToPalm (subsumed). Round-trip test
included.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 11: Tools menu rewire

**Files:**
- Modify: `src/kf6/actionmanager.h` (rename signal)
- Modify: `src/kf6/actionmanager.cpp` (rename action + signal emission)
- Modify: `src/kf6/kf6mainwindow.h` (rename slot)
- Modify: `src/kf6/kf6mainwindow.cpp` (rename slot, replace handler body)

- [ ] **Step 1: Rename in `ActionManager`**

`actionmanager.h` / `actionmanager.cpp`:
- Rename signal `copyPCToPalmRequested` → `clobberPalmFromPCRequested`.
- Rename the QAction member and the action's text:
  - Action `i18n("Copy PC to Palm")` → `i18n("Clobber Palm from PC")`.
  - Action object name from `m_actionCopyPCToPalm` (or current name) → `m_actionClobberPalmFromPC`.

- [ ] **Step 2: Rename in `KF6MainWindow`**

`kf6mainwindow.h` / `kf6mainwindow.cpp`:
- Rename slot `onCopyPCToPalm` → `onClobberPalmFromPC`.
- Update the `connect()` at line ~358 to wire `clobberPalmFromPCRequested` to `onClobberPalmFromPC`.
- Replace the slot body. The current body (line ~1957) calls `m_palmRuntime->copyPCToPalm()`. Replace with:

```cpp
void KF6MainWindow::onClobberPalmFromPC()
{
    if (!m_palmRuntime || !m_palmRuntime->hasDeviceAccess()) {
        m_logWidget->logError(i18n("Clobber Palm from PC: no Palm device connected"));
        return;
    }

    // Build the domain → mapping-IDs map for Palm-direct mappings.
    ClobberDialog::DomainMappings dm;
    for (const auto &domain : {QStringLiteral("calendar"),
                               QStringLiteral("contacts"),
                               QStringLiteral("memo"),
                               QStringLiteral("todo")}) {
        const auto ids = m_palmRuntime->palmDirectMappingsForDomain(domain);
        if (!ids.isEmpty()) dm.insert(domain, ids);
    }
    if (dm.isEmpty()) {
        m_logWidget->logError(i18n("Clobber Palm from PC: no Palm-direct mappings configured"));
        return;
    }

    ClobberDialog dlg(dm, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const auto ids = dlg.selectedMappingIds();
    if (ids.isEmpty()) return;

    auto *watcher = new QFutureWatcher<PalmRunResult>(this);
    QObject::connect(watcher, &QFutureWatcher<PalmRunResult>::finished,
                     watcher, &QObject::deleteLater);
    watcher->setFuture(m_palmRuntime->clobberSync(ids));
}
```

Add `#include "runtime/clobberdialog.h"` near the top of `kf6mainwindow.cpp`.

- [ ] **Step 3: Build full tree**

```bash
cmake --build build -j 8
```
Expected: 100% built, no errors.

- [ ] **Step 4: Manual smoke check**

Launch the binary:
```bash
./build/wildpalms
```
Open Tools menu; confirm "Clobber Palm from PC" is present, "Copy PC to Palm" is gone. Without a Palm connected, clicking should log "no Palm device connected" without crashing.

- [ ] **Step 5: Run full ctest**

```bash
ctest --test-dir build -j 8
```
Expected: previously-passing tests still pass; the new clobber tests pass; the pre-existing 3-failure cluster from `docs/2026-06-04-v0.63-pin-bump-test-regressions.md` may still fail — that's unrelated and tracked separately.

- [ ] **Step 6: Commit**

```bash
git add src/kf6/actionmanager.h src/kf6/actionmanager.cpp \
        src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "feat(kf6): Tools menu 'Clobber Palm from PC' replaces 'Copy PC to Palm'

ClobberDialog drives per-conduit selection; on accept calls
PalmRuntime::clobberSync(ids). Menu label deliberately reads
'Clobber' to emphasize severity. The old copyPCToPalm runtime
method is deleted.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

## Phase 3: Device-backed verification

### Task 12: Hardware test loop

**Files:**
- None (manual procedure)

This is the original ask: "freshly sync my palm, over and over, as though it were the first sync."

- [ ] **Step 1: Connect a real Palm device**

Confirm the connection per `reference_palm_ttyusb_connection` memory: press HotSync, watch for `/dev/ttyUSB1` (or `/dev/ttyUSB0`).

- [ ] **Step 2: Run baseline hotSync to populate**

In WildPalms with an existing profile holding desktop data, Tools → Hot Sync. Confirm Palm receives the data.

- [ ] **Step 3: Edit a Palm-side record**

On the Palm device, modify or add a Calendar event.

- [ ] **Step 4: Run Clobber Palm from PC, all four conduits**

Tools → "Clobber Palm from PC" → tick Calendar, Contacts, Memo, ToDo → Clobber → confirm warning dialog.

- [ ] **Step 5: Verify**

- Palm-side edit from Step 3 is gone.
- Palm Calendar/Contacts/Memo/ToDo apps show only the desktop's data.
- Desktop data is unchanged (open Akonadi / CalDAV / Markdown files and confirm).
- No mass-delete-guard prompt fired.

- [ ] **Step 6: Repeat 3-5 at least three times**

Confirm the loop works reliably and no per-cycle profile recreation is needed.

- [ ] **Step 7: Document the verified run**

Append a one-paragraph "verified on hardware YYYY-MM-DD" note to `docs/superpowers/specs/2026-06-05-clobber-sync-design.md` so the device-backed gate is recorded.

```bash
git add docs/superpowers/specs/2026-06-05-clobber-sync-design.md
git commit -m "docs(spec): clobber-sync verified on hardware

Verified the freshen-Palm loop on a real device: clobber wipes Palm,
re-pushes hub data, no mass-delete-guard fires, desktop data untouched.
Repeated three full cycles without recreating the profile.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 8: Push the whole branch**

```bash
git push origin feature/three-tier-sync
```

---

## Out of scope (tracked separately)

- The three v0.63+ failing tests (`tst_palm_runtime_route_first_sync`, `tst_palm_runtime_route_recategorization`, `tst_runtime_carddav_e2e`) — see `docs/2026-06-04-v0.63-pin-bump-test-regressions.md`.
- Hub↔remote-only sync (no Palm) — separate design needed; called out in §1 of the spec as a known gap.
- Per-mapping (rather than per-conduit) ClobberDialog UX — v2 deferred per design.
