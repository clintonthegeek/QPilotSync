# Hub↔Remote Routing — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore remote sync after sub-project C by translating each persisted user remote SyncMapping into a per-route `LogicalCalendar` whose Primary is a category-filtered view of the hub (a libkalburator `FilteredCollectionBackend`) and whose Sync1 is the user's remote calendar. `generateMappings(Star)` then runs the union of C's per-domain Palm↔hub mappings and the new per-route view↔remote mappings.

**Architecture:** `PalmRuntime` gains a `m_routeViews` vector that owns one `FilteredCollectionBackend` per category-route (registered as `wp-route-<id>`). A new free function in `src/runtime/routemapping.{h,cpp}` translates each persisted SyncMapping JSON entry into a typed `RouteSpec` (carrying the LC + the filter + the binding ids). `finishConnect` builds the route views, appends per-route LCs to the LC list, and calls `generateMappings(...)` over the union. Wildcard-source mappings bypass the filter (bind the LC's Primary directly to the unfiltered hub collection).

**Tech Stack:** C++/Qt6, KF6, CMake (legacy, build dir `build-c`), CTest, libkalburator (FetchContent, must be re-pinned to a version that exports `Kalburator::Shape::RecordFilter` + `Kalburator::Sinks::FilteredCollectionBackend`).

**Spec:** `docs/superpowers/specs/2026-05-28-subproject-hub-remote-routing-design.md`.
**Lib RFC (prerequisite):** `docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md`.

**Iteration build:** `cmake -S . -B build-c -DCMAKE_BUILD_TYPE=Debug -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator` then `cmake --build build-c -j"$(nproc)"`.

---

## File structure

| File | Role |
|------|------|
| `CMakeLists.txt` | bump `WILDPALMS_LIBKALBURATOR_GIT_TAG` to the release that carries `FilteredCollectionBackend`. |
| `src/runtime/routemapping.{h,cpp}` (new) | One free function `translateRouteSpec(const SyncMapping &persisted, const QHash<QString, CategoryMappingStore*> &stores) -> std::optional<RouteSpec>`. `RouteSpec` is a small struct describing what to build (filter or wildcard) for one persisted mapping. |
| `src/runtime/palmruntime.h` | Add member `std::vector<std::unique_ptr<Kalburator::Sinks::FilteredCollectionBackend>> m_routeViews;` (after `m_hub`, before `m_engine`). Declare `void buildRouteLogicalCalendars(QList<Kalburator::Sync::LogicalCalendar> &lcs);`. |
| `src/runtime/palmruntime.cpp` | Implement `buildRouteLogicalCalendars`. Call it inside `finishConnect()` after the per-domain LC loop and BEFORE `generateMappings(lcs, Star)`. |
| `tests/runtime/tst_route_mapping.cpp` (new) | Unit-test `translateRouteSpec`: slot-mapped, wildcard, missing slot name. |
| `tests/runtime/tst_palm_runtime_routes.cpp` (new) | Integration: persisted mapping ⇒ engine receives the per-route SyncMapping after Palm↔hub. |
| `tests/runtime/tst_palm_runtime_route_first_sync.cpp` (new) | Mock remote backend; Palm slot 3 → remote calendar in one sync, and reverse. |
| `tests/runtime/tst_palm_runtime_route_recategorization.cpp` (new) | Hub canon `categories` change moves record between two remote routes across two syncs. |
| `tests/runtime/CMakeLists.txt` | Register the four new tests. |

---

### Task 1: Re-pin libkalburator + verify `FilteredCollectionBackend` is exported

**Files:** `CMakeLists.txt:63`

- [ ] **Step 1: Confirm the new release tag exists on Codeberg.**

The libkalburator RFC at `docs/2026-05-28-libkalburator-filteredcollectionbackend-proposal.md` proposes the addition. Before this plan can proceed, libkalburator must have landed the API in a tagged release (call it `<TARGET_TAG>` — likely `v0.58` or a successor of `v0.57`).

Run:
```bash
git -C /home/clinton/dev/libkalburator fetch --tags origin 2>&1 | tail
git -C /home/clinton/dev/libkalburator ls-remote --tags origin | grep -E "v0\.5[89]|v0\.6" | head
```
Pick the lowest tag that includes `FilteredCollectionBackend`. If no such tag exists yet, STOP — this plan blocks until the lib has shipped it. Record the chosen tag as `<TARGET_TAG>` for the rest of this plan.

- [ ] **Step 2: Verify the actual exports match the RFC.**

Check that the symbols the plan depends on exist in the chosen tag:
```bash
git -C /home/clinton/dev/libkalburator show <TARGET_TAG>:src/types/recordfilter.h | head -40
git -C /home/clinton/dev/libkalburator show <TARGET_TAG>:src/universal/filteredcollectionbackend.h | head -40
```
Both must be present. Note the EXACT namespace (the RFC proposes `Kalburator::Shape::RecordFilter` and `Kalburator::Sinks::FilteredCollectionBackend`) and adjust later tasks' includes/usings if the implementation chose a different namespace.

- [ ] **Step 3: Bump the pin.**

In `CMakeLists.txt:63`:
```cmake
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "<TARGET_TAG>" CACHE STRING
    "libkalburator tag to fetch when WILDPALMS_LIBKALBURATOR_SOURCE_DIR is unset")
```
(Replace `<TARGET_TAG>` with the actual tag string from Step 1.)

- [ ] **Step 4: Configure + build against the new pin.**

```bash
cd /home/clinton/dev/WildPalms
cmake --build build-c -j"$(nproc)" 2>&1 | grep -cE "error:" | xargs echo "errors:"
```
Expected: `errors: 0` (no source changes yet; this just confirms the new lib version compiles WP as-is).

- [ ] **Step 5: Run the full suite as a sanity baseline.**

```bash
ctest --test-dir build-c 2>&1 | tail -4
```
Expected: 103/103 (or whatever count the branch currently has — same as after sub-project C). Any new failure caused by the lib bump must be investigated before proceeding; do NOT continue this plan with regressions.

- [ ] **Step 6: Commit the pin bump.**

```bash
git add CMakeLists.txt
git commit -m "build(deps): re-pin libkalburator to <TARGET_TAG> (FilteredCollectionBackend)

Required by hub<->remote routing (sub-project after C). Spec:
docs/superpowers/specs/2026-05-28-subproject-hub-remote-routing-design.md

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: `routemapping.{h,cpp}` — translate persisted SyncMapping → RouteSpec

**Files:**
- Create: `src/runtime/routemapping.h`
- Create: `src/runtime/routemapping.cpp`
- Modify: `src/CMakeLists.txt` (add the two files to the runtime target — look for where `palmruntime.cpp` is listed and add alongside)
- Create: `tests/runtime/tst_route_mapping.cpp`
- Modify: `tests/runtime/CMakeLists.txt` (register the new test mirroring an existing `tst_*` registration in the same file)

- [ ] **Step 1: Write the failing test** at `tests/runtime/tst_route_mapping.cpp`:
```cpp
#include <QtTest/QtTest>
#include <QHash>

#include "runtime/routemapping.h"
#include "palm/calendar/categorymappingstore.h"
#include "synctypes.h"

using WildPalms::Runtime::RouteSpec;
using WildPalms::Runtime::translateRouteSpec;
using WildPalms::PalmCalendar::CategoryMappingStore;
using Kalburator::Sync::SyncMapping;

class TestRouteMapping : public QObject { Q_OBJECT
private slots:
    void slotMapped_yieldsFilteredRoute()
    {
        CategoryMappingStore cats;
        cats.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
        QHash<QString, CategoryMappingStore*> stores{{QStringLiteral("calendar"), &cats}};

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-work");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QStringLiteral("palm:calendar/3");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("WorkCal");
        persisted.enabled        = true;

        const auto specOpt = translateRouteSpec(persisted, stores);
        QVERIFY(specOpt.has_value());
        const auto &s = *specOpt;
        QCOMPARE(s.kind,                   RouteSpec::Kind::Filtered);
        QCOMPARE(s.domain,                 QStringLiteral("calendar"));
        QCOMPARE(s.hubCollectionId,        QStringLiteral("calendar"));
        QCOMPARE(s.categoryName,           QStringLiteral("Work"));
        QCOMPARE(s.remoteBackendId,        QStringLiteral("caldav-uuid"));
        QCOMPARE(s.remoteCollectionId,     QStringLiteral("WorkCal"));
        QCOMPARE(s.lcId,                   QStringLiteral("wp-route-user-cal-work"));
    }

    void wildcardSource_yieldsDirectRoute()
    {
        QHash<QString, CategoryMappingStore*> stores;  // unused for wildcard

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-all");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QString();   // wildcard
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Personal");
        persisted.enabled        = true;

        const auto specOpt = translateRouteSpec(persisted, stores);
        QVERIFY(specOpt.has_value());
        QCOMPARE(specOpt->kind,            RouteSpec::Kind::Direct);
        QCOMPARE(specOpt->domain,          QStringLiteral("calendar"));
        QCOMPARE(specOpt->hubCollectionId, QStringLiteral("calendar"));
        QCOMPARE(specOpt->categoryName,    QString());
        QCOMPARE(specOpt->remoteCollectionId, QStringLiteral("Personal"));
    }

    void slotMapped_unknownSlotName_returnsNullopt()
    {
        CategoryMappingStore cats;  // slot 7 NOT named
        QHash<QString, CategoryMappingStore*> stores{{QStringLiteral("calendar"), &cats}};

        SyncMapping persisted;
        persisted.id             = QStringLiteral("user-cal-7");
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QStringLiteral("palm:calendar/7");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Foo");
        persisted.enabled        = true;

        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }

    void disabledPersisted_returnsNullopt()
    {
        QHash<QString, CategoryMappingStore*> stores;
        SyncMapping persisted;
        persisted.sourceBackend  = QStringLiteral("calendar");
        persisted.sourceCalendar = QString();
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("Personal");
        persisted.enabled        = false;
        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }

    void unknownPalmDomain_returnsNullopt()
    {
        QHash<QString, CategoryMappingStore*> stores;
        SyncMapping persisted;
        persisted.sourceBackend  = QStringLiteral("not-a-palm-plugin");
        persisted.sourceCalendar = QStringLiteral("palm:???/3");
        persisted.targetBackend  = QStringLiteral("caldav-uuid");
        persisted.targetCalendar = QStringLiteral("X");
        persisted.enabled        = true;
        QCOMPARE(translateRouteSpec(persisted, stores), std::nullopt);
    }
};
QTEST_GUILESS_MAIN(TestRouteMapping)
#include "tst_route_mapping.moc"
```

Register the test in `tests/runtime/CMakeLists.txt` by copying the block for an existing palm-runtime test (e.g. `tst_palm_runtime_modes`) and renaming. Look for `ecm_add_test(...)` / `add_test(...)` / link-libraries lines and mirror them.

- [ ] **Step 2: Run, expect FAIL** (`routemapping.h` doesn't exist):
```bash
cmake --build build-c -j"$(nproc)" --target tst_route_mapping 2>&1 | grep -cE "error:" | xargs echo "errors:"
```
Expected: nonzero (file not found).

- [ ] **Step 3: Implement `routemapping.h`:**
```cpp
#ifndef WILDPALMS_RUNTIME_ROUTEMAPPING_H
#define WILDPALMS_RUNTIME_ROUTEMAPPING_H

#include <QHash>
#include <QString>
#include <optional>

namespace Kalburator::Sync { struct SyncMapping; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Runtime {

/// A typed translation of one persisted user SyncMapping into the pieces the
/// runtime needs to materialize a per-route LogicalCalendar.
///
/// Two kinds exist because the wizard / F.3 graph writes two shapes of
/// persisted mapping (see spec §6.2):
///   - Filtered: sourceCalendar == "palm:<domain>/<slot>" → a slot-mapped route;
///     a FilteredCollectionBackend wrapping wp-hub:<domain> with filter
///     `categories Contains <categoryName>`.
///   - Direct:   sourceCalendar.isEmpty() → a wildcard route; the LC's Primary
///     binds directly to wp-hub:<domain> (no filter, no wrapper backend).
struct RouteSpec {
    enum class Kind { Filtered, Direct };
    Kind     kind;
    QString  domain;             ///< "calendar"/"contacts"/"todo"/"note"
    QString  hubCollectionId;    ///< the hub's per-domain collection id (== domain today)
    QString  categoryName;       ///< empty for Direct
    QString  remoteBackendId;    ///< from persisted.targetBackend
    QString  remoteCollectionId; ///< from persisted.targetCalendar
    QString  lcId;               ///< "wp-route-<persisted.id>"
};

/// Map plugin id → "<canonical domain>" (the domain whose hub collection the
/// plugin syncs to). Static; used by translation. Public for testability.
QString domainForPalmPluginId(const QString &pluginId);

/// Translate one persisted SyncMapping into a RouteSpec, or std::nullopt
/// when the mapping cannot be translated (disabled, unknown plugin, unknown
/// slot, malformed sourceCalendar).
std::optional<RouteSpec> translateRouteSpec(
    const Kalburator::Sync::SyncMapping &persisted,
    const QHash<QString, WildPalms::PalmCalendar::CategoryMappingStore*> &storesByDomain);

} // namespace WildPalms::Runtime

#endif
```

- [ ] **Step 4: Implement `routemapping.cpp`:**
```cpp
#include "routemapping.h"

#include "palm/calendar/categorymappingstore.h"
#include "synctypes.h"

namespace WildPalms::Runtime {

QString domainForPalmPluginId(const QString &pluginId)
{
    if (pluginId == QLatin1String("calendar")) return QStringLiteral("calendar");
    if (pluginId == QLatin1String("contacts")) return QStringLiteral("contacts");
    if (pluginId == QLatin1String("memo"))     return QStringLiteral("note");
    if (pluginId == QLatin1String("todo"))     return QStringLiteral("todo");
    return {};
}

std::optional<RouteSpec> translateRouteSpec(
    const Kalburator::Sync::SyncMapping &p,
    const QHash<QString, WildPalms::PalmCalendar::CategoryMappingStore*> &stores)
{
    if (!p.enabled) return std::nullopt;

    const QString domain = domainForPalmPluginId(p.sourceBackend);
    if (domain.isEmpty()) return std::nullopt;

    RouteSpec spec;
    spec.domain             = domain;
    spec.hubCollectionId    = domain;          // hub collection id == canonical domain id
    spec.remoteBackendId    = p.targetBackend;
    spec.remoteCollectionId = p.targetCalendar;
    spec.lcId               = QStringLiteral("wp-route-") + p.id;

    // Direct: sourceCalendar empty == wildcard ("send all of this domain to this remote").
    if (p.sourceCalendar.isEmpty()) {
        spec.kind         = RouteSpec::Kind::Direct;
        spec.categoryName = QString();
        return spec;
    }

    // Filtered: sourceCalendar must be "palm:<domain>/<slot>".
    // Parse and resolve the slot name from the domain's store.
    const QString prefix = QStringLiteral("palm:") + domain + QLatin1Char('/');
    if (!p.sourceCalendar.startsWith(prefix)) return std::nullopt;
    bool ok = false;
    const int slot = p.sourceCalendar.mid(prefix.size()).toInt(&ok);
    if (!ok || slot < 0 || slot > 15) return std::nullopt;

    auto storeIt = stores.constFind(domain);
    if (storeIt == stores.constEnd() || !storeIt.value()) return std::nullopt;

    // Resolve via the Palm DB name used by the relevant plugin. (Each plugin
    // populates its store under a single db name — DatebookDB / AddressDB /
    // ToDoDB / MemoDB — but the store is keyed by db name, so we ask the
    // first/only db it has data for.)
    // The simplest robust resolution: try the known db name for the domain.
    const QString dbName = (domain == QLatin1String("calendar")) ? QStringLiteral("DatebookDB")
                         : (domain == QLatin1String("contacts")) ? QStringLiteral("AddressDB")
                         : (domain == QLatin1String("todo"))     ? QStringLiteral("ToDoDB")
                         : (domain == QLatin1String("note"))     ? QStringLiteral("MemoDB")
                         : QString();
    if (dbName.isEmpty()) return std::nullopt;

    const QString name = storeIt.value()->slotName(dbName, slot);
    if (name.isEmpty()) return std::nullopt;  // unknown slot name → drop the route

    spec.kind         = RouteSpec::Kind::Filtered;
    spec.categoryName = name;
    return spec;
}

} // namespace WildPalms::Runtime
```

Add `RouteSpec` equality (for `QCOMPARE(spec, nullopt)` style tests to compile and for any later assertions):
```cpp
// Add inside the namespace in routemapping.h, after the struct:
inline bool operator==(const RouteSpec &a, const RouteSpec &b) {
    return a.kind == b.kind && a.domain == b.domain
        && a.hubCollectionId == b.hubCollectionId
        && a.categoryName == b.categoryName
        && a.remoteBackendId == b.remoteBackendId
        && a.remoteCollectionId == b.remoteCollectionId
        && a.lcId == b.lcId;
}
```

Add the two source files to `src/CMakeLists.txt` alongside `palmruntime.cpp` / `palmruntime.h`.

- [ ] **Step 5: Run, expect PASS:**
```bash
cmake --build build-c -j"$(nproc)" --target tst_route_mapping
ctest --test-dir build-c -R tst_route_mapping
```
Expected: 1/1 passed.

- [ ] **Step 6: Commit (main repo).**
```bash
git add src/runtime/routemapping.h src/runtime/routemapping.cpp \
        src/CMakeLists.txt \
        tests/runtime/tst_route_mapping.cpp tests/runtime/CMakeLists.txt
git commit -m "feat(runtime): translateRouteSpec — persisted SyncMapping -> RouteSpec

Translates the two persisted shapes (wildcard / slot-mapped per F.3) into a
typed RouteSpec the runtime will use to materialize a FilteredCollectionBackend
+ LogicalCalendar per route. Pure function with unit tests.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 3: PalmRuntime — own per-route views, build per-route LCs in `finishConnect`

**Files:**
- Modify: `src/runtime/palmruntime.h` (member + private method decl + includes)
- Modify: `src/runtime/palmruntime.cpp` (implement + wire into `finishConnect`)

- [ ] **Step 1: Add header declarations.** In `palmruntime.h`, after the `m_hub` member declaration:
```cpp
    // Hub<->remote routing (sub-project after C): one FilteredCollectionBackend
    // per category-route. Declared right after m_hub (which it borrows) and
    // BEFORE m_engine so the engine's registered borrowed pointers are still
    // valid at destruction.
    std::vector<std::unique_ptr<Kalburator::Sinks::FilteredCollectionBackend>>
        m_routeViews;
```

Add a forward declaration with the other libkalburator namespace forwards:
```cpp
namespace Kalburator::Sinks {
    class FilteredCollectionBackend;
}
```

In the `private:` section near `ensureHubCollections`, add the method declaration:
```cpp
    /// Append one LogicalCalendar per category-route (translated from the
    /// persisted user mappings) to `lcs`. Owned FilteredCollectionBackend
    /// instances land in m_routeViews and are registered with m_registry
    /// under the id "wp-route-<lcId>". No-op when m_mappings is empty.
    void buildRouteLogicalCalendars(QList<Kalburator::Sync::LogicalCalendar> &lcs);
```

- [ ] **Step 2: Add the source includes** in `palmruntime.cpp`, alongside the existing libkalburator includes:
```cpp
#include <filteredcollectionbackend.h>
#include <recordfilter.h>

#include "routemapping.h"
```

- [ ] **Step 3: Implement `buildRouteLogicalCalendars`.** Add it near `ensureHubCollections`:
```cpp
void PalmRuntime::buildRouteLogicalCalendars(
    QList<Kalburator::Sync::LogicalCalendar> &lcs)
{
    using namespace WildPalms::CalendarPlugin;
    using namespace WildPalms::ContactsPlugin;
    using namespace WildPalms::Memo;
    using namespace WildPalms::TodoPlugin;
    using Kalburator::Sync::LogicalCalendar;
    using Kalburator::Sync::CalendarBackendBinding;
    using Kalburator::Sync::BackendRole;

    if (m_mappings.isEmpty()) return;

    // Collect the per-domain CategoryMappingStore pointers from the loaded
    // plugins. The plugin owns its store; we borrow it for translation.
    QHash<QString, WildPalms::PalmCalendar::CategoryMappingStore*> stores;
    for (const auto &plugin : m_palmPlugins) {
        if (auto *p = dynamic_cast<CalendarBackendPlugin *>(plugin.get()))
            stores.insert(QStringLiteral("calendar"), p->categoryStore());
        else if (auto *p = dynamic_cast<ContactsBackendPlugin *>(plugin.get()))
            stores.insert(QStringLiteral("contacts"), p->categoryStore());
        else if (auto *p = dynamic_cast<MemoPlugin *>(plugin.get()))
            stores.insert(QStringLiteral("note"), p->categoryStore());
        else if (auto *p = dynamic_cast<TodoBackendPlugin *>(plugin.get()))
            stores.insert(QStringLiteral("todo"), p->categoryStore());
    }

    for (const auto &persisted : m_mappings) {
        const auto specOpt =
            WildPalms::Runtime::translateRouteSpec(persisted, stores);
        if (!specOpt) continue;
        const auto &s = *specOpt;

        // Decide the Primary binding's backend + collection ids.
        QString primaryBackendId = QStringLiteral("wp-hub");
        QString primaryColId     = s.hubCollectionId;

        if (s.kind == WildPalms::Runtime::RouteSpec::Kind::Filtered) {
            // Materialize a FilteredCollectionBackend that wraps the hub for
            // this category. Register under "wp-route-<lcId>" so the engine
            // can resolve it by id.
            Kalburator::Shape::RecordFilter filter;
            filter.property = Kalburator::Shape::PropertyId{QStringLiteral("categories")};
            filter.op       = Kalburator::Shape::RecordFilter::Op::Contains;
            filter.value    = s.categoryName;

            const QString virtualColId =
                QStringLiteral("route-") + s.categoryName;

            auto view = std::make_unique<Kalburator::Sinks::FilteredCollectionBackend>(
                m_hub.get(),
                s.hubCollectionId,
                virtualColId,
                filter);

            m_registry->registerBackendInstance(s.lcId, view.get());
            m_routeViews.push_back(std::move(view));

            primaryBackendId = s.lcId;
            primaryColId     = virtualColId;
        }
        // else Kind::Direct: Primary stays wp-hub:<domain>. No wrapper needed.

        LogicalCalendar lc;
        lc.id          = s.lcId;
        lc.domain      = Kalburator::Shape::DomainId{s.domain};
        lc.displayName = s.lcId;
        lc.syncEnabled = true;

        CalendarBackendBinding primary;
        primary.backendId  = primaryBackendId;
        primary.calendarId = primaryColId;
        primary.role       = BackendRole::Primary;
        lc.bindings.append(primary);

        CalendarBackendBinding sync;
        sync.backendId  = s.remoteBackendId;
        sync.calendarId = s.remoteCollectionId;
        sync.role       = BackendRole::Sync1;
        sync.syncOrder  = 1;
        lc.bindings.append(sync);

        lcs.append(lc);
    }
}
```

- [ ] **Step 4: Expose `categoryStore()` from each plugin (if not already public).**

Each Palm backend plugin owns `m_categoryStore` as a `std::unique_ptr`. Add a const accessor `WildPalms::PalmCalendar::CategoryMappingStore *categoryStore() const { return m_categoryStore.get(); }` to:
- `src/plugins/calendar/calendarbackendplugin.h`
- `src/plugins/contacts/contactsbackendplugin.h`
- `src/plugins/memo/memobackendplugin.h`
- `src/plugins/todos/todobackendplugin.h`

Each is a one-line addition in the `public:` section. Commit each submodule + bump the four gitlinks.

- [ ] **Step 5: Wire into `finishConnect`.**

In `palmruntime.cpp finishConnect()`, locate the block that ends with
`m_mappings = Kalburator::Sync::generateMappings(lcs, Kalburator::Sync::SyncTopology::Star);`
(introduced in sub-project C). Replace those final two lines with:
```cpp
    // Translate persisted user mappings into per-route LCs. Each Filtered
    // route materializes a FilteredCollectionBackend wrapping the hub; each
    // Direct route binds the LC's Primary to wp-hub directly. lcs is appended
    // to in place.
    buildRouteLogicalCalendars(lcs);

    m_mappings = Kalburator::Sync::generateMappings(
        lcs, Kalburator::Sync::SyncTopology::Star);
    m_engine->setSyncMappings(m_mappings);
```

Note the subtle but important interaction: the persisted mappings are read into `m_mappings` by `loadMappingsFromProfile()` at the top of `finishConnect` (the documented "TODO(C-remote)" seam). `buildRouteLogicalCalendars` iterates that very `m_mappings` — translating each into a route LC — and then the SAME `m_mappings` is OVERWRITTEN by `generateMappings(lcs, Star)`. We've finished reading before the overwrite, so the order is safe.

- [ ] **Step 6: Build:**
```bash
cmake --build build-c -j"$(nproc)" 2>&1 | grep -cE "error:" | xargs echo "errors:"
```
Expected: 0.

- [ ] **Step 7: Run the full suite as a sanity:**
```bash
ctest --test-dir build-c 2>&1 | tail -4
```
Expected: 103/103 or higher (no integration tests yet; existing tests must not regress). If a runtime test that DIDN'T use persisted mappings has started behaving differently, investigate before continuing.

- [ ] **Step 8: Commit superproject (and any submodule gitlinks updated in Step 4).**
```bash
git -C src/plugins/calendar commit -am "feat: expose categoryStore() accessor (for hub<->remote routing)" \
  || echo "(no change in calendar submodule)"
# repeat for contacts/memo/todos as needed
git add src/runtime/palmruntime.h src/runtime/palmruntime.cpp \
        src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
git commit -m "feat(runtime): build per-route LogicalCalendars at finishConnect

Translates persisted user remote SyncMappings (via translateRouteSpec) into
per-route LCs whose Primary is either a FilteredCollectionBackend (category
route) or the hub directly (wildcard route). Routes feed into the same
generateMappings(Star) call as the C per-domain LCs.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```
(Push of the submodules + superproject is deferred to Task 7.)

---

### Task 4: Integration test — persisted mapping ⇒ per-route engine mapping

**Files:** `tests/runtime/tst_palm_runtime_routes.cpp` (new); `tests/runtime/CMakeLists.txt` (register).

- [ ] **Step 1: Write the failing test:**
```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "synctypes.h"

class TstPalmRuntimeRoutes : public QObject { Q_OBJECT
private slots:
    void filteredRoute_yieldsRouteMappingInAdditionToPerDomain()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        // Seed a persisted slot-mapped user mapping.
        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u1");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/3");
        m[QStringLiteral("targetBackend")]   = QStringLiteral("caldav-uuid");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("WorkCal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        // Manually populate the calendar plugin's CategoryMappingStore with
        // slot 3 = "Work" (in normal operation AppInfo parsing does this at
        // createPalmBackend time; here we set it directly via the plugin's
        // accessor for the test).
        using WildPalms::CalendarPlugin::CalendarBackendPlugin;
        CalendarBackendPlugin *cal = nullptr;
        for (const auto &p : runtime.palmPlugins())
            if (auto *c = dynamic_cast<CalendarBackendPlugin*>(p.get())) cal = c;
        QVERIFY(cal);
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));

        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        const auto mappings = runtime.palmMappings();
        // 4 per-domain (Palm<->hub) + 1 per-route (view<->WorkCal) = 5.
        QCOMPARE(mappings.size(), 5);

        // Find the route mapping.
        int routeIdx = -1;
        for (int i = 0; i < mappings.size(); ++i)
            if (mappings[i].id.contains(QStringLiteral("wp-route-u1"))) routeIdx = i;
        QVERIFY(routeIdx >= 0);

        const auto &r = mappings[routeIdx];
        QCOMPARE(r.sourceBackend,  QStringLiteral("wp-route-u1"));
        QCOMPARE(r.sourceCalendar, QStringLiteral("route-Work"));
        QCOMPARE(r.targetBackend,  QStringLiteral("caldav-uuid"));
        QCOMPARE(r.targetCalendar, QStringLiteral("WorkCal"));

        // The wp-route-u1 backend must be registered in the runtime registry.
        QVERIFY(runtime.backendRegistry().backendInstance(
                    QStringLiteral("wp-route-u1")) != nullptr);
    }

    void wildcardRoute_bindsHubDirectly()
    {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u2");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QString();   // wildcard
        m[QStringLiteral("targetBackend")]   = QStringLiteral("caldav-uuid");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("Personal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        const auto mappings = runtime.palmMappings();
        // 4 per-domain + 1 wildcard route = 5.
        QCOMPARE(mappings.size(), 5);

        int routeIdx = -1;
        for (int i = 0; i < mappings.size(); ++i)
            if (mappings[i].id.contains(QStringLiteral("wp-route-u2"))) routeIdx = i;
        QVERIFY(routeIdx >= 0);
        const auto &r = mappings[routeIdx];
        // Direct: Primary is wp-hub itself, not a wp-route-* wrapper.
        QCOMPARE(r.sourceBackend,  QStringLiteral("wp-hub"));
        QCOMPARE(r.sourceCalendar, QStringLiteral("calendar"));
        QCOMPARE(r.targetBackend,  QStringLiteral("caldav-uuid"));
        QCOMPARE(r.targetCalendar, QStringLiteral("Personal"));

        // No wp-route-u2 backend gets registered for a direct route.
        QVERIFY(runtime.backendRegistry().backendInstance(
                    QStringLiteral("wp-route-u2")) == nullptr);
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeRoutes)
#include "tst_palm_runtime_routes.moc"
```

Register in `tests/runtime/CMakeLists.txt` mirroring `tst_palm_runtime_modes` (same link libraries).

- [ ] **Step 2: Build + run:**
```bash
cmake --build build-c -j"$(nproc)" --target tst_palm_runtime_routes
ctest --test-dir build-c -R tst_palm_runtime_routes
```
Expected: 2/2 pass. If the count assertion fails (e.g. 4 instead of 5), `buildRouteLogicalCalendars` isn't getting called or isn't adding the LC; read the test failure carefully.

- [ ] **Step 3: Commit (main repo).**
```bash
git add tests/runtime/tst_palm_runtime_routes.cpp tests/runtime/CMakeLists.txt
git commit -m "test(runtime): per-route LCs append to the C per-domain set

Asserts: filtered route (palm:<domain>/<slot>) materializes a wp-route-<id>
FilteredCollectionBackend and its mapping pairs route-<slotName> with the
remote; wildcard route binds Primary to wp-hub directly with no wrapper.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 5: Integration test — first-sync routing (Palm slot 3 ⇄ remote)

**Files:** `tests/runtime/tst_palm_runtime_route_first_sync.cpp` (new); register.

This test uses `MockBlobBackend` (libkalburator-provided, already used by other runtime tests) as the "remote". The PalmRuntime is given a mock device with one record in category 3, the persisted mapping `(palm:calendar/3 → mock-remote:WorkCal)` and slot 3 = "Work" in the store. After `hotSync()`, the mock remote should have one record; after a second sync seeded with a record on the remote side, the Palm should have it in slot 3.

- [ ] **Step 1: Write the failing test.** Model the device + mock setup on `tests/runtime/tst_palm_runtime_hotsync.cpp` (which already does the analogous pattern for Palm↔hub):
```cpp
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmrecord.h"
#include "palm/calendar/datebookcodec.h"
#include "synctypes.h"
#include "backendregistry.h"
#include "iblobbackend.h"

// MockBlobBackend lives in libkalburator's test fixtures and is used by
// tst_palm_runtime_hotsync.cpp; copy that include layout here.
#include "mockblobbackend.h"

class TstPalmRuntimeRouteFirstSync : public QObject { Q_OBJECT
private slots:
    void initTestCase() {}

    void palm_slot3_record_lands_on_mock_remote_workcal()
    {
        // 1. Build a Palm record in DatebookDB slot 3 ("Work").
        auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
        mockDb->createDatabase(QStringLiteral("DatebookDB"));
        auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
        event->setSummary(QStringLiteral("Sprint planning"));
        event->setDtStart(QDateTime(QDate(2026,6,1), QTime(9,0), QTimeZone::utc()));
        event->setDtEnd  (QDateTime(QDate(2026,6,1), QTime(10,0), QTimeZone::utc()));
        WildPalms::PalmSync::PalmRecord pr =
            WildPalms::PalmCalendar::DatebookCodec::encode(event, /*slot*/ 3);
        pr.recordId = 101;
        mockDb->createRecord(QStringLiteral("DatebookDB"), pr);

        // 2. Stand up the runtime + persist the route mapping + name slot 3.
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        WildPalms::Runtime::PalmRuntime runtime(tmp.path());

        QJsonObject m;
        m[QStringLiteral("id")]              = QStringLiteral("u1");
        m[QStringLiteral("sourceBackend")]   = QStringLiteral("calendar");
        m[QStringLiteral("sourceCalendar")]  = QStringLiteral("palm:calendar/3");
        m[QStringLiteral("targetBackend")]   = QStringLiteral("mock-remote");
        m[QStringLiteral("targetCalendar")]  = QStringLiteral("WorkCal");
        m[QStringLiteral("mode")]            = QStringLiteral("TwoWay");
        m[QStringLiteral("conflictPolicy")]  = QStringLiteral("AskUser");
        m[QStringLiteral("lossPolicy")]      = QStringLiteral("Warn");
        m[QStringLiteral("enabled")]         = true;
        QJsonArray arr; arr.append(m);
        runtime.reloadMappings(arr);

        using WildPalms::CalendarPlugin::CalendarBackendPlugin;
        CalendarBackendPlugin *cal = nullptr;
        for (const auto &p : runtime.palmPlugins())
            if (auto *c = dynamic_cast<CalendarBackendPlugin*>(p.get())) cal = c;
        QVERIFY(cal);
        cal->categoryStore()->setSlotName(
            QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));

        // 3. Register a MockBlobBackend with id "mock-remote" + collection "WorkCal".
        auto mockRemote = std::make_unique<Kalburator::Test::MockBlobBackend>();
        Kalburator::Sync::CollectionInfo workcal;
        workcal.id = QStringLiteral("WorkCal");
        workcal.name = QStringLiteral("WorkCal");
        workcal.type = QStringLiteral("calendar");
        mockRemote->createCollection(workcal,
            Kalburator::Shape::Shape{
                Kalburator::Shape::DomainId{QStringLiteral("calendar")},
                Kalburator::Shape::EncodingId{QStringLiteral("ical")} });
        runtime.backendRegistry().registerBackendInstance(
            QStringLiteral("mock-remote"), mockRemote.get());

        // 4. Connect the mock device (triggers finishConnect → build route LCs).
        auto dev = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
            std::move(mockDb), nullptr);
        runtime.setDeviceAccessForTest(std::move(dev));

        // 5. Hot sync; expected: WorkCal gains the record.
        auto fut = runtime.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 5000);
        QVERIFY(fut.resultAt(0).success);

        const auto remoteRecords = mockRemote->loadRecords(QStringLiteral("WorkCal"));
        QCOMPARE(remoteRecords.size(), 1);
    }
};
QTEST_GUILESS_MAIN(TstPalmRuntimeRouteFirstSync)
#include "tst_palm_runtime_route_first_sync.moc"
```

Register in `tests/runtime/CMakeLists.txt` (same libs as `tst_palm_runtime_hotsync` plus whatever provides `Kalburator::Test::MockBlobBackend`).

- [ ] **Step 2: Run, expect PASS** (`ctest --test-dir build-c -R tst_palm_runtime_route_first_sync`). If it fails at the count assertion, inspect the engine log + verify the route mapping is being executed (`runtime.palmMappings()` should include the route mapping; loadRecords on the filtered view should return the record after Palm↔hub has run).

- [ ] **Step 3: Commit (main repo).**

---

### Task 6: Integration test — recategorization moves a record between routes

**Files:** `tests/runtime/tst_palm_runtime_route_recategorization.cpp` (new); register.

Set up TWO routes for calendar: slot 3 = "Work" → MockRemote:WorkCal, slot 4 = "Home" → MockRemote:HomeCal. Insert a Palm record in slot 3, sync — it lands on WorkCal. Then mutate the hub canon directly (simulating a hub-side recategorization via a future UI) so `categories = ["Home"]`. Sync again — the engine must (a) delete the record from WorkCal (the Work-route's filtered view no longer returns it) and (b) create it on HomeCal (the Home-route's filtered view now returns it).

- [ ] **Step 1: Write the failing test** modeled on Task 5's setup. The hub-side mutation step uses `runtime.backendRegistry().backendInstance("wp-hub")` cast to `Sinks::GenericSqliteBackend*` and calls `updateRecord` with a modified canon JSON whose `"categories"` array is `["Home"]`. Add a second persisted mapping for slot 4. After the first sync, fetch the hub record, modify it, write back, sync again, and assert:
```cpp
QCOMPARE(mockRemote->loadRecords(QStringLiteral("WorkCal")).size(), 0);
QCOMPARE(mockRemote->loadRecords(QStringLiteral("HomeCal")).size(), 1);
```
Use the same skeleton as Task 5; share the codec/encoding helpers.

- [ ] **Step 2: Run, expect PASS.**

- [ ] **Step 3: Commit (main repo).**

---

### Task 7: Verify (FetchContent path), push

**Files:** none (pure verification + push).

- [ ] **Step 1: Fresh FetchContent build (real delivery path) + full ctest.**
```bash
cd /home/clinton/dev/WildPalms
rm -rf build-rcheck
cmake -S . -B build-rcheck -DCMAKE_BUILD_TYPE=Debug 2>&1 | grep -iE "fetching <TARGET_TAG>|Configuring done|CMake Error"
cmake --build build-rcheck -j"$(nproc)" 2>&1 | grep -cE "error:" | xargs echo "errors:"
ctest --test-dir build-rcheck 2>&1 | tail -4
```
Expected: `errors: 0` and `X/X passed` where X is the new total (103 + 3 new = 106, or whatever the count actually is; the absolute number doesn't matter — what matters is no failures).

- [ ] **Step 2: Push submodules (if Step 4 of Task 3 made any submodule commits).**
```bash
for s in calendar contacts memo todos; do
  git -C src/plugins/$s push origin HEAD 2>&1 | tail -2
done
```

- [ ] **Step 3: Push the superproject branch.**
```bash
git push origin feature/three-tier-sync 2>&1 | tail -2
```

- [ ] **Step 4: Clean scratch.**
```bash
rm -rf build-rcheck
```

---

## Self-Review

- **Spec coverage:**
  - §3 in-scope item "handoff" → not implemented by a task (the doc is already committed `3467f24`; this plan consumes the result).
  - §3 in-scope "per-route FilteredCollectionBackend instances + per-route LCs" → Task 3.
  - §3 in-scope "translation of persisted mappings" → Task 2 (function) + Task 3 Step 5 (call site).
  - §3 in-scope "tests proving routing, reverse, recategorization" → Tasks 4 / 5 / 6.
  - §6.1 sequencing (after C per-domain LCs, before `generateMappings`) → Task 3 Step 5.
  - §6.2 translation rule slot-mapped / wildcard / edge cases → Task 2 covers all four cases (slot, wildcard, unknown-slot, disabled, unknown-plugin).
  - §6.3 lifetime (`m_routeViews` declared after `m_hub` before `m_engine`) → Task 3 Step 1.
  - §7 data flow → exercised by Tasks 5 / 6.
  - §9 success criteria → each criterion is the literal assertion of Tasks 4 / 5 / 6.
- **Placeholder scan:** `<TARGET_TAG>` is a deliberate run-time-resolved value with the exact discovery commands in Task 1 — not a vague placeholder. `MockBlobBackend` is referenced from libkalburator's test fixtures (the existing runtime tests use it; the include path follows their pattern). Step 6 of Task 3 instructs "investigate before continuing" for non-route runtime tests — that is a real verification step, not a stand-in for missing content. The Task 6 step 1 shares the skeleton with Task 5 by reference (skeleton fully shown in Task 5; the only deltas are the second persisted mapping + the hub-mutation step + the assertion deltas — those are explicit in the body of Task 6's step 1).
- **Type / name consistency:** `RouteSpec`, `Kind::{Filtered, Direct}`, `lcId = "wp-route-<id>"`, virtual collection id `"route-<categoryName>"`, hub collection id == canonical domain id, plugin-id↔domain mapping (`memo`→`note`) — consistent across Tasks 2, 3, 4, 5, 6.

If you find issues during execution, prefer surfacing them and fixing inline over forcing a stale plan.
