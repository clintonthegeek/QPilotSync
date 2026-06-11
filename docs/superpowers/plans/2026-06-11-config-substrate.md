# Configuration Substrate (Sub-project A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded-4-conduits assumptions with a conduit descriptor contract, add the first credential-less source contribution, and move category routes to names-first with device reconciliation (AppInfo write) and visible per-route status.

**Architecture:** `PimPlugin` is promoted to the conduit descriptor (virtuals over the surface the four plugins already implement by convention); PalmRuntime/routemapping/wizard/graph enumerate descriptors instead of casting to concrete types. Category routes persist category *names* (`palm:<domain>/name:<X>`); a reconciler binds names to device slots at connect, writing AppInfo via the existing `CategoryInfo` pack machinery when slots must be created. `LocalFolderContribution` proves the everything-is-a-provider model.

**Tech Stack:** Qt6/KF6, libkalburator v0.69 (no lib changes needed), QtTest. Spec: `docs/superpowers/specs/2026-06-11-config-substrate-design.md`.

**Build/test commands** (legacy `build/` dir):
```bash
make -C build -j$(($(nproc)-1))                       # build
ctest --test-dir build --output-on-failure -R <name>  # one test
ctest --test-dir build -j$(($(nproc)-1))              # full suite (must stay green)
```

**Submodule discipline:** Tasks 2 touches the four conduit submodules (`src/plugins/{calendar,contacts,memo,todos}`). Workflow per submodule: edit → commit *inside* the submodule → `git push` the submodule to its GitHub remote → `git add src/plugins/<x>` in the superproject to bump the gitlink (committed together with the superproject change that needs it).

---

## Task 1: PimPlugin becomes the conduit descriptor

**Files:**
- Modify: `src/plugins/pimplugin.h`
- Create: `src/plugins/pimplugin.cpp`
- Modify: `src/CMakeLists.txt` (add the new .cpp to the WildPalmsCore source list — find the list with `grep -n "runtime/palmruntime.cpp" src/CMakeLists.txt` and add `plugins/pimplugin.cpp` alongside)
- Create: `tests/runtime/tst_conduit_descriptor.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (register the new test the same way `tst_domainfilter` is registered — copy its block, rename)

- [ ] **Step 1: Write the failing test**

`tests/runtime/tst_conduit_descriptor.cpp`:

```cpp
// Sub-project A (config substrate): the conduit descriptor contract.
// Absorbs tst_domainfilter's matching cases (domainfilter.cpp is deleted in
// Task 5) and pins the descriptor defaults a third-party conduit relies on.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "plugins/pimplugin.h"
#include <collectioninfo.h>

using Kalburator::Sync::CollectionInfo;
using WildPalms::Plugins::PimPlugin;

namespace {
// Minimal conduit: only the pure virtuals, defaults everywhere else.
class TestConduit : public PimPlugin {
public:
    explicit TestConduit(QString domain) : m_domain(std::move(domain)) {}
    QString conduitId() const override { return m_domain; }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{m_domain}; }
    QString primaryDbName() const override { return QStringLiteral("TestDB"); }
    QString conduitDisplayName() const override { return QStringLiteral("Test"); }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
private:
    QString m_domain;
};
} // namespace

class TstConduitDescriptor : public QObject {
    Q_OBJECT
private slots:
    void calendarMatching();
    void todoMatching();
    void contactsMatching();
    void noteMatching();
    void genericDomainFallback();
    void tasksOnlyCalendarExcludedFromCalendar();
    void descriptorDefaults();
};

void TstConduitDescriptor::calendarMatching()
{
    TestConduit c(QStringLiteral("calendar"));
    CollectionInfo byType;
    byType.type = QStringLiteral("calendar");
    QVERIFY(c.matchesCollection(byType));                 // empty contentTypes -> type fallback
    CollectionInfo byContent;
    byContent.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(c.matchesCollection(byContent));
    CollectionInfo contacts;
    contacts.type = QStringLiteral("contacts");
    QVERIFY(!c.matchesCollection(contacts));
}

void TstConduitDescriptor::todoMatching()
{
    TestConduit c(QStringLiteral("todo"));
    CollectionInfo vtodo;
    vtodo.type = QStringLiteral("calendar");
    vtodo.contentTypes = { QStringLiteral("VEVENT"), QStringLiteral("VTODO") };
    QVERIFY(c.matchesCollection(vtodo));
    CollectionInfo todos;
    todos.type = QStringLiteral("todos");
    QVERIFY(c.matchesCollection(todos));
    CollectionInfo eventsOnly;
    eventsOnly.contentTypes = { QStringLiteral("VEVENT") };
    QVERIFY(!c.matchesCollection(eventsOnly));
}

void TstConduitDescriptor::contactsMatching()
{
    TestConduit c(QStringLiteral("contacts"));
    CollectionInfo byType;  byType.type = QStringLiteral("contacts");
    QVERIFY(c.matchesCollection(byType));
    CollectionInfo byCard;  byCard.contentTypes = { QStringLiteral("VCARD") };
    QVERIFY(c.matchesCollection(byCard));
}

void TstConduitDescriptor::noteMatching()
{
    TestConduit c(QStringLiteral("note"));
    CollectionInfo memos;  memos.type = QStringLiteral("memos");
    QVERIFY(c.matchesCollection(memos));
    CollectionInfo cal;    cal.type = QStringLiteral("calendar");
    QVERIFY(!c.matchesCollection(cal));
}

void TstConduitDescriptor::genericDomainFallback()
{
    // A third-party "document" conduit matches collections typed "document"
    // without any WildPalms source change.
    TestConduit c(QStringLiteral("document"));
    CollectionInfo doc;  doc.type = QStringLiteral("document");
    QVERIFY(c.matchesCollection(doc));
    CollectionInfo cal;  cal.type = QStringLiteral("calendar");
    QVERIFY(!c.matchesCollection(cal));
}

void TstConduitDescriptor::tasksOnlyCalendarExcludedFromCalendar()
{
    // DAV providers type everything "calendar"; contentTypes are authoritative.
    TestConduit c(QStringLiteral("calendar"));
    CollectionInfo tasksOnly;
    tasksOnly.type = QStringLiteral("calendar");
    tasksOnly.contentTypes = { QStringLiteral("VTODO") };
    QVERIFY(!c.matchesCollection(tasksOnly));
}

void TstConduitDescriptor::descriptorDefaults()
{
    TestConduit c(QStringLiteral("document"));
    QCOMPARE(c.claimedDatabases(), QStringList{ QStringLiteral("TestDB") });
    QVERIFY(c.supportsCategories());
    QCOMPARE(c.categoryStore(), nullptr);
    QCOMPARE(c.categorySlotNames(), QStringList{});   // no store -> empty, no crash
    QCOMPARE(c.createConflictHandler(), nullptr);
    QVERIFY(!c.hasMainView());
    QCOMPARE(c.createMainView(nullptr), nullptr);
}

WILDPALMS_QTEST_MAIN(TstConduitDescriptor)
#include "tst_conduit_descriptor.moc"
```

- [ ] **Step 2: Register the test in CMake and verify it fails to compile**

In `tests/runtime/CMakeLists.txt`, duplicate the `tst_domainfilter` registration block, renaming to `tst_conduit_descriptor`.

Run: `make -C build -j$(($(nproc)-1)) tst_conduit_descriptor 2>&1 | grep error: | head -5`
Expected: compile errors — `conduitId`/`matchesCollection` etc. do not exist on `PimPlugin`.

- [ ] **Step 3: Extend PimPlugin**

`src/plugins/pimplugin.h` — replace the class body so it reads:

```cpp
#include "plugin.h"   // Kalburator::Plugin (libkalburator)
#include <shape.h>    // Kalburator::Shape::DomainId
#include <QStringList>
#include <memory>

namespace Kalburator::Sync { class SyncBackendBase; struct CollectionInfo; }
namespace Kalburator::Conflict { class ConflictHandler; }
namespace WildPalms::Runtime { class PalmRuntime; class PalmDeviceAccess; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
class QWidget;

namespace WildPalms::Plugins {

/**
 * @brief WP-local base for Palm conduit plugins — the CONDUIT DESCRIPTOR.
 *
 * Sub-project A (2026-06-11 config substrate): the surface the four stock
 * conduits implemented by convention is promoted to virtuals here, so
 * PalmRuntime, the wizard, and the graph enumerate conduits generically
 * instead of dynamic_casting to concrete types. A third-party conduit
 * participates everywhere by subclassing this and joining the load batch.
 */
class PimPlugin : public Kalburator::Plugin {
public:
    // ── lifecycle hooks (pre-existing) ─────────────────────────────
    virtual void setHub(Kalburator::Sync::SyncBackendBase *hub) { Q_UNUSED(hub); }
    virtual void setRuntime(WildPalms::Runtime::PalmRuntime *runtime) {
        Q_UNUSED(runtime);
    }

    // ── identity / declaration ─────────────────────────────────────
    /// Bare conduit id ("calendar", "todo", …). PERSISTED in mapping rows
    /// as sourceBackend — treat as a frozen identifier.
    virtual QString conduitId() const = 0;
    /// Hub-collection domain. Note memo's domain is "note".
    virtual Kalburator::Shape::DomainId domain() const = 0;
    virtual QString primaryDbName() const = 0;
    virtual QStringList claimedDatabases() const { return { primaryDbName() }; }
    virtual QString conduitDisplayName() const = 0;
    virtual QString conduitIconName() const { return QStringLiteral("folder-sync"); }

    // ── capabilities ───────────────────────────────────────────────
    virtual bool supportsCategories() const { return true; }
    /// Which provider collections can serve as a sync target for this
    /// conduit. Default: per-domain type/contentTypes matching (subsumes
    /// the old app/wizard/domainfilter.cpp); override for custom domains
    /// with richer source semantics.
    virtual bool matchesCollection(const Kalburator::Sync::CollectionInfo &c) const;

    // ── factories ──────────────────────────────────────────────────
    virtual std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) = 0;
    /// Default nullptr — a conduit without conflict UI is legal.
    virtual Kalburator::Conflict::ConflictHandler *createConflictHandler() { return nullptr; }
    /// Contract: non-null iff supportsCategories(). Default nullptr.
    virtual WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const { return nullptr; }
    /// 16-entry slot-name snapshot; empty if no store / not yet populated.
    virtual QStringList categorySlotNames() const;
    virtual bool hasMainView() const { return false; }
    virtual QWidget *createMainView(QWidget *parent) const { Q_UNUSED(parent); return nullptr; }
};

} // namespace WildPalms::Plugins
```

(Keep the include guard and file comment; this replaces the old minimal class.)

`src/plugins/pimplugin.cpp` (new):

```cpp
#include "pimplugin.h"

#include "palm/calendar/categorymappingstore.h"
#include <collectioninfo.h>

namespace WildPalms::Plugins {

bool PimPlugin::matchesCollection(const Kalburator::Sync::CollectionInfo &c) const
{
    const QString d = domain().toString();
    if (d == QLatin1String("calendar")) {
        // contentTypes are authoritative when reported (DAV types everything
        // "calendar", including tasks-only collections); bare type is the
        // fallback for providers that don't report components (Akonadi).
        if (!c.contentTypes.isEmpty())
            return c.contentTypes.contains(QStringLiteral("VEVENT"));
        return c.type == QLatin1String("calendar");
    }
    if (d == QLatin1String("todo"))
        return c.type == QLatin1String("todos")
            || c.contentTypes.contains(QStringLiteral("VTODO"));
    if (d == QLatin1String("contacts"))
        return c.type == QLatin1String("contacts")
            || c.contentTypes.contains(QStringLiteral("VCARD"));
    if (d == QLatin1String("note"))
        return c.type == QLatin1String("memos");
    // Generic fallback for third-party domains: match the domain name.
    return c.type == d;
}

QStringList PimPlugin::categorySlotNames() const
{
    auto *store = categoryStore();
    if (!store) return {};
    return store->sixteenSlotNames(primaryDbName());
}

} // namespace WildPalms::Plugins
```

Check the include path for `categorymappingstore.h` and `shape.h` against how other `src/` files include them (`grep -rn "categorymappingstore.h" src/runtime/routemapping.cpp src/runtime/palmruntime.cpp | head -2`) and match the existing convention.

- [ ] **Step 4: Build and run the test**

Run: `make -C build -j$(($(nproc)-1)) tst_conduit_descriptor && ctest --test-dir build -R tst_conduit_descriptor --output-on-failure`
Expected: PASS (7 cases).

Note: the four stock plugins do NOT yet override the new pure virtuals, so
`WildPalmsCore` may fail to compile if they are instantiated anywhere in this
target — they are (PalmRuntime). If the full build breaks here, proceed
immediately to Task 2; Tasks 1+2 land as one commit in that case (build the
test target only to verify Task 1's logic first).

- [ ] **Step 5: Commit (or hold for a joint commit with Task 2 if the full build requires it)**

```bash
git add src/plugins/pimplugin.h src/plugins/pimplugin.cpp src/CMakeLists.txt \
        tests/runtime/tst_conduit_descriptor.cpp tests/runtime/CMakeLists.txt
git commit -m "feat(plugins): PimPlugin becomes the conduit descriptor (substrate A1)"
```

---

## Task 2: The four stock conduits implement the descriptor

**Files (each is a git submodule — see Submodule discipline above):**
- Modify: `src/plugins/calendar/calendarbackendplugin.h` (+ `.cpp` if return type lives there)
- Modify: `src/plugins/contacts/contactsbackendplugin.h` (+ `.cpp`)
- Modify: `src/plugins/memo/memobackendplugin.h` (+ `.cpp`)
- Modify: `src/plugins/todos/todobackendplugin.h` (+ `.cpp`)

- [ ] **Step 1: Read the icon map so descriptor icons match today's UI**

Run: `grep -n -B2 -A8 "kIcons" src/runtime/palmruntime.cpp`
Note the icon name per conduit id; each plugin's `conduitIconName()` override below returns its value verbatim.

- [ ] **Step 2: Add descriptor overrides to each plugin**

For **calendar** (`src/plugins/calendar/calendarbackendplugin.h`), inside the class, next to the existing convention methods:

```cpp
    // ── Conduit descriptor (PimPlugin virtuals, substrate A1) ──────
    QString conduitId() const override { return pluginId(); }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{QStringLiteral("calendar")}; }
    QString conduitDisplayName() const override { return displayName(); }
    QString conduitIconName() const override
    { return QStringLiteral(/* value from kIcons["calendar"] */); }
```

and mark the existing methods as overrides (signatures must match the base
exactly — `const` included):

```cpp
    QString primaryDbName() const override { return QStringLiteral("DatebookDB"); }
    QStringList claimedDatabases() const override { return {QStringLiteral("DatebookDB")}; }
    QStringList categorySlotNames() const override;
    WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const override;
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>          // ← return type WIDENED
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *device) override;
    Kalburator::Conflict::ConflictHandler *createConflictHandler() override;
    bool     hasMainView()   const override;
    QWidget *createMainView(QWidget *parent) const override;
```

The `createPalmBackend` return type changes from
`std::unique_ptr<Kalburator::Sync::SyncBackend>` to
`std::unique_ptr<Kalburator::Sync::SyncBackendBase>` (unique_ptr is not
covariant). Update the matching `.cpp` definition's return type; the body
(`return std::make_unique<PalmCalendarBackend>(...)`) converts implicitly.
If submodule-local tests call `createPalmBackend` and use SyncBackend-only
API on the result, adapt them with a `dynamic_cast` at the call site.

Repeat for **contacts** (domain `"contacts"`, db `AddressDB`), **memo**
(domain `"note"`, db `MemoDB` — memo has no `createConflictHandler`; the
base default covers it), **todos** (domain `"todo"`, db `ToDoDB`).

- [ ] **Step 3: Build everything and run the full suite**

Run: `make -C build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: clean build; 100% tests pass (the runtime still uses the cast
chains — they keep working because the concrete types are unchanged).

- [ ] **Step 4: Commit each submodule, push, bump gitlinks**

```bash
for sub in calendar contacts memo todos; do
  git -C src/plugins/$sub add -A
  git -C src/plugins/$sub commit -m "feat: implement PimPlugin conduit descriptor (WP substrate A1)"
  git -C src/plugins/$sub push
done
git add src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
# If Task 1 was held: also add Task 1's files here.
git commit -m "feat(conduits): stock plugins implement the conduit descriptor"
```

---

## Task 3: PalmRuntime enumerates descriptors (kill the cast chains)

**Files:**
- Modify: `src/runtime/palmruntime.h` (add `conduits()` accessor)
- Modify: `src/runtime/palmruntime.cpp` (sites listed below)

- [ ] **Step 1: Add the accessor**

`palmruntime.h`, public section:

```cpp
    /// All loaded conduit plugins, as descriptors (substrate A1). Stable for
    /// the lifetime of this PalmRuntime. Used by finishConnect, route
    /// translation, and (later) the wizard/graph surfaces.
    QList<WildPalms::Plugins::PimPlugin*> conduits() const;
```

`palmruntime.cpp`:

```cpp
QList<WildPalms::Plugins::PimPlugin*> PalmRuntime::conduits() const
{
    QList<WildPalms::Plugins::PimPlugin*> out;
    for (const auto &p : m_palmPlugins)
        if (auto *c = dynamic_cast<WildPalms::Plugins::PimPlugin*>(p.get()))
            out.append(c);
    return out;
}
```

(One residual dynamic_cast — to the *base* — replaces five chains to four
concrete types. `#include "plugins/pimplugin.h"` is already present for the
setHub dispatch.)

- [ ] **Step 2: Replace the five sites + two tables, one at a time, building between each**

1. **finishConnect backend/snapshot dispatch** (the two cast-chain blocks
   shown at `palmruntime.cpp:494-556`) becomes:

```cpp
    for (auto *c : conduits()) {
        const QString id = c->conduitId();
        std::unique_ptr<Kalburator::Sync::SyncBackendBase> ownedBackend =
            c->createPalmBackend(m_device.get());
        if (!ownedBackend) {
            qWarning() << "[PalmRuntime::finishConnect] Plugin" << id
                       << "returned null backend";
            continue;
        }
        if (m_profile && c->supportsCategories()) {
            const QStringList slotNames = c->categorySlotNames();
            if (slotNames.size() == 16)
                m_profile->setCategorySlotNames(c->primaryDbName(), slotNames);
        }
        m_registry->registerBackendInstance(id, ownedBackend.get());
        m_ownedBackends.push_back(std::move(ownedBackend));
        qDebug() << "[PalmRuntime::finishConnect] Registered backend plugin:" << id;
    }
```

   Delete the four `using namespace WildPalms::...Plugin;` lines that only
   served the casts (where no other use remains).

2. **Conflict-handler dispatch**: find it with
   `grep -n "createConflictHandler" src/runtime/palmruntime.cpp` and replace
   its cast chain with:

```cpp
    for (auto *c : conduits()) {
        if (auto *handler = c->createConflictHandler()) {
            /* keep the existing registration code for `handler`, keyed by
               c->conduitId() exactly as the old chain keyed it */
        }
    }
```

3. **Tickle palm-id collection** (`palmruntime.cpp:184-199`):

```cpp
        QSet<QString> palmIds;
        for (auto *c : conduits())
            palmIds.insert(c->conduitId());
```

4. **`kPalmBackendIds`** (line ~71): delete the array; replace its uses
   (`grep -n "kPalmBackendIds" src/runtime/palmruntime.cpp`) with a helper:

```cpp
bool PalmRuntime::isPalmConduitBackendId(const QString &backendId) const
{
    for (auto *c : conduits())
        if (c->conduitId() == backendId) return true;
    return false;
}
```

   (declare in `palmruntime.h` next to `conduits()`).

5. **`ensureHubCollections`** (lines 320-341): replace the `domains[]` table:

```cpp
    for (auto *c : conduits()) {
        const QString dom = c->domain().toString();
        Kalburator::Sync::CollectionInfo info;
        info.id   = dom;
        info.name = dom;
        info.type = dom;
        m_hub->createCollection(
            info,
            Shape{ DomainId{dom}, EncodingId{QStringLiteral("canon")} });
    }
```

   **Ordering check:** `ensureHubCollections` must run after
   `registerPalmPlugins()` populates `m_palmPlugins`. Verify with
   `grep -n "ensureHubCollections\|registerPalmPlugins" src/runtime/palmruntime.cpp`
   — if the hub-collection call currently precedes plugin registration in the
   ctor, move it after.

6. **Default palm↔hub LC wiring** (the `wiring[]` table at lines ~565-590):

```cpp
    for (auto *c : conduits()) {
        const QString palmId  = c->conduitId();
        const QString hubCol  = c->domain().toString();
        const QString palmCol = QStringLiteral("palm:") + hubCol;
        if (!m_registry->backendInstance(palmId)) continue;
        /* keep the existing LogicalCalendar construction verbatim, fed by
           palmId/hubCol/palmCol */
    }
```

7. **`resolveMappingIdentity`** (lines ~810-830): replace the chain with:

```cpp
        for (auto *c : conduits()) {
            if (c->conduitId() == m.sourceBackend) {
                outLabel = c->conduitDisplayName();
                outIconName = c->conduitIconName();
                break;
            }
        }
```

   Then delete the now-unused `kIcons` hash if nothing else references it
   (`grep -n "kIcons" src/runtime/palmruntime.cpp`).

- [ ] **Step 3: Full build + full suite**

Run: `make -C build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: 100% pass — this refactor is behavior-preserving; the route/clobber/
e2e tests are the regression net.

- [ ] **Step 4: Commit**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp
git commit -m "refactor(runtime): PalmRuntime enumerates conduit descriptors (substrate A1)"
```

---

## Task 4: Descriptor-driven route translation with names-first rows and status

**Files:**
- Modify: `src/runtime/routemapping.h`, `src/runtime/routemapping.cpp`
- Modify: `src/runtime/palmruntime.h`, `src/runtime/palmruntime.cpp` (buildRouteLogicalCalendars, status storage)
- Create: `tests/runtime/tst_routemapping.cpp` (+ register in `tests/runtime/CMakeLists.txt`)

Row form changes from `"palm:<domain>/<slot>"` to `"palm:<domain>/name:<categoryName>"`.
No migration (user decision: no back-compat); profiles are recreated.

- [ ] **Step 1: Write the failing tests**

`tests/runtime/tst_routemapping.cpp`:

```cpp
// Substrate A3: names-first route translation + per-route status.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "runtime/routemapping.h"
#include "plugins/pimplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include <synctypes.h>

using namespace WildPalms::Runtime;
using Kalburator::Sync::SyncMapping;

namespace {
class StubConduit : public WildPalms::Plugins::PimPlugin {
public:
    StubConduit(QString id, QString domain, QString db)
        : m_id(std::move(id)), m_domain(std::move(domain)), m_db(std::move(db)) {}
    QString conduitId() const override { return m_id; }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{m_domain}; }
    QString primaryDbName() const override { return m_db; }
    QString conduitDisplayName() const override { return m_id; }
    WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const override
    { return &m_store; }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
    mutable WildPalms::PalmCalendar::CategoryMappingStore m_store;
private:
    QString m_id, m_domain, m_db;
};

SyncMapping row(const QString &src, const QString &srcCal)
{
    SyncMapping m;
    m.id = QStringLiteral("m1");
    m.sourceBackend = src;
    m.sourceCalendar = srcCal;
    m.targetBackend = QStringLiteral("acc:col");
    m.targetCalendar = QStringLiteral("col");
    m.enabled = true;
    return m;
}
} // namespace

class TstRouteMapping : public QObject {
    Q_OBJECT
private slots:
    void directRouteIsActive();
    void namedRouteResolvesAgainstStore();
    void namedRouteWaitsBeforeDevice();
    void namedRouteReportsNoFreeSlot();
    void unknownConduitIsNotARoute();
    void disabledRowIsNotARoute();
};

void TstRouteMapping::directRouteIsActive()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(row(QStringLiteral("calendar"), QString()),
                                      { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->kind, RouteSpec::Kind::Direct);
    QCOMPARE(t.spec->domain, QStringLiteral("calendar"));
}

void TstRouteMapping::namedRouteResolvesAgainstStore()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    cal.m_store.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->kind, RouteSpec::Kind::Filtered);
    QCOMPARE(t.spec->categoryName, QStringLiteral("Work"));
}

void TstRouteMapping::namedRouteWaitsBeforeDevice()
{
    // Store has never been populated (no slot names at all): the route still
    // produces a spec (hub<->remote filtering works by name), with status
    // WaitingForDevice instead of today's silent drop.
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::WaitingForDevice);
    QCOMPARE(t.spec->categoryName, QStringLiteral("Work"));
}

void TstRouteMapping::namedRouteReportsNoFreeSlot()
{
    // Store populated (device seen) but the name is absent: the reconciler
    // could not place it (table full) — surface NoFreeSlot, still produce
    // the spec so hub<->remote continues to flow.
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    for (int i = 1; i <= 15; ++i)
        cal.m_store.setSlotName(QStringLiteral("DatebookDB"), i,
                                QStringLiteral("Cat%1").arg(i));
    const auto t = translateRouteSpec(
        row(QStringLiteral("calendar"), QStringLiteral("palm:calendar/name:Work")),
        { &cal });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::NoFreeSlot);
}

void TstRouteMapping::unknownConduitIsNotARoute()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    const auto t = translateRouteSpec(row(QStringLiteral("plucker"), QString()),
                                      { &cal });
    QVERIFY(!t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::NotARoute);
}

void TstRouteMapping::disabledRowIsNotARoute()
{
    StubConduit cal(QStringLiteral("calendar"), QStringLiteral("calendar"),
                    QStringLiteral("DatebookDB"));
    auto m = row(QStringLiteral("calendar"), QString());
    m.enabled = false;
    const auto t = translateRouteSpec(m, { &cal });
    QVERIFY(!t.spec.has_value());
}

WILDPALMS_QTEST_MAIN(TstRouteMapping)
#include "tst_routemapping.moc"
```

If `CategoryMappingStore` lacks a public default ctor or `setSlotName` differs,
check `src/palm/calendar/categorymappingstore.h` and adjust the fixture (the
API surveyed: `setSlotName(dbName, slot, name)`, `slotForName(dbName, name)`).

- [ ] **Step 2: Verify it fails to compile**

Run: `make -C build -j$(($(nproc)-1)) tst_routemapping 2>&1 | grep error: | head -5`
Expected: errors — `RouteStatus`, `RouteTranslation`, and the new
`translateRouteSpec` signature do not exist.

- [ ] **Step 3: Implement the new routemapping API**

`src/runtime/routemapping.h` — replace the translate declaration (RouteSpec
struct itself is unchanged):

```cpp
#include <optional>
#include <QList>
namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Runtime {

enum class RouteStatus {
    Active,            ///< runnable; category (if any) bound on-device
    WaitingForDevice,  ///< named category; no device snapshot yet
    NoFreeSlot,        ///< named category; device table full, not placed
    NotARoute,         ///< disabled, unknown conduit, or malformed row
};

struct RouteTranslation {
    std::optional<RouteSpec> spec;     ///< set whenever the row is well-formed
    RouteStatus status = RouteStatus::NotARoute;
    QString categoryName;              ///< parsed name for Filtered rows
};

/// Translate one persisted row against the registered conduit descriptors.
/// Category rows use the names-first form "palm:<domain>/name:<categoryName>"
/// (substrate A3). Never silently drops a well-formed row: the status says
/// why a route is not (yet) fully bound.
RouteTranslation translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QList<WildPalms::Plugins::PimPlugin*> &conduits);

} // namespace WildPalms::Runtime
```

`src/runtime/routemapping.cpp` — replace `domainForPalmPluginId` + the old
function with:

```cpp
#include "routemapping.h"
#include "plugins/pimplugin.h"
#include "palm/calendar/categorymappingstore.h"
#include <synctypes.h>

namespace WildPalms::Runtime {

RouteTranslation translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QList<WildPalms::Plugins::PimPlugin*> &conduits)
{
    RouteTranslation out;
    if (!p.enabled) return out;

    WildPalms::Plugins::PimPlugin *conduit = nullptr;
    for (auto *c : conduits)
        if (c->conduitId() == p.sourceBackend) { conduit = c; break; }
    if (!conduit) return out;

    const QString domain = conduit->domain().toString();

    RouteSpec spec;
    spec.domain             = domain;
    spec.hubCollectionId    = domain;
    spec.remoteBackendId    = p.targetBackend;
    spec.remoteCollectionId = p.targetCalendar;
    spec.lcId               = QStringLiteral("wp-route-") + p.id;

    if (p.sourceCalendar.isEmpty()) {
        spec.kind         = RouteSpec::Kind::Direct;
        spec.categoryName = QString();
        out.spec   = spec;
        out.status = RouteStatus::Active;
        return out;
    }

    const QString prefix =
        QStringLiteral("palm:") + domain + QStringLiteral("/name:");
    if (!p.sourceCalendar.startsWith(prefix)) return out;   // malformed
    const QString name = p.sourceCalendar.mid(prefix.size());
    if (name.isEmpty()) return out;

    spec.kind         = RouteSpec::Kind::Filtered;
    spec.categoryName = name;
    out.spec          = spec;
    out.categoryName  = name;

    // Status from the conduit's reconciled category store. The route is
    // produced regardless — hub<->remote filtering works by name; the
    // status reports the device-side binding state.
    auto *store = conduit->categoryStore();
    const QString db = conduit->primaryDbName();
    if (!store || store->populatedSlots(db).isEmpty()) {
        out.status = RouteStatus::WaitingForDevice;
    } else if (store->slotForName(db, name) >= 0) {
        out.status = RouteStatus::Active;
    } else {
        out.status = RouteStatus::NoFreeSlot;
    }
    return out;
}

} // namespace WildPalms::Runtime
```

(Check `populatedSlots`'s exact name/signature in
`src/palm/calendar/categorymappingstore.h`; the survey lists
`QList<int> populatedSlots(const QString &dbName)`.)

- [ ] **Step 4: Update buildRouteLogicalCalendars + status surface**

`palmruntime.h`:

```cpp
    /// Per-mapping route status from the last buildRouteLogicalCalendars
    /// (substrate A3 — replaces the old silent drop of unresolved routes).
    QHash<QString, WildPalms::Runtime::RouteStatus> routeStatuses() const
    { return m_routeStatuses; }
signals:
    void routeStatusesChanged();
private:
    QHash<QString, WildPalms::Runtime::RouteStatus> m_routeStatuses;
```

`palmruntime.cpp` `buildRouteLogicalCalendars`: delete the `stores` hash
construction; the loop becomes:

```cpp
    m_routeStatuses.clear();
    const auto cs = conduits();
    for (const auto &persisted : m_mappings) {
        const auto t = WildPalms::Runtime::translateRouteSpec(persisted, cs);
        if (t.status != RouteStatus::NotARoute)
            m_routeStatuses.insert(persisted.id, t.status);
        if (!t.spec) continue;
        const auto &s = *t.spec;
        /* existing body unchanged from here: Filtered -> FCB + register,
           Direct -> wp-hub primary; LC construction identical */
    }
    Q_EMIT routeStatusesChanged();
```

- [ ] **Step 5: Update the graph view's row writer/reader to the name form**

`src/app/mapping/syncmappingsgraphview.cpp`:

- `palmCollectionIdForSlot(dbName, slot)` becomes name-based; the graph
  already holds the slot-name snapshot it renders, so:

```cpp
QString SyncMappingGraphView::palmCollectionIdForSlot(const QString &dbName, int slot)
{
    // Substrate A3: rows carry category NAMES. Slot 0 (Unfiled) and
    // whole-domain edges keep the empty sourceCalendar (Direct route).
    const QString domain = m_domainForDb.value(dbName);
    const QString name   = slotNameForRender(dbName, slot);  // existing snapshot accessor
    if (slot == 0 || name.isEmpty() || domain.isEmpty())
        return QString();
    return QStringLiteral("palm:%1/name:%2").arg(domain, name);
}
```

  `m_domainForDb` is a new `QHash<QString,QString>` member with setter
  `setDomainForDb(...)`; `SyncMappingsPage::reloadGraph()` fills it from
  `palmRuntime->conduits()` (`primaryDbName() -> domain().toString()`).
  Locate the snapshot accessor the view already uses for slot labels
  (`grep -n "slotName\|m_snapshot" src/app/mapping/syncmappingsgraphview.cpp | head`)
  and reuse it; adjust the name above to the real one.

- Edge reconstruction (the `palmCollectionIdForSlot(db, 0).chopped(1)` prefix
  parse around line 172): parse the `name:` form instead — extract the name,
  look up its slot in the snapshot for positioning; an edge whose name has no
  slot renders attached to the DB header with a "pending" visual state
  (re-use the disabled style; full status badges are sub-project C).

This fixes a latent bug: the old writer emitted `palm:contact/<n>` and
`palm:memo/<n>` while translateRouteSpec expected `palm:contacts/…` and
`palm:note/…` — graph-created contact/memo slot routes never translated.
Domain now comes from the descriptor on both sides.

- [ ] **Step 6: Build, run new + full tests**

Run: `make -C build -j$(($(nproc)-1)) && ctest --test-dir build -R tst_routemapping --output-on-failure && ctest --test-dir build -j$(($(nproc)-1))`
Expected: tst_routemapping 6/6; full suite green. If `tst_palm_runtime_route_*`
fixtures seed slot-form rows (`palm:calendar/3`), update those fixtures to the
name form and seed the matching store/snapshot names — the tests' assertions
about routing behavior stay identical.

- [ ] **Step 7: Commit**

```bash
git add src/runtime/routemapping.h src/runtime/routemapping.cpp \
        src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        src/app/mapping/ tests/runtime/
git commit -m "feat(routes): names-first category rows + per-route status (substrate A3)"
```

---

## Task 5: Wizard and main window enumerate conduits; domainfilter dies

**Files:**
- Create: `src/runtime/conduitcatalog.h`, `src/runtime/conduitcatalog.cpp` (+ CMake)
- Modify: `src/runtime/palmruntime.cpp` (registerPalmPlugins uses the factory)
- Modify: `src/app/wizard/newprofilewizard.h/.cpp`, `targetpickerpage.h/.cpp`, `targetpickerrow.h/.cpp`
- Delete: `src/app/wizard/domainfilter.h`, `src/app/wizard/domainfilter.cpp`, `tests/runtime/tst_domainfilter.cpp` (cases live in tst_conduit_descriptor since Task 1)
- Modify: `src/kf6/kf6mainwindow.cpp` (main-view registration)
- Modify: `src/CMakeLists.txt`, `src/app/wizard/CMakeLists.txt` (if present), `tests/runtime/CMakeLists.txt`

- [ ] **Step 1: Extract the stock-conduit factory**

`src/runtime/conduitcatalog.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_CONDUITCATALOG_H
#define WILDPALMS_RUNTIME_CONDUITCATALOG_H

#include <memory>
#include <vector>

namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Runtime {

/// Fresh instances of the stock conduit plugins. The single source of truth
/// for "which conduits exist": PalmRuntime::registerPalmPlugins() loads
/// these into its batch, and the wizard owns a transient set purely for
/// descriptor queries (matchesCollection, display names) — descriptor
/// methods are const and need no device or hub.
std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> createStockConduits();

} // namespace WildPalms::Runtime
#endif
```

`src/runtime/conduitcatalog.cpp`:

```cpp
#include "conduitcatalog.h"

#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"

namespace WildPalms::Runtime {

std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> createStockConduits()
{
    std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> out;
    out.push_back(std::make_unique<WildPalms::CalendarPlugin::CalendarBackendPlugin>());
    out.push_back(std::make_unique<WildPalms::ContactsPlugin::ContactsBackendPlugin>());
    out.push_back(std::make_unique<WildPalms::Memo::MemoPlugin>());
    out.push_back(std::make_unique<WildPalms::TodoPlugin::TodoBackendPlugin>());
    return out;
}

} // namespace WildPalms::Runtime
```

(Match include paths to how `palmruntime.cpp` includes the plugin headers.)

`registerPalmPlugins()` consumes it — replace the four `make_unique` lines and
the four manifest entries:

```cpp
    auto conduitPlugins = createStockConduits();
    QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> items{
        /* the six stock kalburator entries, unchanged */
    };
    for (const auto &c : conduitPlugins)
        items.append({ c.get(),
            mkPalmManifest(QStringLiteral("wildpalms.") + c->conduitId(),
                           c->domain().toString()) });
    ...
    for (auto &c : conduitPlugins)
        m_palmPlugins.push_back(std::move(c));
```

Note: `mkPalmManifest("wildpalms.memo", "note")` is exactly what the old table
produced for memo — `conduitId()`/`domain()` reproduce all four rows.

- [ ] **Step 2: Wizard enumerates conduits**

`newprofilewizard.h`: add member
`std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> m_conduits;`
and accessor `const std::vector<...>& conduits() const`.

`newprofilewizard.cpp` ctor: replace the 4-string seeding loop:

```cpp
    m_conduits = WildPalms::Runtime::createStockConduits();
    for (const auto &c : m_conduits) {
        MappingSpec s;
        s.pluginId = c->conduitId();
        s.kind     = TargetKind::RawFiles;
        m_state.mappings.append(s);
    }
```

`TargetPickerPage`: ctor gains
`const std::vector<std::unique_ptr<PimPlugin>> *conduits` (passed by the
wizard); `buildRows()` iterates it instead of the literal list, passing the
`PimPlugin*` down to each `TargetPickerRow`.

`TargetPickerRow`: ctor takes `const WildPalms::Plugins::PimPlugin *conduit`
instead of the bare `pluginId` string (keep the id available via
`conduit->conduitId()`); the filter call site changes from

```cpp
            if (!collectionMatchesDomain(c, m_pluginId)) continue;
```
to
```cpp
            if (!m_conduit->matchesCollection(c)) continue;
```

Row labels: use `conduit->conduitDisplayName()` where the row currently
derives a label from the plugin id (check `grep -n "m_pluginId" src/app/wizard/targetpickerrow.cpp`).

- [ ] **Step 3: Delete domainfilter, move stragglers**

```bash
git rm src/app/wizard/domainfilter.h src/app/wizard/domainfilter.cpp \
       tests/runtime/tst_domainfilter.cpp
```
Remove both from their CMakeLists. `grep -rn "domainfilter\|collectionMatchesDomain" src/ tests/`
must come back empty.

- [ ] **Step 4: Main-view registration via descriptors**

`kf6mainwindow.cpp` (lines ~675-683): replace the cast chain:

```cpp
    for (auto *c : m_palmRuntime->conduits()) {
        if (!c->hasMainView()) continue;
        QWidget *view = c->createMainView(/* keep the existing parent arg */);
        /* keep the existing KPageWidget registration code, labeled with
           c->conduitDisplayName() and c->conduitIconName() */
    }
```

- [ ] **Step 5: Build + full suite + wizard tests**

Run: `make -C build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))`
Expected: green. `tst_targetpickerpage`/`tst_reviewpage`/`tst_kf6mainwindow_newprofile`
exercise the new enumeration path; fix compile fallout in their fixtures
(constructor signature changes) without changing what they assert.

- [ ] **Step 6: Commit**

```bash
git add -A ':!docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md'
git commit -m "refactor(wizard,kf6): enumerate conduit descriptors; delete domainfilter (substrate A1)"
```

---

## Task 6: Profile persists desired categories + initial-sync flag

**Files:**
- Modify: `src/profile.h`, `src/profile.cpp`
- Modify: `tests/test_profile.cpp` (add cases; find its CMake registration — already wired)

- [ ] **Step 1: Write the failing tests** (append to `tests/test_profile.cpp`, matching its existing style — check the file's fixture conventions first):

```cpp
void TestProfile::desiredCategoryNames_roundTrip()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QCOMPARE(p.desiredCategoryNames(QStringLiteral("DatebookDB")), QStringList{});

    const QStringList names{ QStringLiteral("Work"), QStringLiteral("Personal") };
    p.setDesiredCategoryNames(QStringLiteral("DatebookDB"), names);

    Profile reloaded(dir.path());
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.desiredCategoryNames(QStringLiteral("DatebookDB")), names);
}

void TestProfile::desiredCategoryNames_capsAtFifteen()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QStringList sixteen;
    for (int i = 0; i < 16; ++i) sixteen << QStringLiteral("C%1").arg(i);
    p.setDesiredCategoryNames(QStringLiteral("DatebookDB"), sixteen);
    QCOMPARE(p.desiredCategoryNames(QStringLiteral("DatebookDB")).size(), 15);
}

void TestProfile::initialSyncPending_roundTrip()
{
    QTemporaryDir dir;
    Profile p(dir.path());
    p.initialize();
    QVERIFY(!p.initialSyncPending());           // default false
    p.setInitialSyncPending(true);
    Profile reloaded(dir.path());
    QVERIFY(reloaded.load());
    QVERIFY(reloaded.initialSyncPending());
}
```

(Register the three slots in the test class declaration.)

- [ ] **Step 2: Run to verify failure** — `make -C build -j$(($(nproc)-1)) test_profile 2>&1 | grep -c error:` → nonzero (methods missing).

- [ ] **Step 3: Implement** in `profile.h` (next to `categorySlotNames`):

```cpp
    /// Substrate A3/A4: the category names configuration WANTS on the device
    /// for this database (<=15; Unfiled implicit). The reconciler binds them
    /// to slots at device connect, writing AppInfo for missing ones.
    QStringList desiredCategoryNames(const QString &dbName) const;
    void setDesiredCategoryNames(const QString &dbName, const QStringList &names);

    /// Substrate A4: set by the wizard (sub-project B) when a freshly created
    /// profile's first sync must clobber the Palm; cleared after it runs.
    bool initialSyncPending() const;
    void setInitialSyncPending(bool pending);
```

`profile.cpp` — follow the exact pattern of `categorySlotNames`/
`setCategorySlotNames` (same QSettings object, same sync-on-write behavior;
read that implementation first at `profile.cpp:413-430`):

```cpp
QStringList Profile::desiredCategoryNames(const QString &dbName) const
{
    /* same settings handle as categorySlotNames */
    settings.beginGroup(QStringLiteral("desiredCategories/") + dbName);
    const QStringList names = settings.value(QStringLiteral("names")).toStringList();
    settings.endGroup();
    return names;
}

void Profile::setDesiredCategoryNames(const QString &dbName, const QStringList &names)
{
    QStringList capped = names;
    while (capped.size() > 15) capped.removeLast();
    settings.beginGroup(QStringLiteral("desiredCategories/") + dbName);
    settings.setValue(QStringLiteral("names"), capped);
    settings.endGroup();
    settings.sync();
}

bool Profile::initialSyncPending() const
{
    return settings.value(QStringLiteral("initialSync/pending"), false).toBool();
}

void Profile::setInitialSyncPending(bool pending)
{
    settings.setValue(QStringLiteral("initialSync/pending"), pending);
    settings.sync();
}
```

- [ ] **Step 4: Run** — `ctest --test-dir build -R test_profile --output-on-failure` → PASS.

- [ ] **Step 5: Commit** — `git add src/profile.h src/profile.cpp tests/test_profile.cpp && git commit -m "feat(profile): desired category tables + initialSyncPending (substrate A4)"`

---

## Task 7: Device-level AppInfo write

**Files:**
- Modify: `src/runtime/palmdeviceaccess.h`, `src/runtime/palmdeviceaccess.cpp`
- Modify: the impl class behind `m_impl->readAppBlock(dbName)` — find it:
  `grep -rn "QByteArray readAppBlock(const QString" src/palm/` (expected:
  `KPilotLink` or `KPilotDeviceLink`); add the write counterpart there.

- [ ] **Step 1: Locate the read path end-to-end**

Run: `grep -rn "readAppBlock" src/palm/*.h src/palm/*.cpp src/runtime/palmdeviceaccess.* | head -20`
Map: `PalmDeviceAccess::readAppBlock(dbName)` → marshaled → impl's
`readAppBlock(dbName)` → `openDatabase` + `dlp_ReadAppBlock` + `closeDatabase`.
The interface-level `writeAppBlock(int dbHandle, const unsigned char*, size_t)`
already exists (`kpilotlink.h:71`).

- [ ] **Step 2: Add the dbName-level write to the impl class**, mirroring its
dbName-level read (same open-mode handling but read-write, same error logging):

```cpp
bool KPilotLink::writeAppBlock(const QString &dbName, const QByteArray &block)
{
    int handle = -1;
    if (!openDatabase(dbName, /*readWrite=*/true, &handle))   // match the real
        return false;                                          // open API here
    const bool ok = writeAppBlock(handle,
        reinterpret_cast<const unsigned char *>(block.constData()),
        static_cast<size_t>(block.size()));
    closeDatabase(handle);
    return ok;
}
```

(Adapt the open/close calls to the class's actual API — copy the body shape of
its `readAppBlock(const QString&)` and invert the data direction.)

- [ ] **Step 3: Add the marshaled wrapper** on `PalmDeviceAccess`, copying the
`readAppBlock` marshaling at `palmdeviceaccess.cpp:394-399`:

```cpp
bool PalmDeviceAccess::writeAppBlock(const QString &dbName, const QByteArray &block)
{
    bool result = false;
    /* same BlockingQueuedConnection invoke pattern as readAppBlock: */
    runOnLinkThread([this, &dbName, &block, &result]() {
        result = m_impl->writeAppBlock(dbName, block);
    });
    return result;
}
```

(`runOnLinkThread` stands for whatever invoke helper `readAppBlock` uses —
copy it exactly.)

- [ ] **Step 4: Build** — `make -C build -j$(($(nproc)-1))` → clean. No new test
binary here: device I/O is exercised through the reconciler integration
(Task 9) with a fake, and on hardware later (hardware-verification queue).

- [ ] **Step 5: Commit** — `git add src/palm/ src/runtime/palmdeviceaccess.* && git commit -m "feat(palm): dbName-level writeAppBlock through the link thread (substrate A3)"`

---

## Task 8: CategoryReconciler (pure logic)

**Files:**
- Create: `src/runtime/categoryreconciler.h`, `src/runtime/categoryreconciler.cpp` (+ add to `src/CMakeLists.txt`)
- Create: `tests/runtime/tst_category_reconciler.cpp` (+ CMake)

- [ ] **Step 1: Write the failing tests**

```cpp
// Substrate A3: pure category reconciliation over AppInfo bytes.
#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"

#include "runtime/categoryreconciler.h"
#include "palm/categoryinfo.h"

using WildPalms::Runtime::reconcileCategories;

namespace {
// Build a synthetic AppInfo block: category region with the given names,
// followed by `tailBytes` of app-specific data that MUST survive untouched.
QByteArray makeAppInfoBlock(const QStringList &names, int tailBytes)
{
    CategoryInfo ci;
    // A zeroed table parses as empty; round-trip through pack to get valid
    // wire bytes. Slot 0 is conventionally "Unfiled".
    QByteArray zeroed(276, '\0');
    ci.parse(reinterpret_cast<const unsigned char *>(zeroed.constData()),
             static_cast<size_t>(zeroed.size()));
    ci.setCategory(0, QStringLiteral("Unfiled"));
    int slot = 1;
    for (const QString &n : names) ci.setCategory(slot++, n);
    QByteArray block(static_cast<int>(ci.packSize()) + tailBytes, '\0');
    ci.pack(reinterpret_cast<unsigned char *>(block.data()), ci.packSize());
    for (int i = static_cast<int>(ci.packSize()); i < block.size(); ++i)
        block[i] = static_cast<char>(0xAB);   // sentinel tail
    return block;
}
} // namespace

class TstCategoryReconciler : public QObject {
    Q_OBJECT
private slots:
    void existingNamesBindWithoutWrite();
    void missingNamesClaimSlotsAndRewrite();
    void caseInsensitiveMatch();
    void fullTableReportsNoFreeSlot();
    void appSpecificTailPreserved();
};

void TstCategoryReconciler::existingNamesBindWithoutWrite()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("Work") }, 4);
    const auto r = reconcileCategories(block, { QStringLiteral("Work") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QVERIFY(r.updatedAppInfoBlock.isEmpty());     // nothing to write
    QVERIFY(r.noFreeSlot.isEmpty());
}

void TstCategoryReconciler::missingNamesClaimSlotsAndRewrite()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("Work") }, 4);
    const auto r = reconcileCategories(block,
        { QStringLiteral("Work"), QStringLiteral("Errands") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QCOMPARE(r.bound.value(QStringLiteral("Errands")), 2);
    QVERIFY(!r.updatedAppInfoBlock.isEmpty());
    // Round-trip: the written block parses and contains the new name.
    CategoryInfo check;
    QVERIFY(check.parse(
        reinterpret_cast<const unsigned char *>(r.updatedAppInfoBlock.constData()),
        static_cast<size_t>(r.updatedAppInfoBlock.size())));
    QCOMPARE(check.categoryName(2), QStringLiteral("Errands"));
}

void TstCategoryReconciler::caseInsensitiveMatch()
{
    const auto block = makeAppInfoBlock({ QStringLiteral("work") }, 0);
    const auto r = reconcileCategories(block, { QStringLiteral("Work") });
    QCOMPARE(r.bound.value(QStringLiteral("Work")), 1);
    QVERIFY(r.updatedAppInfoBlock.isEmpty());
}

void TstCategoryReconciler::fullTableReportsNoFreeSlot()
{
    QStringList fifteen;
    for (int i = 1; i <= 15; ++i) fifteen << QStringLiteral("Cat%1").arg(i);
    const auto block = makeAppInfoBlock(fifteen, 0);
    const auto r = reconcileCategories(block, { QStringLiteral("Overflow") });
    QVERIFY(r.bound.isEmpty() || !r.bound.contains(QStringLiteral("Overflow")));
    QCOMPARE(r.noFreeSlot, QStringList{ QStringLiteral("Overflow") });
    QVERIFY(r.updatedAppInfoBlock.isEmpty());
}

void TstCategoryReconciler::appSpecificTailPreserved()
{
    const auto block = makeAppInfoBlock({}, 8);   // 8 sentinel bytes after categories
    const auto r = reconcileCategories(block, { QStringLiteral("New") });
    QVERIFY(!r.updatedAppInfoBlock.isEmpty());
    QCOMPARE(r.updatedAppInfoBlock.size(), block.size());
    QCOMPARE(r.updatedAppInfoBlock.right(8), block.right(8));   // tail untouched
}

WILDPALMS_QTEST_MAIN(TstCategoryReconciler)
#include "tst_category_reconciler.moc"
```

- [ ] **Step 2: Verify compile failure** — `make -C build ... tst_category_reconciler 2>&1 | grep error: | head -3` → reconciler missing.

- [ ] **Step 3: Implement**

`src/runtime/categoryreconciler.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_CATEGORYRECONCILER_H
#define WILDPALMS_RUNTIME_CATEGORYRECONCILER_H

#include <QByteArray>
#include <QHash>
#include <QStringList>

namespace WildPalms::Runtime {

struct CategoryReconcileResult {
    QHash<QString, int> bound;       ///< desired name -> device slot
    QStringList noFreeSlot;          ///< names that could not be placed
    QByteArray updatedAppInfoBlock;  ///< full block to write back; empty = no write
};

/// Pure function over AppInfo bytes (substrate A3). Matches desired names
/// case-insensitively against the device category table; claims free slots
/// for missing names. Device categories NOT in the desired set are left
/// alone (the wizard's clobber path replaces the table wholesale — that is
/// sub-project B, not this function). The category region occupies the
/// first packSize() bytes; any app-specific tail is preserved verbatim.
CategoryReconcileResult reconcileCategories(const QByteArray &appInfoBlock,
                                            const QStringList &desiredNames);

} // namespace WildPalms::Runtime
#endif
```

`src/runtime/categoryreconciler.cpp`:

```cpp
#include "categoryreconciler.h"
#include "palm/categoryinfo.h"

namespace WildPalms::Runtime {

CategoryReconcileResult reconcileCategories(const QByteArray &appInfoBlock,
                                            const QStringList &desiredNames)
{
    CategoryReconcileResult out;

    CategoryInfo ci;
    if (!ci.parse(reinterpret_cast<const unsigned char *>(appInfoBlock.constData()),
                  static_cast<size_t>(appInfoBlock.size())))
        return out;   // unparseable block: bind nothing, write nothing

    bool mutated = false;
    for (const QString &name : desiredNames) {
        if (name.isEmpty()) continue;
        int slot = ci.categoryIndex(name);          // case-insensitive
        if (slot < 0) {
            slot = ci.addCategory(name);            // claims first free 1..15
            if (slot < 0) {
                out.noFreeSlot.append(name);
                continue;
            }
            mutated = true;
        }
        out.bound.insert(name, slot);
    }

    if (mutated) {
        QByteArray block = appInfoBlock;            // preserve app-specific tail
        const size_t packSz = ci.packSize();
        if (static_cast<int>(packSz) <= block.size()) {
            ci.pack(reinterpret_cast<unsigned char *>(block.data()), packSz);
            out.updatedAppInfoBlock = block;
        }
        // If the block is somehow smaller than packSize, refuse to write
        // rather than corrupt: leave updatedAppInfoBlock empty.
    }
    return out;
}

} // namespace WildPalms::Runtime
```

- [ ] **Step 4: Run** — `ctest --test-dir build -R tst_category_reconciler --output-on-failure` → 5/5 PASS. If `makeAppInfoBlock`'s zeroed-parse trick fails (parse rejects all-zero), build the fixture block via pilot-link's `pack_CategoryAppInfo` on a value-initialized `CategoryAppInfo_t` instead — see `categoryinfo.cpp` for the unpack call to mirror.

- [ ] **Step 5: Commit** — `git add src/runtime/categoryreconciler.* tests/runtime/tst_category_reconciler.cpp src/CMakeLists.txt tests/runtime/CMakeLists.txt && git commit -m "feat(runtime): CategoryReconciler — names-first slot binding over AppInfo bytes (substrate A3)"`

---

## Task 9: Reconciler integration in finishConnect

**Files:**
- Modify: `src/runtime/palmruntime.cpp` (finishConnect, before the createPalmBackend loop)

- [ ] **Step 1: Integrate** — insert before the conduit backend loop (order
matters: reconcile FIRST so `createPalmBackend`'s AppInfo read sees the final
table):

```cpp
    // Substrate A3: reconcile desired category names against each device
    // table BEFORE backends read AppInfo, writing new slots when claimed.
    m_categoryNoFreeSlot.clear();
    for (auto *c : conduits()) {
        if (!c->supportsCategories() || !m_profile) continue;
        const QString db = c->primaryDbName();
        // Desired = persisted desired set ∪ names referenced by enabled rows.
        QStringList desired = m_profile->desiredCategoryNames(db);
        const QString prefix = QStringLiteral("palm:")
            + c->domain().toString() + QStringLiteral("/name:");
        for (const auto &m : m_mappings) {
            if (!m.enabled || m.sourceBackend != c->conduitId()) continue;
            if (m.sourceCalendar.startsWith(prefix)) {
                const QString n = m.sourceCalendar.mid(prefix.size());
                if (!n.isEmpty() && !desired.contains(n, Qt::CaseInsensitive))
                    desired.append(n);
            }
        }
        if (desired.isEmpty()) continue;

        const QByteArray block = m_device->readAppBlock(db);
        if (block.isEmpty()) continue;
        const auto r = WildPalms::Runtime::reconcileCategories(block, desired);
        if (!r.updatedAppInfoBlock.isEmpty()) {
            if (!m_device->writeAppBlock(db, r.updatedAppInfoBlock))
                qWarning() << "[PalmRuntime] AppInfo write failed for" << db;
            else
                qDebug() << "[PalmRuntime] created" << r.bound.size()
                         << "category binding(s) on" << db;
        }
        if (!r.noFreeSlot.isEmpty()) {
            m_categoryNoFreeSlot.insert(db, r.noFreeSlot);
            qWarning() << "[PalmRuntime] no free category slot on" << db
                       << "for" << r.noFreeSlot;
        }
    }
```

Add `QHash<QString, QStringList> m_categoryNoFreeSlot;` to `palmruntime.h`
(private) — `translateRouteSpec`'s store-based status already reports
`NoFreeSlot` for these names (the store, populated after reconciliation,
won't contain them); the member exists for diagnostics and future UI.

Include `"categoryreconciler.h"` in palmruntime.cpp.

- [ ] **Step 2: Build + full suite** — `make -C build -j$(($(nproc)-1)) && ctest --test-dir build -j$(($(nproc)-1))` → green. The runtime tests' fake devices return empty app blocks → reconciler no-ops; the route tests updated in Task 4 cover the populated path.

- [ ] **Step 3: Commit** — `git add src/runtime/palmruntime.* && git commit -m "feat(runtime): reconcile desired categories at connect; write AppInfo for claimed slots (substrate A3)"`

---

## Task 10: LocalFolderContribution (first credential-less source)

**Files:**
- Create: `src/runtime/localfolderprovider.h`, `src/runtime/localfolderprovider.cpp`
- Create: `src/runtime/localfoldercontribution.h` (header-only, mirrors the lib's `MultiProtocolDavBackendContribution` shape)
- Modify: `src/runtime/standardcontributions.cpp` (register it)
- Modify: `src/app/accounts/accountformwidget.cpp` (label: `"local-folder"` → `tr("Local folder")`)
- Create: `tests/runtime/tst_localfolder_provider.cpp` (+ CMake)
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

```cpp
// Substrate A2: everything-is-a-provider — local folders as a credential-less
// contribution in the same registry as DAV/Akonadi.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "../wildpalms_qtest_main.h"

#include "runtime/localfolderprovider.h"
#include "runtime/localfoldercontribution.h"
#include <backendconfiguration.h>
#include <collectioninfo.h>

using namespace WildPalms::Runtime;
using Kalburator::Sync::BackendConfiguration;

class TstLocalFolderProvider : public QObject {
    Q_OBJECT
private slots:
    void contributionCreatesProvider();
    void connectListsConfiguredFolders();
    void createBackendDispatchesPerDomain();
    void missingFolderFailsConnect();
};

static BackendConfiguration cfgWith(const QList<QPair<QString,QString>> &entries)
{
    BackendConfiguration cfg;
    cfg.id = QStringLiteral("lf-1");
    cfg.type = QStringLiteral("local-folder");
    cfg.displayName = QStringLiteral("My folders");
    QVariantList list;
    for (const auto &e : entries) {
        QVariantMap m;
        m.insert(QStringLiteral("path"), e.first);
        m.insert(QStringLiteral("domain"), e.second);
        list.append(m);
    }
    cfg.connectionParams.insert(QStringLiteral("entries"), list);
    return cfg;
}

void TstLocalFolderProvider::contributionCreatesProvider()
{
    LocalFolderContribution contrib;
    QCOMPARE(contrib.backendType(), QStringLiteral("local-folder"));
    auto provider = contrib.createProvider(nullptr);
    QVERIFY(provider);
    QCOMPARE(provider->kind(), QStringLiteral("local-folder"));
}

void TstLocalFolderProvider::connectListsConfiguredFolders()
{
    QTemporaryDir d1, d2;
    LocalFolderProvider p;
    p.load(cfgWith({ { d1.path(), QStringLiteral("note") },
                     { d2.path(), QStringLiteral("calendar") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    QVERIFY(f.resultAt(0));
    QVERIFY(p.isConnected());
    const auto cols = p.collections();
    QCOMPARE(cols.size(), 2);
    QCOMPARE(cols[0].type, QStringLiteral("note"));      // memos match note domain
    QCOMPARE(cols[1].type, QStringLiteral("calendar"));
    QVERIFY(!cols[0].readOnly);
}

void TstLocalFolderProvider::createBackendDispatchesPerDomain()
{
    QTemporaryDir d1, d2;
    LocalFolderProvider p;
    p.load(cfgWith({ { d1.path(), QStringLiteral("note") },
                     { d2.path(), QStringLiteral("todo") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    const auto cols = p.collections();
    auto noteBackend = p.createBackend(cols[0].id);
    auto todoBackend = p.createBackend(cols[1].id);
    QVERIFY(noteBackend);   // MarkdownFilesBackend
    QVERIFY(todoBackend);   // RawFilesBackend fallback
    QVERIFY(!p.createBackend(QStringLiteral("nonexistent")));
}

void TstLocalFolderProvider::missingFolderFailsConnect()
{
    LocalFolderProvider p;
    p.load(cfgWith({ { QStringLiteral("/nonexistent/path/xyz"),
                       QStringLiteral("note") } }));
    auto f = p.connect();
    QTRY_VERIFY_WITH_TIMEOUT(f.isFinished(), 5000);
    QVERIFY(!f.resultAt(0));
    QVERIFY(!p.isConnected());
    QVERIFY(!p.lastError().isEmpty());
}

WILDPALMS_QTEST_MAIN(TstLocalFolderProvider)
#include "tst_localfolder_provider.moc"
```

- [ ] **Step 2: Verify compile failure**, as before.

- [ ] **Step 3: Implement the provider**

`src/runtime/localfolderprovider.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_LOCALFOLDERPROVIDER_H
#define WILDPALMS_RUNTIME_LOCALFOLDERPROVIDER_H

#include <iprovider.h>
#include <backendconfiguration.h>
#include <collectioninfo.h>

namespace WildPalms::Runtime {

/// Credential-less provider (substrate A2): each configured (path, domain)
/// entry is one writable collection. Proves the everything-is-a-provider
/// model for local sources; ICS-feed/document stores follow the pattern.
class LocalFolderProvider : public Kalburator::Sync::IProvider {
    Q_OBJECT
public:
    explicit LocalFolderProvider(QObject *parent = nullptr);

    QString id() const override          { return m_cfg.id; }
    QString kind() const override        { return QStringLiteral("local-folder"); }
    QString displayName() const override { return m_cfg.displayName; }
    void load(const Kalburator::Sync::BackendConfiguration &cfg) override;
    Kalburator::Sync::BackendConfiguration save() const override { return m_cfg; }
    QWidget *createConfigWidget(QWidget *parent) override;
    QFuture<bool> connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }
    QList<Kalburator::Sync::CollectionInfo> collections() const override
    { return m_collections; }
    std::unique_ptr<Kalburator::Sync::IBlobBackend>
        createBackend(const QString &collectionId) override;
    QString lastWarning() const override { return {}; }
    QString lastError() const { return m_lastError; }

private:
    struct Entry { QString path; QString domain; QString collectionId; };
    Kalburator::Sync::BackendConfiguration m_cfg;
    QList<Entry> m_entries;
    QList<Kalburator::Sync::CollectionInfo> m_collections;
    bool m_connected = false;
    QString m_lastError;
};

} // namespace WildPalms::Runtime
#endif
```

`src/runtime/localfolderprovider.cpp`:

```cpp
#include "localfolderprovider.h"

#include <markdownfilesbackend.h>
#include <rawfilesbackend.h>

#include <QDir>
#include <QFutureInterface>

namespace WildPalms::Runtime {

LocalFolderProvider::LocalFolderProvider(QObject *parent)
    : Kalburator::Sync::IProvider(parent) {}

void LocalFolderProvider::load(const Kalburator::Sync::BackendConfiguration &cfg)
{
    m_cfg = cfg;
    m_entries.clear();
    const QVariantList list =
        cfg.connectionParams.value(QStringLiteral("entries")).toList();
    int i = 0;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.path   = m.value(QStringLiteral("path")).toString();
        e.domain = m.value(QStringLiteral("domain")).toString();
        // Stable, path-independent collection id within this provider.
        e.collectionId = QStringLiteral("folder-%1").arg(i++);
        if (!e.path.isEmpty() && !e.domain.isEmpty())
            m_entries.append(e);
    }
}

QFuture<bool> LocalFolderProvider::connect()
{
    // Synchronous: validate paths, build collections, resolve immediately.
    m_lastError.clear();
    m_collections.clear();
    bool ok = !m_entries.isEmpty();
    if (m_entries.isEmpty())
        m_lastError = QStringLiteral("No folders configured");
    for (const auto &e : m_entries) {
        if (!QDir(e.path).exists()) {
            ok = false;
            m_lastError = QStringLiteral("Folder does not exist: %1").arg(e.path);
            break;
        }
        Kalburator::Sync::CollectionInfo ci;
        ci.id       = e.collectionId;
        ci.name     = QDir(e.path).dirName();
        ci.type     = e.domain;
        ci.readOnly = false;
        m_collections.append(ci);
    }
    if (!ok) {
        m_collections.clear();
        emit error(m_lastError);
    } else {
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
    }
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(ok);
    fi.reportFinished();
    return fi.future();
}

void LocalFolderProvider::disconnect()
{
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<Kalburator::Sync::IBlobBackend>
LocalFolderProvider::createBackend(const QString &collectionId)
{
    if (!m_connected) return nullptr;
    for (const auto &e : m_entries) {
        if (e.collectionId != collectionId) continue;
        // v1 dispatch (substrate spec A2): note -> Markdown; rest -> RawFiles.
        if (e.domain == QLatin1String("note"))
            return std::make_unique<Kalburator::Sinks::MarkdownFilesBackend>(e.path);
        return std::make_unique<Kalburator::Sinks::RawFilesBackend>(e.path);
    }
    return nullptr;
}

QWidget *LocalFolderProvider::createConfigWidget(QWidget *parent)
{
    // Minimal v1: the lib's ProviderConfigDialog tolerates a null widget;
    // a folder-list editor widget ships with sub-project C's source UI.
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace WildPalms::Runtime
```

Verify the sink classes' namespaces/headers:
`grep -rn "namespace" build/_deps/libkalburator-src/src/universal/rawfilesbackend.h | head -3`
and adjust `Kalburator::Sinks::` if they live elsewhere. Verify `lastError()`
exists on IProvider (WP-A7 added it lib-side at v0.69 —
`grep -n "lastError" build/_deps/libkalburator-src/src/sync/iprovider.h`);
if it is virtual on the base, mark the override accordingly.

`src/runtime/localfoldercontribution.h`:

```cpp
#ifndef WILDPALMS_RUNTIME_LOCALFOLDERCONTRIBUTION_H
#define WILDPALMS_RUNTIME_LOCALFOLDERCONTRIBUTION_H

#include <backendcontribution.h>
#include "localfolderprovider.h"

namespace WildPalms::Runtime {

class LocalFolderContribution : public Kalburator::Sync::BackendContribution {
public:
    QString backendType() const override { return QStringLiteral("local-folder"); }
    QString displayName() const override { return QStringLiteral("Local folder"); }
    QList<Kalburator::Shape::Shape> nativeShapes() const override { return {}; }
    std::unique_ptr<Kalburator::Sync::IProvider>
        createProvider(QObject *parent) const override
    { return std::make_unique<LocalFolderProvider>(parent); }
};

} // namespace WildPalms::Runtime
#endif
```

- [ ] **Step 4: Register + label**

`standardcontributions.cpp`: add after the multiproto registration:

```cpp
    registry->registerContribution(
        std::make_shared<WildPalms::Runtime::LocalFolderContribution>());
```
(+ `#include "localfoldercontribution.h"`.)

`accountformwidget.cpp` label chain: add

```cpp
        else if (label == QStringLiteral("local-folder"))
            label = tr("Local folder");
```

- [ ] **Step 5: Build + tests** — new test green, then FULL suite (watch
`tst_accountformwidget` / `tst_accounts_page` / wizard tests for combo-count
assumptions; fix counts, not semantics).

- [ ] **Step 6: Commit** — `git add src/runtime/localfolder* src/runtime/standardcontributions.cpp src/app/accounts/accountformwidget.cpp tests/runtime/tst_localfolder_provider.cpp src/CMakeLists.txt tests/runtime/CMakeLists.txt && git commit -m "feat(sources): LocalFolderContribution — first credential-less provider (substrate A2)"`

---

## Task 11: Extensibility proof — the fifth conduit

**Files:**
- Modify: `src/runtime/palmruntime.h/.cpp` (test seam)
- Create: `tests/runtime/tst_fifth_conduit.cpp` (+ CMake)

- [ ] **Step 1: Add the test seam** to PalmRuntime (public, `ForTest` suffix per house style):

```cpp
    /// Substrate A1 test seam: append an extra conduit descriptor after
    /// construction. Re-runs hub-collection creation so the new domain gets
    /// its hub collection. NOT a production plugin-loading path.
    void appendConduitForTest(std::unique_ptr<WildPalms::Plugins::PimPlugin> conduit);
```

```cpp
void PalmRuntime::appendConduitForTest(
    std::unique_ptr<WildPalms::Plugins::PimPlugin> conduit)
{
    m_palmPlugins.push_back(std::move(conduit));
    ensureHubCollections();   // idempotent: createCollection on an existing id
                              // is a no-op / harmless re-create — verify in
                              // GenericSqliteBackend and guard here if not
}
```

(Verify idempotence: `grep -n "createCollection" build/_deps/libkalburator-src/src/universal/genericsqlitebackend.* | head`. If duplicate creation is not safe, guard with an existence check before creating.)

- [ ] **Step 2: Write the proof test**

```cpp
// Substrate A1 acceptance: a fifth conduit participates everywhere a stock
// conduit does, with ZERO WildPalms source changes beyond registration.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "../wildpalms_qtest_main.h"

#include "runtime/palmruntime.h"
#include "runtime/routemapping.h"
#include "plugins/pimplugin.h"
#include <synctypes.h>

using namespace WildPalms::Runtime;

namespace {
class FakeDocumentConduit : public WildPalms::Plugins::PimPlugin {
public:
    QString conduitId() const override { return QStringLiteral("document"); }
    Kalburator::Shape::DomainId domain() const override
    { return Kalburator::Shape::DomainId{QStringLiteral("document")}; }
    QString primaryDbName() const override { return QStringLiteral("DocumentDB"); }
    QString conduitDisplayName() const override { return QStringLiteral("Documents"); }
    bool supportsCategories() const override { return false; }
    std::unique_ptr<Kalburator::Sync::SyncBackendBase>
        createPalmBackend(WildPalms::Runtime::PalmDeviceAccess *) override
    { return nullptr; }
};
} // namespace

class TstFifthConduit : public QObject {
    Q_OBJECT
private slots:
    void fifthConduitIsEnumerated();
    void routesTranslateForFifthConduit();
};

void TstFifthConduit::fifthConduitIsEnumerated()
{
    QTemporaryDir dir;
    PalmRuntime rt(dir.path() + "/state");
    const int stockCount = rt.conduits().size();
    rt.appendConduitForTest(std::make_unique<FakeDocumentConduit>());
    QCOMPARE(rt.conduits().size(), stockCount + 1);
    QVERIFY(rt.isPalmConduitBackendId(QStringLiteral("document")));
}

void TstFifthConduit::routesTranslateForFifthConduit()
{
    FakeDocumentConduit doc;
    Kalburator::Sync::SyncMapping m;
    m.id = QStringLiteral("doc-route");
    m.sourceBackend  = QStringLiteral("document");
    m.sourceCalendar = QString();                 // direct (no categories)
    m.targetBackend  = QStringLiteral("acc:col");
    m.targetCalendar = QStringLiteral("col");
    m.enabled = true;
    const auto t = translateRouteSpec(m, { &doc });
    QVERIFY(t.spec.has_value());
    QCOMPARE(t.status, RouteStatus::Active);
    QCOMPARE(t.spec->domain, QStringLiteral("document"));
    QCOMPARE(t.spec->hubCollectionId, QStringLiteral("document"));
}

WILDPALMS_QTEST_MAIN(TstFifthConduit)
#include "tst_fifth_conduit.moc"
```

(`isPalmConduitBackendId` must be public for this — adjust visibility in Task 3
if it was added private.)

- [ ] **Step 3: Build, run, full suite, commit**

```bash
make -C build -j$(($(nproc)-1)) && \
ctest --test-dir build -R tst_fifth_conduit --output-on-failure && \
ctest --test-dir build -j$(($(nproc)-1))
git add src/runtime/palmruntime.* tests/runtime/tst_fifth_conduit.cpp tests/runtime/CMakeLists.txt
git commit -m "test(runtime): fifth-conduit extensibility proof (substrate A1 acceptance)"
```

---

## Task 12: Close out

**Files:**
- Modify: `CLAUDE.md` (status section: substrate landed; hardware-verification queue gains "first live AppInfo write")
- Modify: `docs/superpowers/specs/2026-06-11-config-two-doors-umbrella-design.md` (mark A "implemented")

- [ ] **Step 1:** Full suite one more time: `ctest --test-dir build -j$(($(nproc)-1))` → 100%.
- [ ] **Step 2:** Update CLAUDE.md "Current branch and state" + the umbrella doc's sub-project A line; note the new test binaries and that AppInfo write joins clobber Task 12 in the hardware-verification queue.
- [ ] **Step 3:** `git add CLAUDE.md docs/ && git commit -m "docs: substrate (sub-project A) landed; AppInfo write queued for hardware verification"`

---

## Self-review notes (already applied)

- **Spec coverage:** A1 → Tasks 1-5, 11; A2 → Task 10; A3 → Tasks 4, 7, 8, 9; A4 → Task 6. The graph-view row-form update (spec A3 "both doors write one schema") → Task 4 Step 5.
- **Order-of-operations hazard:** reconciliation must precede `createPalmBackend` (Task 9) and `ensureHubCollections` must follow plugin registration (Task 3 Step 2.5) — both called out inline.
- **Type consistency:** `conduits()` returns `QList<PimPlugin*>`; `translateRouteSpec(p, conduits)` and the test fixtures use the same list type; `createPalmBackend` returns `unique_ptr<SyncBackendBase>` everywhere after Task 2.
- **Known verify-points for the executor** (signatures read from v0.69 _deps but worth re-checking at implementation time): `Kalburator::Sinks` namespace for RawFiles/Markdown backends; `IProvider::lastError()` virtuality (WP-A7); `CategoryInfo::parse` acceptance of an all-zero block (Task 8 Step 4 fallback given); `GenericSqliteBackend::createCollection` idempotence (Task 11 Step 1 guard given).
