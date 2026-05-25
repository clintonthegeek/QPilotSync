# Phase 3: Calendar Canon Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move WildPalms calendar's Palm↔iCal conversion out of the backend and into a declared shape-graph edge, so the engine sees the full `palm → ical → canon` chain with an honest `LossProfile` — finally putting the primary domain on the same first-class footing as contacts.

**Architecture:** Today `PalmCalendarBackend` declares `(calendar, ical)` and `loadRecords()` runs `encodePalmToIcs()` internally, presenting iCal bytes — so the palm→ical loss is invisible to the engine. This plan flips the backend to declare `(calendar, palm)` and present raw `PalmRecord` wire bytes, and adds a `CalendarDomainExtension` registering `(calendar, palm)` plus `palm ↔ ical` edges (stage bodies reuse the existing `icstranscoder`). libkalburator already bridges `ical ↔ canon` and preserves unknown VEVENT `X-` properties into `providerExtras["x-ical"]` (verified), so identity round-trips. This is the exact pattern contacts already uses (`palm ↔ vcard4`), applied to calendar.

**Tech Stack:** C++/Qt6, CMake (dev dir `build-dev`, local libkalburator override), ctest, KCalendarCore. The calendar plugin is the git submodule `src/plugins/calendar` (`wildpalms-conduit-calendar`), on branch `feature/canon-adoption-phase1`. Tests live in the superproject under `tests/plugins/calendar/`.

---

## Context the engineer needs

- **This is a behavior-changing migration**, unlike Phase 2. The backend stops emitting iCal and starts emitting Palm wire bytes. Existing tests that assert iCal output WILL need updating (Task 7). The sync result must be unchanged end-to-end (palm→ical→canon reaches the same canonical), which the verification test (Task 6) and full suite (Task 8) prove.

- **The proven sibling is contacts.** Every change here mirrors a contacts file that already shipped:
  - `src/plugins/contacts/palmtovcardtransformation.{h,cpp}` → mirror as `palmtoicstransformation.{h,cpp}`
  - `src/plugins/contacts/contactsdomainextension.{h,cpp}` → mirror as `calendardomainextension.{h,cpp}`
  - `PalmContactsBackend` (presents `pr.toWireBytes()`, `br.type="contacts"`, consumes `PalmRecord::fromWireBytes`) → mirror in `PalmCalendarBackend`
  - `ContactsBackendPlugin` ctor calls `ContactsDomainExtension::registerWith(TransformationRegistry::instance())` → mirror in `CalendarBackendPlugin`
  - `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp` → mirror as `tst_calendar_canon_roundtrip.cpp`

- **Existing transcoder is reused, not rewritten.** `src/plugins/calendar/icstranscoder.cpp` already has `encodePalmToIcs(PalmRecord)` and `decodeIcsToPalm(QByteArray, int slotHint)`. The new stages wrap these plus `PalmRecord` wire (de)serialization — no new conversion logic.

- **DatebookCodec field coverage** (from `src/palm/calendar/datebookcodec.cpp`, for the honest loss profile): Palm DatebookDB carries start/end, all-day, summary, note, ONE display alarm, and a recurrence subset (daily / weekly+byday / monthly-by-day / monthly-by-date / yearly, with interval, end, exceptions). It does NOT carry: location, attendees, organizer, priority, status, url, attachments, categories, transparency, free/busy, classification, color, multiple alarms, or complex RRULE constructs (BYSETPOS/BYWEEKNO/etc.).

- **Loss-profile vocabulary:** use the **canon** calendar `PropertyId` names (the composable vocabulary libkalburator's `canon↔ical` edge uses), exactly as contacts' `vcardToPalmLoss()` used canon contacts names. Canon calendar property names (from `calendarcanonproperties.cpp`): `location`, `locations`, `attendees`, `organizer`, `priority`, `status`, `url`, `attachments`, `categories`, `timeTransparency`, `freeBusyStatus`, `onlineMeeting`, `eventType`, `classification`, `color`, `recurrence`, `alarms`.

- **Conflict handler is NOT changed.** `CalendarBackendPlugin::enrichConflictSnapshot` parses `snapshot.content` as iCal. Contacts' equivalent parses vCard even though its backend emits Palm wire — it's best-effort (returns early if the bytes aren't that format) and was shipped that way. Calendar mirrors that: leave `enrichConflictSnapshot` parsing iCal, unchanged. Do not touch it.

- **Identity round-trip:** `DatebookCodec` stamps `X-WP-PALM-RECORDID` and `X-WP-PALM-CATEGORY-SLOT` on the Event; libkalburator's `ICalToCanonStage` preserves these into `providerExtras["x-ical"]` and `CanonToICalStage` restores them. So recordId + slot survive `palm→ical→canon→ical→palm`. The verification test proves it.

- **Build/test commands** (`build-dev` configured against the canon branch from Phase 1):
  - `cmake --build build-dev -j"$(nproc)" [--target <t>]`
  - `ctest --test-dir build-dev -R calendar --output-on-failure`
  - Reconfigure if needed: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`
  - Submodule edits (`src/plugins/calendar/*`) are committed inside the submodule; superproject test edits + the pointer bump are superproject commits. Do NOT push (the controller gates pushes).

---

## File inventory

| File | Change |
|------|--------|
| `src/plugins/calendar/palmtoicstransformation.h` (submodule) | **Create** — `PalmToIcsStage`, `IcsToPalmStage`, `palmToIcsLoss()`, `icsToPalmLoss()` |
| `src/plugins/calendar/palmtoicstransformation.cpp` (submodule) | **Create** |
| `src/plugins/calendar/calendardomainextension.h` (submodule) | **Create** — `CalendarDomainExtension::registerWith` |
| `src/plugins/calendar/calendardomainextension.cpp` (submodule) | **Create** |
| `src/plugins/calendar/palmcalendarbackend.cpp` (submodule) | **Modify** — `nativeShapes()`→palm; present/consume Palm wire |
| `src/plugins/calendar/calendarbackendplugin.cpp` (submodule) | **Modify** — register the extension in the ctor |
| `src/plugins/calendar/CMakeLists.txt` (submodule) | **Modify** — add the two new sources to the plugin target |
| `tests/plugins/calendar/CMakeLists.txt` | **Modify** — new test targets + new sources on backend-compiling targets |
| `tests/plugins/calendar/tst_calendarblobbackend.cpp` | **Modify** — expect Palm wire, not iCal |
| `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp` | **Create** — the verification test |

---

### Task 1: Create the palm↔ics transformation stages

**Files:**
- Create: `src/plugins/calendar/palmtoicstransformation.h`
- Create: `src/plugins/calendar/palmtoicstransformation.cpp`

Mirror `src/plugins/contacts/palmtovcardtransformation.{h,cpp}` (read it first as the template).

- [ ] **Step 1: Create the header**

`src/plugins/calendar/palmtoicstransformation.h`:
```cpp
#pragma once

#include "lossprofile.h"
#include "transformationedge.h"   // Kalburator::Shape::TransformationStage

namespace WildPalms::CalendarPlugin {

// (calendar, palm) -> (calendar, ical): wraps DatebookCodec via icstranscoder.
// Lossless: a Palm appointment is a subset of a VEVENT (identity X- stamps
// are preserved downstream by libkalburator's ical<->canon stage).
class PalmToIcsStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

// (calendar, ical) -> (calendar, palm): lossy; Palm DatebookDB cannot hold
// most VEVENT fields.
class IcsToPalmStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

Kalburator::Shape::LossProfile palmToIcsLoss();
Kalburator::Shape::LossProfile icsToPalmLoss();

} // namespace WildPalms::CalendarPlugin
```
(Confirm the exact `TransformationStage` base header/spelling by checking `palmtovcardtransformation.h` — match whatever it includes/uses.)

- [ ] **Step 2: Create the implementation**

`src/plugins/calendar/palmtoicstransformation.cpp`:
```cpp
#include "palmtoicstransformation.h"

#include "icstranscoder.h"
#include "palm/sync/palmrecord.h"

using namespace Kalburator::Shape;

namespace WildPalms::CalendarPlugin {

QByteArray PalmToIcsStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    const auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(sourceBytes);
    return encodePalmToIcs(pr);
}

QByteArray IcsToPalmStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    // slotHint = -1: the backend remaps the category slot authoritatively on
    // write; the X-WP-PALM-CATEGORY-SLOT property rides along for context.
    const auto prOpt = decodeIcsToPalm(sourceBytes, /*slotHint*/ -1);
    if (!prOpt) return {};
    return prOpt->toWireBytes();
}

LossProfile palmToIcsLoss()
{
    return {};  // lossless: Palm appointment is a subset of a VEVENT
}

LossProfile icsToPalmLoss()
{
    LossProfile p;
    // Palm DatebookDB has no field for these (canon property vocabulary):
    for (const char *name : {
            "location", "locations", "attendees", "organizer", "priority",
            "status", "url", "attachments", "categories", "timeTransparency",
            "freeBusyStatus", "onlineMeeting", "eventType", "classification", "color" }) {
        p.affected.insert(PropertyId{QString::fromLatin1(name)}, LossKind::Dropped);
    }
    // Survive in reduced form: Palm holds one display alarm and a recurrence
    // subset (no BYSETPOS/BYWEEKNO/multi-rule).
    p.affected.insert(PropertyId{QStringLiteral("alarms")},     LossKind::Simplified);
    p.affected.insert(PropertyId{QStringLiteral("recurrence")}, LossKind::Simplified);
    return p;
}

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 3: Commit (deferred to Task 3 build — these files don't compile standalone without the extension/CMake). Proceed to Task 2.**

---

### Task 2: Create the CalendarDomainExtension

**Files:**
- Create: `src/plugins/calendar/calendardomainextension.h`
- Create: `src/plugins/calendar/calendardomainextension.cpp`

Mirror `src/plugins/contacts/contactsdomainextension.{h,cpp}`.

- [ ] **Step 1: Create the header**

`src/plugins/calendar/calendardomainextension.h`:
```cpp
#pragma once

namespace Kalburator::Shape { class TransformationRegistry; }

namespace WildPalms::CalendarPlugin {

// Registers the (calendar, palm) peer shape and palm<->ical edges with the
// shape graph. The ical<->canon hop is libkalburator's (CalendarStockShapes).
class CalendarDomainExtension {
public:
    static void registerWith(Kalburator::Shape::TransformationRegistry &registry);
};

} // namespace WildPalms::CalendarPlugin
```

- [ ] **Step 2: Create the implementation**

`src/plugins/calendar/calendardomainextension.cpp`:
```cpp
#include "calendardomainextension.h"

#include "palmtoicstransformation.h"
#include "propertycatalogue.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace WildPalms::CalendarPlugin {

namespace {

PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    // Palm DatebookDB native fields; used by loss-profile UI to describe a
    // palm-shape record.
    cat.addProperty({ PropertyId{"summary"},    PropertyKind::String,   QStringLiteral("Summary") });
    cat.addProperty({ PropertyId{"note"},       PropertyKind::String,   QStringLiteral("Note") });
    cat.addProperty({ PropertyId{"start"},      PropertyKind::Json,     QStringLiteral("Start") });
    cat.addProperty({ PropertyId{"end"},        PropertyKind::Json,     QStringLiteral("End") });
    cat.addProperty({ PropertyId{"allDay"},     PropertyKind::Boolean,  QStringLiteral("All Day") });
    cat.addProperty({ PropertyId{"recurrence"}, PropertyKind::StringList,QStringLiteral("Recurrence") });
    cat.addProperty({ PropertyId{"alarms"},     PropertyKind::Json,     QStringLiteral("Alarm") });
    cat.addProperty({ PropertyId{"category"},   PropertyKind::Integer,  QStringLiteral("Category Slot") });
    return cat;
}

} // namespace

void CalendarDomainExtension::registerWith(TransformationRegistry &registry)
{
    const Shape palm{ DomainId{"calendar"}, EncodingId{"palm"} };
    const Shape ical{ DomainId{"calendar"}, EncodingId{"ical"} };

    // Defensive: libkalburator's CalendarStockShapes normally registers the
    // ical peer shape at plugin-load time. In unit tests that don't run the
    // full init path, register a placeholder so registerEdge's endpoint check
    // passes; registerShape is idempotent (real catalogue replaces it later).
    if (registry.catalogueFor(ical) == nullptr) {
        registry.registerShape(ical, {});
    }
    registry.registerShape(palm, makePalmCatalogue());

    // palm -> ical (lossless; identity X- stamps preserved by ical<->canon)
    registry.registerEdge(TransformationEdge{
        palm, ical, palmToIcsLoss(), std::make_shared<PalmToIcsStage>() });

    // ical -> palm (lossy; Palm DatebookDB holds a subset of VEVENT)
    registry.registerEdge(TransformationEdge{
        ical, palm, icsToPalmLoss(), std::make_shared<IcsToPalmStage>() });
}

} // namespace WildPalms::CalendarPlugin
```
(Verify `TransformationEdge`'s constructor arg order and the `registerShape`/`catalogueFor`/`registerEdge` signatures against `contactsdomainextension.cpp` — they must match exactly.)

---

### Task 3: Flip the backend to the palm shape

**Files:**
- Modify: `src/plugins/calendar/palmcalendarbackend.cpp`

Read `src/plugins/contacts/palmcontactsbackend.cpp` first for the proven wire-bytes pattern.

- [ ] **Step 1: Change `nativeShapes()` to palm**

Replace:
```cpp
QList<Kalburator::Shape::Shape> PalmCalendarBackend::nativeShapes() const
{
    return { { Kalburator::Shape::DomainId{QStringLiteral("calendar")},
               Kalburator::Shape::EncodingId{QStringLiteral("ical")} } };
}
```
with:
```cpp
QList<Kalburator::Shape::Shape> PalmCalendarBackend::nativeShapes() const
{
    return { { Kalburator::Shape::DomainId{QStringLiteral("calendar")},
               Kalburator::Shape::EncodingId{QStringLiteral("palm")} } };
}
```

- [ ] **Step 2: Present Palm wire in the three read paths**

In `loadRecords`, `loadRecord`, and `modifiedSince`, replace the iCal encoding with raw wire bytes. Specifically, in each, change:
```cpp
        QByteArray ics = encodePalmToIcs(pr);
        if (ics.isEmpty()) continue;

        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = ics;
        br.type         = QStringLiteral("text/calendar");
```
to:
```cpp
        Kalburator::Sync::BackendRecord br;
        br.id           = idForPalmRecord(pr.recordId);
        br.data         = pr.toWireBytes();
        br.type         = QStringLiteral("calendar");
```
(For `loadRecord`, which uses `*pr`/`pr->`, the analogous change is `br.data = pr->toWireBytes();` and dropping the `encodePalmToIcs`/empty-check. Match the single-record control flow — it returns `std::nullopt` only on decode-id/backend failure, not on empty bytes.)

- [ ] **Step 3: Consume Palm wire in create/update**

In `createRecord`, replace:
```cpp
    auto prOpt = decodeIcsToPalm(record.data, slot);
    if (!prOpt) return {};

    auto pr = *prOpt;
    pr.category     = static_cast<std::uint8_t>(slot);
```
with:
```cpp
    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.category     = static_cast<std::uint8_t>(slot);
```
In `updateRecord`, replace:
```cpp
    auto prOpt = decodeIcsToPalm(record.data, slot);
    if (!prOpt) return false;

    auto pr = *prOpt;
    pr.recordId     = rid;
```
with:
```cpp
    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.recordId     = rid;
```

- [ ] **Step 4: Drop the now-unused icstranscoder include if present**

`palmcalendarbackend.cpp` includes `"icstranscoder.h"`. After Steps 2–3 it no longer calls `encodePalmToIcs`/`decodeIcsToPalm` directly. Remove the `#include "icstranscoder.h"` line (the transcoder is now only used by the stages). Keep `#include "palm/sync/palmrecord.h"`.

(Do not build yet — needs CMake + plugin registration. Continue to Task 4.)

---

### Task 4: Register the extension in the plugin

**Files:**
- Modify: `src/plugins/calendar/calendarbackendplugin.cpp`

- [ ] **Step 1: Add includes**

Near the existing includes, add:
```cpp
#include "calendardomainextension.h"
#include "transformationregistry.h"
```

- [ ] **Step 2: Register in the constructor**

In the `CalendarBackendPlugin::CalendarBackendPlugin()` constructor body (after the member init list, inside `{ ... }`), add:
```cpp
    // Phase 3: register the (calendar, palm) peer shape and palm<->ical edges
    // with the process-wide TransformationRegistry at plugin construction
    // (mirrors ContactsBackendPlugin). Idempotent across instances.
    CalendarDomainExtension::registerWith(
        Kalburator::Shape::TransformationRegistry::instance());
```

---

### Task 5: Wire the new sources into CMake + build to green

**Files:**
- Modify: `src/plugins/calendar/CMakeLists.txt` (submodule)
- Modify: `tests/plugins/calendar/CMakeLists.txt` (superproject)

- [ ] **Step 1: Add the new sources to the plugin target**

In `src/plugins/calendar/CMakeLists.txt`, find where the plugin's sources are listed (it currently lists `palmcalendarbackend.cpp`, `icstranscoder.cpp`, `calendarbackendplugin.cpp`, etc.) and add:
```cmake
    palmtoicstransformation.cpp
    calendardomainextension.cpp
```
(Mirror how `src/plugins/contacts/CMakeLists.txt` lists `palmtovcardtransformation.cpp` and `contactsdomainextension.cpp`.)

- [ ] **Step 2: Add the new sources to every test target that compiles the backend or plugin**

In `tests/plugins/calendar/CMakeLists.txt`, the targets `tst_calendarblobbackend` and `tst_calendarbackendplugin` compile `palmcalendarbackend.cpp` (and the plugin). They now also need `${CALENDAR_PLUGIN_SRC_DIR}/palmtoicstransformation.cpp` and (for the plugin target) `${CALENDAR_PLUGIN_SRC_DIR}/calendardomainextension.cpp`. Add those sources to those targets, and ensure each has the libkalburator include prepend (`target_include_directories(<t> BEFORE PRIVATE $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>)`) and links `Kalburator::Sync`. (Check the existing `CALENDAR_PLUGIN_SRC_DIR` variable name at the top of the file; reuse it.)

- [ ] **Step 3: Build the plugin + calendar tests**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -25`
Fix any compile errors against the proven sibling files. The build of the calendar plugin and `tst_calendarbackendplugin` must succeed. `tst_calendarblobbackend` may now FAIL TO COMPILE OR (when run) fail, because it expects iCal output — that's fixed in Task 7. For now, ensure everything except the known `tst_calendarblobbackend` assertions compiles.

- [ ] **Step 4: Commit the submodule changes (stages + extension + backend + plugin + plugin CMake)**

```bash
git -C src/plugins/calendar add palmtoicstransformation.h palmtoicstransformation.cpp \
    calendardomainextension.h calendardomainextension.cpp \
    palmcalendarbackend.cpp calendarbackendplugin.cpp CMakeLists.txt
git -C src/plugins/calendar commit -m "feat: calendar as (calendar,palm) shape peer with palm<->ical edges (Phase 3)

Move Palm<->iCal conversion from the backend into PalmToIcsStage/IcsToPalmStage;
declare (calendar,palm) native + register edges via CalendarDomainExtension;
backend now presents/consumes Palm wire bytes. The engine sees the full
palm->ical->canon chain with an honest LossProfile.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 6: Create the canon round-trip verification test

**Files:**
- Create: `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp`
- Modify: `tests/plugins/calendar/CMakeLists.txt`

Mirror `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp` (read it first).

- [ ] **Step 1: Create the test**

`tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp`:
```cpp
#include <QTest>

// WildPalms calendar plugin
#include "calendardomainextension.h"
#include "palm/sync/palmrecord.h"
#include "palm/calendar/datebookcodec.h"      // DatebookCodec, X-WP-PALM-* property names

// libkalburator shape graph + calendar canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "calendardomaindefinition.h"
#include "calendarstockshapes.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

using namespace Kalburator::Shape;
using WildPalms::CalendarPlugin::CalendarDomainExtension;
using WildPalms::PalmSync::PalmRecord;

namespace {

ShapeRegistries makeCalendarRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Calendar::CalendarDomainDefinition def;
    const auto spine = def.canonicalSpine();        // [ (ical, cat), (canon, cat) ]
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

    Kalburator::Calendar::CalendarStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (calendar, palm) + palm<->ical edges. Not part of stock; the
    // palm->ical->canon path only compiles once this runs.
    CalendarDomainExtension::registerWith(reg);

    return regs;
}

// Build a (calendar, palm) record's wire bytes carrying known identity + a
// recurring event, via DatebookCodec::encode on an Event we construct.
QByteArray makePalmRecordBytes(quint8 slot, quint32 recordId,
                               const QString &summary)
{
    auto event = KCalendarCore::Event::Ptr(new KCalendarCore::Event);
    event->setSummary(summary);
    event->setDtStart(QDateTime(QDate(2026, 6, 1), QTime(9, 0), QTimeZone::utc()));
    event->setDtEnd(QDateTime(QDate(2026, 6, 1), QTime(10, 0), QTimeZone::utc()));
    event->recurrence()->setDaily(1);
    event->setNonKDECustomProperty(
        WildPalms::PalmCalendar::DatebookCodec::RecordIdProperty,
        QString::number(recordId).toLatin1());

    PalmRecord pr = WildPalms::PalmCalendar::DatebookCodec::encode(event, slot);
    return pr.toWireBytes();
}

} // namespace

class TestCalendarCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentityAndRecurrence()
    {
        const auto regs = makeCalendarRegistries();
        const Shape palm { DomainId{"calendar"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"calendar"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 2, /*recordId*/ 77,
                                QStringLiteral("Standup"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(), "no palm->canon pipeline (palm->ical->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        // Decode the round-tripped palm wire back to an Event and check identity.
        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        const auto decoded = WildPalms::PalmCalendar::DatebookCodec::decode(pr2);
        QVERIFY2(decoded.isValid(), "round-tripped palm record did not decode");
        QCOMPARE(decoded.event->summary(), QStringLiteral("Standup"));
        QVERIFY2(decoded.event->recurs(), "daily recurrence lost across canon round-trip");

        const QString rid = decoded.event->nonKDECustomProperty(
            WildPalms::PalmCalendar::DatebookCodec::RecordIdProperty);
        QCOMPARE(rid, QStringLiteral("77"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeCalendarRegistries();
        const Shape palm { DomainId{"calendar"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"calendar"}, EncodingId{"canon"} };

        QVERIFY2(regs.transformation.inspect(palm, canon).isLossless(),
                 "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm cannot hold most calendar fields)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("attendees")}),
                 LossKind::Dropped);
    }
};

QTEST_GUILESS_MAIN(TestCalendarCanonRoundtrip)
#include "tst_calendar_canon_roundtrip.moc"
```
(If a libkalburator API name differs — `CalendarStockShapes`, `CalendarDomainDefinition`, the `Kalburator::Calendar::` namespace, `nonKDECustomProperty`, `DatebookCodec::encode` arity — match the real spelling from `../libkalburator/tests/calendar/tst_calendar_canon_roundtrip.cpp`, `src/palm/calendar/datebookcodec.h`, and the contacts test. Do NOT stub around a mismatch; report it.)

- [ ] **Step 2: Register the test target in CMake**

Append to `tests/plugins/calendar/CMakeLists.txt` (mirror the contacts canon test block; reuse `CALENDAR_PLUGIN_SRC_DIR`):
```cmake
# --- Phase 3: calendar canon round-trip ---
add_executable(tst_calendar_canon_roundtrip
    tst_calendar_canon_roundtrip.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/calendardomainextension.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/palmtoicstransformation.cpp
    ${CALENDAR_PLUGIN_SRC_DIR}/icstranscoder.cpp
)
target_include_directories(tst_calendar_canon_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CALENDAR_PLUGIN_SRC_DIR}
)
target_include_directories(tst_calendar_canon_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_calendar_canon_roundtrip
    PRIVATE
        WildPalmsPalmCalendar   # DatebookCodec, CategoryMappingStore
        WildPalmsPalmSync       # PalmRecord
        Kalburator::Sync
        KF6::CalendarCore
        Qt::Test
        Qt::Core
)
add_test(NAME tst_calendar_canon_roundtrip COMMAND tst_calendar_canon_roundtrip)
set_tests_properties(tst_calendar_canon_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```
(Verify the `WildPalmsPalmCalendar` / `WildPalmsPalmSync` lib names and that `DatebookCodec` lives in `WildPalmsPalmCalendar` — check the contacts/calendar test CMake and `src/palm/calendar/CMakeLists.txt`.)

- [ ] **Step 3: Build + run ONLY this test**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_calendar_canon_roundtrip 2>&1 | tail -15`
then `ctest --test-dir build-dev -R calendar_canon_roundtrip --output-on-failure 2>&1 | tail -30`.

- [ ] **Step 4: Branch on the result**

**If it PASSES:** identity + recurrence survive and loss is honest — proceed.
**If `palmCanonRoundTripPreservesIdentityAndRecurrence` FAILS:** capture the canon JSON (temporarily `qDebug() << canonBytes`, then revert), check whether `providerExtras["x-ical"]` carries `X-WP-PALM-RECORDID` / whether recurrence survived. Conclude whether libkalburator's ical↔canon dropped the X-prop or the recurrence, vs a WP stage bug. STOP and report (do not weaken assertions).
**If `lossProfileIsHonest` FAILS:** report which sub-assertion; likely a property-name mismatch between `icsToPalmLoss()` and the canon vocabulary — fix the name in `palmtoicstransformation.cpp` to match `calendarcanonproperties.cpp`.

- [ ] **Step 5: Commit the test (superproject)**

```bash
git add tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp tests/plugins/calendar/CMakeLists.txt
git commit -m "test(calendar): canon round-trip + loss-honesty verification (Phase 3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 7: Fix the existing backend test for the palm-wire shape

**Files:**
- Modify: `tests/plugins/calendar/tst_calendarblobbackend.cpp`

- [ ] **Step 1: Identify the iCal-shaped assertions**

Read `tests/plugins/calendar/tst_calendarblobbackend.cpp`. Find every place that assumes `loadRecords()`/`loadRecord()` returns iCal (e.g. asserting `br.type == "text/calendar"`, parsing `br.data` as iCal via `ICalFormat`, or checking for `BEGIN:VCALENDAR`). The backend now returns `PalmRecord` wire bytes with `br.type == "calendar"`.

- [ ] **Step 2: Update them to the wire-bytes contract**

For each such assertion, mirror how `tests/plugins/contacts/tst_contactsblobbackend.cpp` verifies the palm-wire backend: decode `br.data` via `PalmRecord::fromWireBytes(br.data)` (then `DatebookCodec::decode` if it needs to inspect event fields), and expect `br.type == "calendar"`. Update the assertions to check the decoded values rather than iCal text. Do not delete coverage — translate it.

- [ ] **Step 3: Ensure the test target compiles the new sources**

If `tst_calendarblobbackend` needs `palmtoicstransformation.cpp` (it shouldn't, if the backend no longer references the transcoder directly — but it will need `WildPalmsPalmCalendar`/`WildPalmsPalmSync` for `DatebookCodec`/`PalmRecord` and `fromWireBytes`). Confirm links are present (they already link the palm libs). Add includes for `palm/sync/palmrecord.h` and `palm/calendar/datebookcodec.h` as needed.

- [ ] **Step 4: Build + run it**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_calendarblobbackend 2>&1 | tail -10`
then `ctest --test-dir build-dev -R calendarblobbackend --output-on-failure 2>&1 | tail -20`.
Expected: PASS.

- [ ] **Step 5: Commit (superproject)**

```bash
git add tests/plugins/calendar/tst_calendarblobbackend.cpp
git commit -m "test(calendar): tst_calendarblobbackend expects Palm wire bytes (Phase 3)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8: Full suite green + submodule pointer bump

**Files:** superproject gitlink for `src/plugins/calendar`.

- [ ] **Step 1: Clean build + full suite**

Run: `cmake --build build-dev -j"$(nproc)" 2>&1 | tail -10` then
`ctest --test-dir build-dev --output-on-failure 2>&1 | tail -20`.
Expected: all pass. Count = previous (96) + 1 new test (`tst_calendar_canon_roundtrip`) = 97. If any OTHER calendar test (e.g. `tst_icstranscoder`, `tst_calendarbackendplugin`, `tst_calendarconflicthandler`) fails, diagnose: `tst_icstranscoder` should still pass (the transcoder is unchanged); a plugin/conflict failure may indicate the shape flip affected an assertion — report it.

- [ ] **Step 2: Bump the calendar submodule pointer and commit**

```bash
git add src/plugins/calendar
git status   # confirm only the calendar gitlink moved
git commit -m "build: bump calendar submodule (Phase 3 canon migration)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 3: Report the final unpushed state**

`git log --oneline main..HEAD` (superproject) and `git -C src/plugins/calendar log --oneline -4`. State what a push would publish. Do NOT push.

---

## Phase 3 done when

- Calendar backend declares `(calendar, palm)` and presents/consumes Palm wire; the palm↔ical conversion is a registered shape edge with an honest `LossProfile`.
- `tst_calendar_canon_roundtrip` passes (identity + recurrence survive `palm→canon→palm`; loss honest).
- `tst_calendarblobbackend` updated and green; full suite 97/97; no other regressions.
- Calendar submodule changes committed + pointer bumped (unpushed).

## Self-review notes

- **Spec coverage:** the migration (stages, extension, backend flip, plugin registration, CMake) is Tasks 1–5; verification is Task 6; the forced test churn is Task 7; regression gate + pointer bump is Task 8. The conflict handler is explicitly left unchanged (documented, mirrors contacts).
- **No placeholders:** the two new files, the backend diffs, the plugin registration, and the verification test are complete code. Three spots say "match the proven sibling/upstream spelling" (TransformationStage base header, TransformationEdge ctor arg order, libkalburator calendar API names) — each names the exact authority file to copy and forbids stubbing. Task 7's translation references `tst_contactsblobbackend.cpp` as the concrete pattern because the existing assertions aren't reproduced here.
- **Type consistency:** stage classes implement `transform(QByteArray)->QByteArray` (matches contacts); `LossProfile.affected` four-kind API (matches Phase 1 port); `PalmRecord::toWireBytes`/`fromWireBytes` (matches contacts backend); loss keys use canon calendar `PropertyId`s from `calendarcanonproperties.cpp`.
- **DRY:** stage bodies reuse the existing `icstranscoder` (`encodePalmToIcs`/`decodeIcsToPalm`) — no new conversion logic; mirrors contacts reusing its vCard transcoder.
- **Risk noted:** Task 6 Step 4 handles the (real but unlikely, given contacts proved X-prop preservation) chance that identity/recurrence doesn't survive — branch + report, don't weaken.
