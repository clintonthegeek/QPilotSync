# Phase 5: Memo → `note` Canon Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put WildPalms Memo on the same first-class shape-graph footing as calendar (Phase 3) and todo (Phase 4), while **keeping the human-readable Markdown-on-disk feature**. Today `MemoBlobBackend` declares `(blob, raw)` and pre-transcodes Palm→Markdown internally (via `memomarkdown`), so the palm→markdown loss is invisible to the engine and the sync never touches a canon. libkalburator has delivered a **`note`** domain (`(note, canon)` canonical, `(note, markdown)` peer, `markdown↔canon` edges, and a `MarkdownFilesBackend` sink — branch `feature/canon-upgrade-convergence`, suite 115/115). This plan flips the backend to declare `(note, palm)` and present raw `PalmRecord` wire bytes, registers a `NoteDomainExtension` providing `(note, palm) ↔ (note, markdown)` edges (stage bodies reuse the existing `memomarkdown` encode/decode), and — the Phase-5-specific extra — wires the memo on-disk peer to libkalburator's `MarkdownFilesBackend` declaring `(note, markdown)` so the disk keeps human-readable `.md` files instead of Palm wire bytes.

**Architecture:** This is the calendar/todo pattern (`palm ↔ <peer>`), with three substitutions and one addition:
- domain is `note`; the peer encoding is **`markdown`** (not `ical`/`ical-vtodo`);
- the WP edge bodies wrap `memomarkdown::encode`/`decode` + `PalmRecord` wire (de)serialization (no new conversion logic);
- **divergence (loss model):** libkalburator's `markdown↔canon` edge declares `frontmatter=Reversible` (identity rides in `providerExtras["frontmatter"]`). `LossProfile::isLossless()` returns `affected.isEmpty()`, so **Reversible counts as non-lossless**. Therefore `palm→canon` is NOT lossless here (unlike Phase 3/4) — but it **drops nothing**. The verification test asserts `droppedProperties().isEmpty()` + `frontmatter=Reversible` going up, and `body=Simplified` coming down. (This is the one place the Phase 4 template's assertion does not carry over.)
- **addition (the Markdown sink):** in `palmruntime.cpp::finishConnect`, the auto-created per-slot peer for the `note` domain must be a `Kalburator::Sinks::MarkdownFilesBackend` declaring `(note, markdown)` instead of the generic `RawFilesBackend` mirroring the source shape. Without this, the disk would hold Palm wire bytes and `memoview.cpp` (reads `*.md`) would break.

**Tech Stack:** C++/Qt6, CMake (dev dir `build-dev`, local libkalburator override at `../libkalburator` on `feature/canon-upgrade-convergence`), ctest. The memo plugin is the git submodule `src/plugins/memo` (`wildpalms-conduit-memo`), currently on branch **`main`** — Phase 5 work goes on a new `feature/canon-adoption-phase1` branch in the submodule (the other three plugins are already on that branch). Tests live in the superproject under `tests/plugins/memo/`.

---

## Context the engineer needs

- **This is a behavior-changing migration**, like Phase 3/4 (not a verify-lock). The backend stops emitting Markdown bytes and starts emitting Palm wire bytes; the Markdown re-appears on the *disk peer* because that peer becomes a `MarkdownFilesBackend`. The end-to-end sync result (what `.md` files land on disk) must be unchanged, which the verification test (Task 7) + the unchanged `memoview` format prove.

- **The proven siblings are calendar (Phase 3) and todo (Phase 4).** Every change here mirrors a calendar/todo file that already shipped:
  - `src/plugins/calendar/palmtoicstransformation.{h,cpp}` → mirror as `src/plugins/memo/palmtomarkdowntransformation.{h,cpp}`
  - `src/plugins/calendar/calendardomainextension.{h,cpp}` → mirror as `src/plugins/memo/notedomainextension.{h,cpp}`
  - `PalmCalendarBackend`/`TodoBlobBackend` (present `pr.toWireBytes()`, consume `PalmRecord::fromWireBytes`) → mirror in `MemoBlobBackend`
  - `CalendarBackendPlugin` ctor calls `CalendarDomainExtension::registerWith(...)` → mirror in `MemoPlugin` ctor
  - `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp` → mirror as `tst_memo_note_roundtrip.cpp`

- **Existing conversion code is reused, not rewritten.** `src/plugins/memo/memomarkdown.{h,cpp}` already has `encode(MarkdownMemo)` and `decode(QString)` (namespace `WildPalms::Memo`), and `palm/codecs/memocodec.h` has `encodeMemo`/`decodeMemo`. The new stages wrap these plus `PalmRecord` wire (de)serialization — no new conversion logic. **`memomarkdown` is NOT redundant** (the handoff's "delete redundant code" step): it becomes the body of the `palm↔markdown` edge. What *is* removed is the in-backend `palmToMarkdownRecord`/`markdownRecordToPalm` helpers (moved into the stages).

- **Memo differs from calendar/todo in two structural ways:**
  1. **No per-category routing.** `MemoBlobBackend` exposes ONE collection (`palm:memo`) covering all records; category is a per-record field carried in the markdown frontmatter (`category:`), not a collection slot. So `createRecord`/`updateRecord` keep the category that arrives in the wire bytes (decoded from frontmatter by the stage) — they do NOT override it from collection context (the opposite of Phase 4 todo). No `slotHint` clamping is needed.
  2. **Category name is decorative + store-dependent.** The sync path already constructs `MemoBlobBackend` with a **null** `CategoryMappingStore` (`memobackendplugin.cpp:55`), so `categoryName` is already omitted in practice. A `TransformationStage` is stateless and has no store, so the `palm→markdown` stage omits `categoryName` — **no regression** (it was already null in the sync path).

- **Loss-profile vocabulary:** use the **canon note** property names. From `libkalburator/src/note/noteproperties.cpp`: `uid`, `body`, `categories`, `lastmodified`. The frontmatter side-channel key is `frontmatter` (libkalburator's `markdown↔canon` edge declares `affected[PropertyId{"frontmatter"}]=Reversible`).

- **Identity round-trip:** `memomarkdown::encode` always writes `id: <recordId>` and (when slot≠0) `category: <slot>` and (when private) `private: true` into the YAML frontmatter. libkalburator's `MarkdownToCanonStage` carries the **entire frontmatter block verbatim** into `providerExtras["frontmatter"]` and parses `id:` into `uid`; `CanonToMarkdownStage` re-emits it verbatim. So recordId + category slot + private survive `palm→markdown→canon→markdown→palm`. Verified byte-stable: WP's `encode()` output `---\nid: N\ncategory: M\n---\n\n<body>\n` is exactly what libkalburator's split/rejoin reproduces. The verification test (Task 7) proves recordId + category survive.

- **Conflict handler is NOT changed.** `MemoPlugin::enrichConflictSnapshot` decodes `snapshot.content` as a Palm memo on the source side (best-effort, mirrors calendar/todo). After this migration the snapshot `content` may carry Palm wire bytes; `decodeMemo` returns early if it isn't a memo POD — exactly the documented best-effort behavior. Do NOT touch it.

- **`memoview.cpp` compatibility:** it reads `*.md` files with `id:`/`category:` frontmatter. The new disk peer (`MarkdownFilesBackend`) writes the `(note, markdown)` peer encoding verbatim — which is `memomarkdown::encode`'s output (because WP's `palm→markdown` stage produces it). Same filename derivation (first body line) and same frontmatter keys. So `memoview` stays compatible; no view change in Phase 5.

- **Build/test commands** (`build-dev` configured against the canon branch):
  - `cmake --build build-dev -j"$(nproc)" [--target <t>]`
  - `ctest --test-dir build-dev -R memo --output-on-failure`
  - Reconfigure if needed: `cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator)`
  - Submodule edits (`src/plugins/memo/*`) are committed **inside the submodule** on `feature/canon-adoption-phase1`; superproject test edits, the `palmruntime.cpp` change, and the pointer bump are superproject commits. Do NOT push (the controller gates pushes).

---

## File inventory

| File | Change |
|------|--------|
| `src/plugins/memo/palmtomarkdowntransformation.h` (submodule) | **Create** — `PalmToMarkdownStage`, `MarkdownToPalmStage`, `palmToMarkdownLoss()`, `markdownToPalmLoss()` |
| `src/plugins/memo/palmtomarkdowntransformation.cpp` (submodule) | **Create** |
| `src/plugins/memo/notedomainextension.h` (submodule) | **Create** — `NoteDomainExtension::registerWith` |
| `src/plugins/memo/notedomainextension.cpp` (submodule) | **Create** |
| `src/plugins/memo/memoblobbackend.h` (submodule) | **Modify** — `nativeShapes()` → `(note, palm)` |
| `src/plugins/memo/memoblobbackend.cpp` (submodule) | **Modify** — present/consume Palm wire; drop internal transcoding |
| `src/plugins/memo/memobackendplugin.cpp` (submodule) | **Modify** — register the extension in the ctor |
| `src/plugins/memo/CMakeLists.txt` (submodule) | **Modify** — add the two new sources |
| `src/runtime/palmruntime.cpp` | **Modify** — note-domain peer uses `MarkdownFilesBackend` declaring `(note, markdown)` |
| `tests/plugins/memo/CMakeLists.txt` | **Modify** — new test target + new sources on plugin-compiling targets |
| `tests/plugins/memo/tst_memoblobbackend.cpp` | **Modify** — expect Palm wire, not Markdown |
| `tests/plugins/memo/tst_memo_note_roundtrip.cpp` | **Create** — the verification test |

---

### Task 0: Branch the memo submodule

The memo submodule is on `main`; the other three plugins are on `feature/canon-adoption-phase1`. Create the matching branch off the current HEAD so Phase 5 commits land there.

- [ ] **Step 1:** `git -C src/plugins/memo checkout -b feature/canon-adoption-phase1`
- [ ] **Step 2:** Confirm `git -C src/plugins/memo status` is clean and on the new branch.

---

### Task 1: Create the palm↔markdown transformation stages

**Files:**
- Create: `src/plugins/memo/palmtomarkdowntransformation.h`
- Create: `src/plugins/memo/palmtomarkdowntransformation.cpp`

Mirror `src/plugins/calendar/palmtoicstransformation.{h,cpp}`.

- [ ] **Step 1: header**

```cpp
#ifndef WILDPALMS_MEMO_PALMTOMARKDOWNTRANSFORMATION_H
#define WILDPALMS_MEMO_PALMTOMARKDOWNTRANSFORMATION_H

#include "transformationedge.h"

namespace WildPalms::Memo {

// (note, palm) -> (note, markdown): wraps the Palm MemoDB codec via memomarkdown.
// Lossless: a Palm memo (plain text + private flag + category slot) maps onto the
// markdown frontmatter+body. Identity (recordId, category slot, private) rides in
// the YAML frontmatter, which libkalburator's markdown<->canon stage carries
// verbatim in providerExtras["frontmatter"].
class PalmToMarkdownStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

// (note, markdown) -> (note, palm): Simplified; Palm MemoDB holds plain text only,
// so any Markdown structure in the body is flattened.
class MarkdownToPalmStage : public Kalburator::Shape::TransformationStage {
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

Kalburator::Shape::LossProfile palmToMarkdownLoss();
Kalburator::Shape::LossProfile markdownToPalmLoss();

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_PALMTOMARKDOWNTRANSFORMATION_H
```

- [ ] **Step 2: implementation**

```cpp
#include "palmtomarkdowntransformation.h"

#include "memomarkdown.h"
#include "palm/codecs/memocodec.h"
#include "palm/sync/palmrecord.h"

using namespace Kalburator::Shape;

namespace WildPalms::Memo {

QByteArray PalmToMarkdownStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    const auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(sourceBytes);

    MarkdownMemo m;
    m.recordId     = pr.recordId;
    m.categorySlot = pr.category;
    m.content      = WildPalms::PalmCodecs::decodeMemo(pr.data)
                         .value_or(WildPalms::PalmCodecs::Memo{});
    m.content.isPrivate =
        (pr.attributes & WildPalms::PalmSync::PalmRecord::AttrSecret) != 0;
    // categoryName is decorative + store-dependent; the sync path runs with a
    // null CategoryMappingStore, so omitting it here is not a regression.
    return encode(m).toUtf8();
}

QByteArray MarkdownToPalmStage::transform(const QByteArray &sourceBytes) const
{
    if (sourceBytes.isEmpty()) return {};
    const MarkdownMemo m = decode(QString::fromUtf8(sourceBytes));

    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = m.recordId;   // from frontmatter id:; 0 if absent
    pr.category = static_cast<std::uint8_t>(m.categorySlot);
    pr.data     = WildPalms::PalmCodecs::encodeMemo(m.content);
    if (m.content.isPrivate)
        pr.attributes |= WildPalms::PalmSync::PalmRecord::AttrSecret;
    return pr.toWireBytes();
}

LossProfile palmToMarkdownLoss()
{
    // lossless: text + private flag + category slot all survive into the markdown
    // frontmatter/body (carried through canon verbatim by libkalburator's
    // markdown<->canon Reversible edge).
    return {};
}

LossProfile markdownToPalmLoss()
{
    LossProfile p;
    // Palm MemoDB holds plain text only: Markdown structure flattens to text.
    p.affected.insert(PropertyId{QStringLiteral("body")}, LossKind::Simplified);
    // The canon multi-value `categories` list has no Palm equivalent (Palm holds
    // a single category slot, which rides in providerExtras frontmatter — not the
    // canon `categories` property).
    p.affected.insert(PropertyId{QStringLiteral("categories")}, LossKind::Dropped);
    return p;
}

} // namespace WildPalms::Memo
```

(Confirm `MarkdownMemo`/`encode`/`decode` field + function names against `memomarkdown.h`; `Memo`/`encodeMemo`/`decodeMemo`/`isPrivate`/`text` against `palm/codecs/memocodec.h`; `LossProfile`/`PropertyId`/`LossKind` against calendar's `palmtoicstransformation.cpp`.)

---

### Task 2: Create the NoteDomainExtension

**Files:**
- Create: `src/plugins/memo/notedomainextension.h`
- Create: `src/plugins/memo/notedomainextension.cpp`

Mirror `src/plugins/calendar/calendardomainextension.{h,cpp}`.

- [ ] **Step 1: header**

```cpp
#ifndef WILDPALMS_MEMO_NOTEDOMAINEXTENSION_H
#define WILDPALMS_MEMO_NOTEDOMAINEXTENSION_H

namespace Kalburator::Shape { class TransformationRegistry; }

namespace WildPalms::Memo {

// Registers the (note, palm) peer shape plus palm<->markdown edges with a
// TransformationRegistry. Idempotent (registerShape/registerEdge tolerate
// re-registration). libkalburator owns (note,markdown)<->(note,canon); we own
// palm<->markdown.
class NoteDomainExtension {
public:
    static void registerWith(Kalburator::Shape::TransformationRegistry &registry);
};

} // namespace WildPalms::Memo

#endif // WILDPALMS_MEMO_NOTEDOMAINEXTENSION_H
```

- [ ] **Step 2: implementation**

```cpp
#include "notedomainextension.h"

#include "palmtomarkdowntransformation.h"
#include "propertycatalogue.h"
#include "transformationregistry.h"

using namespace Kalburator::Shape;

namespace WildPalms::Memo {

namespace {

PropertyCatalogue makePalmCatalogue()
{
    PropertyCatalogue cat;
    // Palm MemoDB native fields; used by loss-profile UI to describe a palm-shape
    // record.
    cat.addProperty({ PropertyId{"body"},     PropertyKind::String,  QStringLiteral("Body") });
    cat.addProperty({ PropertyId{"private"},  PropertyKind::Boolean, QStringLiteral("Private") });
    cat.addProperty({ PropertyId{"category"}, PropertyKind::Integer, QStringLiteral("Category Slot") });
    return cat;
}

} // namespace

void NoteDomainExtension::registerWith(TransformationRegistry &registry)
{
    const Shape palm    { DomainId{"note"}, EncodingId{"palm"} };
    const Shape markdown{ DomainId{"note"}, EncodingId{"markdown"} };

    // Defensive: libkalburator's note domain plugin registers the markdown peer
    // shape at PluginManager load time. If that static-init registrar didn't run
    // in this address space (e.g. a unit test), the markdown shape is absent and
    // registerEdge would assert "to-shape not registered". Register a placeholder;
    // registerShape is idempotent, so libkalburator's real catalogue replaces it.
    if (registry.catalogueFor(markdown) == nullptr) {
        registry.registerShape(markdown, {});
    }
    registry.registerShape(palm, makePalmCatalogue());

    // palm -> markdown (lossless; identity rides in frontmatter -> providerExtras)
    registry.registerEdge(TransformationEdge{
        palm, markdown, palmToMarkdownLoss(), std::make_shared<PalmToMarkdownStage>() });

    // markdown -> palm (Simplified; Palm MemoDB holds plain text)
    registry.registerEdge(TransformationEdge{
        markdown, palm, markdownToPalmLoss(), std::make_shared<MarkdownToPalmStage>() });
}

} // namespace WildPalms::Memo
```

(Match calendar's exact `registerShape`/`registerEdge`/`catalogueFor`/`TransformationEdge`/`PropertyKind` spelling. `EncodingId` must be exactly `"markdown"` — confirmed from `libkalburator/src/note/notestockshapes.cpp`.)

---

### Task 3: Flip the backend to present/consume Palm wire

**Files:**
- Modify: `src/plugins/memo/memoblobbackend.h`
- Modify: `src/plugins/memo/memoblobbackend.cpp`

- [ ] **Step 1: `nativeShapes()` in the header** — replace the `(blob, raw)` body and its stale K.8b comment:

```cpp
    QList<Kalburator::Shape::Shape> nativeShapes() const override {
        return { { Kalburator::Shape::DomainId{QStringLiteral("note")},
                   Kalburator::Shape::EncodingId{QStringLiteral("palm")} } };
    }
```

- [ ] **Step 2: drop the internal transcoding in the .cpp.** Remove the anonymous-namespace helpers `palmToMarkdownRecord` and `markdownRecordToPalm` and the now-unused includes `#include "memomarkdown.h"`, `#include "palm/codecs/memocodec.h"`, `#include "palm/calendar/categorymappingstore.h"`. Add a single wire-bytes helper:

```cpp
Kalburator::Sync::BackendRecord palmToWireRecord(
    const WildPalms::PalmSync::PalmRecord &pr)
{
    Kalburator::Sync::BackendRecord br;
    br.id           = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), pr.recordId);
    br.type         = QStringLiteral("note");
    br.data         = pr.toWireBytes();
    br.contentHash  = pr.contentHash();
    br.lastModified = pr.lastModified;
    br.isDeleted    = pr.isDeleted();
    return br;
}
```

(The `m_categoryStore` member is now unused by the .cpp but kept in the header so the ctor signature is stable for callers/tests. GCC does not warn on an unused pointer member.)

- [ ] **Step 3: `loadRecords`** — replace `palmToMarkdownRecord(pr, m_categoryStore)` with `palmToWireRecord(pr)`.

- [ ] **Step 4: `loadRecord`** — replace `palmToMarkdownRecord(*pr, m_categoryStore)` with `palmToWireRecord(*pr)`.

- [ ] **Step 5: `modifiedSince`** — replace `palmToMarkdownRecord(*pr, m_categoryStore)` with `palmToWireRecord(*pr)`.

- [ ] **Step 6: `createRecord`** — consume wire bytes; keep the record's own category (memo is not slot-routed):

```cpp
QString MemoBlobBackend::createRecord(const QString &collectionId,
                                      const Kalburator::Sync::BackendRecord &record)
{
    if (!m_palmBackend || !isMemoCollection(collectionId)) return {};
    if (record.data.isEmpty()) return {};

    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.recordId     = 0;   // device assigns
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();
    // pr.category is kept as decoded from the markdown frontmatter — memo
    // carries category per-record, not per-collection.

    const auto newId = m_palmBackend->createPalmRecord(QLatin1String(PalmDbName), pr);
    if (newId == 0) return {};
    return WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QLatin1String(PalmDbName), newId);
}
```

- [ ] **Step 7: `updateRecord`** — consume wire bytes:

```cpp
bool MemoBlobBackend::updateRecord(const Kalburator::Sync::BackendRecord &record)
{
    if (!m_palmBackend) return false;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!WildPalms::PalmSync::PalmBackend::decodeRecordId(
            record.id, &dbName, &numericId)) return false;
    if (dbName != QLatin1String(PalmDbName)) return false;
    if (record.data.isEmpty()) return false;

    auto pr = WildPalms::PalmSync::PalmRecord::fromWireBytes(record.data);
    pr.recordId     = numericId;
    pr.lastModified = record.lastModified.isValid()
        ? record.lastModified
        : QDateTime::currentDateTimeUtc();

    return m_palmBackend->updatePalmRecord(dbName, pr);
}
```

(`deleteRecord`, `deletedSince`, `supportsDeleteTracking`, collections, and id helpers are unchanged. Add `#include "palm/sync/palmrecord.h"` if not already present — it is needed for `fromWireBytes`.)

---

### Task 4: Register the extension in the plugin ctor

**Files:**
- Modify: `src/plugins/memo/memobackendplugin.cpp`

- [ ] **Step 1: includes** — add `#include "notedomainextension.h"` and `#include "transformationregistry.h"` next to the existing includes.

- [ ] **Step 2: register in the ctor.** `MemoPlugin::MemoPlugin()` already has a body (member-init of `m_categoryStore`). Add inside the braces:

```cpp
{
    // Phase 5: register the (note, palm) peer shape and palm<->markdown edges
    // with the process-wide TransformationRegistry at plugin construction
    // (mirrors CalendarBackendPlugin). Idempotent across instances.
    NoteDomainExtension::registerWith(
        Kalburator::Shape::TransformationRegistry::instance());
}
```

---

### Task 5: Add the new sources to the plugin CMake target

**Files:**
- Modify: `src/plugins/memo/CMakeLists.txt`

- [ ] **Step 1: list the two new sources** in `add_library(wildpalms_memo_static STATIC ...)`:

```cmake
add_library(wildpalms_memo_static STATIC
    memobackendplugin.cpp           memobackendplugin.h
    memoblobbackend.cpp             memoblobbackend.h
    memomarkdown.cpp                memomarkdown.h
    palmtomarkdowntransformation.cpp palmtomarkdowntransformation.h
    notedomainextension.cpp         notedomainextension.h
    memoview.cpp                    memoview.h
)
```

(No link-library change: the static already links `Kalburator::Sync` PUBLIC, which provides the shape-graph headers/symbols.)

- [ ] **Step 2: configure + build the plugin static**

`cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=$(realpath ../libkalburator) 2>&1 | tail -5`
then `cmake --build build-dev -j"$(nproc)" --target wildpalms_memo_static 2>&1 | tail -15`. Expected: clean.

- [ ] **Step 3: commit the submodule feature work** (on `feature/canon-adoption-phase1`)

```bash
cd src/plugins/memo
git add palmtomarkdowntransformation.h palmtomarkdowntransformation.cpp \
        notedomainextension.h notedomainextension.cpp \
        memoblobbackend.h memoblobbackend.cpp \
        memobackendplugin.cpp CMakeLists.txt
git commit -m "feat: memo as (note,palm) shape peer with palm<->markdown edges (Phase 5)

Move Palm<->Markdown conversion from the backend into PalmToMarkdownStage/
MarkdownToPalmStage; declare (note,palm) native + register edges via
NoteDomainExtension; backend now presents/consumes Palm wire bytes. The engine
sees the full palm->markdown->canon chain; identity (recordId/category/private)
rides in the YAML frontmatter carried verbatim through libkalburator's
markdown<->canon Reversible edge.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
cd ../../..
```

---

### Task 6: Wire the Markdown sink in the runtime

**Files:**
- Modify: `src/runtime/palmruntime.cpp`

This is the Phase-5-specific change with no Phase-3/4 analogue. The auto-created per-slot peer (`finishConnect`, ~lines 355-366) currently always makes a `RawFilesBackend` declaring the source shape. For the `note` domain, make it a `MarkdownFilesBackend` declaring `(note, markdown)` so the engine routes `palm→markdown→canon` into a human-readable `.md` sink.

- [ ] **Step 1: include** — add near the `rawfilesbackend.h` include: `#include "markdownfilesbackend.h"`. (Confirm the existing include spelling for `RawFilesBackend`; match it.)

- [ ] **Step 2: replace the peer-construction block** (the `palmShape` + `pcBackend` + `createCollection` lines). Use a `RawFilesBackend` base pointer so the 2-arg `createCollection(info, shape)` stays reachable (`MarkdownFilesBackend` IS-A `RawFilesBackend`):

```cpp
            const auto palmShape = registeredSrc
                ? registeredSrc->shapeFor(palmCol.id)
                : Kalburator::Shape::Shape::Any();

            // Phase 5: the note domain keeps human-readable Markdown on disk, so
            // its peer is a MarkdownFilesBackend declaring (note, markdown) — the
            // engine then routes palm->markdown->canon into it. Every other domain
            // keeps the generic RawFilesBackend mirroring the source shape.
            const bool isNote = palmShape.domain
                == Kalburator::Shape::DomainId{QStringLiteral("note")};

            std::unique_ptr<Kalburator::Sinks::RawFilesBackend> pcBackend;
            Kalburator::Shape::Shape peerShape;
            if (isNote) {
                pcBackend = std::make_unique<Kalburator::Sinks::MarkdownFilesBackend>(rootPath);
                peerShape = Kalburator::Shape::Shape{
                    Kalburator::Shape::DomainId{QStringLiteral("note")},
                    Kalburator::Shape::EncodingId{QStringLiteral("markdown")} };
            } else {
                pcBackend = std::make_unique<Kalburator::Sinks::RawFilesBackend>(rootPath);
                peerShape = palmShape;
            }

            Kalburator::Sync::CollectionInfo pcCol;
            pcCol.id   = safeColId;
            pcCol.name = palmCol.name;
            pcBackend->createCollection(pcCol, peerShape);

            m_registry->registerBackendInstance(pcId, pcBackend.get());
            m_ownedBackends.push_back(std::move(pcBackend));
```

(Confirm `m_ownedBackends` is a `std::vector<std::unique_ptr<...SyncBackend>>` so the upcast move compiles; confirm `Shape::domain` is the member name — calendar's domain extension uses `s.encoding`, the sibling field. If the field is named differently, match it.)

- [ ] **Step 3: build the app/runtime target**

`cmake --build build-dev -j"$(nproc)" 2>&1 | tail -15`. Expected: clean.

- [ ] **Step 4: commit (superproject)**

```bash
git add src/runtime/palmruntime.cpp
git commit -m "runtime: note-domain peer uses MarkdownFilesBackend (note,markdown) sink (Phase 5)

Per-slot auto-mapping now selects a Kalburator::Sinks::MarkdownFilesBackend
declaring (note, markdown) for the note domain, so memo syncs keep writing
human-readable .md files; all other domains keep the generic RawFilesBackend
mirroring the source shape.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 7: Add the canon round-trip verification test

**Files:**
- Create: `tests/plugins/memo/tst_memo_note_roundtrip.cpp`

Mirror `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp`, with the loss assertions adjusted for the Reversible frontmatter (see Architecture divergence).

- [ ] **Step 1: write the test**

```cpp
#include <QTest>

// WildPalms memo plugin
#include "notedomainextension.h"
#include "memomarkdown.h"
#include "palm/sync/palmrecord.h"
#include "palm/codecs/memocodec.h"

// libkalburator shape graph + note canon (via Kalburator::Sync)
#include "shaperegistries.h"
#include "transformationregistry.h"
#include "shape.h"
#include "pipeline.h"
#include "lossprofile.h"
#include "notedomaindefinition.h"
#include "notestockshapes.h"

using namespace Kalburator::Shape;
using WildPalms::Memo::NoteDomainExtension;
using WildPalms::PalmSync::PalmRecord;

namespace {

ShapeRegistries makeNoteRegistries()
{
    ShapeRegistries regs;
    auto &reg = regs.transformation;

    Kalburator::Note::NoteDomainDefinition def;
    const auto spine = def.canonicalSpine();   // [ (note, canon) ]
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

    Kalburator::Note::NoteStockShapes stock;
    for (const auto &[shape, cat] : stock.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : stock.edges())
        reg.registerEdge(edge);

    // WildPalms: (note, palm) + palm<->markdown edges. Not part of stock; the
    // palm->markdown->canon path only compiles once this runs.
    NoteDomainExtension::registerWith(reg);

    return regs;
}

// Build a (note, palm) record's wire bytes carrying a known recordId + slot.
QByteArray makePalmMemoBytes(quint8 slot, quint32 recordId, const QString &text)
{
    WildPalms::PalmCodecs::Memo memo;
    memo.text = text;

    PalmRecord pr;
    pr.recordId = recordId;
    pr.category = slot;
    pr.data     = WildPalms::PalmCodecs::encodeMemo(memo);
    return pr.toWireBytes();
}

} // namespace

class TestMemoNoteRoundtrip : public QObject {
    Q_OBJECT
private slots:

    void palmCanonRoundTripPreservesIdentity()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        const QByteArray palmBytes =
            makePalmMemoBytes(/*slot*/ 3, /*recordId*/ 88,
                              QStringLiteral("Buy milk\nand eggs"));

        const auto fwd = regs.transformation.compile(palm, canon);
        QVERIFY2(fwd.has_value(),
                 "no palm->canon pipeline (palm->markdown->canon should compile)");
        const QByteArray canonBytes = fwd->apply(palmBytes);
        QVERIFY2(!canonBytes.isEmpty(), "palm->canon produced empty bytes");

        const auto rev = regs.transformation.compile(canon, palm);
        QVERIFY2(rev.has_value(), "no canon->palm pipeline");
        const QByteArray palmBytes2 = rev->apply(canonBytes);
        QVERIFY2(!palmBytes2.isEmpty(), "canon->palm produced empty bytes");

        const PalmRecord pr2 = PalmRecord::fromWireBytes(palmBytes2);
        QCOMPARE(pr2.recordId, 88u);
        QCOMPARE(static_cast<int>(pr2.category), 3);
        const auto decoded = WildPalms::PalmCodecs::decodeMemo(pr2.data);
        QVERIFY2(decoded.has_value(), "round-tripped palm memo did not decode");
        QCOMPARE(decoded->text, QStringLiteral("Buy milk\nand eggs"));
    }

    void lossProfileIsHonest()
    {
        const auto regs = makeNoteRegistries();
        const Shape palm { DomainId{"note"}, EncodingId{"palm"}  };
        const Shape canon{ DomainId{"note"}, EncodingId{"canon"} };

        // palm->canon is NOT isLossless here: libkalburator's markdown<->canon
        // edge declares frontmatter=Reversible (identity rides in providerExtras),
        // and Reversible is a non-empty affected entry. But nothing is DROPPED.
        const LossProfile up = regs.transformation.inspect(palm, canon);
        QVERIFY2(up.droppedProperties().isEmpty(),
                 "palm->canon must drop nothing");
        QCOMPARE(up.affected.value(PropertyId{QStringLiteral("frontmatter")}),
                 LossKind::Reversible);

        const LossProfile down = regs.transformation.inspect(canon, palm);
        QVERIFY2(!down.isLossless(),
                 "canon->palm must report loss (Palm holds plain text only)");
        QCOMPARE(down.affected.value(PropertyId{QStringLiteral("body")}),
                 LossKind::Simplified);
    }
};

QTEST_GUILESS_MAIN(TestMemoNoteRoundtrip)
#include "tst_memo_note_roundtrip.moc"
```

**Verify before building:** `Kalburator::Note` is the namespace of `NoteDomainDefinition`/`NoteStockShapes` (check `libkalburator/src/note/notedomaindefinition.h`). `Memo` POD field `text` + `encodeMemo`/`decodeMemo` against `palm/codecs/memocodec.h`. `TransformationRegistry::inspect` exists (calendar uses it).

- [ ] **Step 2: add the test target to CMake** — append to `tests/plugins/memo/CMakeLists.txt` (reuse `MEMO_PLUGIN_SRC_DIR`):

```cmake
# --- Phase 5: memo (note) canon round-trip ---
add_executable(tst_memo_note_roundtrip
    tst_memo_note_roundtrip.cpp
    ${MEMO_PLUGIN_SRC_DIR}/notedomainextension.cpp
    ${MEMO_PLUGIN_SRC_DIR}/palmtomarkdowntransformation.cpp
    ${MEMO_PLUGIN_SRC_DIR}/memomarkdown.cpp
)
target_include_directories(tst_memo_note_roundtrip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${MEMO_PLUGIN_SRC_DIR}
)
target_include_directories(tst_memo_note_roundtrip BEFORE
    PRIVATE
        $<TARGET_PROPERTY:Kalburator::Sync,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(tst_memo_note_roundtrip
    PRIVATE
        WildPalmsPalmCodecs     # encodeMemo / decodeMemo
        WildPalmsPalmSync       # PalmRecord
        Kalburator::Sync
        Qt::Test
        Qt::Core
)
add_test(NAME tst_memo_note_roundtrip COMMAND tst_memo_note_roundtrip)
set_tests_properties(tst_memo_note_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: build + run ONLY this test, then branch on the result**

`cmake --build build-dev -j"$(nproc)" --target tst_memo_note_roundtrip 2>&1 | tail -15`
then `ctest --test-dir build-dev -R memo_note_roundtrip --output-on-failure 2>&1 | tail -30`.

**If it PASSES:** identity + loss honesty hold — proceed.
**If `palmCanonRoundTripPreservesIdentity` FAILS:** capture the canon JSON (`qDebug() << canonBytes`, then revert). Check whether `providerExtras["frontmatter"]` carried the `id:`/`category:` lines. Conclude whether libkalburator's `markdown↔canon` dropped them vs a WP stage bug. STOP and report — do NOT weaken the assertion.
**If `lossProfileIsHonest` FAILS on `frontmatter`/`body`:** the loss key doesn't match (libkalburator uses `frontmatter`; our edge uses `body`/`categories`). Re-confirm the keys in `libkalburator/src/note/notestockshapes.cpp` and `palmtomarkdowntransformation.cpp`.

- [ ] **Step 4: commit (superproject)**

```bash
git add tests/plugins/memo/tst_memo_note_roundtrip.cpp tests/plugins/memo/CMakeLists.txt
git commit -m "test(memo): note canon round-trip + loss-honesty verification (Phase 5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 8: Fix the existing backend test for the palm-wire shape

**Files:**
- Modify: `tests/plugins/memo/tst_memoblobbackend.cpp`

- [ ] **Step 1: read the test and identify the markdown-shaped assertions.** It currently asserts `br.data` contains Markdown text / `br.type == "memos"`, and builds create/update payloads via `WildPalms::Memo::encode(MarkdownMemo)`. After the flip the backend presents/consumes Palm wire bytes.

- [ ] **Step 2: translate to the wire-bytes contract** (mirror `tst_calendarblobbackend.cpp` / `tst_todoblobbackend.cpp`):
  - type assertion → `QCOMPARE(rec.type, QStringLiteral("note"));`
  - create/update payloads → build `br.data = pr.toWireBytes()` from a seed `PalmRecord` (or `encodeMemo` into `pr.data` then `pr.toWireBytes()`), not `encode(MarkdownMemo)`.
  - load-content checks → decode and assert on the field:
    ```cpp
    const PalmRecord pr = PalmRecord::fromWireBytes(rec.data);
    const auto pod = WildPalms::PalmCodecs::decodeMemo(pr.data);
    QVERIFY(pod.has_value());
    QCOMPARE(pod->text, QStringLiteral("..."));
    ```
  - Adjust includes: drop `memomarkdown.h` if no longer referenced; ensure `palm/sync/palmrecord.h` + `palm/codecs/memocodec.h` are included.

  Do not delete coverage — translate it. Collection/delete/change-detection tests that never inspected encoded bytes are unaffected.

- [ ] **Step 3: the `tst_memoblobbackend` target** compiles `memoblobbackend.cpp` + `memomarkdown.cpp` directly. The backend no longer references `memomarkdown` or the stages, so **no new sources are needed** on this target. (If a link error names a stage/extension symbol, that means the backend still references it — fix the backend, don't add sources.) Build + run:

`cmake --build build-dev -j"$(nproc)" --target tst_memoblobbackend 2>&1 | tail -10`
then `ctest --test-dir build-dev -R "memoblobbackend" --output-on-failure 2>&1 | tail -20`. Expected: PASS.

- [ ] **Step 4: commit (superproject)**

```bash
git add tests/plugins/memo/tst_memoblobbackend.cpp
git commit -m "test(memo): tst_memoblobbackend expects Palm wire bytes (Phase 5)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

---

### Task 9: Full suite green + submodule pointer bump

**Files:** superproject gitlink for `src/plugins/memo`.

- [ ] **Step 1: clean build + full suite**

`cmake --build build-dev -j"$(nproc)" 2>&1 | tail -10` then
`ctest --test-dir build-dev --output-on-failure 2>&1 | tail -25`.
Expected: previous (98) + 1 new test (`tst_memo_note_roundtrip`) = **99**. Watch for:
- `tst_memomarkdown` — unchanged (memomarkdown untouched); should pass.
- `tst_memobackendplugin` — compiles `memobackendplugin.cpp`, which now `#include`s `notedomainextension.h` and calls `registerWith`. It will need the two new sources on its target OR fail to link. Add them (Step 1a).
- `tst_palm_runtime_modes` (or whichever exercises `palmruntime.cpp`) — links the runtime change; confirm it still passes.

- [ ] **Step 1a: add new sources to `tst_memobackendplugin` if it fails to link** — append to that `add_executable` in `tests/plugins/memo/CMakeLists.txt`:

```cmake
    ${MEMO_PLUGIN_SRC_DIR}/palmtomarkdowntransformation.cpp
    ${MEMO_PLUGIN_SRC_DIR}/notedomainextension.cpp
```

Rebuild + rerun. Commit this CMake change with the Task 8 edits if it was needed (`git add tests/plugins/memo/CMakeLists.txt`).

- [ ] **Step 2: bump the memo submodule pointer**

```bash
git add src/plugins/memo
git status   # confirm only the memo gitlink moved (plus tests/runtime edits already committed)
git commit -m "build: bump memo submodule (Phase 5 note canon migration)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 3: report the final unpushed state** — `git log --oneline main..HEAD` (superproject) and `git -C src/plugins/memo log --oneline -4`. State what a push would publish. Do NOT push.

---

## Phase 5 done when

- Memo backend declares `(note, palm)` and presents/consumes Palm wire; the palm↔markdown conversion is a registered shape edge with an honest `LossProfile`.
- The memo on-disk peer is a `MarkdownFilesBackend` declaring `(note, markdown)` — disk keeps human-readable `.md` files; `memoview` stays compatible.
- `tst_memo_note_roundtrip` passes (recordId + category survive `palm→canon→palm`; loss honest: `palm→canon` drops nothing + frontmatter Reversible; `canon→palm` reports `body`=Simplified).
- `tst_memoblobbackend` updated and green; full suite **99/99**; no other regressions.
- Memo submodule changes committed on `feature/canon-adoption-phase1` + pointer bumped (unpushed). Do NOT merge to WildPalms `main` / ship until libkalburator's canon convergence merges to *its* `main` and is device-verified.

## Self-review notes

- **Spec coverage:** submodule branch (Task 0), stages (Task 1), domain extension (Task 2), backend flip (Task 3), plugin registration (Task 4), plugin CMake (Task 5), the Phase-5-specific Markdown sink (Task 6), verification (Task 7), forced test churn (Task 8), regression gate + pointer bump (Task 9). Conflict handler explicitly left unchanged (mirrors calendar/todo).
- **Divergences from Phase 4 documented:** (a) loss model — `palm→canon` is not lossless because `markdown↔canon` is Reversible; test asserts "drops nothing" + `frontmatter=Reversible` instead of `isLossless`. (b) the runtime sink selection (Task 6) has no Phase 3/4 analogue. (c) memo is single-collection / per-record category — `createRecord`/`updateRecord` keep the wire-bytes category instead of overriding from a collection slot.
- **No placeholders:** the new files, backend diffs, plugin registration, runtime sink wiring, and verification test are complete code. Spots marked "confirm/match the sibling spelling" name the exact authority file.
- **DRY:** stage bodies reuse `memomarkdown` + `memocodec` — no new conversion logic; the libkalburator `MarkdownFilesBackend` is reused for the sink (we wrote no WP file backend).
</content>
</invoke>
