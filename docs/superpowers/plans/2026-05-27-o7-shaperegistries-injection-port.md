# O7 ShapeRegistries Injection Port (v0.57) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port WildPalms to libkalburator **v0.57**, which deleted the transitional ambient-`ShapeRegistries` scaffolding (O7). WildPalms must (a) construct one `Shape::ShapeRegistries` at its composition root and inject it into both `PluginManager` and `SyncEngine`, and (b) convert its four plugin submodules from the imperative `XDomainExtension::registerWith(TransformationRegistry::instance())` pattern to the idiomatic `Plugin::shapeContributions()` pattern (the same one libkalburator's stock plugins use).

**Architecture:** Per-`PalmRuntime` `ShapeRegistries` (a value member), replacing the process-global singletons. The four plugins each expose a `ShapeContribution` subclass (peer `(domain, palm)` shape + its catalogue, plus the two palm↔peer edges); `PluginManager::loadInProcess` registers those into the injected registries. This also lets us delete the fragile `s_globalRegistrationDone` guard and the documented heap-corruption-on-re-registration path (`palmruntime.cpp:234-256`), because per-instance registries make re-registration across `PalmRuntime` instances a non-issue.

**Tech Stack:** C++/Qt6, KF6, CMake (legacy, build dir `build-dev`), CTest, libkalburator v0.57 (sibling at `../libkalburator`, FetchContent dep). The four plugins are git **submodules** (`src/plugins/{calendar,contacts,memo,todos}`), each its own repo — commits land in the submodule, then the superproject records the new gitlink.

**Iteration build:** Use a source-override build dir against the local v0.57 checkout for fast iteration: `cmake -S . -B build-v057 -DCMAKE_BUILD_TYPE=Debug -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator`. The pin bump + FetchContent verification is the final task.

**Key API (libkalburator v0.57, verbatim references):**
- `Shape::ShapeContribution` (`src/shape/shapecontribution.h`): pure-virtual `DomainId targetDomain() const`, `QList<std::pair<Shape, PropertyCatalogue>> peerShapes() const`, `QList<TransformationEdge> edges() const`.
- `SyncEngine(BackendRegistry*, ISyncHost*, Shape::ShapeRegistries&, QObject* =nullptr)` — only ctor.
- `PluginManager(Sync::BackendRegistry*, Shape::ShapeRegistries&)` — only ctor.
- `ShapeRegistries { TransformationRegistry transformation; DomainRegistry domain; DomainOperationsRegistry operations; }`.
- `applyPlugin` requires each shape contribution's `targetDomain()` to be in the plugin manifest's `definesDomains`+`requiresDomains`; edge endpoints must already be registered (the stock plugins register `ical`/`vcard4`/`canon`/`ical-vtodo` first, so WP plugins must load *after* `registerStockPlugins`).

---

### Task 1: Composition root — inject ShapeRegistries in PalmRuntime

**Files:**
- Modify: `src/runtime/palmruntime.h` (add member)
- Modify: `src/runtime/palmruntime.cpp:117-133` (construction), `:234-272` (plugin load), `:255` (domain check), `mkPalmManifest` `:93-105`

- [ ] **Step 1: Add the ShapeRegistries member**

In `src/runtime/palmruntime.h`, add an include `#include <shaperegistries.h>` and, as a member declared **before** `m_pluginManager` and `m_engine` (construction order: registries must outlive both):
```cpp
    Kalburator::Shape::ShapeRegistries m_shape;
```

- [ ] **Step 2: Inject into SyncEngine (palmruntime.cpp:131)**

Replace:
```cpp
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get());
```
with:
```cpp
    m_engine = std::make_unique<Kalburator::Sync::SyncEngine>(
        m_registry.get(), m_syncHost.get(), m_shape);
```

- [ ] **Step 3: Inject into PluginManager (palmruntime.cpp:243)**

Replace:
```cpp
        m_pluginManager = std::make_unique<Kalburator::PluginManager>(m_registry.get());
```
with:
```cpp
        m_pluginManager = std::make_unique<Kalburator::PluginManager>(m_registry.get(), m_shape);
```

- [ ] **Step 4: Replace registerStockPlugins + the global guard with ONE combined batch (palmruntime.cpp:234-272)**

> **Corrected after verifying v0.57 `PluginManager::resolve()`** (`pluginmanager.cpp:20-53`): `resolve()` builds its `definedBy` map **only from the current batch**, and `loadInProcess` calls `reset()` each time. So the old approach (call `registerStockPlugins` as batch 1, then load WP plugins as batch 2 with `requiresDomains`) fails with `MissingDependency` — exactly what the legacy `mkPalmManifest` comment warned about. The authoritative consumer pattern is **PlanStan's** (`PlanStan/src/app/appcontroller.cpp:40-79`): build ONE items list — stock domain/infra plugins (with `definesDomains`) + the consumer's own plugins (with `requiresDomains`) — and call `loadInProcess` ONCE, so `resolve()` topologically orders the consumer plugins after their definers.

Do **not** call `registerStockPlugins` (it hides its items and is a separate batch). Instead include the stock **domain/infra** plugins WP needs directly. Exclude the stock DAV *provider* plugins — WP already seeds CalDav/CardDav/Akonadi **backend contributions** via `registerStandardContributions` (called in the ctor), and the stock domain plugins return empty `backendContributions()`, so there is no double-registration. The `s_globalRegistrationDone` guard and `DomainRegistry::instance()` check are deleted: per-instance `m_shape` makes them unnecessary.

Add includes near the top of `palmruntime.cpp` (angle-bracket — libkalburator headers): `universalstorageplugin.h`, `blobplugin.h`, `noteplugin.h`, `todoplugin.h`, `contactsplugin.h`, `calendarplugin.h`. Replace the guarded load block with:
```cpp
    m_pluginManager = std::make_unique<Kalburator::PluginManager>(m_registry.get(), m_shape);

    // v0.57 composition (mirrors PlanStan/src/app/appcontroller.cpp): ONE batch
    // of stock domain/infra plugins (which DEFINE the canonical domains + peer
    // shapes ical/vcard4/canon/ical-vtodo) plus WP's four plugins (which REQUIRE
    // those domains and contribute the (domain,palm) peer + palm<->peer edges).
    // resolve() orders requirers after definers within the batch. DAV *provider*
    // plugins are intentionally omitted — registerStandardContributions() (ctor)
    // already seeds the CalDav/CardDav/Akonadi backend contributions.
    static Kalburator::UniversalStoragePlugin s_universal;
    static Kalburator::Blob::BlobPlugin       s_blob;
    static Kalburator::Note::NotePlugin       s_note;
    static Kalburator::Todo::TodoPlugin       s_todo;
    static Kalburator::Contacts::ContactsPlugin s_contacts;
    static Kalburator::Calendar::CalendarPlugin s_calendar;

    QList<QPair<Kalburator::Plugin *, Kalburator::PluginManifest>> items{
        { &s_universal, mkStockManifest(QStringLiteral("kalburator.universal-storage")) },
        { &s_blob,      mkStockManifest(QStringLiteral("kalburator.blob"),     {QStringLiteral("blob")}) },
        { &s_note,      mkStockManifest(QStringLiteral("kalburator.note"),     {QStringLiteral("note")}) },
        { &s_todo,      mkStockManifest(QStringLiteral("kalburator.todo"),     {QStringLiteral("todo")}) },
        { &s_contacts,  mkStockManifest(QStringLiteral("kalburator.contacts"), {QStringLiteral("contacts")}) },
        { &s_calendar,  mkStockManifest(QStringLiteral("kalburator.calendar"), {QStringLiteral("calendar")}) },
        { cal.get(),    mkPalmManifest(QStringLiteral("wildpalms.calendar"), QStringLiteral("calendar")) },
        { con.get(),    mkPalmManifest(QStringLiteral("wildpalms.contacts"), QStringLiteral("contacts")) },
        { memo.get(),   mkPalmManifest(QStringLiteral("wildpalms.memo"),     QStringLiteral("note"))     },
        { todo.get(),   mkPalmManifest(QStringLiteral("wildpalms.todo"),     QStringLiteral("todo"))     },
    };
    if (!m_pluginManager->loadInProcess(items)) {
        qWarning() << "[PalmRuntime] plugin load rejected:"
                   << m_pluginManager->rejected().size();
        return;
    }
```
**Domain fix:** memo's domain is `note` (shapes `(note,palm)`/`(note,canon)`), not `memo`.

**Multi-instance note:** the stock plugin instances are `static` (shared, stateless contribution providers — matches `stock_plugins.cpp`); WP's `cal/con/memo/todo` are per-`PalmRuntime`. Each `PalmRuntime` has its own `m_shape`, so a second instance re-runs `loadInProcess` into its own fresh registries — no cross-instance conflict (this is what the old `s_globalRegistrationDone` guard worked around).

- [ ] **Step 5: Add mkStockManifest helper; make mkPalmManifest declare requiresDomains (palmruntime.cpp:85-105)**

Add a stock-manifest helper alongside `mkPalmManifest` (a stock plugin DEFINES its domain):
```cpp
static Kalburator::PluginManifest mkStockManifest(const QString &id,
                                                  QStringList defines = {})
{
    Kalburator::PluginManifest m;
    m.id = id;
    m.version = QStringLiteral("1.0");
    m.displayName = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    m.definesDomains = std::move(defines);
    return m;
}
```
And update `mkPalmManifest` so its second parameter populates `requiresDomains`:

- [ ] **Step 5: Make mkPalmManifest declare the required domain (palmruntime.cpp:93-105)**

`applyPlugin` rejects a shape contribution whose `targetDomain()` isn't in the manifest. Update `mkPalmManifest` so its second parameter populates `requiresDomains`:
```cpp
static Kalburator::PluginManifest mkPalmManifest(const QString &id,
                                                 const QString &domain)
{
    Kalburator::PluginManifest m;
    m.id = id;
    m.version = QStringLiteral("1.0");
    m.displayName = id;
    m.kalburatorPluginVersion = QStringLiteral("1.0");
    // WP plugins DEFINE no canonical domain (stock plugins do); they REQUIRE
    // the canonical domain they augment with a (domain, palm) peer + edges.
    m.requiresDomains = { domain };
    return m;
}
```
(Adjust surrounding field names to match the existing `PluginManifest` usage in this file; keep any fields the old body set besides `definesDomains`.)

- [ ] **Step 6: Verify it compiles so far (will still fail on the 4 plugin ctors — expected)**

Run:
```bash
cd /home/clinton/dev/WildPalms
cmake --build build-v057 -j"$(nproc)" 2>&1 | grep -E "error:|palmruntime" | head
```
Expected: no errors *in palmruntime.cpp*; remaining errors are the four `*backendplugin.cpp` `::instance()` calls (fixed in Tasks 2–5).

- [ ] **Step 7: Commit (superproject)**

```bash
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp
git commit -m "refactor(runtime): inject per-PalmRuntime ShapeRegistries (O7)

Owns one Shape::ShapeRegistries member, injected into PluginManager and
SyncEngine via v0.57's only ctors. Drops the s_globalRegistrationDone guard
and DomainRegistry::instance() check — per-instance registries make
cross-instance re-registration a non-issue. mkPalmManifest now declares the
required canonical domain so applyPlugin accepts each plugin's shape
contribution. Memo's domain corrected to 'note'.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: Convert the Calendar plugin to a ShapeContribution

**Files (submodule `src/plugins/calendar`):**
- Rewrite: `src/plugins/calendar/calendardomainextension.h` / `.cpp` → a `CalendarStockShapes : ShapeContribution`
- Modify: `src/plugins/calendar/calendarbackendplugin.h` (override `shapeContributions()`), `.cpp` (remove ctor registration)

- [ ] **Step 1: Rewrite the header as a ShapeContribution**

Replace `src/plugins/calendar/calendardomainextension.h` contents with:
```cpp
#ifndef WILDPALMS_CALENDAR_CALENDARDOMAINEXTENSION_H
#define WILDPALMS_CALENDAR_CALENDARDOMAINEXTENSION_H

#include <shapecontribution.h>

namespace WildPalms::CalendarPlugin {

// Contributes the (calendar, palm) peer shape and palm<->ical edges to the
// shape graph. The ical<->canon hop is libkalburator's (CalendarStockShapes).
class CalendarPalmShapes : public Kalburator::Shape::ShapeContribution {
public:
    Kalburator::Shape::DomainId targetDomain() const override;
    QList<std::pair<Kalburator::Shape::Shape, Kalburator::Shape::PropertyCatalogue>>
        peerShapes() const override;
    QList<Kalburator::Shape::TransformationEdge> edges() const override;
};

} // namespace WildPalms::CalendarPlugin

#endif // WILDPALMS_CALENDAR_CALENDARDOMAINEXTENSION_H
```

- [ ] **Step 2: Rewrite the .cpp as a ShapeContribution**

Replace `src/plugins/calendar/calendardomainextension.cpp` contents with (the `makePalmCatalogue()` body is unchanged from the current file; the edges move from `registerWith` into `edges()`; the defensive `ical` placeholder registration is **dropped** — the stock calendar plugin registers `ical` before this plugin loads):
```cpp
#include "calendardomainextension.h"

#include "palmtoicstransformation.h"
#include <propertycatalogue.h>

using namespace Kalburator::Shape;

namespace WildPalms::CalendarPlugin {

namespace {

PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"summary"},    PropertyKind::String,    QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"note"},       PropertyKind::String,    QStringLiteral("Note") });
    cat.addProperty({ PropertyId{"start"},      PropertyKind::Json,      QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"end"},        PropertyKind::Json,      QStringLiteral("End") });
    cat.addProperty({ PropertyId{"allDay"},     PropertyKind::Boolean,   QStringLiteral("All Day") });
    cat.addProperty({ PropertyId{"recurrence"}, PropertyKind::StringList,QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"alarms"},     PropertyKind::Json,      QStringLiteral("Alarm") });
    cat.addProperty({ PropertyId{"category"},   PropertyKind::Integer,   QStringLiteral("Category Slot") });
    return cat;
}

} // namespace

DomainId CalendarPalmShapes::targetDomain() const
{
    return DomainId{QStringLiteral("calendar")};
}

QList<std::pair<Shape, PropertyCatalogue>> CalendarPalmShapes::peerShapes() const
{
    const Shape palm{ DomainId{"calendar"}, EncodingId{"palm"} };
    return { { palm, makePalmCatalogue() } };
}

QList<TransformationEdge> CalendarPalmShapes::edges() const
{
    const Shape palm{ DomainId{"calendar"}, EncodingId{"palm"} };
    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };
    return {
        TransformationEdge{ palm, ical, palmToIcsLoss(), std::make_shared<PalmToIcsStage>() },
        TransformationEdge{ ical, palm, icsToPalmLoss(), std::make_shared<IcsToPalmStage>() },
    };
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 3: Override shapeContributions() in the plugin; remove ctor registration**

In `src/plugins/calendar/calendarbackendplugin.h`, change the `shapeContributions()` override from returning `{}` to declaring it returns the contribution (keep signature; implement in .cpp). In `src/plugins/calendar/calendarbackendplugin.cpp`:
- Remove the ctor lines `CalendarDomainExtension::registerWith(Kalburator::Shape::TransformationRegistry::instance());` (and the now-unused `#include`/using for `TransformationRegistry`).
- Add:
```cpp
QList<std::shared_ptr<Kalburator::Shape::ShapeContribution>>
CalendarBackendPlugin::shapeContributions() const
{
    return { std::make_shared<CalendarPalmShapes>() };
}
```
(Include `calendardomainextension.h` in the .cpp.)

- [ ] **Step 4: Build (calendar should now compile)**

```bash
cmake --build build-v057 -j"$(nproc)" 2>&1 | grep -E "error:.*calendar" | head
```
Expected: no calendar errors. (Remaining: contacts/memo/todos.)

- [ ] **Step 5: Commit the submodule, then record the gitlink**

```bash
git -C src/plugins/calendar add -A
git -C src/plugins/calendar commit -m "refactor: contribute (calendar,palm) shapes via ShapeContribution (O7)

Replaces the imperative CalendarDomainExtension::registerWith(::instance())
with a CalendarPalmShapes : ShapeContribution that PluginManager registers
into the injected ShapeRegistries. Drops the defensive ical placeholder
(stock calendar plugin registers ical before this plugin loads).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
git add src/plugins/calendar
```
(The superproject gitlink is committed together with the other plugins in Task 6, or now — either is fine; keep one gitlink-bump commit per plugin or one combined. Combined is cleaner; defer the superproject `git commit` of gitlinks to Task 7.)

---

### Task 3: Convert the Contacts plugin to a ShapeContribution

**Files (submodule `src/plugins/contacts`):** same shape as Task 2.

- [ ] **Step 1: Rewrite `contactsdomainextension.h`** as `ContactsPalmShapes : ShapeContribution` (mirror Task 2 Step 1, namespace `WildPalms::ContactsPlugin`, class doc "(contacts, palm) peer + palm<->vcard4 edges").

- [ ] **Step 2: Rewrite `contactsdomainextension.cpp`** — `makePalmCatalogue()` body unchanged from current file; drop the defensive `vcard4` placeholder:
```cpp
#include "contactsdomainextension.h"
#include "palmtovcardtransformation.h"
#include <propertycatalogue.h>

using namespace Kalburator::Shape;

namespace WildPalms::ContactsPlugin {
namespace {
PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"name"},     PropertyKind::Json,    QStringLiteral("Name") });
    cat.addProperty({ PropertyId{"company"},  PropertyKind::String,  QStringLiteral("Company") });
    cat.addProperty({ PropertyId{"phones"},   PropertyKind::Json,    QStringLiteral("Phones") });
    cat.addProperty({ PropertyId{"address"},  PropertyKind::Json,    QStringLiteral("Address") });
    cat.addProperty({ PropertyId{"note"},     PropertyKind::String,  QStringLiteral("Note") });
    cat.addProperty({ PropertyId{"category"}, PropertyKind::Integer, QStringLiteral("Category Slot") });
    return cat;
}
} // namespace

DomainId ContactsPalmShapes::targetDomain() const { return DomainId{QStringLiteral("contacts")}; }

QList<std::pair<Shape, PropertyCatalogue>> ContactsPalmShapes::peerShapes() const
{
    const Shape palm{ DomainId{"contacts"}, EncodingId{"palm"} };
    return { { palm, makePalmCatalogue() } };
}

QList<TransformationEdge> ContactsPalmShapes::edges() const
{
    const Shape palm     { DomainId{"contacts"}, EncodingId{"palm"}   };
    const Shape canonical{ DomainId{"contacts"}, EncodingId{"vcard4"} };
    return {
        TransformationEdge{ palm, canonical, palmToVCardLoss(), std::make_shared<PalmToVCardStage>() },
        TransformationEdge{ canonical, palm, vcardToPalmLoss(), std::make_shared<VCardToPalmStage>() },
    };
}
} // namespace WildPalms::ContactsPlugin
```

- [ ] **Step 3:** In `contactsbackendplugin.{h,cpp}`, override `shapeContributions()` to return `{ std::make_shared<ContactsPalmShapes>() }`; remove the ctor `ContactsDomainExtension::registerWith(...::instance())` line + unused includes.

- [ ] **Step 4: Build** — `cmake --build build-v057 -j"$(nproc)" 2>&1 | grep -E "error:.*contacts" | head` → no contacts errors.

- [ ] **Step 5: Commit submodule** (`git -C src/plugins/contacts commit -m "refactor: contribute (contacts,palm) shapes via ShapeContribution (O7) ..."`).

---

### Task 4: Convert the Memo plugin to a ShapeContribution

**Files (submodule `src/plugins/memo`):** same shape; note the peer endpoint is `(note, canon)` directly (memo's edges are palm↔canon, per the current `registerWith`).

- [ ] **Step 1: Rewrite `notedomainextension.h`** as `NotePalmShapes : ShapeContribution` (namespace `WildPalms::Memo`).

- [ ] **Step 2: Rewrite `notedomainextension.cpp`** — `makePalmCatalogue()` body unchanged; drop the defensive `canon` placeholder:
```cpp
#include "notedomainextension.h"
#include "palmnotetransformation.h"
#include <propertycatalogue.h>

using namespace Kalburator::Shape;

namespace WildPalms::Memo {
namespace {
PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"body"},     PropertyKind::String,  QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"private"},  PropertyKind::Boolean, QStringLiteral("Private") });
    cat.addProperty({ PropertyId{"category"}, PropertyKind::Integer, QStringLiteral("Category Slot") });
    return cat;
}
} // namespace

DomainId NotePalmShapes::targetDomain() const { return DomainId{QStringLiteral("note")}; }

QList<std::pair<Shape, PropertyCatalogue>> NotePalmShapes::peerShapes() const
{
    const Shape palm{ DomainId{"note"}, EncodingId{"palm"} };
    return { { palm, makePalmCatalogue() } };
}

QList<TransformationEdge> NotePalmShapes::edges() const
{
    const Shape palm { DomainId{"note"}, EncodingId{"palm"} };
    const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };
    return {
        TransformationEdge{ palm, canon, palmToCanonLoss(), std::make_shared<PalmToCanonStage>() },
        TransformationEdge{ canon, palm, canonToPalmLoss(), std::make_shared<CanonToPalmStage>() },
    };
}
} // namespace WildPalms::Memo
```

- [ ] **Step 3:** In `memobackendplugin.{h,cpp}`, override `shapeContributions()` → `{ std::make_shared<NotePalmShapes>() }`; remove ctor `NoteDomainExtension::registerWith(...::instance())`.

- [ ] **Step 4: Build** — `grep -E "error:.*memo"` → none.

- [ ] **Step 5: Commit submodule.**

---

### Task 5: Convert the Todos plugin to a ShapeContribution

**Files (submodule `src/plugins/todos`):** same shape; edges palm↔`ical-vtodo`.

- [ ] **Step 1: Rewrite `tododomainextension.h`** as `TodoPalmShapes : ShapeContribution` (namespace `WildPalms::TodoPlugin`).

- [ ] **Step 2: Rewrite `tododomainextension.cpp`** — `makePalmCatalogue()` body unchanged; drop the defensive `ical-vtodo` placeholder:
```cpp
#include "tododomainextension.h"
#include "palmtovtodotransformation.h"
#include <propertycatalogue.h>

using namespace Kalburator::Shape;

namespace WildPalms::TodoPlugin {
namespace {
PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    cat.addProperty({ PropertyId{"summary"},        PropertyKind::String,  QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"},    PropertyKind::String,  QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"due"},            PropertyKind::Json,    QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"priority"},       PropertyKind::Integer, QStringLiteral("Priority") });
    cat.addProperty({ PropertyId{"status"},         PropertyKind::String,  QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"classification"}, PropertyKind::String,  QStringLiteral("Classification") });
    cat.addProperty({ PropertyId{"category"},       PropertyKind::Integer, QStringLiteral("Category Slot") });
    return cat;
}
} // namespace

DomainId TodoPalmShapes::targetDomain() const { return DomainId{QStringLiteral("todo")}; }

QList<std::pair<Shape, PropertyCatalogue>> TodoPalmShapes::peerShapes() const
{
    const Shape palm{ DomainId{"todo"}, EncodingId{"palm"} };
    return { { palm, makePalmCatalogue() } };
}

QList<TransformationEdge> TodoPalmShapes::edges() const
{
    const Shape palm { DomainId{"todo"}, EncodingId{"palm"} };
    const Shape vtodo{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };
    return {
        TransformationEdge{ palm, vtodo, palmToVTodoLoss(), std::make_shared<PalmToVTodoStage>() },
        TransformationEdge{ vtodo, palm, vtodoToPalmLoss(), std::make_shared<VTodoToPalmStage>() },
    };
}
} // namespace WildPalms::TodoPlugin
```

- [ ] **Step 3:** In `todobackendplugin.{h,cpp}`, override `shapeContributions()` → `{ std::make_shared<TodoPalmShapes>() }`; remove ctor `TodoDomainExtension::registerWith(...::instance())`.

- [ ] **Step 4: Build** — `grep -E "error:.*todo"` → none.

- [ ] **Step 5: Commit submodule.**

---

### Task 6: Fix the two contacts tests that poke the global singletons

**Files:**
- Modify: `tests/plugins/contacts/tst_contactsdomainextension.cpp` (lines 16,17,22,38,63)
- Modify: `tests/plugins/contacts/tst_contactsbackendplugin.cpp` (lines 89,90,91,201)

- [ ] **Step 1: Replace `::instance()` with a local ShapeRegistries**

In each test, construct a local `Kalburator::Shape::ShapeRegistries shape;` in the fixture and use `shape.transformation` / `shape.domain` / `shape.operations` where the test currently calls `TransformationRegistry::instance()` / `DomainRegistry::instance()` / `DomainOperationsRegistry::instance()`. The `.clear()` calls become unnecessary (a fresh `shape` is empty each test) — remove them. Where the test drives a plugin's registration, build a `PluginManager pm(&backendReg, shape)`, call `Kalburator::registerStockPlugins(pm)` then `pm.loadInProcess(...)` with the plugin under test (so the peer endpoints exist), and assert against `shape.transformation`. Match the existing assertions' intent; only the registry source changes.

- [ ] **Step 2: Build the tests**

```bash
cmake --build build-v057 -j"$(nproc)" 2>&1 | grep -E "error:" | head
```
Expected: **0 errors** across the whole project now.

- [ ] **Step 3: Commit** `git add tests/plugins/contacts/ && git commit -m "test(contacts): use injected ShapeRegistries, not deleted ::instance() (O7)"`.

---

### Task 7: Bump the pin to v0.57, verify the real delivery path, record gitlinks, commit

**Files:** `CMakeLists.txt:63`, submodule gitlinks.

- [ ] **Step 1: Confirm v0.57 tag is on Codeberg**

```bash
git -C ../libkalburator ls-remote --tags origin v0.57
```
Expected: a line ending `refs/tags/v0.57`. If absent, STOP — the pin won't resolve for clean builds.

- [ ] **Step 2: Bump the pin**

In `CMakeLists.txt:63`, set the tag to `v0.57`:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v0.57" CACHE STRING
    "libkalburator tag to fetch when WILDPALMS_LIBKALBURATOR_SOURCE_DIR is unset")
```

- [ ] **Step 3: Fresh FetchContent build + full test suite (the real CI/laptop path)**

```bash
rm -rf build-v057check
cmake -S . -B build-v057check -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1 | grep -iE "fetching v0.57|Configuring done|CMake Error"
cmake --build build-v057check -j"$(nproc)" 2>&1 | grep -cE "error:" | xargs echo "errors:"
ctest --test-dir build-v057check 2>&1 | tail -4
```
Expected: `fetching v0.57 from Codeberg`; `errors: 0`; `103/103` (or the current count) tests pass.

- [ ] **Step 4: Clean scratch; stage superproject (pin + all four gitlinks)**

```bash
rm -rf build-v057check
git add CMakeLists.txt src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
git status --short
```
Expected: `CMakeLists.txt` modified + four gitlink updates staged.

- [ ] **Step 5: Commit the superproject**

```bash
git commit -m "build(deps): adopt libkalburator v0.57 (O7 ShapeRegistries injection)

Bumps the pin v0.56-o15-converged -> v0.57 and records the four plugin
submodule gitlinks that convert to the ShapeContribution pattern. WP now
injects one ShapeRegistries at the PalmRuntime composition root. Verified on
the FetchContent-from-Codeberg path: 0 errors, full ctest suite green.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 6: Push superproject + submodules**

```bash
git -C src/plugins/calendar push && git -C src/plugins/contacts push && git -C src/plugins/memo push && git -C src/plugins/todos push
git push
```
(Push submodule commits BEFORE the superproject so the recorded gitlinks resolve on other machines.)

---

## Self-Review

- **Spec coverage:** Implements the mandatory O7 port (umbrella spec sub-project A2, pulled forward because v0.57 deleted the transitional scaffolding). The topology-generator *adoption* (generateMappings/authority) is explicitly out of scope here — it's async, gated on a Phase-3 handoff libkalburator hasn't written yet.
- **Placeholder scan:** Per-plugin code is given in full (catalogues, shapes, edges, stage classes, loss fns) from the verified current `registerWith` bodies. Task 6 describes test edits by intent because the assertions vary; the mechanical change (registry source) is explicit.
- **Consistency:** All four plugins follow the identical `XPalmShapes : ShapeContribution` structure; the memo domain is `note` throughout (Task 1 Step 4 manifest, Task 4 shapes); pin target `v0.57` consistent in Tasks 7.1–7.3.
- **Ordering:** stock plugins load before WP plugins (Task 1 Step 4), so the dropped defensive placeholder registrations (Tasks 2–5) are safe — peer endpoints (`ical`/`vcard4`/`canon`/`ical-vtodo`) exist when WP edges register. The submodule-before-superproject push order is in Task 7 Step 6.
- **Risk:** the dropped defensive placeholders assume the stock plugins always register peers first. If any unit test wires a plugin's contribution WITHOUT loading stock plugins, its edge registration will fail the endpoint check — Task 6 handles this for the known contacts tests by loading stock plugins in the fixture.
