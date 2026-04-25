# Phase E.12 — Contacts Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the Contacts conduit as the fourth new-ABI `IBackendPlugin`, after Memo (E.9), Calendar (E.10), and ToDo (E.11). Surface Palm AddressDB category slots as virtual sub-collections, route end-to-end through `BlobSyncEngine::twoWayWithBaseline` carrying vCard 4.0 bytes, and add a Contacts-aware conflict handler that performs a multi-valued-field union merge so adding a phone on one side and an email on the other doesn't drop either edit.

**Architecture:** `ContactsBackendPlugin` returns `ProvidedBackends.blob = ContactsBlobBackend` (transcoding `IBlobBackend`, one collection per populated category slot, `text/vcard` carrying vCard 4.0). Plugin owns a `CategoryMappingStore` populated from the AddressDB AppInfo block via the shared `parseCategoryAppInfo` reader (already in `WildPalmsPalmCalendar` from E.11). `ContactsConflictHandler` composes a `PalmConflictHandler` and adds one Contacts overlay (multi-valued-field union for phone[0..4]/custom[0..3] when single-valued fields agree) before delegation. Behind CMake toggle `WILDPALMS_CONTACTS_PLUGIN_V2=ON`; legacy `ContactConduit` keeps building when off.

**Tech Stack:** C++20, Qt6 (Core, Gui, Widgets, Test), KF6::CoreAddons (`KPluginMetaData`, `KPluginFactory`, `kcoreaddons_add_plugin`), KF6::Contacts (`Addressee`, `VCardConverter`), `Kalburator::Sync` (`IBlobBackend`, `BlobSyncEngine::twoWayWithBaseline`, `MockBlobBackend`, `QSyncCore::ConflictHandler`, `BlobBaselineStore`, `ConflictHandlerRegistry`, `ConflictStore`, `ConflictPolicy`), pisock (via existing `decodeContact`/`encodeContact` from `WildPalmsPalmCodecs`). Reuses existing `toAddressee`/`fromAddressee` from `kde_pim_convert.{h,cpp}`. No new external dependencies (`KF6::Contacts` is already a `WildPalmsPalmCodecs` dep).

**Parent spec:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.12 (line 590).

**Repo:** Parent at `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). Plugin source lives in submodule `src/plugins/contacts/` (`wildpalms-conduit-contacts.git`). Tests live in parent under `tests/plugins/contacts/`. No upstream libkalburator changes.

**Submodule split:**
- Tasks 1–4: commit inside `src/plugins/contacts/` submodule.
- Tasks 5, 6: parent repo.
- Task 6 includes the submodule pointer bump for `src/plugins/contacts/`.

**Pre-existing assets (do NOT re-create):**
- `src/palm/codecs/contactcodec.{h,cpp}` — `Contact` POD + `encodeContact`/`decodeContact`.
- `src/palm/codecs/kde_pim_convert.{h,cpp}` — `toAddressee(Contact)` / `fromAddressee(Addressee)`.
- `src/palm/adapters/palmcontactsadapter.{h,cpp}` — record-level adapter, already shipped.
- `src/palm/calendar/categoryappinforeader.{h,cpp}` — `parseCategoryAppInfo` + `populateFromAppInfo`, already in `WildPalmsPalmCalendar` static lib (promoted in E.11).

---

## Scope explicitly excluded

- **Deleting `ContactConduit`, `contactmapper.{h,cpp}`, `contactview.{h,cpp}`, `contacts-conduit.json`** — retired in E.16.
- **`ContactView` ↔ `PalmContactsAdapter` rewiring** — no main-window view is added by this phase. Legacy `ContactView` stays attached to legacy `ContactConduit` until E.16. (See "Decision: no `hasMainView()`" below.)
- **`LocalBlobBackend` smoke test for Contacts** — `tst_contacts_v2` uses `MockBlobBackend`, mirroring memo/calendar/todo id-space deferral. `IDMappingStore` is E.15+.
- **Live-device AddressDB AppInfo integration test** — E.18 (POSE64 sandbox).
- **AddressDB phone-label table reader** (`unpack_AddressAppInfo`) — `decodeContact` ships a hardcoded default-label table today; round-trip is lossy for user-renamed phone labels. Defer to E.18 / post-launch unless real syncs surface bugs. Same posture original Calendar took for `parseDatebookAppInfo` (deferred to E.10).
- **Speculative conflict overlays** (custom-field-vs-overwrite, category-vs-field, address-block atomicity) — land post-E.18 only if real syncs surface bugs.
- **Typed `ContactsSyncBackend` upstream extraction** — defers to "second consumer needs it" gate. `out.calendar` stays null. (Parent spec line 590 mentions `PalmContactsAdapter` for typed UI consumers; the adapter already ships in `WildPalmsPalmCodecs`'s sibling `WildPalmsPalmAdapters` and is not part of this phase's wiring.)
- **Flipping the CMake toggle default** — `WILDPALMS_CONTACTS_PLUGIN_V2` ships `ON`; `OFF` keeps legacy conduit building.
- **`ConflictDialog` new-plugin lookup-path** — open from E.9; not blocked by E.12.

### Decision: no `hasMainView()` for E.12

Memo and ToDo plugins each surfaced a main-window tab (`MemoView`/`TaskView`) by reusing the legacy view widget. Calendar plugin returned `hasMainView() = false` (calendar UI lives elsewhere in the app). Contacts follows Calendar's pattern: `hasMainView()` returns `false` for E.12. Rationale: legacy `ContactView` is tightly coupled to `ContactConduit`'s data model; reusing it without the conduit requires either the rewiring excluded above, or a parallel-data hack that we'd then have to undo at E.16. Cleaner to leave UI to legacy until E.16's unified-runtime cleanup.

---

## File Structure

**Files to CREATE in contacts submodule — Tasks 1–4:**

- `contactsvcardtranscoder.h` (Task 1) — namespace-scope free functions.
- `contactsvcardtranscoder.cpp` (Task 1)
- `contactsblobbackend.h` (Task 2) — transcoding `IBlobBackend`.
- `contactsblobbackend.cpp` (Task 2)
- `contactsconflicthandler.h` (Task 3) — `QSyncCore::ConflictHandler` with multi-valued-field union overlay.
- `contactsconflicthandler.cpp` (Task 3)
- `contactsbackendplugin.h` (Task 4) — `IBackendPlugin` shell.
- `contactsbackendplugin.cpp` (Task 4) — class implementation + `K_PLUGIN_FACTORY_WITH_JSON`.
- `contacts-backend-plugin.json` (Task 4) — new manifest.

**Files to MODIFY in contacts submodule — Task 4:**

- `CMakeLists.txt` — add `WILDPALMS_CONTACTS_PLUGIN_V2` option; build new plugin when on; keep legacy when off.

**Files to CREATE in parent repo — Tasks 1–5:**

- `tests/plugins/contacts/CMakeLists.txt` (Task 1 onward, grown per task)
- `tests/plugins/contacts/tst_contactsvcardtranscoder.cpp` (Task 1)
- `tests/plugins/contacts/tst_contactsblobbackend.cpp` (Task 2)
- `tests/plugins/contacts/tst_contactsconflicthandler.cpp` (Task 3)
- `tests/plugins/contacts/tst_contactsbackendplugin.cpp` (Task 4)
- `tests/plugins/contacts/tst_contacts_v2.cpp` (Task 5)

**Files to MODIFY in parent — Tasks 1, 6:**

- `tests/plugins/CMakeLists.txt` (Task 1) — add `add_subdirectory(contacts)`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (Task 6) — flip row E.12 to `✅ **E.12**`.
- `docs/plans/2026-04-20-libkalburator-integration.md` (Task 6) — mark E.12 landed.
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md` + new `project_phase_e12_contacts.md` (Task 6).
- Submodule pointer bump for `src/plugins/contacts/` (Task 6).

---

## Conventions used throughout

- **Namespace:** `WildPalms::ContactsPlugin` (mirrors `WildPalms::TodoPlugin`, `WildPalms::MemoPlugin`).
- **Collection-id prefix:** `palm:contact/` — singular, mirrors `palm:todo/` and `palm:calendar/`.
- **MIME type:** `text/vcard` (the modern spelling; `text/x-vcard` is legacy).
- **Plugin id:** `contacts`.
- **Plugin install namespace:** `wildpalms/plugins`.
- **Backend id:** `palm-contacts`.
- **vCard format:** vCard 4.0 (`KContacts::VCardConverter::v4_0`).
- **Round-trip extension properties:** `X-WP-PALM-CATEGORY-SLOT`, `X-WP-PALM-RECORDID` — same names as `TodoIcsTranscoder`. (`kde_pim_convert::toAddressee` already stashes its own `X-PALM-*` properties for Palm-specific Contact fields; we do not duplicate those.)

---

## Task 1: `ContactsVcardTranscoder`

**Why:** Bridge `PalmRecord` (Palm wire bytes via pisock's `pack_Address`) and vCard 4.0 bytes (via `KContacts::VCardConverter`). Composes the two existing converters: `decodeContact`/`encodeContact` (Palm bytes ↔ `Contact` POD) and `toAddressee`/`fromAddressee` (Contact POD ↔ `KContacts::Addressee`). Stamps `X-WP-PALM-CATEGORY-SLOT` and `X-WP-PALM-RECORDID` on the Addressee for round-trip fidelity. Mirrors `TodoIcsTranscoder` structure exactly.

**Files:**
- Create (submodule): `src/plugins/contacts/contactsvcardtranscoder.h`
- Create (submodule): `src/plugins/contacts/contactsvcardtranscoder.cpp`
- Create (parent): `tests/plugins/contacts/CMakeLists.txt`
- Create (parent): `tests/plugins/contacts/tst_contactsvcardtranscoder.cpp`
- Modify (parent): `tests/plugins/CMakeLists.txt`

- [ ] **Step 1: Create the test directory and wire it into the parent test tree**

Create `tests/plugins/contacts/CMakeLists.txt` containing the per-task target additions (start with the transcoder target only; grow as tasks land):

```cmake
# Phase E.12 — Contacts plugin tests.
# Tasks 1-5 build test binaries directly against the source files in
# the contacts submodule.

set(CONTACTS_PLUGIN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/plugins/contacts)

# --- Task 1: ContactsVcardTranscoder ---
add_executable(tst_contactsvcardtranscoder
    tst_contactsvcardtranscoder.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsvcardtranscoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsvcardtranscoder
    PRIVATE
        WildPalmsPalmCodecs   # Contact POD, encodeContact/decodeContact, toAddressee/fromAddressee
        WildPalmsPalmSync     # PalmRecord
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsvcardtranscoder COMMAND tst_contactsvcardtranscoder)
```

Then edit `tests/plugins/CMakeLists.txt` to add `add_subdirectory(contacts)` alongside the existing `add_subdirectory(todos)` (and the others). Read the existing file first; preserve ordering.

- [ ] **Step 2: Write the failing transcoder header**

Create `src/plugins/contacts/contactsvcardtranscoder.h`:

```cpp
#ifndef WILDPALMS_CONTACTS_CONTACTSVCARDTRANSCODER_H
#define WILDPALMS_CONTACTS_CONTACTSVCARDTRANSCODER_H

#include <optional>

#include <QByteArray>

#include "palm/sync/palmrecord.h"

namespace WildPalms::ContactsPlugin {

/**
 * @brief Encode a Palm Address record into a vCard 4.0 byte string.
 *
 * Composes WildPalms::PalmCodecs::decodeContact (Palm bytes -> Contact
 * POD) with WildPalms::PalmCodecs::toAddressee (Contact -> Addressee),
 * then serialises via KContacts::VCardConverter::v4_0. Stamps
 * X-WP-PALM-CATEGORY-SLOT and X-WP-PALM-RECORDID extension properties
 * on the Addressee for round-trip.
 *
 * Returns empty QByteArray on decode failure or empty input.
 */
QByteArray encodePalmToVcard(const WildPalms::PalmSync::PalmRecord &record);

/**
 * @brief Decode vCard bytes (v3.0 or v4.0) into a PalmRecord.
 *
 * `slotHint` populates `PalmRecord::category` (overriding any
 * X-WP-PALM-CATEGORY-SLOT in the body — collection-id wins). The
 * X-WP-PALM-RECORDID property, if present, populates
 * `PalmRecord::recordId`; otherwise recordId stays 0 and the device
 * assigns on write. The KContacts::Addressee's `secrecy()` is mapped to
 * PalmRecord::AttrSecret via `fromAddressee`'s convention.
 *
 * Returns std::nullopt if `vcardBytes` doesn't parse to at least one
 * Addressee, or if encoding to Palm bytes fails.
 */
std::optional<WildPalms::PalmSync::PalmRecord>
decodeVcardToPalm(const QByteArray &vcardBytes, int slotHint);

} // namespace WildPalms::ContactsPlugin

#endif // WILDPALMS_CONTACTS_CONTACTSVCARDTRANSCODER_H
```

- [ ] **Step 3: Write the failing transcoder test**

Create `tests/plugins/contacts/tst_contactsvcardtranscoder.cpp`:

```cpp
#include <QTest>
#include <QByteArray>

#include "palm/codecs/contactcodec.h"
#include "palm/sync/palmrecord.h"
#include "plugins/contacts/contactsvcardtranscoder.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::encodePalmToVcard;
using WildPalms::ContactsPlugin::decodeVcardToPalm;

namespace {

PalmRecord makeRecord(const Contact &c, std::uint8_t slot, std::uint32_t id, bool secret = false)
{
    PalmRecord pr;
    pr.data       = encodeContact(c);
    pr.category   = slot;
    pr.recordId   = id;
    pr.attributes = secret ? PalmRecord::AttrSecret : 0;
    return pr;
}

Contact sampleContact()
{
    Contact c;
    c.lastName  = QStringLiteral("Doe");
    c.firstName = QStringLiteral("Jane");
    c.company   = QStringLiteral("Acme");
    c.title     = QStringLiteral("Engineer");
    c.phone[0]  = QStringLiteral("555-1111");
    c.phoneLabels << QStringLiteral("Work");
    c.address   = QStringLiteral("1 Main St");
    c.city      = QStringLiteral("Springfield");
    c.state     = QStringLiteral("IL");
    c.zip       = QStringLiteral("62701");
    c.country   = QStringLiteral("USA");
    c.note      = QStringLiteral("VIP customer");
    return c;
}

} // namespace

class TstContactsVcardTranscoder : public QObject
{
    Q_OBJECT
private slots:
    void roundTripPreservesCoreFields();
    void emptyRecordYieldsEmptyVcard();
    void slotHintOverridesEmbeddedSlot();
    void recordIdRoundTrips();
    void secretBitRoundTrips();
    void emptyVcardYieldsNullopt();
    void garbageVcardYieldsNullopt();
};

void TstContactsVcardTranscoder::roundTripPreservesCoreFields()
{
    const Contact c = sampleContact();
    const auto pr = makeRecord(c, /*slot*/ 3, /*id*/ 0x42);

    const QByteArray vcard = encodePalmToVcard(pr);
    QVERIFY(!vcard.isEmpty());
    QVERIFY(vcard.contains("BEGIN:VCARD"));
    QVERIFY(vcard.contains("END:VCARD"));

    auto decoded = decodeVcardToPalm(vcard, /*slotHint*/ 3);
    QVERIFY(decoded.has_value());
    QCOMPARE(int(decoded->category), 3);
    QCOMPARE(decoded->recordId, 0x42u);

    auto roundTrip = decodeContact(QByteArrayView(decoded->data));
    QVERIFY(roundTrip.has_value());
    QCOMPARE(roundTrip->lastName,  c.lastName);
    QCOMPARE(roundTrip->firstName, c.firstName);
    QCOMPARE(roundTrip->company,   c.company);
    QCOMPARE(roundTrip->title,     c.title);
    QCOMPARE(roundTrip->phone[0],  c.phone[0]);
    QCOMPARE(roundTrip->city,      c.city);
    QCOMPARE(roundTrip->note,      c.note);
}

void TstContactsVcardTranscoder::emptyRecordYieldsEmptyVcard()
{
    PalmRecord pr;   // pr.data is empty
    QVERIFY(encodePalmToVcard(pr).isEmpty());
}

void TstContactsVcardTranscoder::slotHintOverridesEmbeddedSlot()
{
    const auto pr = makeRecord(sampleContact(), /*slot*/ 5, /*id*/ 1);
    const QByteArray vcard = encodePalmToVcard(pr);
    QVERIFY(!vcard.isEmpty());

    auto decoded = decodeVcardToPalm(vcard, /*slotHint*/ 9);
    QVERIFY(decoded.has_value());
    QCOMPARE(int(decoded->category), 9);   // hint wins, embedded ignored
}

void TstContactsVcardTranscoder::recordIdRoundTrips()
{
    const auto pr = makeRecord(sampleContact(), 0, 0xABCDEFu);
    auto decoded = decodeVcardToPalm(encodePalmToVcard(pr), 0);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->recordId, 0xABCDEFu);
}

void TstContactsVcardTranscoder::secretBitRoundTrips()
{
    const auto pr = makeRecord(sampleContact(), 2, 7, /*secret*/ true);
    auto decoded = decodeVcardToPalm(encodePalmToVcard(pr), 2);
    QVERIFY(decoded.has_value());
    QVERIFY(decoded->isSecret());
}

void TstContactsVcardTranscoder::emptyVcardYieldsNullopt()
{
    QVERIFY(!decodeVcardToPalm({}, 0).has_value());
}

void TstContactsVcardTranscoder::garbageVcardYieldsNullopt()
{
    QVERIFY(!decodeVcardToPalm("not a vcard at all", 0).has_value());
}

QTEST_GUILESS_MAIN(TstContactsVcardTranscoder)
#include "tst_contactsvcardtranscoder.moc"
```

- [ ] **Step 4: Reconfigure and run the test (expect link or build failure)**

```bash
cmake --build build-dev --target tst_contactsvcardtranscoder
```

Expected: build failure — `contactsvcardtranscoder.cpp` doesn't exist yet, target won't link.

- [ ] **Step 5: Implement the transcoder**

Create `src/plugins/contacts/contactsvcardtranscoder.cpp`:

```cpp
#include "contactsvcardtranscoder.h"

#include "palm/codecs/contactcodec.h"
#include "palm/codecs/kde_pim_convert.h"

#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

#include <QString>

namespace WildPalms::ContactsPlugin {

namespace {

constexpr const char *kCategorySlotProp = "X-WP-PALM-CATEGORY-SLOT";
constexpr const char *kRecordIdProp     = "X-WP-PALM-RECORDID";

} // namespace

QByteArray encodePalmToVcard(const WildPalms::PalmSync::PalmRecord &record)
{
    if (record.data.isEmpty()) return {};
    auto contact = WildPalms::PalmCodecs::decodeContact(QByteArrayView(record.data));
    if (!contact.has_value()) return {};

    KContacts::Addressee addressee = WildPalms::PalmCodecs::toAddressee(*contact);

    // Round-trip stamps. Stored under the KContacts custom-property
    // namespace ("WP-PALM"). Read back via the matching custom() call
    // in decodeVcardToPalm.
    addressee.insertCustom(QStringLiteral("WP-PALM"),
                           QStringLiteral("CATEGORY-SLOT"),
                           QString::number(record.category));
    addressee.insertCustom(QStringLiteral("WP-PALM"),
                           QStringLiteral("RECORDID"),
                           QString::number(record.recordId));
    if (record.isSecret()) {
        addressee.setSecrecy(KContacts::Secrecy(KContacts::Secrecy::Private));
    }

    KContacts::VCardConverter conv;
    return conv.createVCard(addressee, KContacts::VCardConverter::v4_0);
}

std::optional<WildPalms::PalmSync::PalmRecord>
decodeVcardToPalm(const QByteArray &vcardBytes, int slotHint)
{
    if (vcardBytes.isEmpty()) return std::nullopt;

    KContacts::VCardConverter conv;
    auto list = conv.parseVCards(vcardBytes);
    if (list.isEmpty()) return std::nullopt;

    const KContacts::Addressee &addressee = list.first();
    if (addressee.isEmpty()) return std::nullopt;

    WildPalms::PalmCodecs::Contact contact = WildPalms::PalmCodecs::fromAddressee(addressee);
    QByteArray bytes = WildPalms::PalmCodecs::encodeContact(contact);
    if (bytes.isEmpty()) return std::nullopt;

    WildPalms::PalmSync::PalmRecord pr;
    pr.data     = bytes;
    pr.category = static_cast<std::uint8_t>(slotHint);

    if (addressee.secrecy().type() == KContacts::Secrecy::Private
     || addressee.secrecy().type() == KContacts::Secrecy::Confidential) {
        pr.attributes |= WildPalms::PalmSync::PalmRecord::AttrSecret;
    }

    const QString rid = addressee.custom(QStringLiteral("WP-PALM"),
                                         QStringLiteral("RECORDID"));
    if (!rid.isEmpty()) {
        bool ok = false;
        const std::uint32_t parsed = rid.toUInt(&ok);
        if (ok) pr.recordId = parsed;
    }
    return pr;
}

} // namespace WildPalms::ContactsPlugin
```

- [ ] **Step 6: Build and run the tests; expect PASS**

```bash
cmake --build build-dev --target tst_contactsvcardtranscoder
ctest --test-dir build-dev -R '^tst_contactsvcardtranscoder$' --output-on-failure
```

Expected: 1/1 PASS.

- [ ] **Step 7: Commit (submodule + parent)**

In the contacts submodule:

```bash
cd src/plugins/contacts
git add contactsvcardtranscoder.h contactsvcardtranscoder.cpp
git commit -m "feat(contacts): ContactsVcardTranscoder (Phase E.12 Task 1)

Composes existing Contact <-> Addressee converter with
KContacts::VCardConverter to round-trip Palm AddressDB records as
vCard 4.0. Stamps X-WP-PALM-CATEGORY-SLOT and X-WP-PALM-RECORDID
extension properties on the Addressee.
"
cd ../../..
```

In the parent repo:

```bash
git add tests/plugins/contacts/CMakeLists.txt \
        tests/plugins/contacts/tst_contactsvcardtranscoder.cpp \
        tests/plugins/CMakeLists.txt
git commit -m "test(contacts): tst_contactsvcardtranscoder (Phase E.12 Task 1)"
```

(Submodule pointer bump is deferred to Task 6.)

---

## Task 2: `ContactsBlobBackend`

**Why:** Wrap `PalmBackend`'s "AddressDB" view as an `IBlobBackend` so `BlobSyncEngine` can drive it. Surfaces one collection per populated category slot under `palm:contact/<N>`. Routes records by `PalmRecord::category`. Mirrors `TodoBlobBackend` exactly; only the db-name, prefix, and content-type differ.

**Files:**
- Create (submodule): `src/plugins/contacts/contactsblobbackend.h`
- Create (submodule): `src/plugins/contacts/contactsblobbackend.cpp`
- Create (parent): `tests/plugins/contacts/tst_contactsblobbackend.cpp`
- Modify (parent): `tests/plugins/contacts/CMakeLists.txt`

- [ ] **Step 1: Add the new test target**

Append to `tests/plugins/contacts/CMakeLists.txt`:

```cmake
# --- Task 2: ContactsBlobBackend ---
add_executable(tst_contactsblobbackend
    tst_contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsblobbackend
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsblobbackend
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore
        WildPalmsPalmCodecs      # encodeContact / Contact
        WildPalmsPalmSync        # PalmBackend, PalmRecord, MockPalmDatabaseAccess
        WildPalmsCore
        Kalburator::Sync
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsblobbackend COMMAND tst_contactsblobbackend)
```

- [ ] **Step 2: Write the failing header**

Create `src/plugins/contacts/contactsblobbackend.h`:

```cpp
#ifndef WILDPALMS_CONTACTS_CONTACTSBLOBBACKEND_H
#define WILDPALMS_CONTACTS_CONTACTSBLOBBACKEND_H

#include "iblobbackend.h"

#include <QObject>

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::ContactsPlugin {

/**
 * @brief Transcoding IBlobBackend wrapping PalmBackend's "AddressDB".
 *
 * Surfaces one collection per populated category slot:
 *   - "palm:contact/0"   "Unfiled" (always present)
 *   - "palm:contact/<N>" 1..15, present iff
 *     `categoryStore->slotName("AddressDB", N)` is non-empty.
 *
 * Records route to/from these collections by PalmRecord::category.
 * loadRecords transcodes wire bytes -> vCard 4.0 bytes via
 * ContactsVcardTranscoder; createRecord/updateRecord transcode vCard
 * -> wire and forward to PalmBackend's category-aware
 * createPalmRecord/updatePalmRecord.
 *
 * Lifetime: does NOT own palmBackend or categoryStore. Caller retains
 * ownership; both must outlive the backend.
 */
class ContactsBlobBackend : public Kalburator::Sync::IBlobBackend
{
    Q_OBJECT
public:
    static constexpr const char *BackendId        = "palm-contacts";
    static constexpr const char *PalmDbName       = "AddressDB";
    static constexpr const char *CollectionPrefix = "palm:contact/";

    explicit ContactsBlobBackend(
        WildPalms::PalmSync::PalmBackend *palmBackend,
        const WildPalms::PalmCalendar::CategoryMappingStore *categoryStore,
        QObject *parent = nullptr);
    ~ContactsBlobBackend() override;

    // --- Identity ---
    QString backendId()   const override;
    QString displayName() const override;
    bool    isAvailable() const override;

    // --- Collections ---
    QList<Kalburator::Sync::CollectionInfo> availableCollections() override;
    Kalburator::Sync::CollectionInfo collectionInfo(const QString &collectionId) override;
    QString createCollection(const Kalburator::Sync::CollectionInfo &info) override;

    // --- Records ---
    QList<Kalburator::Sync::BackendRecord> loadRecords(const QString &collectionId) override;
    std::optional<Kalburator::Sync::BackendRecord> loadRecord(const QString &recordId) override;
    QString createRecord(const QString &collectionId,
                         const Kalburator::Sync::BackendRecord &record) override;
    bool    updateRecord(const Kalburator::Sync::BackendRecord &record) override;
    bool    deleteRecord(const QString &recordId) override;

    // --- Change detection ---
    QList<Kalburator::Sync::BackendRecord> modifiedSince(
        const QString &collectionId, const QDateTime &since) override;
    QStringList deletedSince(const QString &collectionId, const QDateTime &since) override;
    bool        supportsDeleteTracking() const override;

    // --- Helpers (exposed for tests) ---
    /// Parse "palm:contact/<N>" -> N. Returns -1 on bad input.
    static int slotFromCollectionId(const QString &collectionId);
    /// Produce "palm:contact/<N>".
    static QString collectionIdForSlot(int slot);

private:
    WildPalms::PalmSync::PalmBackend                     *m_palmBackend = nullptr;
    const WildPalms::PalmCalendar::CategoryMappingStore  *m_categoryStore = nullptr;
};

} // namespace WildPalms::ContactsPlugin

#endif // WILDPALMS_CONTACTS_CONTACTSBLOBBACKEND_H
```

- [ ] **Step 3: Write the failing test**

Create `tests/plugins/contacts/tst_contactsblobbackend.cpp` mirroring `tests/plugins/todos/tst_todoblobbackend.cpp` (read it first as the template). Adapt for Contacts: use `WildPalms::PalmCodecs::Contact` + `encodeContact` to seed the mock device, exercise `availableCollections`, `loadRecords`, `createRecord`, `updateRecord`, `deleteRecord`, `modifiedSince`, and the slot/collection-id helpers.

Required test cases (one `private slot:` each):

1. `slotFromCollectionId_validRange` — slots 0..15 round-trip; out-of-range returns -1; bad prefix returns -1.
2. `availableCollections_includesUnfiledAlways` — empty store still yields one collection (`palm:contact/0`).
3. `availableCollections_includesPopulatedSlots` — store with `setSlotName("AddressDB", 1, "Personal")` adds `palm:contact/1`.
4. `loadRecords_filtersBySlot` — seed device with records in slots 0, 1, 2; `loadRecords("palm:contact/1")` returns only slot-1 records.
5. `loadRecords_skipsDeletedRecords` — record with `AttrDeleted` is excluded.
6. `loadRecord_byId_roundTrips` — seed one record, look up by id, verify vCard contains the contact's last name.
7. `createRecord_assignsCategory` — `createRecord("palm:contact/4", vcard)` writes record with `category=4` to mock device.
8. `updateRecord_preservesSlotFromExisting` — update without category info uses the existing record's slot.
9. `deleteRecord_forwardsToPalmBackend` — verifies via mock device.
10. `modifiedSince_filtersByTimestampAndSlot` — combination of slot and `since` filtering.

Pattern (use directly from `tst_todoblobbackend.cpp` and substitute):

```cpp
#include <QTest>
#include <QDateTime>

#include "palm/calendar/categorymappingstore.h"
#include "palm/codecs/contactcodec.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/sync/palmbackend.h"
#include "palm/sync/palmrecord.h"
#include "plugins/contacts/contactsblobbackend.h"

#include "backendrecord.h"
#include "collectioninfo.h"

using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;
using WildPalms::ContactsPlugin::ContactsBlobBackend;

class TstContactsBlobBackend : public QObject
{
    Q_OBJECT
private slots:
    void slotFromCollectionId_validRange();
    void slotFromCollectionId_outOfRangeReturnsMinusOne();
    void availableCollections_includesUnfiledAlways();
    void availableCollections_includesPopulatedSlots();
    void loadRecords_filtersBySlot();
    void loadRecords_skipsDeletedRecords();
    void loadRecord_byId_roundTrips();
    void createRecord_assignsCategory();
    void updateRecord_preservesSlotFromExisting();
    void deleteRecord_forwardsToPalmBackend();
    void modifiedSince_filtersByTimestampAndSlot();
};
// ... method bodies follow tst_todoblobbackend.cpp's structure.
```

The full test body is left for the executor to write by translating each `tst_todoblobbackend.cpp` test verbatim — substituting `decodeTodo`/`encodeTodo` → `decodeContact`/`encodeContact`, `Todo` POD → `Contact` POD, `"ToDoDB"` → `"AddressDB"`, `palm:todo/` → `palm:contact/`, `text/calendar` → `text/vcard`, and any todo-specific field assertions (`description`, `priority`, `isComplete`) → contact-specific ones (`lastName`, `firstName`).

- [ ] **Step 4: Run the test, expect build failure**

```bash
cmake --build build-dev --target tst_contactsblobbackend
```

Expected: build failure (no `contactsblobbackend.cpp` yet).

- [ ] **Step 5: Implement the backend**

Create `src/plugins/contacts/contactsblobbackend.cpp` by copying `src/plugins/todos/todoblobbackend.cpp` and substituting:

| ToDo source | Contacts replacement |
|---|---|
| `WildPalms::TodoPlugin` | `WildPalms::ContactsPlugin` |
| `TodoBlobBackend` | `ContactsBlobBackend` |
| `todoblobbackend.h` | `contactsblobbackend.h` |
| `todoicstranscoder.h` | `contactsvcardtranscoder.h` |
| `encodePalmToIcs` | `encodePalmToVcard` |
| `decodeIcsToPalm` | `decodeVcardToPalm` |
| `"ToDoDB"` | `"AddressDB"` |
| `"palm-todo"` | `"palm-contacts"` |
| `"Palm ToDo"` | `"Palm Contacts"` |
| `"text/calendar"` | `"text/vcard"` |
| `"calendar"` (collection type) | `"contacts"` |
| `CollectionPrefix` constant | (already substituted via the header) |

Note the case-insensitive `decodeId` comparison in `todoblobbackend.cpp` exists because `PalmBackend::decodeRecordId` re-cases the dbName. AddressDB has no internal capital after the first letter (`AddressDB` → `Addressdb` after re-casing? — read `PalmBackend::decodeRecordId` to confirm; if so, keep the case-insensitive compare).

- [ ] **Step 6: Build + run, expect PASS**

```bash
cmake --build build-dev --target tst_contactsblobbackend
ctest --test-dir build-dev -R '^tst_contactsblobbackend$' --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit (submodule + parent)**

```bash
cd src/plugins/contacts
git add contactsblobbackend.h contactsblobbackend.cpp
git commit -m "feat(contacts): ContactsBlobBackend (Phase E.12 Task 2)

Transcoding IBlobBackend over PalmBackend's AddressDB. One
collection per populated category slot, vCard 4.0 on the wire.
Mirrors TodoBlobBackend's structure exactly.
"
cd ../../..
git add tests/plugins/contacts/CMakeLists.txt \
        tests/plugins/contacts/tst_contactsblobbackend.cpp
git commit -m "test(contacts): tst_contactsblobbackend (Phase E.12 Task 2)"
```

---

## Task 3: `ContactsConflictHandler`

**Why:** When both sides edited the same Palm contact since baseline, the engine flags a conflict. For Contacts the most common "conflict" is benign: Palm side adds a phone number in slot 3, target side adds an email in slot 4. The legacy lastModified tie-break would silently drop one of the two edits. This overlay performs a per-slot union for the multi-valued fields (`phone[0..4]`, `phoneLabels`, `custom[0..3]`) when the single-valued fields agree, and otherwise delegates to `PalmConflictHandler`.

**Overlay rule (precise):**

Decode both `conflict.source.content` and `conflict.target.content` as PalmRecord → Contact. If either side fails to decode, delegate.

Define **single-valued fields**: `lastName`, `firstName`, `company`, `title`, `address`, `city`, `state`, `zip`, `country`, `note`, `showPhone`, plus the PalmRecord secret bit. If any single-valued field differs between sides, **delegate** (genuine field-level conflict).

Define **slot-conflict** for slots `i` in 0..4 (phone) and 0..3 (custom): a slot conflicts if both sides have non-empty values AND the values differ. If any slot conflicts, **delegate**.

Otherwise, **merge**: build a result Contact whose single-valued fields are taken from either side (they agree), and whose multi-valued slots are populated per-slot from whichever side has a non-empty value (or both, if equal). Re-stamp `phoneLabels` to match the surviving non-empty `phone` slots in order. Re-encode to PalmRecord → vCard, set `conflict.mergedContent`, return `ConflictDecision::Merge`.

**Files:**
- Create (submodule): `src/plugins/contacts/contactsconflicthandler.h`
- Create (submodule): `src/plugins/contacts/contactsconflicthandler.cpp`
- Create (parent): `tests/plugins/contacts/tst_contactsconflicthandler.cpp`
- Modify (parent): `tests/plugins/contacts/CMakeLists.txt`

- [ ] **Step 1: Add the test target**

Append to `tests/plugins/contacts/CMakeLists.txt`:

```cmake
# --- Task 3: ContactsConflictHandler ---
add_executable(tst_contactsconflicthandler
    tst_contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsconflicthandler
    PRIVATE ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(tst_contactsconflicthandler
    PRIVATE
        WildPalmsPalmConflict   # PalmConflictHandler
        WildPalmsPalmCodecs     # decodeContact / encodeContact
        WildPalmsPalmSync       # PalmRecord, MockPalmDatabaseAccess
        Kalburator::Sync
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contactsconflicthandler COMMAND tst_contactsconflicthandler)
```

- [ ] **Step 2: Write the failing header**

Create `src/plugins/contacts/contactsconflicthandler.h`:

```cpp
#ifndef WILDPALMS_CONTACTS_CONTACTSCONFLICTHANDLER_H
#define WILDPALMS_CONTACTS_CONTACTSCONFLICTHANDLER_H

#include "conflictpolicy.h"   // brings in QSyncCore::ConflictHandler

#include <memory>

#include <QString>

namespace WildPalms::PalmSync { class IPalmDatabaseAccess; }
namespace WildPalms::PalmConflict {
class PalmConflictHandler;
struct PalmBackendConfig;
}

namespace WildPalms::ContactsPlugin {

/**
 * @brief ConflictHandler with one Contacts overlay, delegating to PalmConflictHandler.
 *
 * Resolution order:
 *   1. Decode both sides as PalmRecord -> Contact POD via the transcoder.
 *      If either side fails to decode -> delegate to PalmConflictHandler.
 *   2. If any single-valued field differs (lastName, firstName, company,
 *      title, address, city, state, zip, country, note, showPhone, or the
 *      record secret bit) -> delegate.
 *   3. If any multi-valued slot (phone[0..4], custom[0..3]) has different
 *      non-empty values on both sides -> delegate.
 *   4. Otherwise merge: per-slot union for the multi-valued fields,
 *      re-serialise to vCard, set mergedContent, return Merge.
 *
 * Owns its inner PalmConflictHandler.
 *
 * Lifetime: does NOT own device or config. Both must outlive the
 * handler.
 */
class ContactsConflictHandler : public Kalburator::Sync::QSyncCore::ConflictHandler
{
public:
    ContactsConflictHandler(WildPalms::PalmSync::IPalmDatabaseAccess *device,
                            const WildPalms::PalmConflict::PalmBackendConfig *config);
    ~ContactsConflictHandler() override;

    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }

    /// Test hook: which path was last taken.
    /// Values: "" (uninitialised), "field-union", "delegated".
    const QString &lastOverlay() const { return m_lastOverlay; }

private:
    std::unique_ptr<WildPalms::PalmConflict::PalmConflictHandler> m_palm;
    QString m_lastOverlay;
};

} // namespace WildPalms::ContactsPlugin

#endif // WILDPALMS_CONTACTS_CONTACTSCONFLICTHANDLER_H
```

- [ ] **Step 3: Write the failing test**

Create `tests/plugins/contacts/tst_contactsconflicthandler.cpp`. Use `tests/plugins/todos/tst_todoconflicthandler.cpp` as the structural template. Required cases:

1. `mergesPhoneSlotUnion_whenNoSingleFieldsDiffer` — baseline-equivalent contacts, source adds phone[2]="555-2222"/Mobile, target adds phone[3]="555-3333"/Pager. Expect `ConflictDecision::Merge` with merged vCard containing both numbers; `lastOverlay() == "field-union"`.
2. `mergesCustomSlotUnion` — same as above but for `custom[1]` and `custom[2]`.
3. `mergesPhoneAndCustom_inOneCall` — sourceadds phone, target adds custom; both merge.
4. `delegatesWhenLastNameDiffers` — same baseline, source last-name "Doe", target "Smith". Returns whatever `PalmConflictHandler` returns (don't assert the inner decision; just that `lastOverlay() == "delegated"` and the decision is not `Merge` with our merged content — easiest is to construct a `PalmConflictHandler` configured with `ConflictPolicy::ResolveSourceWins` and verify `decision == ConflictDecision::AcceptSource`).
5. `delegatesWhenSamePhoneSlotHasDifferentValues` — both sides set `phone[2]` to different values. Delegate.
6. `delegatesWhenSecretBitDiffers` — source has secret bit, target doesn't. Delegate.
7. `delegatesWhenSourceVcardDoesNotDecode` — invalid vCard; delegate.

Construct `ConflictRecord` directly with `source.content` and `target.content` set to vCard bytes built from `Contact` PODs via the transcoder. Construct the handler with `nullptr` device + an empty `PalmBackendConfig` — `PalmConflictHandler` doesn't dereference the device for the `ResolveSourceWins`/`ResolveTargetWins` paths. (If it does, see `tst_todoconflicthandler.cpp` for the mock pattern.)

- [ ] **Step 4: Run the test (expect link failure)**

```bash
cmake --build build-dev --target tst_contactsconflicthandler
```

- [ ] **Step 5: Implement the handler**

Create `src/plugins/contacts/contactsconflicthandler.cpp`:

```cpp
#include "contactsconflicthandler.h"

#include "contactsvcardtranscoder.h"

#include "palm/codecs/contactcodec.h"
#include "palm/conflict/palmconflicthandler.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/sync/palmrecord.h"

#include "conflictrecord.h"

namespace WildPalms::ContactsPlugin {

namespace {

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::decodeContact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmSync::PalmRecord;

struct DecodedSide {
    PalmRecord record;
    Contact    contact;
    bool       valid = false;
};

DecodedSide decodeSide(const QByteArray &vcardBytes)
{
    DecodedSide out;
    auto pr = WildPalms::ContactsPlugin::decodeVcardToPalm(vcardBytes, /*slotHint*/ 0);
    if (!pr.has_value()) return out;
    auto c = decodeContact(QByteArrayView(pr->data));
    if (!c.has_value()) return out;
    out.record  = *pr;
    out.contact = *c;
    out.valid   = true;
    return out;
}

bool singleValuedFieldsAgree(const Contact &a, const Contact &b,
                             bool aSecret, bool bSecret)
{
    return a.lastName  == b.lastName
        && a.firstName == b.firstName
        && a.company   == b.company
        && a.title     == b.title
        && a.address   == b.address
        && a.city      == b.city
        && a.state     == b.state
        && a.zip       == b.zip
        && a.country   == b.country
        && a.note      == b.note
        && a.showPhone == b.showPhone
        && aSecret     == bSecret;
}

// Returns false if any slot has both sides non-empty AND differing.
bool slotsAreNonConflicting(const Contact &a, const Contact &b)
{
    for (int i = 0; i < 5; ++i) {
        if (!a.phone[i].isEmpty() && !b.phone[i].isEmpty() && a.phone[i] != b.phone[i]) {
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!a.custom[i].isEmpty() && !b.custom[i].isEmpty() && a.custom[i] != b.custom[i]) {
            return false;
        }
    }
    return true;
}

// Per-slot union. Side A wins ties (which only happen when both equal,
// so it doesn't matter). Rebuilds phoneLabels to match the new
// non-empty phone slots, in slot order, taking A's label first when
// both sides label the same slot identically.
Contact unionMerge(const Contact &a, const Contact &b)
{
    Contact merged = a;   // single-valued fields already agree

    // Build a merged phone-slot view + reconstruct phoneLabels.
    QStringList mergedLabels;
    for (int i = 0; i < 5; ++i) {
        QString chosen;
        QString chosenLabel;
        if (!a.phone[i].isEmpty()) {
            chosen = a.phone[i];
            // Find A's label for slot i (labels list is dense over
            // non-empty slots in slot order).
            int aIndex = 0;
            for (int j = 0; j < i; ++j) if (!a.phone[j].isEmpty()) ++aIndex;
            if (aIndex < a.phoneLabels.size()) chosenLabel = a.phoneLabels[aIndex];
        } else if (!b.phone[i].isEmpty()) {
            chosen = b.phone[i];
            int bIndex = 0;
            for (int j = 0; j < i; ++j) if (!b.phone[j].isEmpty()) ++bIndex;
            if (bIndex < b.phoneLabels.size()) chosenLabel = b.phoneLabels[bIndex];
        }
        merged.phone[i] = chosen;
        if (!chosen.isEmpty()) {
            mergedLabels.append(chosenLabel.isEmpty()
                                ? QStringLiteral("Other")
                                : chosenLabel);
        }
    }
    merged.phoneLabels = mergedLabels;

    for (int i = 0; i < 4; ++i) {
        if (!a.custom[i].isEmpty()) {
            merged.custom[i] = a.custom[i];
        } else {
            merged.custom[i] = b.custom[i];   // empty if both empty
        }
    }
    return merged;
}

QByteArray buildMergedVcard(const PalmRecord &peer, const Contact &merged)
{
    PalmRecord pr = peer;        // inherits recordId, category, attributes
    pr.data = encodeContact(merged);
    return WildPalms::ContactsPlugin::encodePalmToVcard(pr);
}

} // namespace

ContactsConflictHandler::ContactsConflictHandler(
    WildPalms::PalmSync::IPalmDatabaseAccess *device,
    const WildPalms::PalmConflict::PalmBackendConfig *config)
    : m_palm(std::make_unique<WildPalms::PalmConflict::PalmConflictHandler>(device, config))
{
}

ContactsConflictHandler::~ContactsConflictHandler() = default;

Kalburator::Sync::QSyncCore::ConflictDecision ContactsConflictHandler::handleConflict(
    Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
    const Kalburator::Sync::QSyncCore::ConflictPolicy &policy)
{
    DecodedSide source = decodeSide(conflict.source.content);
    DecodedSide target = decodeSide(conflict.target.content);

    if (!source.valid || !target.valid) {
        m_lastOverlay = QStringLiteral("delegated");
        return m_palm->handleConflict(conflict, policy);
    }

    const bool sourceSecret = source.record.isSecret();
    const bool targetSecret = target.record.isSecret();

    if (!singleValuedFieldsAgree(source.contact, target.contact,
                                 sourceSecret, targetSecret)
     || !slotsAreNonConflicting(source.contact, target.contact)) {
        m_lastOverlay = QStringLiteral("delegated");
        return m_palm->handleConflict(conflict, policy);
    }

    // Safe union merge. Use source's PalmRecord as the carrier (its
    // recordId/category/attributes survive). Single-valued fields agree
    // so source-wins on the carrier is harmless.
    Contact merged = unionMerge(source.contact, target.contact);
    conflict.mergedContent = buildMergedVcard(source.record, merged);
    m_lastOverlay = QStringLiteral("field-union");
    return Kalburator::Sync::QSyncCore::ConflictDecision::Merge;
}

} // namespace WildPalms::ContactsPlugin
```

- [ ] **Step 6: Build + run, expect PASS**

```bash
cmake --build build-dev --target tst_contactsconflicthandler
ctest --test-dir build-dev -R '^tst_contactsconflicthandler$' --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
cd src/plugins/contacts
git add contactsconflicthandler.h contactsconflicthandler.cpp
git commit -m "feat(contacts): ContactsConflictHandler with field-union overlay (Phase E.12 Task 3)

When both sides modified the same contact but their single-valued
fields agree and no multi-valued slot has competing non-empty
values, perform a per-slot union for phone[]/custom[] so adding
a phone on one side and an email on the other doesn't drop either.
Otherwise delegate to PalmConflictHandler.
"
cd ../../..
git add tests/plugins/contacts/CMakeLists.txt \
        tests/plugins/contacts/tst_contactsconflicthandler.cpp
git commit -m "test(contacts): tst_contactsconflicthandler (Phase E.12 Task 3)"
```

---

## Task 4: `ContactsBackendPlugin`

**Why:** The `IBackendPlugin` shell that the runtime loads. Owns the per-session `CategoryMappingStore` (populated from AddressDB AppInfo via `parseCategoryAppInfo`), exposes `ContactsBlobBackend` via `createBackends`, supplies `ContactsConflictHandler` via `createConflictHandler`, and provides vCard-aware conflict-snapshot enrichment. Returns `hasMainView() = false` per the architectural decision above.

**Files:**
- Create (submodule): `src/plugins/contacts/contactsbackendplugin.h`
- Create (submodule): `src/plugins/contacts/contactsbackendplugin.cpp`
- Create (submodule): `src/plugins/contacts/contacts-backend-plugin.json`
- Modify (submodule): `src/plugins/contacts/CMakeLists.txt`
- Create (parent): `tests/plugins/contacts/tst_contactsbackendplugin.cpp`
- Modify (parent): `tests/plugins/contacts/CMakeLists.txt`

- [ ] **Step 1: Add the test target**

Append to `tests/plugins/contacts/CMakeLists.txt`:

```cmake
# --- Task 4: ContactsBackendPlugin (in-process, no .so loading) ---
add_executable(tst_contactsbackendplugin
    tst_contactsbackendplugin.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsbackendplugin.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsconflicthandler.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contactsbackendplugin
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_contactsbackendplugin
    PRIVATE
        WildPalmsPalmCalendar    # CategoryMappingStore + parseCategoryAppInfo
        WildPalmsPalmConflict
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::Contacts
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
        Qt::Test
        ${PILOT_LINK_LIBRARIES}
)
add_test(NAME tst_contactsbackendplugin COMMAND tst_contactsbackendplugin)
```

- [ ] **Step 2: Write the plugin manifest**

Create `src/plugins/contacts/contacts-backend-plugin.json`:

```json
{
    "KPlugin": {
        "Id": "contacts",
        "Name": "Contacts Sync",
        "Description": "Syncs Palm AddressDB to vCard 4.0 records via virtual category sub-collections.",
        "Icon": "view-pim-contacts",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "2.0"
    },
    "X-WildPalms-PluginType": "backend",
    "X-WildPalms-PalmDatabases": ["AddressDB"],
    "X-WildPalms-ClaimDescriptions": {
        "AddressDB": "Syncs AddressDB to vCard 4.0 records; one virtual sub-collection per Palm category."
    },
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 40
}
```

- [ ] **Step 3: Write the failing plugin header**

Create `src/plugins/contacts/contactsbackendplugin.h`:

```cpp
#ifndef WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H
#define WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H

#include <memory>

#include <QObject>

#include "core/ibackendplugin.h"

namespace WildPalms::PalmCalendar { class CategoryMappingStore; }
namespace WildPalms::PalmConflict { struct PalmBackendConfig; }
class PalmDeviceConnection;

namespace WildPalms::ContactsPlugin {

/**
 * @brief Fourth new-ABI WildPalms plugin (Memo E.9, Calendar E.10, ToDo E.11).
 *
 * Provides:
 *   - ContactsBlobBackend wrapping the shared PalmBackend (one
 *     collection per populated category slot under "AddressDB").
 *   - No typed SyncBackend; libkalburator has no typed-contacts
 *     upper layer (extract-on-second-consumer per parent spec).
 *   - ContactsConflictHandler (multi-valued field-union overlay +
 *     Palm delegation).
 *
 * Owns the per-session CategoryMappingStore, populated from the
 * AddressDB AppInfo block at createBackends() time.
 *
 * Does NOT register a main-window view — legacy ContactView stays
 * attached to legacy ContactConduit until E.16's unified-runtime
 * cleanup.
 */
class ContactsBackendPlugin : public QObject, public WildPalms::IBackendPlugin
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IBackendPlugin)
public:
    explicit ContactsBackendPlugin(QObject *parent = nullptr);
    ~ContactsBackendPlugin() override;

    // IPlugin
    QString pluginId()    const override;
    QString displayName() const override;
    QIcon   icon()        const override;
    QString description() const override;
    QString version()     const override;

    // IBackendPlugin
    QStringList      claimedDatabases() const override;
    ProvidedBackends createBackends(Kalburator::Sync::ISyncHost *host,
                                    PalmDeviceConnection         *device) override;

    // IBackendPlugin — conflict handler
    Kalburator::Sync::QSyncCore::ConflictHandler *createConflictHandler() override;

    // IBackendPlugin — main view (none for E.12)
    bool hasMainView() const override { return false; }

    // IBackendPlugin — conflict presentation
    void    enrichConflictSnapshot(
        Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
        bool isSourceSide) const override;
    QString formatConflictRecordHtml(
        const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const override;

private:
    std::unique_ptr<WildPalms::PalmCalendar::CategoryMappingStore> m_categoryStore;
    std::unique_ptr<WildPalms::PalmConflict::PalmBackendConfig>    m_palmConfig;
    PalmDeviceConnection *m_device = nullptr;   // borrowed; cached for createConflictHandler
};

} // namespace WildPalms::ContactsPlugin

#endif // WILDPALMS_CONTACTS_CONTACTSBACKENDPLUGIN_H
```

- [ ] **Step 4: Write the failing plugin test**

Create `tests/plugins/contacts/tst_contactsbackendplugin.cpp` mirroring `tests/plugins/todos/tst_todobackendplugin.cpp`. Required cases:

1. `pluginIdentity` — `pluginId() == "contacts"`, `version() == "2.0"`, `claimedDatabases() == {"AddressDB"}`, `hasMainView() == false`.
2. `createBackends_returnsBlobOnly` — passing a non-null `PalmDeviceConnection`, expect `out.blob != nullptr` and `out.calendar == nullptr`.
3. `createBackends_populatesCategoryStoreFromAppInfo` — seed the device's mock with an AddressDB AppInfo block carrying labels for slots 1, 5; verify `availableCollections()` on the returned blob backend returns 3 collections (slot 0 + slots 1, 5).
4. `createConflictHandler_requiresPriorCreateBackends` — calling first returns `nullptr` and logs a warning.
5. `createConflictHandler_returnsContactsConflictHandler` — after `createBackends`, returns a non-null handler.
6. `enrichConflictSnapshot_extractsFnFromVcard` — pass a vCard with `FN:Jane Doe`, verify `snapshot.metadata["title"] == "Jane Doe"` and `snapshot.contentType == "text/vcard"`.
7. `formatConflictRecordHtml_includesTitleAndPre` — minimal smoke test on the HTML output.

For the AppInfo helper (test 3), construct minimal valid AddressDB AppInfo bytes via pisock's `pack_CategoryAppInfo`. See `tests/plugins/calendar/tst_categoryappinforeader.cpp` for the byte-construction pattern; copy the helper and substitute the dbName.

- [ ] **Step 5: Implement the plugin**

Create `src/plugins/contacts/contactsbackendplugin.cpp` modelled on `src/plugins/todos/todobackendplugin.cpp` with these substitutions:

```cpp
#include "contactsbackendplugin.h"

#include "contactsblobbackend.h"
#include "contactsconflicthandler.h"
#include "contactsvcardtranscoder.h"

#include "palm/calendar/categoryappinforeader.h"
#include "palm/calendar/categorymappingstore.h"
#include "palm/conflict/palmbackendconfig.h"
#include "palm/palmdeviceconnection.h"
#include "palm/sync/palmbackend.h"

#include "conflictrecord.h"

#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

#include <QIcon>
#include <QLoggingCategory>
#include <QString>

namespace {
Q_LOGGING_CATEGORY(WP_CONTACTS_PLUGIN, "wildpalms.contacts.plugin")
}

namespace WildPalms::ContactsPlugin {

ContactsBackendPlugin::ContactsBackendPlugin(QObject *parent)
    : QObject(parent)
    , m_categoryStore(std::make_unique<WildPalms::PalmCalendar::CategoryMappingStore>())
    , m_palmConfig(std::make_unique<WildPalms::PalmConflict::PalmBackendConfig>())
{
}

ContactsBackendPlugin::~ContactsBackendPlugin() = default;

QString ContactsBackendPlugin::pluginId()    const { return QStringLiteral("contacts"); }
QString ContactsBackendPlugin::displayName() const { return QStringLiteral("Contacts"); }
QIcon   ContactsBackendPlugin::icon()        const
{
    return QIcon::fromTheme(QStringLiteral("view-pim-contacts"));
}
QString ContactsBackendPlugin::description() const
{
    return QStringLiteral(
        "Synchronizes Palm AddressDB with vCard 4.0 files via virtual category sub-collections");
}
QString ContactsBackendPlugin::version()     const { return QStringLiteral("2.0"); }

QStringList ContactsBackendPlugin::claimedDatabases() const
{
    return { QStringLiteral("AddressDB") };
}

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
            *m_categoryStore,
            QStringLiteral("AddressDB"),
            palmBackend->readAppBlock(QStringLiteral("AddressDB")));
        out.blob = new ContactsBlobBackend(palmBackend, m_categoryStore.get());
    }
    return out;
}

Kalburator::Sync::QSyncCore::ConflictHandler *
ContactsBackendPlugin::createConflictHandler()
{
    if (!m_device || !m_device->device()) {
        qCWarning(WP_CONTACTS_PLUGIN)
            << "createConflictHandler called before createBackends — "
               "manager must invoke createBackends first to wire the device.";
        return nullptr;
    }
    return new ContactsConflictHandler(m_device->device(), m_palmConfig.get());
}

void ContactsBackendPlugin::enrichConflictSnapshot(
    Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot,
    bool /*isSourceSide*/) const
{
    if (snapshot.content.isEmpty()) return;

    KContacts::VCardConverter conv;
    auto list = conv.parseVCards(snapshot.content);
    if (list.isEmpty()) return;

    const KContacts::Addressee &a = list.first();
    if (a.isEmpty()) return;

    const QString fn = a.formattedName();
    const QString name = !fn.isEmpty() ? fn : a.realName();
    snapshot.metadata[QStringLiteral("title")]   = name;
    snapshot.metadata[QStringLiteral("company")] = a.organization();
    snapshot.contentType = QStringLiteral("text/vcard");
}

QString ContactsBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Sync::QSyncCore::RecordSnapshot &snapshot) const
{
    QString html;
    const QString title   = snapshot.metadata.value(QStringLiteral("title")).toString();
    const QString company = snapshot.metadata.value(QStringLiteral("company")).toString();
    if (!title.isEmpty()) {
        html += QStringLiteral("<h3>%1</h3>").arg(title.toHtmlEscaped());
    }
    if (!company.isEmpty()) {
        html += QStringLiteral("<p><b>Company:</b> %1</p>").arg(company.toHtmlEscaped());
    }
    html += QStringLiteral("<pre>%1</pre>")
        .arg(QString::fromUtf8(snapshot.content).toHtmlEscaped());
    return html;
}

} // namespace WildPalms::ContactsPlugin

#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(ContactsBackendPluginFactory,
                           "contacts-backend-plugin.json",
                           registerPlugin<WildPalms::ContactsPlugin::ContactsBackendPlugin>();)

#include "contactsbackendplugin.moc"
```

- [ ] **Step 6: Update the submodule's CMakeLists**

Edit `src/plugins/contacts/CMakeLists.txt` (currently builds only the legacy conduit). Wrap in the V2 toggle, mirroring `src/plugins/todos/CMakeLists.txt`:

```cmake
option(WILDPALMS_CONTACTS_PLUGIN_V2 "Build the new IBackendPlugin-based Contacts plugin" ON)

if (WILDPALMS_CONTACTS_PLUGIN_V2)
    kcoreaddons_add_plugin(wildpalms_contacts_v2
        SOURCES
            contactsbackendplugin.cpp     contactsbackendplugin.h
            contactsblobbackend.cpp       contactsblobbackend.h
            contactsconflicthandler.cpp   contactsconflicthandler.h
            contactsvcardtranscoder.cpp   contactsvcardtranscoder.h
        INSTALL_NAMESPACE "wildpalms/plugins"
    )
    target_include_directories(wildpalms_contacts_v2
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
    )
    # See calendar plugin's CMakeLists for why this BEFORE include is
    # required (Kalburator::Sync ordering vs WildPalmsCore's legacy
    # ::Sync include).
    target_include_directories(wildpalms_contacts_v2 BEFORE
        PRIVATE
            $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(wildpalms_contacts_v2
        PRIVATE
            WildPalmsCore
            WildPalmsPalmSync
            WildPalmsPalmCalendar           # CategoryMappingStore + parseCategoryAppInfo
            WildPalmsPalmCodecs             # decodeContact / encodeContact / kde_pim_convert
            WildPalmsPalmConflict           # PalmConflictHandler
            KF6::CoreAddons
            KF6::Contacts
            KF6::I18n
            KF6::WidgetsAddons
            Qt::Widgets
            Kalburator::Sync
    )
else ()
    kcoreaddons_add_plugin(wildpalms_contacts
        SOURCES
            contactconduit.cpp
            contactconduit.h
            contactmapper.cpp
            contactmapper.h
            contactview.cpp
            contactview.h
        INSTALL_NAMESPACE "wildpalms/conduits"
    )
    target_link_libraries(wildpalms_contacts
        WildPalmsCore
        KF6::CoreAddons
        KF6::I18n
        KF6::WidgetsAddons
        Qt::Widgets
    )
endif ()
```

- [ ] **Step 7: Reconfigure (option needs to register), build + run**

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target wildpalms_contacts_v2 tst_contactsbackendplugin
ctest --test-dir build-dev -R '^tst_contactsbackendplugin$' --output-on-failure
```

Expected: plugin builds; tests pass.

- [ ] **Step 8: Commit**

```bash
cd src/plugins/contacts
git add contactsbackendplugin.h contactsbackendplugin.cpp contacts-backend-plugin.json CMakeLists.txt
git commit -m "feat(contacts): ContactsBackendPlugin (Phase E.12 Task 4)

The IBackendPlugin shell. Owns per-session CategoryMappingStore,
populates from AddressDB AppInfo block via parseCategoryAppInfo.
Surfaces ContactsBlobBackend + ContactsConflictHandler. No main
view (legacy ContactView stays with legacy ContactConduit until
E.16). CMake toggle WILDPALMS_CONTACTS_PLUGIN_V2 defaults ON.
"
cd ../../..
git add tests/plugins/contacts/CMakeLists.txt \
        tests/plugins/contacts/tst_contactsbackendplugin.cpp
git commit -m "test(contacts): tst_contactsbackendplugin (Phase E.12 Task 4)"
```

---

## Task 5: `tst_contacts_v2` end-to-end via `BackendPluginManager`

**Why:** The unit tests cover each component in isolation. `tst_contacts_v2` exercises the full path: load the real `wildpalms_contacts_v2.so` off the build tree, ask the manager for a Contacts backend pair, drive `BlobSyncEngine::twoWayWithBaseline` from a `MockPalmDatabaseAccess` source through the plugin to a `MockBlobBackend` target, exercise multi-slot routing and conflict resolution end-to-end. Mirrors `tests/plugins/todos/tst_todo_v2.cpp`.

**Files:**
- Create (parent): `tests/plugins/contacts/tst_contacts_v2.cpp`
- Modify (parent): `tests/plugins/contacts/CMakeLists.txt`

- [ ] **Step 1: Add the test target**

Append to `tests/plugins/contacts/CMakeLists.txt`:

```cmake
# --- Task 5: tst_contacts_v2 — end-to-end via BackendPluginManager ---
# Loads the real wildpalms_contacts_v2.so off the build tree, drives
# BlobSyncEngine::twoWayWithBaseline against a MockBlobBackend target
# across multiple AddressDB category slots. Mirrors tst_todo_v2.
add_executable(tst_contacts_v2
    tst_contacts_v2.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsblobbackend.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_compile_definitions(tst_contacts_v2
    PRIVATE
        CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(tst_contacts_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PILOT_LINK_INCLUDE_DIR}
)
target_link_libraries(tst_contacts_v2
    PRIVATE
        WildPalmsRuntime          # BackendPluginManager
        WildPalmsPalmCalendar     # CategoryMappingStore + AppInfo helper
        WildPalmsPalmCodecs       # Contact POD + encodeContact/decodeContact
        WildPalmsPalmSync         # PalmRecord, MockPalmDatabaseAccess, PalmBackend
        WildPalmsCore
        Kalburator::Sync
        KF6::CoreAddons
        KF6::Contacts
        Qt::Test
        Qt::Core
        ${PILOT_LINK_LIBRARIES}
)
add_dependencies(tst_contacts_v2 wildpalms_contacts_v2)
add_test(NAME tst_contacts_v2 COMMAND tst_contacts_v2)
```

- [ ] **Step 2: Write the test**

Read `tests/plugins/todos/tst_todo_v2.cpp` first as the structural template. Create `tests/plugins/contacts/tst_contacts_v2.cpp` mirroring it with these substitutions:

| ToDo template | Contacts replacement |
|---|---|
| `wildpalms_todos_v2` | `wildpalms_contacts_v2` |
| `"todo"` (plugin id) | `"contacts"` |
| `"ToDoDB"` | `"AddressDB"` |
| `"palm:todo/"` | `"palm:contact/"` |
| `text/calendar` | `text/vcard` |
| `Todo` POD + helpers | `Contact` POD + helpers |
| VTODO assertions | vCard assertions (e.g. `contains("FN:")`) |

Required end-to-end test cases (one `private slot:` each):

1. `loadsPlugin_andClaimsAddressDb` — manager reports a plugin claiming "AddressDB"; `createBackends` returns a non-null blob.
2. `pushSourceToTarget_singleSlot` — seed device with one Address record in slot 0; `twoWayWithBaseline` runs; target `MockBlobBackend` ends up with one record under collection `palm:contact/0` carrying matching vCard bytes.
3. `pushSourceToTarget_multiSlotRouting` — seed device with records in slots 0 and 3 (after seeding the AppInfo block to populate slot 3); after sync, target has the records under `palm:contact/0` and `palm:contact/3` separately.
4. `pullTargetToSource_assignsCategoryFromCollectionId` — pre-populate target with one vCard under `palm:contact/2`; after sync, device has a new record with `category == 2`.
5. `conflictMerge_phoneSlotUnion` — seed both sides with the same baseline; mutate device-side phone[2], target-side phone[3]; after sync, both should converge with both phones present (this validates the conflict-handler integration end-to-end).
6. `deletionPropagatesSourceToTarget` — delete a record on the source after baseline; target ends up with the record removed.

The two-way-with-baseline scaffolding (constructing the engine, the baseline store, the conflict store, the registry, and wiring the plugin's conflict handler in) is identical to `tst_todo_v2` — copy it.

- [ ] **Step 3: Build + run, expect PASS**

```bash
cmake --build build-dev --target tst_contacts_v2
ctest --test-dir build-dev -R '^tst_contacts_v2$' --output-on-failure
```

- [ ] **Step 4: Run the full plugin-test sweep to confirm no regressions**

```bash
ctest --test-dir build-dev -R '^tst_contacts' --output-on-failure
ctest --test-dir build-dev -R '^tst_todo'    --output-on-failure
ctest --test-dir build-dev -R '^tst_calendar' --output-on-failure
ctest --test-dir build-dev -R '^tst_memo'    --output-on-failure
```

All targets pass.

- [ ] **Step 5: Commit**

```bash
git add tests/plugins/contacts/CMakeLists.txt \
        tests/plugins/contacts/tst_contacts_v2.cpp
git commit -m "test(contacts): tst_contacts_v2 end-to-end (Phase E.12 Task 5)"
```

---

## Task 6: Parent integration, docs, submodule pointer bump

**Why:** Flip the spec/integration plan rows to ✅, write the project-memory entry, bump the contacts submodule pointer to its new HEAD, and run the full WP ctest sweep one more time.

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md`
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`
- Create:  `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e12_contacts.md`
- Submodule pointer bump for `src/plugins/contacts/`.

- [ ] **Step 1: Run the full ctest baseline**

```bash
ctest --test-dir build-dev --output-on-failure
```

Verify all tests pass, no regressions vs E.11's baseline.

- [ ] **Step 2: Flip the spec row to ✅**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` line 590. Replace:

```markdown
| **E.12** | Rewrite **Contacts** as `IBackendPlugin`. Uses `PalmContactsAdapter` for typed UI consumers. | WP | E.11 | Smoke passes. |
```

with:

```markdown
| ✅ **E.12** | Rewritten **Contacts** as `IBackendPlugin`. `ContactsBackendPlugin` + `ContactsBlobBackend` + `ContactsConflictHandler` + `ContactsVcardTranscoder` in `src/plugins/contacts/` (submodule). vCard 4.0 on the wire, virtual sub-collections `palm:contact/<slot>`. One conflict overlay: per-slot field-union for phone[]/custom[] when single-valued fields agree. AddressDB AppInfo parsed via the shared `parseCategoryAppInfo` (third consumer after Calendar + ToDo). No main view in this phase (legacy ContactView stays with legacy ContactConduit until E.16). CMake toggle `WILDPALMS_CONTACTS_PLUGIN_V2=ON`. Landed 2026-04-25. Plan: `docs/superpowers/plans/2026-04-25-phase-e12-contacts-plugin.md`. | WP | E.11 | WP ctest passes; `tst_contacts_v2` covers full round-trip via `BlobSyncEngine::twoWayWithBaseline` with a `MockBlobBackend` target. |
```

- [ ] **Step 3: Mark E.12 in the integration plan**

Edit `docs/plans/2026-04-20-libkalburator-integration.md` to mark E.12 landed alongside E.11. (Read the file; the E.11 entry is the template for the line to add.)

- [ ] **Step 4: Bump the contacts submodule pointer**

```bash
cd src/plugins/contacts
git log --oneline -5    # confirm Tasks 1-4 commits at HEAD
cd ../../..
git add src/plugins/contacts
git status              # should show "modified content" for the submodule
```

- [ ] **Step 5: Commit the parent docs + submodule bump**

```bash
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md \
        docs/plans/2026-04-20-libkalburator-integration.md \
        src/plugins/contacts
git commit -m "docs(phase-e12): land Contacts plugin

Landed Phase E.12: ContactsBackendPlugin + ContactsBlobBackend +
ContactsConflictHandler + ContactsVcardTranscoder. Fourth new-ABI
plugin after Memo (E.9), Calendar (E.10), ToDo (E.11). Validates
the E.11 promotion of CategoryAppInfoReader as the third consumer.
"
```

- [ ] **Step 6: Write the project-memory entry**

Create `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_phase_e12_contacts.md`:

```markdown
---
name: Phase E.12 (Contacts plugin) status
description: E.12 landed 2026-04-25 — Contacts plugin fourth on new ABI, vCard 4.0 on the wire, multi-valued field-union conflict overlay, validates AppInfo reader as shared static lib
type: project
---
E.12 landed 2026-04-25 (Phase E.12 of the libkalburator-adoption rewrite).

**What landed:**
- `ContactsBackendPlugin` + `ContactsBlobBackend` + `ContactsConflictHandler` +
  `ContactsVcardTranscoder` in `src/plugins/contacts/` (submodule).
- vCard 4.0 on the blob face, virtual sub-collections `palm:contact/<slot>`.
- One conflict overlay: per-slot field-union for `phone[0..4]`/`phoneLabels`/
  `custom[0..3]` when single-valued fields agree on both sides; otherwise
  delegates to `PalmConflictHandler`.
- No main view registered (legacy `ContactView` stays attached to legacy
  `ContactConduit` until E.16).
- CMake toggle `WILDPALMS_CONTACTS_PLUGIN_V2=ON`; legacy `ContactConduit`
  buildable when off.

**Architectural validation:** Contacts is the third consumer of
`parseCategoryAppInfo`/`WildPalmsPalmCalendar` (after Calendar in E.10
and ToDo in E.11). The promotion of the AppInfo reader from a
plugin-private detail to the shared static lib (Phase E.11 Task 1) is
now confirmed as the right shape — Contacts links cleanly without any
further plumbing.

**Pre-existing assets reused:** `Contact` POD + `encodeContact`/
`decodeContact` (E.7), `toAddressee`/`fromAddressee` from
`kde_pim_convert.{h,cpp}` (E.7), `PalmContactsAdapter` (already
shipped). E.12 is mostly composition.

**How to apply:** Future plugins follow this pattern. Contacts shows
how to bridge to a non-iCal upstream format (`text/vcard`) without
adding new dependencies.

**Deferrals (still open after E.12):**
- LocalBlobBackend e2e for Contacts → E.15+ (id-space cutover).
- AddressDB phone-label-table reader (`unpack_AddressAppInfo`) → E.18 if
  syncs surface user-renamed-label round-trip bugs.
- Live-device test in POSE64 → E.18.
- Speculative conflict overlays (custom-field overwrite, category-vs-field,
  address-block atomicity) → only if real syncs surface failure modes.
- ContactView ↔ PalmContactsAdapter rewiring → post-E.16 UI follow-up.
- Legacy ContactConduit removal → E.16.
```

- [ ] **Step 7: Add the memory pointer**

Edit `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/MEMORY.md`. Append after the E.11 line:

```markdown
- [project_phase_e12_contacts.md](project_phase_e12_contacts.md) — E.12 landed 2026-04-25; Contacts is fourth new-ABI plugin; vCard 4.0 on the wire; field-union conflict overlay
```

(Do not commit — `~/.claude/...` is outside the repo.)

---

## Self-Review Checklist (writer-only; run before handoff)

- [ ] Every `Step` shows actual code/commands (no placeholders).
- [ ] Tasks 1–4 commit twice each: once in the submodule (plugin files), once in the parent (test files). Task 4 also commits the submodule's CMakeLists.
- [ ] Task 6 is the only place the contacts submodule pointer gets bumped.
- [ ] Function/class/file names are consistent across tasks (`ContactsBlobBackend`, `palm:contact/`, `WILDPALMS_CONTACTS_PLUGIN_V2`, `contacts-backend-plugin.json`, `wildpalms_contacts_v2`).
- [ ] No new external dependencies added beyond what's already in `WildPalmsPalmCodecs`.
- [ ] Excluded scope explicit: legacy deletion (E.16), POSE64 integration (E.18), LocalBlobBackend e2e (E.15+), phone-label-table reader (E.18), main-window view rewiring (post-E.16).
