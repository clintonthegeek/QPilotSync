# Phase 2: Contacts Canon Verify-and-Lock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove (with a test) and document that WildPalms contacts already sync correctly on the shape graph via `palm → vcard4 → canon` peer-bridging — specifically that a full `palm → canon → palm` round-trip preserves Palm record identity, and that the composed loss is honest — then lock that contract.

**Architecture:** No new transformation stages. Contacts already declares `(contacts, palm)` and edges `palm ↔ (contacts, vcard4)`; libkalburator declares `(contacts, vcard4) ↔ (contacts, canon)`, so the engine compiles `palm → vcard4 → canon`. libkalburator's `VCard4ToCanonStage` collects all KContacts customs into `providerExtras["x-vcard"]` and `CanonToVCard4Stage` re-emits them, so WP's `WP-PALM.*` identity stamps are *expected* to survive the canon hop. This plan verifies that expectation rather than changing the mechanism.

**Tech Stack:** C++/Qt6, CMake (dev build dir `build-dev`, local libkalburator override), ctest, KContacts. The contacts plugin is the git submodule `src/plugins/contacts` (`wildpalms-conduit-contacts`), currently on branch `feature/canon-adoption-phase1`. libkalburator is the sibling `../libkalburator` on `feature/canon-upgrade-convergence`.

---

## Context the engineer needs

- **This is verification, not feature work.** The expected outcome is "it already works; lock it." But there is a real chance the round-trip test FAILS, because Palm identity survival depends on libkalburator's custom-key round-trip fidelity (it re-splits a KContacts custom key like `WP-PALM-RECORDID` back into `insertCustom(app, name, value)` — the app/name boundary is ambiguous). The plan explicitly handles both outcomes (Task 2). Do not weaken assertions to force a green; a real loss is a finding to report.

- **What WP's transcoder actually does** (`src/plugins/contacts/contactsvcardtranscoder.cpp`, already in the tree):
  - `encodePalmToVcard(PalmRecord)`: Palm wire → `KContacts::Addressee` → vCard4 bytes, stamping `insertCustom("WP-PALM","CATEGORY-SLOT", slot)`, `insertCustom("WP-PALM","RECORDID", id)`, and (if secret) `setSecrecy(Private)` + `insertCustom("WP-PALM","SECRET","1")`.
  - `decodeVcardToPalm(bytes, slotHint)`: vCard4 → `Addressee` → Palm wire. Restores `recordId` from the `WP-PALM/RECORDID` custom and the secret bit from either `secrecy()` or the `WP-PALM/SECRET` custom. **It sets `category` from `slotHint`, NOT from the custom** — slot is backend-authoritative (the backend remaps it on write). So a pipeline-level round-trip is NOT expected to restore the original category; the test must not assert it. The `CATEGORY-SLOT` custom rides along for the backend's use.

- **The contacts canon has no privacy field** (verified: `contactscanonproperties.cpp` declares no classification/sensitivity property; vCard4 dropped `CLASS`). So the secret bit's ONLY survival path through canon is the `WP-PALM/SECRET` custom → `providerExtras["x-vcard"]`. If customs don't survive, secret is lost.

- **The proven registry-fixture pattern** is libkalburator's own `tests/contacts/tst_contacts_canon_roundtrip.cpp` (`makeContactsRegistries()`): register the contacts spine (`vcard4` root → `canon` head via `declareCanonical` + `appendCanonicalVersion`), then `ContactsStockShapes` peer shapes + edges. This plan extends that with WP's `ContactsDomainExtension::registerWith` to add the `palm` edges.

- **libkalburator API** (all reachable via linking `Kalburator::Sync`, which exports `src/shape` + `src/contacts` headers):
  - `Kalburator::Shape::ShapeRegistries` (`shaperegistries.h`) — members `.transformation`, `.domain`, `.operations`.
  - `Kalburator::Shape::TransformationRegistry` — `registerShape(Shape, PropertyCatalogue)`, `declareCanonical(DomainId, Shape)`, `appendCanonicalVersion(DomainId, Shape)`, `registerEdge(TransformationEdge)`, `std::optional<Pipeline> compile(Shape from, Shape to)`, `LossProfile inspect(Shape from, Shape to)`.
  - `Kalburator::Shape::Pipeline` — `QByteArray apply(const QByteArray&) const`, `LossProfile composedLoss() const`.
  - `Kalburator::Shape::LossProfile` — `QHash<PropertyId, LossKind> affected`, `bool isLossless() const`.
  - `Kalburator::Contacts::ContactsDomainDefinition` (`contactsdomaindefinition.h`) — `domain()`, `canonicalSpine()`.
  - `Kalburator::Contacts::ContactsStockShapes` (`contactsstockshapes.h`) — `peerShapes()`, `edges()`.

- **Build/test commands** (configure already done in Phase 1):
  - Build a target: `cmake --build build-dev -j"$(nproc)" --target tst_contacts_canon_roundtrip`
  - Run it: `ctest --test-dir build-dev -R contacts_canon_roundtrip --output-on-failure`
  - If `build-dev` is missing, reconfigure: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`

---

## File inventory

| File | Change |
|------|--------|
| `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp` | **Create** — the WP-side canon round-trip + loss-honesty test |
| `tests/plugins/contacts/CMakeLists.txt` | **Modify** — add the new test target |
| `src/plugins/contacts/contactsdomainextension.cpp` (submodule) | **Modify** — add a doc comment locking the bridge contract (Task 4) |
| `docs/superpowers/plans/2026-05-24-phase2-contacts-canon-verify-lock.md` | This plan (already created) |

---

### Task 1: Write the canon round-trip + loss-honesty test

**Files:**
- Create: `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp`
- Modify: `tests/plugins/contacts/CMakeLists.txt`

- [ ] **Step 1: Create the test file**

Create `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp` with exactly:

```cpp
#include <QTest>

// WildPalms contacts plugin
#include "contactsdomainextension.h"          // ContactsDomainExtension::registerWith
#include "palm/sync/palmrecord.h"              // WildPalms::PalmSync::PalmRecord
#include "palm/codecs/contactcodec.h"          // WildPalms::PalmCodecs::Contact, encodeContact, decodeContact

// libkalburator shape graph + contacts canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "contactsdomaindefinition.h"
#include "contactsstockshapes.h"

using namespace Kalburator::Shape;
using WildPalms::ContactsPlugin::ContactsDomainExtension;
using WildPalms::PalmSync::PalmRecord;

namespace {

// Mirror libkalburator's tst_contacts_canon_roundtrip fixture, then add WP's
// palm<->vcard4 edges so the registry can compile palm<->canon.
ShapeRegistries makeContactsRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Contacts::ContactsDomainDefinition def;
    const auto spine = def.canonicalSpine();        // [ (vcard4, cat), (canon, cat) ]
    Q_ASSERT(!spine.isEmpty());
    {
        const auto &[rootShape, rootCat] = spine.first();
        reg.registerShape(rootShape, rootCat);
        reg.declareCanonical(def.domain(), rootShape);
    }
    for (int i = 1; i < spine.size(); ++i) {
        const auto &[s, cat] = spine.at(i);
        reg.registerShape(s, cat);
        reg.appendCanonicalVersion(def.domain(), s);
    }

    Kalburator::Contacts::ContactsStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WP: adds (contacts, palm) + palm<->vcard4 edges.
    ContactsDomainExtension::registerWith(reg);

    return regs;
}

// Build the wire bytes of a (contacts, palm) record carrying known identity.
QByteArray makePalmRecordBytes(quint8 slot, quint32 recordId, bool secret,
                               const QString &last, const QString &first)
{
    WildPalms::PalmCodecs::Contact c;
    c.lastName  = last;
    c.firstName = first;

    PalmRecord pr;
    pr.data     = WildPalms::PalmCodecs::encodeContact(c);
    pr.category = slot;
    pr.recordId = recordId;
    if (secret)
        pr.attributes |= PalmRecord::AttrSecret;
    return pr.toWireBytes();
}

} // namespace

class TestContactsCanonRoundtrip : public QObject {
    Q_OBJECT
private slots:

    // The core lock: palm -> canon -> palm must preserve recordId, the secret
    // bit, and core contact fields. (Slot is intentionally NOT asserted: the
    // demote stage uses slotHint=-1 and the backend remaps slots on write.)
    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeContactsRegistries();
        const Shape palm { DomainId{"contacts"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"contacts"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmRecordBytes(/*slot*/ 3, /*recordId*/ 42, /*secret*/ true,
                                QStringLiteral("Lovelace"), QStringLiteral("Ada"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(), "no palm->canon pipeline (palm->vcard4->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);

        QCOMPARE(pr2.recordId, quint32(42));                 // recordId survives via custom
        QVERIFY2(pr2.isSecret(), "secret bit lost across canon round-trip");

        const auto c2 = WildPalms::PalmCodecs::decodeContact(QByteArrayView(pr2.data));
        QVERIFY2(c2.has_value(), "could not decode round-tripped contact body");
        QCOMPARE(c2->lastName,  QStringLiteral("Lovelace"));
        QCOMPARE(c2->firstName, QStringLiteral("Ada"));
    }

    // Loss honesty: palm->canon is lossless (palm is a subset of canon);
    // canon->palm is lossy and must report it.
    void lossProfileIsHonest()
    {
        const auto regs = makeContactsRegistries();
        const Shape palm { DomainId{"contacts"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"contacts"}, EncodingId{"canon"} };

        const LossProfile up = regs.transformation.inspect(palm, canon);
        QVERIFY2(up.isLossless(), "palm->canon should be lossless");

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(), "canon->palm must report loss (Palm cannot hold most canon fields)");
    }
};

QTEST_GUILESS_MAIN(TestContactsCanonRoundtrip)
#include "tst_contacts_canon_roundtrip.moc"
```

- [ ] **Step 2: Register the test target in CMake**

Append to `tests/plugins/contacts/CMakeLists.txt`:

```cmake
# --- Phase 2: contacts canon round-trip (verify-and-lock) ---
add_executable(tst_contacts_canon_roundtrip
    tst_contacts_canon_roundtrip.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsdomainextension.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/palmtovcardtransformation.cpp
    ${CONTACTS_PLUGIN_SRC_DIR}/contactsvcardtranscoder.cpp
)
target_include_directories(tst_contacts_canon_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CONTACTS_PLUGIN_SRC_DIR}
)
# Prepend libkalburator's include dirs so canon headers (shaperegistries.h,
# contactsstockshapes.h, vcardcanonstages.h, ...) and shape headers resolve.
target_include_directories(tst_contacts_canon_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_contacts_canon_roundtrip
    PRIVATE
        WildPalmsPalmCodecs   # Contact, encodeContact / decodeContact
        WildPalmsPalmSync     # PalmRecord
        Kalburator::Sync      # shape graph + contacts canon stages
        KF6::Contacts
        Qt::Test
        Qt::Core
)
add_test(NAME tst_contacts_canon_roundtrip COMMAND tst_contacts_canon_roundtrip)
set_tests_properties(tst_contacts_canon_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Build the test target**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_contacts_canon_roundtrip 2>&1 | tail -20`
Expected: compiles and links. If headers don't resolve, confirm the `BEFORE` include-dir prepend is present (it carries libkalburator's `src/contacts` + `src/shape` dirs). If a libkalburator API name differs (e.g. `appendCanonicalVersion`), check the real signature in `../libkalburator/src/shape/transformationregistry.h` and the fixture in `../libkalburator/tests/contacts/tst_contacts_canon_roundtrip.cpp`, and match it. Do NOT stub or fake around a missing API — report it.

- [ ] **Step 4: Commit the test (do NOT run it yet — that's Task 2)**

This is a submodule-tracked test? No — `tests/` lives in the superproject. Commit in the superproject:
```bash
git add tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp tests/plugins/contacts/CMakeLists.txt
git commit -m "test(contacts): canon round-trip + loss-honesty verification (Phase 2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 2: Run the test and record the verification outcome

**Files:** none edited; this is the verification gate.

- [ ] **Step 1: Run the test**

Run: `ctest --test-dir build-dev -R contacts_canon_roundtrip --output-on-failure 2>&1 | tail -40`

- [ ] **Step 2: Branch on the result**

**If both test functions PASS:** the verify-and-lock hypothesis holds — contacts identity survives the canon round-trip and loss is honest. Record "Phase 2 verified: palm→canon→palm preserves recordId+secret+core fields; loss honest" and proceed to Task 3.

**If `palmCanonRoundTripPreservesIdentity` FAILS** (e.g. `recordId` is 0 or the secret bit is lost): this is a real finding — WP's `WP-PALM.*` customs are NOT surviving libkalburator's `vcard4 → canon → vcard4` custom round-trip (most likely the custom-key app/name re-split in `CanonToVCard4Stage`). STOP and report it with the exact failing assertion and these diagnostics:
  - Add a temporary debug print of `canonBytes` (the canon JSON) and inspect whether `providerExtras["x-vcard"]` contains the `WP-PALM-*` keys. (`QByteArray` → qDebug.)
  - Determine whether the loss is in libkalburator (custom not preserved/re-split correctly) or in WP (custom key naming). Report the conclusion. Do not fix here — this likely needs either a libkalburator fix (custom-preservation fidelity) or a WP decision to move identity into a dedicated `providerExtras["x-palm"]` namespace (which would escalate Phase 2 to the "first-class palm peer" option). Escalate to the human with the finding.

**If `lossProfileIsHonest` FAILS:** the composed `canon→palm` loss reports lossless (wrong) or `palm→canon` reports loss (unexpected). Report which; this points at the loss profiles registered by WP's `ContactsDomainExtension` (`palmToVCardLoss`/`vcardToPalmLoss`) or the composition. Proceed to Task 3 only after deciding the fix (likely a one-line adjustment in `palmtovcardtransformation.cpp`).

(No commit in this task — it is a gate.)

---

### Task 3: Lock the contract in code documentation

**Only after Task 2's identity test passes** (or its finding is resolved).

**Files:**
- Modify: `src/plugins/contacts/contactsdomainextension.cpp` (submodule)

- [ ] **Step 1: Add a contract-locking comment**

In `src/plugins/contacts/contactsdomainextension.cpp`, immediately above the two `registry.registerEdge(...)` calls, add:

```cpp
    // PHASE 2 (verify-and-lock, 2026-05-24): contacts rides the shape graph via
    // palm -> vcard4 -> canon. We register only the palm <-> vcard4 edges; the
    // vcard4 <-> canon hop is libkalburator's (ContactsStockShapes). Palm identity
    // (RECORDID + secret bit) survives the canon round-trip because libkalburator's
    // VCard4ToCanonStage collects all KContacts customs into providerExtras["x-vcard"]
    // and CanonToVCard4Stage re-emits them. The CATEGORY-SLOT custom rides along but
    // is NOT restored by the demote (slotHint=-1); the backend remaps slots on write.
    // Verified by tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp.
```

- [ ] **Step 2: Build to confirm the comment didn't break anything (it shouldn't)**

Run: `cmake --build build-dev -j"$(nproc)" --target tst_contacts_canon_roundtrip 2>&1 | tail -5`
Expected: builds.

- [ ] **Step 3: Commit inside the contacts submodule**

```bash
git -C src/plugins/contacts add contactsdomainextension.cpp
git -C src/plugins/contacts commit -m "docs(contacts): lock the palm->vcard4->canon bridge contract (Phase 2)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 4: Full suite green + superproject pointer bump

**Files:** superproject gitlink for `src/plugins/contacts`.

- [ ] **Step 1: Run the full suite (no regressions)**

Run: `ctest --test-dir build-dev --output-on-failure 2>&1 | tail -15`
Expected: all pass, count = previous baseline (95) + 1 new test = 96.

- [ ] **Step 2: Bump the contacts submodule pointer and commit**

```bash
git add src/plugins/contacts
git status   # confirm only the contacts gitlink moved
git commit -m "build: bump contacts submodule (Phase 2 bridge-contract doc)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 3: Report the final unpushed state**

Run `git log --oneline main..HEAD` (superproject) and `git -C src/plugins/contacts log --oneline -3`. Report what a push would publish. Do NOT push (the controller gates pushes).

---

## Phase 2 done when

- `tst_contacts_canon_roundtrip` exists, builds, and its identity + loss-honesty assertions pass (or, if they failed, the finding is documented and escalated).
- The bridge contract is documented in `contactsdomainextension.cpp`.
- Full suite green (96/96 expected), contacts submodule pointer bumped, all committed (unpushed).

## Self-review notes

- **Spec coverage:** the chosen scope (verify-and-lock) maps to Task 1 (write test) + Task 2 (run/verify) + Task 3 (document/lock) + Task 4 (regression + commit). The "refine loss profiles if exposed" sub-goal is handled by Task 2's `lossProfileIsHonest` branch.
- **Honest about uncertainty:** Task 2 does not assume green; it branches, because identity survival through libkalburator's custom round-trip is unverified until run. This is intended for a verification plan.
- **No placeholders:** the test body, the CMake block, and the doc comment are complete. The one genuine unknown (exact `appendCanonicalVersion`/spine API spelling) is pinned to the libkalburator fixture `tests/contacts/tst_contacts_canon_roundtrip.cpp` as the authority to match, with an explicit "do not stub around it" instruction.
- **Type consistency:** registry/pipeline/lossprofile API names match libkalburator `src/shape/*.h`; `ContactsDomainExtension::registerWith(TransformationRegistry&)` matches the existing `tst_contactsdomainextension.cpp` usage; `PalmRecord` fields (`data`, `category`, `recordId`, `attributes`, `AttrSecret`, `isSecret()`, `toWireBytes`/`fromWireBytes`) and `PalmCodecs::Contact`/`encodeContact`/`decodeContact` match `contactsvcardtranscoder.cpp`.
- **Commit placement:** test lives in the superproject (`tests/`); the doc comment is a contacts-submodule commit + a superproject pointer bump (Task 4).
