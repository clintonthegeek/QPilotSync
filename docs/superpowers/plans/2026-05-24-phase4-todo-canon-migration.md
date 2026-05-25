# Phase 4: ToDo Canon Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (or superpowers:subagent-driven-development) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put WildPalms ToDo on the same first-class shape-graph footing as calendar (Phase 3) and contacts (Phase 2). Today `TodoBlobBackend` declares `(blob, raw)` and pre-transcodes Palm→VTODO internally, so the palm→vtodo loss is invisible to the engine and the sync never touches the todo canon. This plan flips the backend to declare `(todo, palm)`, present raw `PalmRecord` wire bytes, and registers a `TodoDomainExtension` providing `(todo, palm) ↔ (todo, ical-vtodo)` edges (stage bodies reuse the existing `todoicstranscoder`). libkalburator already bridges `ical-vtodo ↔ canon` (`todostockshapes`) and preserves unmapped VTODO `X-` properties into `providerExtras["x-vtodo"]` (verified), so identity round-trips. The engine then sees the full `palm → ical-vtodo → canon` chain with an honest `LossProfile`.

**Architecture:** This is the exact pattern calendar Phase 3 used (`palm ↔ ical`), with two substitutions: the todo peer encoding is **`ical-vtodo`** (not `ical`), and ToDo starts one rung lower — at the legacy `(blob, raw)` verbatim-copy shape rather than at a typed `(todo, ical-vtodo)`. The peer `RawFilesBackend` inherits the source shape (`registeredSrc->shapeFor()` in `src/runtime/palmruntime.cpp`), so it automatically becomes `(todo, palm)` and needs **no change**; with the new WP edge plus libkalburator's `ical-vtodo ↔ canon`, the `(todo,palm) → (todo,canon) → (todo,palm)` pipeline compiles.

**Tech Stack:** C++/Qt6, CMake (dev dir `build-dev`, local libkalburator override), ctest, KCalendarCore. The todo plugin is the git submodule `src/plugins/todos` (`wildpalms-conduit-todos`), on branch `feature/canon-adoption-phase1`. Tests live in the superproject under `tests/plugins/todos/`.

---

## Context the engineer needs

- **This is a behavior-changing migration**, like Phase 3 (not a verify-lock like Phase 2). The backend stops emitting iCal/VTODO bytes and starts emitting Palm wire bytes. Existing tests that assert iCal output / `type == "text/calendar"` WILL need updating (Task 7). The sync result must be unchanged end-to-end (palm→vtodo→canon reaches the same canonical), which the verification test (Task 6) and full suite (Task 8) prove.

- **The proven sibling is calendar (Phase 3); contacts (Phase 2) is the second reference.** Every change here mirrors a calendar file that already shipped:
  - `src/plugins/calendar/palmtoicstransformation.{h,cpp}` → mirror as `src/plugins/todos/palmtovtodotransformation.{h,cpp}`
  - `src/plugins/calendar/calendardomainextension.{h,cpp}` → mirror as `src/plugins/todos/tododomainextension.{h,cpp}`
  - `PalmCalendarBackend` (presents `pr.toWireBytes()`, `br.type="calendar"`, consumes `PalmRecord::fromWireBytes`) → mirror in `TodoBlobBackend` (`br.type="todo"`)
  - `CalendarBackendPlugin` ctor calls `CalendarDomainExtension::registerWith(TransformationRegistry::instance())` → mirror in `TodoBackendPlugin`
  - `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp` → mirror as `tst_todo_canon_roundtrip.cpp`

- **Existing transcoder is reused, not rewritten.** `src/plugins/todos/todoicstranscoder.cpp` already has `encodePalmToIcs(PalmRecord)` and `decodeIcsToPalm(QByteArray, int slotHint)` (both in namespace `WildPalms::TodoPlugin`). The new stages wrap these plus `PalmRecord` wire (de)serialization — no new conversion logic. (The function names keep their historical "Ics" spelling though the encoding is VTODO; do not rename them.)

- **Palm ToDoDB field coverage** (from `todoicstranscoder.cpp` + `palm/codecs/todocodec`, for the honest loss profile): a Palm ToDo record carries description→summary, note→description, ONE due date (mapped to both DTSTART and DUE, all-day), priority (1–5, widened to KCal 1–9), an isComplete boolean (→ STATUS + COMPLETED), and the record secret bit (→ classification). It does NOT carry: descriptionHtml, percentComplete (granular), a distinct DTSTART, a real COMPLETED timestamp, recurrence, alarms, location, geo, sortOrder, relatedTo, parentUid, checklistItems, linkedResources, or a multi-value CATEGORIES list (the single Palm category is a routing slot, not a record field).

- **Loss-profile vocabulary:** use the **canon todo** `PropertyId` names (the composable vocabulary libkalburator's `canon↔ical-vtodo` edge uses), exactly as calendar's `icsToPalmLoss()` used canon calendar names. Canon todo property names (from `src/todo/todocanonproperties.cpp` in libkalburator): `uid`, `created`, `lastModified`, `summary`, `description`, `descriptionHtml`, `status`, `percentComplete`, `priority`, `categories`, `start`, `due`, `completed`, `recurrence`, `alarms`, `location`, `geo`, `sortOrder`, `relatedTo`, `parentUid`, `checklistItems`, `linkedResources`.

- **Conflict handler is NOT changed.** `TodoBackendPlugin::enrichConflictSnapshot` parses `snapshot.content` as iCal/VTODO. Calendar's and contacts' equivalents still parse their text format even though their backends emit Palm wire — it's best-effort (returns early if the bytes aren't that format) and shipped that way. ToDo mirrors that: leave `enrichConflictSnapshot` unchanged. Do not touch it. (Cosmetic note: after this migration the conflict-snapshot `content` may carry Palm wire rather than VTODO, so `enrichConflictSnapshot` will return early without enriching metadata — exactly the documented best-effort behaviour calendar/contacts accepted. Do not "fix" it in Phase 4.)

- **Identity round-trip:** `todoicstranscoder` stamps `X-WP-PALM-RECORDID` and `X-WP-PALM-CATEGORY-SLOT` on the VTODO via `setNonKDECustomProperty`; libkalburator's `VTodoToCanonStage` preserves unmapped X- props into `providerExtras["x-vtodo"]` (verified: `src/todo/vtodocanonstages.cpp` lines ~284–294) and `CanonToVTodoStage` re-emits them (~500–505). So recordId + slot survive `palm→vtodo→canon→vtodo→palm`. The verification test (Task 6) proves it.

- **Slot is not authoritative after the stage** (mirrors calendar): `decodeIcsToPalm(bytes, /*slotHint*/ -1)` clamps category; the backend (`createRecord`/`updateRecord`) sets the real slot from collection context on write. Treat the wire bytes' category as undefined after `VTodoToPalmStage`.

- **Build/test commands** (`build-dev` configured against the canon branch from Phase 1):
  - `cmake --build build-dev -j"$(nproc)" [--target <t>]`
  - `ctest --test-dir build-dev -R todo --output-on-failure`
  - Reconfigure if needed: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`
  - Submodule edits (`src/plugins/todos/*`) are committed **inside the submodule**; superproject test edits + the pointer bump are superproject commits. Do NOT push (the controller gates pushes).

---

## File inventory

| File | Change |
|------|--------|
| `src/plugins/todos/palmtovtodotransformation.h` (submodule) | **Create** — `PalmToVTodoStage`, `VTodoToPalmStage`, `palmToVTodoLoss()`, `vtodoToPalmLoss()` |
| `src/plugins/todos/palmtovtodotransformation.cpp` (submodule) | **Create** |
| `src/plugins/todos/tododomainextension.h` (submodule) | **Create** — `TodoDomainExtension::registerWith` |
| `src/plugins/todos/tododomainextension.cpp` (submodule) | **Create** |
| `src/plugins/todos/todoblobbackend.h` (submodule) | **Modify** — `nativeShapes()` → `(todo, palm)` |
| `src/plugins/todos/todoblobbackend.cpp` (submodule) | **Modify** — present/consume Palm wire; `br.type="todo"` |
| `src/plugins/todos/todobackendplugin.cpp` (submodule) | **Modify** — register the extension in the ctor |
| `src/plugins/todos/CMakeLists.txt` (submodule) | **Modify** — add the two new sources to the plugin target |
| `tests/plugins/todos/CMakeLists.txt` | **Modify** — new test target + new sources on backend-compiling targets |
| `tests/plugins/todos/tst_todoblobbackend.cpp` | **Modify** — expect Palm wire, not iCal |
| `tests/plugins/todos/tst_todo_canon_roundtrip.cpp` | **Create** — the verification test |

---

### Task 1: Create the palm↔vtodo transformation stages

**Files:**
- Create: `src/plugins/todos/palmtovtodotransformation.h`
- Create: `src/plugins/todos/palmtovtodotransformation.cpp`

Mirror `src/plugins/calendar/palmtoicstransformation.{h,cpp}` (read both first as the template). The only differences are the names (`Ics`→`VTodo`) and the loss profile body (todo field coverage / canon todo vocabulary).

- [ ] **Step 1: Write the header**

```cpp
#ifndef WILDPALMS_TODO_PALMTOVTODOTRANSFORMATION_H
#define WILDPALMS_TODO_PALMTOVTODOTRANSFORMATION_H

#include "transformationedge.h"

namespace WildPalms::TodoPlugin {

// (todo, palm) -> (todo, ical-vtodo): wraps the Palm ToDo codec via
// todoicstranscoder. Lossless: a Palm ToDo is a subset of a VTODO (identity
// X- stamps are preserved downstream by libkalburator's vtodo<->canon stage).
class PalmToVTodoStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

// (todo, ical-vtodo) -> (todo, palm): lossy; Palm ToDoDB cannot hold most
// VTODO fields.
class VTodoToPalmStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

Kalburator::Shape::LossProfile palmToVTodoLoss();
Kalburator::Shape::LossProfile vtodoToPalmLoss();

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_PALMTOVTODOTRANSFORMATION_H
```

(Confirm the `TransformationStage` base lives in `transformationedge.h` — that's where calendar's mirror includes it from. If clangd disagrees, copy the exact include calendar's `palmtoicstransformation.h` uses.)

- [ ] **Step 2: Write the implementation**

```cpp
#include "palmtovtodotransformation.h"

#include "todoicstranscoder.h"
#include "palm/sync/palmrecord.h"

using namespace Kalburator::Shape;

namespace WildPalms::TodoPlugin {

QByteArray PalmToVTodoStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    const auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(sourceBytes);
    return encodePalmToIcs(pr);
}

QByteArray VTodoToPalmStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    // slotHint = -1: the category field of this stage's output is NOT
    // authoritative. The backend (createRecord/updateRecord) sets the real
    // slot from collection context on write; X-WP-PALM-CATEGORY-SLOT rides on
    // the VTODO but the codec does not read it back. Treat the wire bytes'
    // category as undefined after this stage.
    const auto prOpt = decodeIcsToPalm(sourceBytes, /*slotHint*/ -1);
    if (!prOpt) return {};
    return prOpt->toWireBytes();
}

LossProfile palmToVTodoLoss()
{
    // lossless: every Palm ToDoDB field maps directly onto a VTODO field
    // (priority widens 1..5 -> 1..9 injectively). Identity X- stamps are
    // preserved downstream by libkalburator's vtodo<->canon stage.
    return {};
}

LossProfile vtodoToPalmLoss()
{
    LossProfile p;
    // Palm ToDoDB has no field for these (canon todo property vocabulary):
    p.affected.insert(PropertyId{QStringLiteral("descriptionHtml")}, LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("categories")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("start")},           LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("completed")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("recurrence")},      LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("alarms")},          LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("location")},        LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("geo")},             LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("sortOrder")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("relatedTo")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("parentUid")},       LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("checklistItems")},  LossKind::Dropped);
    p.affected.insert(PropertyId{QStringLiteral("linkedResources")}, LossKind::Dropped);
    // Survive in reduced form: Palm holds a complete/incomplete boolean (no
    // granular percent, no NEEDS-ACTION/IN-PROCESS/CANCELLED distinction)...
    p.affected.insert(PropertyId{QStringLiteral("status")},          LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("percentComplete")}, LossKind::Simplified);
    // ...and a coarse 1..5 priority mapped many-to-one from the 1..9 vocab.
    p.affected.insert(PropertyId{QStringLiteral("priority")},        LossKind::Degraded);
    return p;
}

} // namespace WildPalms::TodoPlugin
```

(Verify `LossProfile`/`PropertyId`/`LossKind` names against calendar's `palmtoicstransformation.cpp` — they are the same canon shape API. Do not stub anything.)

---

### Task 2: Create the TodoDomainExtension

**Files:**
- Create: `src/plugins/todos/tododomainextension.h`
- Create: `src/plugins/todos/tododomainextension.cpp`

Mirror `src/plugins/calendar/calendardomainextension.{h,cpp}` (read both first). Substitutions: domain `"todo"`, peer encoding `"ical-vtodo"`, the Palm native catalogue describes ToDo fields.

- [ ] **Step 1: Write the header** (mirror `calendardomainextension.h` verbatim, swapping the class name + namespace):

```cpp
#ifndef WILDPALMS_TODO_TODODOMAINEXTENSION_H
#define WILDPALMS_TODO_TODODOMAINEXTENSION_H

namespace Kalburator::Shape { class TransformationRegistry; }

namespace WildPalms::TodoPlugin {

// Registers the (todo, palm) peer shape plus palm<->ical-vtodo edges with a
// TransformationRegistry. Idempotent (registerShape/registerEdge tolerate
// re-registration), so calling it from every plugin instance is safe.
class TodoDomainExtension {
public:
    static void registerWith(Kalburator::Shape::TransformationRegistry &registry);
};

} // namespace WildPalms::TodoPlugin

#endif // WILDPALMS_TODO_TODODOMAINEXTENSION_H
```

(Confirm the forward-declare / signature exactly matches `calendardomainextension.h`. If calendar's header includes a header instead of forward-declaring `TransformationRegistry`, match that.)

- [ ] **Step 2: Write the implementation** (mirror `calendardomainextension.cpp`):

```cpp
#include "tododomainextension.h"

#include "palmtovtodotransformation.h"
#include "propertycatalogue.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace WildPalms::TodoPlugin {

namespace {

PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    // Palm ToDoDB native fields; used by loss-profile UI to describe a
    // palm-shape record.
    cat.addProperty({ PropertyId{"summary"},     PropertyKind::String,  QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"description"}, PropertyKind::String,  QStringLiteral("Description") });
    cat.addProperty({ PropertyId{"due"},         PropertyKind::Json,    QStringLiteral("Due") });
    cat.addProperty({ PropertyId{"priority"},    PropertyKind::Integer, QStringLiteral("Priority") });
    cat.addProperty({ PropertyId{"status"},      PropertyKind::String,  QStringLiteral("Status") });
    cat.addProperty({ PropertyId{"classification"}, PropertyKind::String, QStringLiteral("Classification") });
    cat.addProperty({ PropertyId{"category"},    PropertyKind::Integer, QStringLiteral("Category Slot") });
    return cat;
}

} // namespace

void TodoDomainExtension::registerWith(TransformationRegistry &registry)
{
    const Shape palm { DomainId{"todo"}, EncodingId{"palm"} };
    const Shape vtodo{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };

    // Defensive: libkalburator's todo domain plugin registers the ical-vtodo
    // peer shape at PluginManager load time (via registerStockPlugins). If that
    // static-init registrar didn't run in this address space (e.g. a unit test
    // that skips full plugin init), the vtodo shape is absent and registerEdge
    // below would assert "to-shape not registered". Register a minimal
    // placeholder; registerShape is idempotent, so libkalburator's real
    // catalogue replaces it under the same key when it runs.
    if (registry.catalogueFor(vtodo) == nullptr) {
        registry.registerShape(vtodo, {});
    }
    registry.registerShape(palm, makePalmCatalogue());

    // palm -> ical-vtodo (lossless; identity X- stamps preserved by vtodo<->canon)
    registry.registerEdge(TransformationEdge{
        palm, vtodo, palmToVTodoLoss(), std::make_shared<PalmToVTodoStage>() });

    // ical-vtodo -> palm (lossy; Palm ToDoDB holds a subset of VTODO)
    registry.registerEdge(TransformationEdge{
        vtodo, palm, vtodoToPalmLoss(), std::make_shared<VTodoToPalmStage>() });
}

} // namespace WildPalms::TodoPlugin
```

(Match calendar's exact `registerShape`/`registerEdge`/`catalogueFor`/`TransformationEdge` ctor spelling. The `EncodingId` must be exactly `"ical-vtodo"` — confirmed from `libkalburator/src/todo/todostockshapes.cpp`.)

---

### Task 3: Flip the backend to present/consume Palm wire

**Files:**
- Modify: `src/plugins/todos/todoblobbackend.h`
- Modify: `src/plugins/todos/todoblobbackend.cpp`

Mirror the Phase 3 diff to `palmcalendarbackend.{h,cpp}` (commit `656dee7` in the calendar submodule).

- [ ] **Step 1: Change `nativeShapes()` in the header**

In `todoblobbackend.h`, replace the `(blob, raw)` body and its now-stale K.8b comment:

```cpp
    QList<Kalburator::Shape::Shape> nativeShapes() const override {
        return { { Kalburator::Shape::DomainId{QStringLiteral("todo")},
                   Kalburator::Shape::EncodingId{QStringLiteral("palm")} } };
    }
```

- [ ] **Step 2: Stop transcoding in `loadRecords`**

In `todoblobbackend.cpp`, drop the `#include "todoicstranscoder.h"` if no longer used by this TU (it will NOT be — the stages own transcoding now; confirm nothing else in the .cpp calls `encodePalmToIcs`/`decodeIcsToPalm` before removing). In `loadRecords`, replace the encode + `text/calendar` block with wire bytes:

```cpp
    for (const auto &pr : records) {
        if (static_cast<int>(pr.category) != slot) continue;
        if (pr.isDeleted()) continue;

        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = pr.toWireBytes();
        br.type         = QStringLiteral("todo");
        br.lastModified = pr.lastModified;
        br.contentHash  = pr.contentHash();
        out.append(br);
    }
```

(Note: the old code skipped records where `encodePalmToIcs` returned empty "(e.g. tombstones)". `isDeleted()` already filters tombstones above; wire serialization does not fail, so the `ics.isEmpty()` guard is correctly dropped.)

- [ ] **Step 3: Same flip in `loadRecord`**

```cpp
    auto pr = m_palmBackend->loadPalmRecord(QStringLiteral("ToDoDB"), rid);
    if (!pr) return std::nullopt;

    Kalburator::Sync::BackendRecord br;
    br.id           = recordId;
    br.data         = pr->toWireBytes();
    br.type         = QStringLiteral("todo");
    br.lastModified = pr->lastModified;
    br.contentHash  = pr->contentHash();
    return br;
```

- [ ] **Step 4: Consume wire bytes in `createRecord`**

```cpp
    const int slot = slotFromCollectionId(collectionId);
    if (slot < 0 || !m_palmBackend) return {};
    if (record.data.isEmpty()) return {};

    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.recordId     = 0;   // device assigns
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();

    const auto newId = m_palmBackend->createPalmRecord(
        QStringLiteral("ToDoDB"), pr);
    if (newId == 0) return {};
    return idForPalmRecord(newId);
```

- [ ] **Step 5: Consume wire bytes in `updateRecord`**

```cpp
    std::uint32_t rid = 0;
    if (!decodeId(record.id, &rid) || !m_palmBackend) return false;
    if (record.data.isEmpty()) return false;

    auto existing = m_palmBackend->loadPalmRecord(
        QStringLiteral("ToDoDB"), rid);
    if (!existing) return false;
    const int slot = static_cast<int>(existing->category);

    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.recordId     = rid;
    pr.category     = static_cast<std::uint8_t>(slot);
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();

    return m_palmBackend->updatePalmRecord(QStringLiteral("ToDoDB"), pr);
```

- [ ] **Step 6: Same flip in `modifiedSince`**

```cpp
    for (const auto &pr : records) {
        if (static_cast<int>(pr.category) != slot) continue;
        if (since.isValid() && pr.lastModified <= since) continue;

        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = pr.toWireBytes();
        br.type         = QStringLiteral("todo");
        br.lastModified = pr.lastModified;
        br.contentHash  = pr.contentHash();
        out.append(br);
    }
```

(`deleteRecord`, `deletedSince`, collections, and id helpers are unchanged.)

---

### Task 4: Register the extension in the plugin ctor

**Files:**
- Modify: `src/plugins/todos/todobackendplugin.cpp`

Mirror the Phase 3 diff to `calendarbackendplugin.cpp`.

- [ ] **Step 1: Add includes**

```cpp
#include "tododomainextension.h"
```
and
```cpp
#include "transformationregistry.h"
```
(place them next to the existing plugin/handler includes, matching calendar's ordering).

- [ ] **Step 2: Register in the constructor body**

In `TodoBackendPlugin::TodoBackendPlugin()`, after the member init list, add:

```cpp
{
    // Phase 4: register the (todo, palm) peer shape and palm<->ical-vtodo edges
    // with the process-wide TransformationRegistry at plugin construction
    // (mirrors CalendarBackendPlugin). Idempotent across instances.
    TodoDomainExtension::registerWith(
        Kalburator::Shape::TransformationRegistry::instance());
}
```

---

### Task 5: Add the new sources to the plugin CMake target

**Files:**
- Modify: `src/plugins/todos/CMakeLists.txt`

- [ ] **Step 1: List the two new sources** in `add_library(wildpalms_todos_static STATIC ...)`, mirroring how calendar's CMake lists `palmtoicstransformation.cpp` + `calendardomainextension.cpp`:

```cmake
add_library(wildpalms_todos_static STATIC
    todobackendplugin.cpp        todobackendplugin.h
    todoblobbackend.cpp          todoblobbackend.h
    todoconflicthandler.cpp      todoconflicthandler.h
    todoicstranscoder.cpp        todoicstranscoder.h
    palmtovtodotransformation.cpp palmtovtodotransformation.h
    tododomainextension.cpp      tododomainextension.h
    taskview.cpp                 taskview.h
)
```

(No link-library change is needed: the static already links `Kalburator::Sync` PUBLIC, which provides the shape graph headers/symbols the new sources use.)

- [ ] **Step 2: Configure + build the plugin static**

Run: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator) 2>&1 | tail -5`
then `cmake --build build-dev -j"$(nproc)" --target wildpalms_todos_static 2>&1 | tail -15`.
Expected: compiles clean. If `transformationedge.h` / `propertycatalogue.h` / `transformationregistry.h` don't resolve, copy the exact include spelling calendar's equivalent files use (they compile against the same `Kalburator::Sync` interface includes).

- [ ] **Step 3: Commit the submodule feature work**

```bash
cd src/plugins/todos
git add palmtovtodotransformation.h palmtovtodotransformation.cpp \
        tododomainextension.h tododomainextension.cpp \
        todoblobbackend.h todoblobbackend.cpp \
        todobackendplugin.cpp CMakeLists.txt
git commit -m "feat: todo as (todo,palm) shape peer with palm<->ical-vtodo edges (Phase 4)

Move Palm<->VTODO conversion from the backend into PalmToVTodoStage/
VTodoToPalmStage; declare (todo,palm) native + register edges via
TodoDomainExtension; backend now presents/consumes Palm wire bytes. The
engine sees the full palm->ical-vtodo->canon chain with an honest LossProfile.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd ../../..
```

---

### Task 6: Add the canon round-trip verification test

**Files:**
- Create: `tests/plugins/todos/tst_todo_canon_roundtrip.cpp`

Mirror `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp` (read it first). Substitutions: `Calendar`→`Todo`, `calendar`→`todo`, `ical`→`ical-vtodo`, build the source palm record from a Palm ToDo POD (no `DatebookCodec` — use `PalmCodecs::encodeTodo` + the transcoder's X- stamp path).

- [ ] **Step 1: Write the test**

The source-record builder differs from calendar: ToDo has no `DatebookCodec`. Build a `PalmRecord` whose `data` is `encodeTodo(pod)`, then route it through `PalmToVTodoStage` is implicit in the pipeline. The recordId identity stamp is applied by the transcoder (`encodePalmToIcs` stamps `X-WP-PALM-RECORDID` from `record.recordId`), so set `pr.recordId` on the source record.

```cpp
#include <QTest>

// WildPalms todo plugin
#include "tododomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/todocodec.h"          // Todo POD, encodeTodo/decodeTodo

// libkalburator shape graph + todo canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "tododomaindefinition.h"
#include "todostockshapes.h"

using namespace Kalburator::Shape;
using WildPalms::TodoPlugin::TodoDomainExtension;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;

namespace {

ShapeRegistries makeTodoRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Todo::TodoDomainDefinition def;
    const auto spine = def.canonicalSpine();
    if (!spine.isEmpty()) {
        const auto &[rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(def.domain(), rootShape);
        for (int i = 1; i < spine.size(); ++i) {
            const auto &[s, cat] = spine.at(i);
            reg.registerShape(s, cat);
            reg.appendCanonicalVersion(def.domain(), s);
        }
    }

    Kalburator::Todo::TodoStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (todo, palm) + palm<->ical-vtodo edges. Not part of stock; the
    // palm->ical-vtodo->canon path only compiles once this runs.
    TodoDomainExtension::registerWith(reg);

    return regs;
}

// Build a (todo, palm) record's wire bytes carrying a known recordId + content.
QByteArray makePalmTodoBytes(quint8 slot, quint32 recordId,
                             const QString &description)
{
    Todo t;
    t.description = description;
    t.priority    = 1;        // highest
    t.isComplete  = false;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = encodeTodo(t);
    return pr.toWireBytes();
}

} // namespace

class TestTodoCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmTodoBytes(/*slot*/ 2, /*recordId*/ 77,
                              QStringLiteral("Buy milk"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->ical-vtodo->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        // Decode the round-tripped palm wire and check summary + recordId survive.
        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 77u);
        const auto decoded = decodeTodo(QByteArrayView(pr2.data));
        QVERIFY2(decoded.has_value(), "round-tripped palm todo did not decode");
        QCOMPARE(decoded->description, QStringLiteral("Buy milk"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeTodoRegistries();
        const Shape palm { DomainId{"todo"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"todo"}, EncodingId{"canon"} };

        QVERIFY2(regs.transformation.inspect(palm, canon).isLossless(),
                 "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm cannot hold most todo fields)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("recurrence")}),
                 LossKind::Dropped);
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("priority")}),
                 LossKind::Degraded);
    }
};

QTEST_GUILESS_MAIN(TestTodoCanonRoundtrip)
#include "tst_todo_canon_roundtrip.moc"
```

**Verify before building:** that `Kalburator::Todo` is the namespace of `TodoDomainDefinition`/`TodoStockShapes` (calendar used `Kalburator::Calendar`). Check `libkalburator/src/todo/tododomaindefinition.h` for the `namespace`. Also confirm the `Todo` POD field names (`description`, `priority`, `isComplete`) against `palm/codecs/todocodec.h` (the existing `tst_todoblobbackend.cpp` uses `t.description` / `t.priority`, so those are correct). If the recordId does NOT survive (see Step 3 branch), that is the real-risk path.

- [ ] **Step 2: Add the test target to CMake**

Append to `tests/plugins/todos/CMakeLists.txt` (mirror the calendar canon test block; reuse `TODOS_PLUGIN_SRC_DIR`):

```cmake
# --- Phase 4: todo canon round-trip ---
add_executable(tst_todo_canon_roundtrip
    tst_todo_canon_roundtrip.cpp
    ${TODOS_PLUGIN_SRC_DIR}/tododomainextension.cpp
    ${TODOS_PLUGIN_SRC_DIR}/palmtovtodotransformation.cpp
    ${TODOS_PLUGIN_SRC_DIR}/todoicstranscoder.cpp
)
target_include_directories(tst_todo_canon_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${TODOS_PLUGIN_SRC_DIR}
)
target_include_directories(tst_todo_canon_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_todo_canon_roundtrip
    PRIVATE
        WildPalmsPalmCodecs     # encodeTodo / decodeTodo
        WildPalmsPalmSync       # PalmRecord
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_todo_canon_roundtrip COMMAND tst_todo_canon_roundtrip)
set_tests_properties(tst_todo_canon_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build + run ONLY this test, then branch on the result**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_todo_canon_roundtrip 2>&1 | tail -15`
then `ctest --test-dir build-dev -R todo_canon_roundtrip --output-on-failure 2>&1 | tail -30`.

**If it PASSES:** identity + loss honesty hold — proceed.
**If `palmCanonRoundTripPreservesIdentity` FAILS:** capture the canon JSON (temporarily `qDebug() << canonBytes`, then revert), check whether `providerExtras["x-vtodo"]` carries `X-WP-PALM-RECORDID`. Conclude whether libkalburator's vtodo↔canon dropped the X-prop vs a WP stage bug (e.g. the transcoder stamps under a key `customProperties()` doesn't surface). STOP and report — do NOT weaken the assertion.
**If `lossProfileIsHonest` FAILS:** report which sub-assertion. A `recurrence`/`priority` key mismatch means the loss-profile name doesn't match the canon vocabulary — fix the name in `palmtovtodotransformation.cpp` to match `libkalburator/src/todo/todocanonproperties.cpp`. A `palm->canon should be lossless` failure means `palmToVTodoLoss()` (or libkalburator's vtodo→canon) reports loss going up — investigate which property and report.

- [ ] **Step 4: Commit the test (superproject)**

```bash
git add tests/plugins/todos/tst_todo_canon_roundtrip.cpp tests/plugins/todos/CMakeLists.txt
git commit -m "test(todo): canon round-trip + loss-honesty verification (Phase 4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 7: Fix the existing backend test for the palm-wire shape

**Files:**
- Modify: `tests/plugins/todos/tst_todoblobbackend.cpp`

- [ ] **Step 1: Identify the iCal-shaped assertions**

Read `tests/plugins/todos/tst_todoblobbackend.cpp`. The shape-dependent spots are:
- `loadRecordsRoutesByCategory`: `QCOMPARE(unfiled[0].type, QStringLiteral("text/calendar"))` — backend now sets `"todo"`. The `.data.contains("Anything")` / `.contains("Personal one")` checks happen to still pass (the description text is embedded in the Palm wire bytes too), but they are now incidental; replace them with a decode-based check (below) so they assert the real contract.
- `createRecordStampsSlotFromCollectionId`: builds `br.data = encodePalmToIcs(seed)` + `br.type = "text/calendar"`. The backend now consumes **wire bytes**, so this must build `br.data = seed.toWireBytes()`.
- `updateRecordPreservesRecordId`: same — `br.data = encodePalmToIcs(modified)` must become `br.data = modified.toWireBytes()`.

- [ ] **Step 2: Update them to the wire-bytes contract**

Mirror how `tests/plugins/contacts/tst_contactsblobbackend.cpp` and `tests/plugins/calendar/tst_calendarblobbackend.cpp` verify the palm-wire backend. Concretely:
- Change the type assertion to `QCOMPARE(unfiled[0].type, QStringLiteral("todo"));`.
- For the create/update payloads, replace `WildPalms::TodoPlugin::encodePalmToIcs(seed)` with `seed.toWireBytes()` (and likewise for `modified`). The `makeTodo()` helper already returns a `PalmRecord`, so `.toWireBytes()` is available directly. The `#include "plugins/todos/todoicstranscoder.h"` can be removed if no longer referenced.
- For the load-content checks, decode and assert on the field: e.g.
  ```cpp
  const auto pod = WildPalms::PalmCodecs::decodeTodo(QByteArrayView(unfiled[0].data));
  QVERIFY(pod.has_value());
  QCOMPARE(pod->description, QStringLiteral("Anything"));
  ```
  (Add `#include "palm/codecs/todocodec.h"` — it is already included.)

Do not delete coverage — translate it. Slot-routing, collection, and delete tests are unaffected (they never inspected encoded bytes).

- [ ] **Step 3: Build + run it**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_todoblobbackend 2>&1 | tail -10`
then `ctest --test-dir build-dev -R "todoblobbackend" --output-on-failure 2>&1 | tail -20`.
Expected: PASS. (No CMake change needed for this target — it already compiles `todoblobbackend.cpp` + `todoicstranscoder.cpp` and links the palm codecs. The new `palmtovtodotransformation.cpp`/`tododomainextension.cpp` are NOT needed by this target since the backend no longer references the transcoder or stages directly. If the link fails for a missing symbol, add the two sources here too — but it should not.)

- [ ] **Step 4: Commit (superproject)**

```bash
git add tests/plugins/todos/tst_todoblobbackend.cpp
git commit -m "test(todo): tst_todoblobbackend expects Palm wire bytes (Phase 4)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8: Full suite green + submodule pointer bump

**Files:** superproject gitlink for `src/plugins/todos`.

- [ ] **Step 1: Clean build + full suite**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -10` then
`ctest --test-dir build-dev --output-on-failure 2>&1 | tail -25`.
Expected: all pass. Count = previous (97) + 1 new test (`tst_todo_canon_roundtrip`) = 98. Watch specifically for:
- `tst_todoicstranscoder` — unchanged (transcoder untouched); should still pass.
- `tst_todobackendplugin` — links the plugin sources; after Task 4 it also constructs `TodoBackendPlugin`, which now calls `TodoDomainExtension::registerWith(...)`. It will need the two new sources on its target (Step 2 below) OR it will fail to link `TodoDomainExtension`/the stages. Add them.
- `tst_todoconflicthandler` — unchanged; should pass.

- [ ] **Step 1a: Add new sources to `tst_todobackendplugin` if it fails to link**

`tst_todobackendplugin` compiles `todobackendplugin.cpp` directly, which now `#include`s `tododomainextension.h` and calls `registerWith`. Append the two new sources to that `add_executable` in `tests/plugins/todos/CMakeLists.txt`:

```cmake
    ${TODOS_PLUGIN_SRC_DIR}/palmtovtodotransformation.cpp
    ${TODOS_PLUGIN_SRC_DIR}/tododomainextension.cpp
```

Rebuild + rerun that target. (This is the analogue of calendar listing `palmtoicstransformation.cpp` + `calendardomainextension.cpp` on `tst_calendarbackendplugin`.) Commit this CMake change with Task 8's test edits if it was needed:

```bash
git add tests/plugins/todos/CMakeLists.txt
```

- [ ] **Step 2: Bump the todos submodule pointer and commit**

```bash
git add src/plugins/todos
git status   # confirm only the todos gitlink moved (plus any tests/ edits already committed)
git commit -m "build: bump todos submodule (Phase 4 canon migration)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 3: Report the final unpushed state**

`git log --oneline main..HEAD` (superproject) and `git -C src/plugins/todos log --oneline -4`. State what a push would publish. Do NOT push.

---

## Phase 4 done when

- ToDo backend declares `(todo, palm)` and presents/consumes Palm wire; the palm↔vtodo conversion is a registered shape edge with an honest `LossProfile`.
- `tst_todo_canon_roundtrip` passes (identity survives `palm→canon→palm`; loss honest: `palm→canon` lossless, `canon→palm` reports `recurrence`=Dropped, `priority`=Degraded).
- `tst_todoblobbackend` updated and green; full suite 98/98; no other regressions.
- ToDo submodule changes committed + pointer bumped (unpushed).

## Self-review notes

- **Spec coverage:** stages (Task 1), domain extension (Task 2), backend flip (Task 3), plugin registration (Task 4), plugin CMake (Task 5); verification (Task 6); forced test churn (Task 7); regression gate + pointer bump (Task 8). Conflict handler explicitly left unchanged (documented, mirrors calendar/contacts).
- **No placeholders:** the two new files, the backend diffs, the plugin registration, and the verification test are complete code. Spots that say "match the proven sibling spelling" (TransformationStage base header, registry method names, `Kalburator::Todo` namespace, `Todo` POD field names) each name the exact authority file to copy and forbid stubbing.
- **Type consistency:** stage classes implement `transform(QByteArray)->QByteArray` (matches calendar/contacts); `LossProfile.affected` four-kind API; `PalmRecord::toWireBytes`/`fromWireBytes`; loss keys use canon todo `PropertyId`s from `todocanonproperties.cpp`.
- **DRY:** stage bodies reuse the existing `todoicstranscoder` (`encodePalmToIcs`/`decodeIcsToPalm`) — no new conversion logic; mirrors calendar reusing `icstranscoder`.
- **Risk noted:** Task 6 Step 3 handles the (real but unlikely, given calendar proved the analogous X-prop preservation for VEVENT and libkalburator's `VTodoToCanonStage` was verified to stash unmapped X- props into `providerExtras["x-vtodo"]`) chance that recordId identity doesn't survive — branch + report, don't weaken. The starting-rung difference (ToDo was `(blob,raw)`, calendar was already `(calendar,ical)`) is absorbed entirely by Task 3's `nativeShapes()` flip; the peer `RawFilesBackend` inherits the new shape automatically (`palmruntime.cpp`), so no peer/runtime change is required.
