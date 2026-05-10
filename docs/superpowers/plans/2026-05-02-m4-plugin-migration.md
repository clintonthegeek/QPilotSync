# M4 Plugin Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate memo, contacts, todos, and webcal backend plugins from `IBackendPlugin` (v1) to `IBackendPluginV2`, re-enable their CMake builds, and grow the test suite from 49 to 63 green tests.

**Architecture:** Four mechanical plugin migrations following the calendar plugin (M2) as template. Each plugin's `createBackends(host, PalmDeviceConnection*)` → `createPalmBackend(PalmDeviceAccess*)`, `enrichConflictSnapshot`/`formatConflictRecordHtml` lose `override` (not on v2 interface). The `_v2` integration tests (which use the deleted `runBlobTwoWay` F1 facade) are kept gated via `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` inner guards, deferred to M6. Build verification uses `WILDPALMS_CALENDAR_MVP_ONLY=ON` (current default) to target 63 tests.

**Tech Stack:** Qt6/C++, KF6, libkalburator `IBackendPluginV2`, `PalmDeviceAccess`, `PalmBackend`.

---

## File Map

### Modify (plugin source)
- `src/plugins/memo/memobackendplugin.h` — v2 interface
- `src/plugins/memo/memobackendplugin.cpp` — v2 impl
- `src/plugins/contacts/contactsbackendplugin.h` — v2 interface, `m_device` type change
- `src/plugins/contacts/contactsbackendplugin.cpp` — v2 impl, `m_device->device()` → `m_device`
- `src/plugins/todos/todobackendplugin.h` — v2 interface, `m_device` type change
- `src/plugins/todos/todobackendplugin.cpp` — v2 impl, `m_device->device()` → `m_device`
- `src/plugins/webcalendar/webcalbackendplugin.h` — v2 interface
- `src/plugins/webcalendar/webcalbackendplugin.cpp` — rename method, update return

### Modify (unit tests)
- `tests/plugins/memo/tst_memobackendplugin.cpp` — `createPalmBackend` + `PalmDeviceAccess` pattern
- `tests/plugins/contacts/tst_contactsbackendplugin.cpp` — same
- `tests/plugins/todos/tst_todobackendplugin.cpp` — same
- `tests/plugins/webcalendar/tst_webcalbackendplugin.cpp` — same

### Modify (CMake)
- `src/plugins/CMakeLists.txt` — remove outer guard for 4 plugins, keep plucker gated
- `tests/plugins/CMakeLists.txt` — remove outer guard for 4 plugins, keep plucker gated
- `tests/plugins/memo/CMakeLists.txt` — add `PalmDeviceAccessLib`, gate `tst_memo_v2`
- `tests/plugins/contacts/CMakeLists.txt` — add `PalmDeviceAccessLib`, gate `tst_contacts_v2`
- `tests/plugins/todos/CMakeLists.txt` — add `PalmDeviceAccessLib`, gate `tst_todo_v2`
- `tests/plugins/webcalendar/CMakeLists.txt` — gate `tst_webcal_v2_e2e`

### Modify (status)
- `CURRENT-STATUS.md` — update
- `FINDINGS.md` — append

---

## Reference: The Calendar Pattern

**Header** (`calendarbackendplugin.h`):
```cpp
#include <memory>
#include <QObject>
#include "core/ibackendplugin_v2.h"
namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::Runtime { class PalmDeviceAccess; }

class XBackendPlugin : public QObject, public WildPalms::IBackendPluginV2 {
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
    // ...
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;
    // enrichConflictSnapshot / formatConflictRecordHtml — no override keyword
private:
    std::unique_ptr<WildPalms::PalmSync::PalmBackend> m_palmBackend;
    WildPalms::Runtime::PalmDeviceAccess *m_device = nullptr; // contacts/todos only
};
```

**Impl** (`calendarbackendplugin.cpp`):
```cpp
std::unique_ptr<Kalburator::Sync::IBlobBackend>
CalendarBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    if (!device) return nullptr;
    m_device = device;
    m_palmBackend = std::make_unique<WildPalms::PalmSync::PalmBackend>(device);
    WildPalms::PalmCalendar::populateFromAppInfo(
        *m_categoryStore, QStringLiteral("DatebookDB"),
        m_palmBackend->readAppBlock(QStringLiteral("DatebookDB")));
    return std::make_unique<CalendarBlobBackend>(m_palmBackend.get(), m_categoryStore.get());
}

ConflictHandler *CalendarBackendPlugin::createConflictHandler()
{
    if (!m_device) { qCWarning(...); return nullptr; }
    return new CalendarConflictHandler(m_device, m_palmConfig.get());
}
```

**Test** (`tst_calendarbackendplugin.cpp`):
```cpp
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
using WildPalms::Runtime::PalmDeviceAccess;

CalendarBackendPlugin p;
auto mock = std::make_unique<MockPalmDatabaseAccess>();
PalmDeviceAccess dev(std::move(mock));
auto backend = p.createPalmBackend(&dev);
QVERIFY(backend != nullptr);
```

---

## Task 1: Migrate MemoBackendPlugin

**Files:**
- Modify: `src/plugins/memo/memobackendplugin.h`
- Modify: `src/plugins/memo/memobackendplugin.cpp`
- Modify: `tests/plugins/memo/tst_memobackendplugin.cpp`

- [ ] **Step 1: Update memobackendplugin.h**

Replace the entire file content:
```cpp
#ifndef WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H
#define WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin_v2.h"

namespace Kalburator::Sync::QSyncCore { struct RecordSnapshot; }
namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::Runtime { class PalmDeviceAccess; }

namespace WildPalms::Memo {

class MemoBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
public:
    explicit MemoBackendPlugin(QObject *parent = nullptr);
    ~MemoBackendPlugin() override;

    // IPlugin
    QString pluginId()    const override;
    QString displayName() const override;
    QIcon   icon()        const override;
    QString description() const override;
    QString version()     const override;

    // IBackendPluginV2
    QStringList claimedDatabases() const override;
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;

    // IBackendPluginV2 — main view
    bool     hasMainView()   const override;
    QWidget *createMainView(QWidget *parent) const override;
    QString  mainViewName()  const override;
    QIcon    mainViewIcon()  const override;

    // Conflict presentation (called by conflict UI layer; not virtual in v2)
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const;

private:
    std::unique_ptr<WildPalms::PalmSync::PalmBackend> m_palmBackend;
};

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_MEMOBACKENDPLUGIN_H
```

- [ ] **Step 2: Update memobackendplugin.cpp**

Replace the first ~15 lines (includes and opening) and the `createBackends` method:

Remove:
```cpp
#include "palm/palmdeviceconnection.h"
```

Add after `#include "memoview.h"`:
```cpp
#include "palm/sync/palmbackend.h"
#include "runtime/palmdeviceaccess.h"
```

Replace the `createBackends` implementation with `createPalmBackend`:
```cpp
// Remove this entire function:
WildPalms::IBackendPlugin::ProvidedBackends
MemoBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                  PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (device) {
        out.blob = new MemoBlobBackend(device->palmBackend(), /*categoryStore=*/nullptr);
    }
    return out;
}

// Add this function:
std::unique_ptr<Kalburator::Sync::IBlobBackend>
MemoBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    if (!device) return nullptr;
    m_palmBackend = std::make_unique<WildPalms::PalmSync::PalmBackend>(device);
    return std::make_unique<MemoBlobBackend>(m_palmBackend.get(), /*categoryStore=*/nullptr);
}
```

Remove `override` from `enrichConflictSnapshot` and `formatConflictRecordHtml` signatures in the `.cpp` (they're already non-override in the header; the `.cpp` must match — no `override` keyword in `.cpp` definitions so this requires no change to the `.cpp` function bodies, only confirming the signatures match the new header).

- [ ] **Step 3: Update tst_memobackendplugin.cpp**

Replace:
```cpp
#include "plugins/memo/memobackendplugin.h"
#include "plugins/memo/memoblobbackend.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/codecs/memocodec.h"

#include "conflictrecord.h"   // Kalburator::Sync::QSyncCore::RecordSnapshot

using WildPalms::Memo::MemoBackendPlugin;
using WildPalms::Memo::MemoBlobBackend;
```
With:
```cpp
#include "plugins/memo/memobackendplugin.h"
#include "plugins/memo/memoblobbackend.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/codecs/memocodec.h"
#include "runtime/palmdeviceaccess.h"

#include "iblobbackend.h"
#include "conflictrecord.h"

using WildPalms::Memo::MemoBackendPlugin;
using WildPalms::Memo::MemoBlobBackend;
using WildPalms::Runtime::PalmDeviceAccess;
```

Replace the test method:
```cpp
// Remove:
void TestMemoBackendPlugin::createBackendsReturnsMemoBlobBackendOverPalmBackend()
{
    WildPalms::PalmSync::MockPalmDatabaseAccess dev;
    PalmDeviceConnection conn(&dev);
    MemoBackendPlugin p;

    auto backends = p.createBackends(nullptr, &conn);
    QVERIFY(backends.blob != nullptr);
    QCOMPARE(backends.blob->backendId(), QStringLiteral("palm-memo"));
    QCOMPARE(backends.calendar, static_cast<Kalburator::Sync::SyncBackend *>(nullptr));

    delete backends.blob;
}

// Add:
void TestMemoBackendPlugin::createPalmBackendReturnsMemoBlobBackend()
{
    MemoBackendPlugin p;
    auto mock = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    auto backend = p.createPalmBackend(&dev);
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendId(), QStringLiteral("palm-memo"));
}
```

Also update the class declaration in the test — rename the slot:
```cpp
// Change:
void createBackendsReturnsMemoBlobBackendOverPalmBackend();
// To:
void createPalmBackendReturnsMemoBlobBackend();
```

- [ ] **Step 4: Build memo plugin only to verify compilation**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake -S . -B build -DWILDPALMS_CALENDAR_MVP_ONLY=ON
cmake --build build --target wildpalms_memo_v2 tst_memobackendplugin 2>&1 | tail -20
```
Expected: compiles without errors.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/plugins/memo/memobackendplugin.h src/plugins/memo/memobackendplugin.cpp tests/plugins/memo/tst_memobackendplugin.cpp
git commit -m "M4 Task 1: migrate MemoBackendPlugin to IBackendPluginV2"
```

---

## Task 2: Migrate ContactsBackendPlugin

**Files:**
- Modify: `src/plugins/contacts/contactsbackendplugin.h`
- Modify: `src/plugins/contacts/contactsbackendplugin.cpp`
- Modify: `tests/plugins/contacts/tst_contactsbackendplugin.cpp`

Key difference from memo: contacts has `createConflictHandler()` which uses `m_device`, and the v1 code calls `m_device->device()` to get `IPalmDatabaseAccess*`. In v2, `m_device` is `PalmDeviceAccess*` which IS-A `IPalmDatabaseAccess*`, so `m_device->device()` becomes `m_device`.

- [ ] **Step 1: Update contactsbackendplugin.h**

Replace header includes and class declaration:
```cpp
#ifndef WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H
#define WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin_v2.h"

namespace Kalburator::Sync::QSyncCore { struct RecordSnapshot; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
namespace WildPalms::PalmConflict { struct PalmBackendConfig; }
namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::Runtime { class PalmDeviceAccess; }

namespace WildPalms::ContactsPlugin {

class ContactsBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
public:
    explicit ContactsBackendPlugin(QObject *parent = nullptr);
    ~ContactsBackendPlugin() override;

    // IPlugin
    QString pluginId()    const override;
    QString displayName() const override;
    QIcon   icon()        const override;
    QString description() const override;
    QString version()     const override;

    // IBackendPluginV2
    QStringList claimedDatabases() const override;
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;

    // IBackendPluginV2 — conflict handler
    Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler() override;

    // No main view for contacts (legacy ContactView stays on legacy conduit until E.16)
    bool hasMainView() const override { return false; }

    // Conflict presentation (called by conflict UI layer; not virtual in v2)
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const;

private:
    std::unique_ptr<WildPalms::PalmCalendar::CategoryMappingStore> m_categoryStore;
    std::unique_ptr<WildPalms::PalmConflict::PalmBackendConfig>    m_palmConfig;
    std::unique_ptr<WildPalms::PalmSync::PalmBackend>              m_palmBackend;
    WildPalms::Runtime::PalmDeviceAccess *m_device = nullptr; // borrowed; cached for createConflictHandler
};

} // namespace WildPalms::ContactsPlugin

#endif // WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H
```

- [ ] **Step 2: Update contactsbackendplugin.cpp**

Remove:
```cpp
#include "palm/palmdeviceconnection.h"
```

Add:
```cpp
#include "palm/sync/palmbackend.h"
#include "runtime/palmdeviceaccess.h"
```

Replace `createBackends` with `createPalmBackend`:
```cpp
// Remove:
WildPalms::IBackendPlugin::ProvidedBackends
ContactsBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                      PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (!device) return out;

    m_device = device;

    auto *palmBackend = device->palmBackend();
    if (palmBackend) {
        WildPalms::PalmCalendar::populateFromAppInfo(
            *m_categoryStore, QStringLiteral("AddressDB"),
            palmBackend->readAppBlock(QStringLiteral("AddressDB")));
        out.blob = new ContactsBlobBackend(palmBackend, m_categoryStore.get());
    }

    return out;
}

// Add:
std::unique_ptr<Kalburator::Sync::IBlobBackend>
ContactsBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    if (!device) return nullptr;

    m_device = device;
    m_palmBackend = std::make_unique<WildPalms::PalmSync::PalmBackend>(device);

    WildPalms::PalmCalendar::populateFromAppInfo(
        *m_categoryStore, QStringLiteral("AddressDB"),
        m_palmBackend->readAppBlock(QStringLiteral("AddressDB")));

    return std::make_unique<ContactsBlobBackend>(m_palmBackend.get(), m_categoryStore.get());
}
```

Update `createConflictHandler` — change the guard and the argument:
```cpp
// Old:
Kalburator::Sync::QSyncCore::ConflictHandler *
ContactsBackendPlugin::createConflictHandler()
{
    if (!m_device || !m_device->device()) {
        qCWarning(WP_CONTACTS_PLUGIN)
            << "createConflictHandler called before createBackends — ...";
        return nullptr;
    }
    return new ContactsConflictHandler(m_device->device(), m_palmConfig.get());
}

// New:
Kalburator::Sync::QSyncCore::ConflictHandler *
ContactsBackendPlugin::createConflictHandler()
{
    if (!m_device) {
        qCWarning(WP_CONTACTS_PLUGIN)
            << "createConflictHandler called before createPalmBackend — "
               "runtime must invoke createPalmBackend first to wire the device.";
        return nullptr;
    }
    // PalmDeviceAccess IS-A IPalmDatabaseAccess; no cast needed.
    return new ContactsConflictHandler(m_device, m_palmConfig.get());
}
```

- [ ] **Step 3: Update tst_contactsbackendplugin.cpp**

Replace includes at top:
```cpp
// Remove:
#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
// Add:
#include "core/ibackendplugin_v2.h"
#include "runtime/palmdeviceaccess.h"
```

Update `using` declarations:
```cpp
// Add:
using WildPalms::Runtime::PalmDeviceAccess;
```

Replace `createBackends_returnsBlobOnly`:
```cpp
void TestContactsBackendPlugin::createBackends_returnsBlobOnly()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);
}
```

Replace `createBackends_populatesCategoryStoreFromAppInfo`:
```cpp
void TestContactsBackendPlugin::createBackends_populatesCategoryStoreFromAppInfo()
{
    QStringList names;
    for (int i = 0; i < 16; ++i) names << QString();
    names[0] = QStringLiteral("Unfiled");
    names[1] = QStringLiteral("Family");
    names[5] = QStringLiteral("Customers");

    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    mock->setAppBlock(QStringLiteral("AddressDB"), buildAddressDbAppInfo(names));
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);

    auto *blob = static_cast<ContactsBlobBackend *>(blobPtr.get());
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 3);
}
```

Replace `createConflictHandler_returnsContactsConflictHandler`:
```cpp
void TestContactsBackendPlugin::createConflictHandler_returnsContactsConflictHandler()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    ContactsBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);   // primes m_device
    Q_UNUSED(blobPtr)

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<ContactsConflictHandler *>(handler) != nullptr);
    delete handler;
}
```

- [ ] **Step 4: Build contacts plugin only to verify compilation**

```bash
cmake --build /home/clinton/dev/refactor-engine-merger/WildPalms/build --target wildpalms_contacts_v2 tst_contactsbackendplugin 2>&1 | tail -20
```
Expected: compiles without errors.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/plugins/contacts/contactsbackendplugin.h src/plugins/contacts/contactsbackendplugin.cpp tests/plugins/contacts/tst_contactsbackendplugin.cpp
git commit -m "M4 Task 2: migrate ContactsBackendPlugin to IBackendPluginV2"
```

---

## Task 3: Migrate TodoBackendPlugin

**Files:**
- Modify: `src/plugins/todos/todobackendplugin.h`
- Modify: `src/plugins/todos/todobackendplugin.cpp`
- Modify: `tests/plugins/todos/tst_todobackendplugin.cpp`

Identical pattern to contacts, substituting "ToDoDB" for "AddressDB" and the todos-specific classes.

- [ ] **Step 1: Update todobackendplugin.h**

```cpp
#ifndef WILDPALMS_TODO_TODOBACKENDPLUGIN_H
#define WILDPALMS_TODO_TODOBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin_v2.h"

namespace Kalburator::Sync::QSyncCore { struct RecordSnapshot; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
namespace WildPalms::PalmConflict { struct PalmBackendConfig; }
namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::Runtime { class PalmDeviceAccess; }

namespace WildPalms::TodoPlugin {

class TodoBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)
public:
    explicit TodoBackendPlugin(QObject *parent = nullptr);
    ~TodoBackendPlugin() override;

    // IPlugin
    QString pluginId()    const override;
    QString displayName() const override;
    QIcon   icon()        const override;
    QString description() const override;
    QString version()     const override;

    // IBackendPluginV2
    QStringList claimedDatabases() const override;
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;

    // IBackendPluginV2 — conflict handler
    Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler() override;

    // IBackendPluginV2 — main view
    bool     hasMainView()   const override;
    QWidget *createMainView(QWidget *parent) const override;
    QString  mainViewName()  const override;
    QIcon    mainViewIcon()  const override;

    // Conflict presentation (called by conflict UI layer; not virtual in v2)
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const;

private:
    std::unique_ptr<WildPalms::PalmCalendar::CategoryMappingStore> m_categoryStore;
    std::unique_ptr<WildPalms::PalmConflict::PalmBackendConfig>    m_palmConfig;
    std::unique_ptr<WildPalms::PalmSync::PalmBackend>              m_palmBackend;
    WildPalms::Runtime::PalmDeviceAccess *m_device = nullptr; // borrowed; cached for createConflictHandler
};

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODOBACKENDPLUGIN_H
```

- [ ] **Step 2: Update todobackendplugin.cpp**

Remove:
```cpp
#include "palm/palmdeviceconnection.h"
```

Add after `#include "palm/sync/palmbackend.h"` (already present):
```cpp
#include "runtime/palmdeviceaccess.h"
```

Replace `createBackends` with `createPalmBackend`:
```cpp
// Remove:
WildPalms::IBackendPlugin::ProvidedBackends
TodoBackendPlugin::createBackends(Kalburator::Sync::ISyncHost *host,
                                  PalmDeviceConnection         *device)
{
    Q_UNUSED(host)
    ProvidedBackends out;
    if (!device) return out;

    m_device = device;

    auto *palmBackend = device->palmBackend();
    if (palmBackend) {
        WildPalms::PalmCalendar::populateFromAppInfo(
            *m_categoryStore, QStringLiteral("ToDoDB"),
            palmBackend->readAppBlock(QStringLiteral("ToDoDB")));
        out.blob = new TodoBlobBackend(palmBackend, m_categoryStore.get());
    }

    return out;
}

// Add:
std::unique_ptr<Kalburator::Sync::IBlobBackend>
TodoBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    if (!device) return nullptr;

    m_device = device;
    m_palmBackend = std::make_unique<WildPalms::PalmSync::PalmBackend>(device);

    WildPalms::PalmCalendar::populateFromAppInfo(
        *m_categoryStore, QStringLiteral("ToDoDB"),
        m_palmBackend->readAppBlock(QStringLiteral("ToDoDB")));

    return std::make_unique<TodoBlobBackend>(m_palmBackend.get(), m_categoryStore.get());
}
```

Update `createConflictHandler`:
```cpp
// Old:
if (!m_device || !m_device->device()) {
    qCWarning(WP_TODO_PLUGIN)
        << "createConflictHandler called before createBackends — ...";
    return nullptr;
}
return new TodoConflictHandler(m_device->device(), m_palmConfig.get());

// New:
if (!m_device) {
    qCWarning(WP_TODO_PLUGIN)
        << "createConflictHandler called before createPalmBackend — "
           "runtime must invoke createPalmBackend first to wire the device.";
    return nullptr;
}
// PalmDeviceAccess IS-A IPalmDatabaseAccess; no cast needed.
return new TodoConflictHandler(m_device, m_palmConfig.get());
```

- [ ] **Step 3: Update tst_todobackendplugin.cpp**

Replace includes:
```cpp
// Remove:
#include "core/ibackendplugin.h"
#include "palm/palmdeviceconnection.h"
// Add:
#include "core/ibackendplugin_v2.h"
#include "runtime/palmdeviceaccess.h"
```

Add `using`:
```cpp
using WildPalms::Runtime::PalmDeviceAccess;
```

Replace `createBackendsPopulatesCategoryStoreFromAppInfo`:
```cpp
void TestTodoBackendPlugin::createBackendsPopulatesCategoryStoreFromAppInfo()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    mock->setAppBlock(QStringLiteral("ToDoDB"), buildAppInfoTwoSlots());
    PalmDeviceAccess dev(std::move(mock));

    TodoBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);

    auto *blob = static_cast<TodoBlobBackend *>(blobPtr.get());
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 3);
    QCOMPARE(cols[1].name, QStringLiteral("Personal"));
    QCOMPARE(cols[2].name, QStringLiteral("Business"));
}
```

Replace `createConflictHandlerReturnsTodoHandler`:
```cpp
void TestTodoBackendPlugin::createConflictHandlerReturnsTodoHandler()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    TodoBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);   // primes m_device
    Q_UNUSED(blobPtr)

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<TodoConflictHandler *>(handler) != nullptr);
    delete handler;
}
```

- [ ] **Step 4: Build todos plugin only to verify compilation**

```bash
cmake --build /home/clinton/dev/refactor-engine-merger/WildPalms/build --target wildpalms_todos_v2 tst_todobackendplugin 2>&1 | tail -20
```
Expected: compiles without errors.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/plugins/todos/todobackendplugin.h src/plugins/todos/todobackendplugin.cpp tests/plugins/todos/tst_todobackendplugin.cpp
git commit -m "M4 Task 3: migrate TodoBackendPlugin to IBackendPluginV2"
```

---

## Task 4: Migrate WebcalBackendPlugin

**Files:**
- Modify: `src/plugins/webcalendar/webcalbackendplugin.h`
- Modify: `src/plugins/webcalendar/webcalbackendplugin.cpp`
- Modify: `tests/plugins/webcalendar/tst_webcalbackendplugin.cpp`

Simplest migration: webcal ignores `device` entirely (no PalmDB access). No `m_palmBackend`, no `m_device`. Just interface rename + return type change.

- [ ] **Step 1: Update webcalbackendplugin.h**

Replace the includes and class declaration:
```cpp
#ifndef WILDPALMS_WEBCAL_WEBCALBACKENDPLUGIN_H
#define WILDPALMS_WEBCAL_WEBCALBACKENDPLUGIN_H

#include <memory>

#include <QJsonObject>
#include <QList>
#include <QObject>

#include "core/ibackendplugin_v2.h"

#include "webcalfeed.h"

class QNetworkAccessManager;

namespace Kalburator::Sync {
class IcsFeedFetcher;
}
namespace WildPalms::Runtime { class PalmDeviceAccess; }

namespace WildPalms::WebcalPlugin {

class WebcalBlobBackend;

class WebcalBackendPlugin : public QObject, public WildPalms::IBackendPluginV2
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPluginV2)

public:
    explicit WebcalBackendPlugin(QObject *parent = nullptr);
    ~WebcalBackendPlugin() override;

    // ===== IPlugin =====
    QString pluginId() const override     { return QStringLiteral("webcalendar"); }
    QString displayName() const override  { return QStringLiteral("Web Calendar Subscriptions"); }
    QString description() const override
    { return QStringLiteral("Subscribe to remote iCalendar feeds (read-only)"); }
    QString version() const override      { return QStringLiteral("2.0.0"); }
    QIcon   icon() const override;

    // ===== IBackendPluginV2 =====
    QStringList claimedDatabases() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;

    bool hasMainView() const override { return false; }

    // ===== Settings (JSON only; no widget) =====
    bool hasSettings() const { return true; }
    void loadSettings(const QJsonObject &settings);
    QJsonObject saveSettings() const;

    // Read-back for tests / future runtime wiring.
    QList<WebcalFeed> feeds() const { return m_feeds; }
    WebcalBlobBackend *currentBackend() const { return m_backend; }

private:
    void enforceSlotUniqueness();

    QList<WebcalFeed>                  m_feeds;
    QNetworkAccessManager             *m_network = nullptr;  // owned (parented)
    Kalburator::Sync::IcsFeedFetcher  *m_fetcher = nullptr;  // owned (parented)
    WebcalBlobBackend                 *m_backend = nullptr;  // owned by manager
};

} // namespace WildPalms::WebcalPlugin

#endif
```

- [ ] **Step 2: Update webcalbackendplugin.cpp**

Replace `createBackends`:
```cpp
// Remove:
WildPalms::IBackendPlugin::ProvidedBackends WebcalBackendPlugin::createBackends(
    Kalburator::Sync::ISyncHost *host, PalmDeviceConnection *device)
{
    Q_UNUSED(host);
    Q_UNUSED(device);

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    if (!m_fetcher) {
        m_fetcher = new Kalburator::Sync::IcsFeedFetcher(m_network, this);
    }

    m_backend = new WebcalBlobBackend(m_feeds, m_fetcher);

    ProvidedBackends out;
    out.blob     = m_backend;
    out.calendar = nullptr;
    return out;
}

// Add:
std::unique_ptr<Kalburator::Sync::IBlobBackend>
WebcalBackendPlugin::createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)
{
    Q_UNUSED(device);  // Webcal claims no Palm DB; fetches from URLs.

    if (!m_network) {
        m_network = new QNetworkAccessManager(this);
    }
    if (!m_fetcher) {
        m_fetcher = new Kalburator::Sync::IcsFeedFetcher(m_network, this);
    }

    auto backend = std::make_unique<WebcalBlobBackend>(m_feeds, m_fetcher);
    m_backend = backend.get();
    return backend;
}
```

Also remove the `#include` for `ibackendplugin.h` if it's included at the top of the .cpp (it isn't — the header includes it; the .cpp just includes the header).

- [ ] **Step 3: Update tst_webcalbackendplugin.cpp**

The test `createBackends_returnsBlobNullCalendar` tests both `blob` and `calendar` from `ProvidedBackends`. After migration, `createPalmBackend` returns only a `unique_ptr<IBlobBackend>`. Replace:

```cpp
// Remove:
void createBackends_returnsBlobNullCalendar()
{
    WebcalBackendPlugin p;
    const auto out = p.createBackends(nullptr, nullptr);
    QVERIFY(out.blob != nullptr);
    QVERIFY(out.calendar == nullptr);
    QVERIFY(dynamic_cast<WebcalBlobBackend *>(out.blob) != nullptr);
    delete out.blob;
}

// Add:
void createPalmBackend_returnsWebcalBlobBackend()
{
    WebcalBackendPlugin p;
    auto blobPtr = p.createPalmBackend(nullptr);
    QVERIFY(blobPtr != nullptr);
    QVERIFY(dynamic_cast<WebcalBlobBackend *>(blobPtr.get()) != nullptr);
}
```

Also add `#include <memory>` if not already present (for `unique_ptr`), and add:
```cpp
#include "iblobbackend.h"
```
after the existing `#include "webcalblobbackend.h"`.

The `metadata_correct` test calls `p.createConflictHandler()` which is still valid (v2 base returns nullptr by default).

- [ ] **Step 4: Build webcal plugin only to verify compilation**

```bash
cmake --build /home/clinton/dev/refactor-engine-merger/WildPalms/build --target wildpalms_webcalendar_v2 tst_webcalbackendplugin 2>&1 | tail -20
```
Expected: compiles without errors.

- [ ] **Step 5: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/plugins/webcalendar/webcalbackendplugin.h src/plugins/webcalendar/webcalbackendplugin.cpp tests/plugins/webcalendar/tst_webcalbackendplugin.cpp
git commit -m "M4 Task 4: migrate WebcalBackendPlugin to IBackendPluginV2"
```

---

## Task 5: CMake Re-Enable

**Files:**
- Modify: `src/plugins/CMakeLists.txt`
- Modify: `tests/plugins/CMakeLists.txt`
- Modify: `tests/plugins/memo/CMakeLists.txt`
- Modify: `tests/plugins/contacts/CMakeLists.txt`
- Modify: `tests/plugins/todos/CMakeLists.txt`
- Modify: `tests/plugins/webcalendar/CMakeLists.txt`

- [ ] **Step 1: Update src/plugins/CMakeLists.txt**

Replace the current content:
```cmake
# Wild Palms plugins
# Each subdirectory here builds a conduit plugin (.so) that links against WildPalmsCore.
# Plugins are loaded at runtime by the sync engine.

add_subdirectory(calendar)
add_subdirectory(install)

if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(memo)
    add_subdirectory(contacts)
    add_subdirectory(todos)
    add_subdirectory(webcalendar)
    add_subdirectory(plucker)
endif()
```

With:
```cmake
# Wild Palms plugins
# Each subdirectory here builds a conduit plugin (.so) that links against WildPalmsCore.
# Plugins are loaded at runtime by the sync engine.

add_subdirectory(calendar)
add_subdirectory(install)
add_subdirectory(memo)
add_subdirectory(contacts)
add_subdirectory(todos)
add_subdirectory(webcalendar)

if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(plucker)
endif()
```

- [ ] **Step 2: Update tests/plugins/CMakeLists.txt**

Replace the content:
```cmake
# Phase E.8 — dummy plugins for exercising the new-ABI managers' factory
# load path. Install namespace "wildpalms_test/plugins" avoids clashing
# with real plugins (wildpalms/conduits, wildpalms/plugins).

find_package(KF6 REQUIRED COMPONENTS CoreAddons)

add_subdirectory(dummy_backend)
add_subdirectory(dummy_action)

# Phase E.10 — Calendar plugin tests.
add_subdirectory(calendar)

# Phase E.15a — Install action tests.
add_subdirectory(install)

# M4: memo/contacts/todos/webcal re-enabled; _v2 integration tests gated
# within each subdirectory pending M6 rewrite against new SyncEngine API.
add_subdirectory(memo)
add_subdirectory(todos)
add_subdirectory(contacts)
add_subdirectory(webcalendar)

if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
    add_subdirectory(plucker)
endif()
```

- [ ] **Step 3: Update tests/plugins/memo/CMakeLists.txt**

Add `PalmDeviceAccessLib` to `tst_memobackendplugin` link libraries (after `WildPalmsCore`):
```cmake
target_link_libraries(tst_memobackendplugin
    PRIVATE
        WildPalmsPalmSync
        WildPalmsPalmCodecs
        WildPalmsPalmCalendar
        WildPalmsCore
        PalmDeviceAccessLib     # PalmDeviceAccess symbols
        Kalburator::Sync
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        Qt::Core
)
```

Wrap `tst_memo_v2` target in inner guard (after the tst_memobackendplugin block):
```cmake
# Task 9: end-to-end test that loads the real .so through
# BackendPluginManager and drives BlobSyncEngine::twoWayWithBaseline.
# NOTE: uses runBlobTwoWay (deleted in Plan 1 M1) and BackendPluginManager
# v1 API; excluded until M6 rewrites against the new SyncEngine API.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
add_executable(tst_memo_v2 tst_memo_v2.cpp)
...
add_test(NAME tst_memo_v2 COMMAND tst_memo_v2)
set_tests_properties(tst_memo_v2 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
```

The entire `add_executable(tst_memo_v2 ...)` block through `set_tests_properties` should be wrapped in the `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` guard.

- [ ] **Step 4: Update tests/plugins/contacts/CMakeLists.txt**

Add `PalmDeviceAccessLib` to `tst_contactsbackendplugin` link libraries (after `WildPalmsCore`).

Wrap `tst_contacts_v2` block (lines for `add_executable(tst_contacts_v2 ...)` through `set_tests_properties`) in:
```cmake
# NOTE: uses runBlobTwoWay (deleted in Plan 1 M1) and BackendPluginManager
# v1 API; excluded until M6 rewrites against the new SyncEngine API.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
...
endif()
```

- [ ] **Step 5: Update tests/plugins/todos/CMakeLists.txt**

Add `PalmDeviceAccessLib` to `tst_todobackendplugin` link libraries (after `WildPalmsCore`).

Wrap `tst_todo_v2` block in:
```cmake
# NOTE: uses runBlobTwoWay (deleted in Plan 1 M1) and BackendPluginManager
# v1 API; excluded until M6 rewrites against the new SyncEngine API.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
...
endif()
```

- [ ] **Step 6: Update tests/plugins/webcalendar/CMakeLists.txt**

Wrap `tst_webcal_v2_e2e` block (the `--- Task 6: end-to-end ---` section) in:
```cmake
# NOTE: uses runBlobMirror (deleted in Plan 1 M1); excluded until M6
# rewrites against the new SyncEngine API.
if(NOT WILDPALMS_CALENDAR_MVP_ONLY)
...
endif()
```

- [ ] **Step 7: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add src/plugins/CMakeLists.txt tests/plugins/CMakeLists.txt tests/plugins/memo/CMakeLists.txt tests/plugins/contacts/CMakeLists.txt tests/plugins/todos/CMakeLists.txt tests/plugins/webcalendar/CMakeLists.txt
git commit -m "M4 Task 5: re-enable memo/contacts/todos/webcal CMake builds"
```

---

## Task 6: Build and Verify

- [ ] **Step 1: Reconfigure and build**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
cmake -S . -B build -DWILDPALMS_CALENDAR_MVP_ONLY=ON -DBUILD_TESTS=ON
cmake --build build 2>&1 | tail -30
```
Expected: zero errors, zero warnings about `createBackends`. The `_v2` tests are excluded by inner guard.

- [ ] **Step 2: Run all tests**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms/build
ctest --output-on-failure 2>&1 | tail -40
```
Expected: 63/63 pass (49 original + 14 new: 3 memo + 4 contacts + 4 todos + 3 webcal).

If any test fails, investigate the failure message. The most likely issues:
- Missing `PalmDeviceAccessLib` link → add to the test CMakeLists
- `enrichConflictSnapshot`/`formatConflictRecordHtml` called via `IBackendPlugin*` cast somewhere → search for callers

- [ ] **Step 3: Verify test count**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms/build
ctest -N 2>&1 | grep "Total Tests:" 
```
Expected: `Total Tests: 63`

---

## Task 7: Update CURRENT-STATUS.md and FINDINGS.md

- [ ] **Step 1: Update CURRENT-STATUS.md**

Update the header date line to `2026-05-02 (M4 complete — memo/contacts/todos/webcal migrated to IBackendPluginV2; 63/63 tests green)`.

Update "Where we are" to mark M4 as complete.

Update "Test posture" section:
```
- WildPalms: **63/63** pass (WILDPALMS_CALENDAR_MVP_ONLY=ON; _v2 integration tests deferred to M6)
```

Append to "Recently committed (WildPalms)":
```
M4 Task 5: re-enable memo/contacts/todos/webcal CMake builds
M4 Task 4: migrate WebcalBackendPlugin to IBackendPluginV2
M4 Task 3: migrate TodoBackendPlugin to IBackendPluginV2
M4 Task 2: migrate ContactsBackendPlugin to IBackendPluginV2
M4 Task 1: migrate MemoBackendPlugin to IBackendPluginV2
```

- [ ] **Step 2: Append to FINDINGS.md**

Add a new entry:
```markdown
## M4: _v2 integration test breakage (2026-05-02)

`tst_{memo,contacts,todos}_v2.cpp` and `tst_webcal_v2_e2e.cpp` use:
1. `BackendPluginManager::plugin(id)` which `dynamic_cast<IBackendPlugin*>` — fails for v2-only plugins
2. `engine.runBlobTwoWay(...)` / `engine.runBlobMirror(...)` — deleted in Plan 1 M1

These tests are gated by `if(NOT WILDPALMS_CALENDAR_MVP_ONLY)` inner guards (matching `tst_calendar_v2`'s treatment). Deferred to M6 which will rewrite them using the mapping-based `SyncEngine::runSyncFuture(mappingId)` API.

`SyncRunner_wp.cpp` has the same broken `runBlobTwoWay`/`runBlobMirror` calls and remains excluded by the runtime's own `WILDPALMS_CALENDAR_MVP_ONLY` gate from M2 Task 12.
```

- [ ] **Step 3: Commit**

```bash
cd /home/clinton/dev/refactor-engine-merger/WildPalms
git add ../../CURRENT-STATUS.md ../../FINDINGS.md
git commit -m "M4: update CURRENT-STATUS and FINDINGS"
```

---

## Self-Review

**Spec coverage:**
- ✓ Memo migrated to IBackendPluginV2
- ✓ Contacts migrated (m_device type change, createConflictHandler fixed)
- ✓ Todos migrated (same pattern as contacts)
- ✓ Webcal migrated (ignores device)
- ✓ CMake guards removed for 4 plugins, plucker stays gated
- ✓ Test coverage restored (49→63 unit tests)
- ✓ `_v2` integration tests deferred to M6 via inner guards (same as tst_calendar_v2)

**Type consistency:**
- All four plugins: `createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device)` — matches `IBackendPluginV2` declaration exactly
- `m_palmBackend`: `std::unique_ptr<WildPalms::PalmSync::PalmBackend>` — matches calendar reference
- `m_device` (contacts/todos): `WildPalms::Runtime::PalmDeviceAccess *` — matches calendar reference
- `createConflictHandler`: passes `m_device` (IS-A `IPalmDatabaseAccess*`) directly — matches calendar pattern

**Potential issue:** `tst_memobackendplugin.cpp` still has tests for `enrichConflictSnapshot` and `formatConflictRecordHtml` which remain as non-virtual methods on `MemoBackendPlugin`. These tests call the methods directly (not through a virtual dispatch), so they compile and run fine after the migration.
