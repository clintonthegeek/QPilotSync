# Sub-project C — Canonical Hub + Domain-Level Star + Category-as-Field — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up a per-profile `GenericSqliteBackend` canonical hub, route the Palm into it with one domain-level `Star` mapping per domain, and carry the Palm category as the canonical `categories` field (name-based; markdown frontmatter for memo).

**Architecture:** `PalmRuntime` owns one hub `GenericSqliteBackend` (`.state/hub.db`) with a canon collection per domain. Each Palm backend gains a domain-level collection (whole DB, unfiltered). `finishConnect` builds one `LogicalCalendar` per domain (Primary=hub, Sync1=Palm), calls `generateMappings(Star)`, and `setSyncMappings`. The four plugins' palm↔peer transcoders gain a borrowed `CategoryMappingStore*` so they map Palm slot ⇄ name into `CATEGORIES`/frontmatter.

**Tech Stack:** C++/Qt6, KF6 (KCalendarCore/KContacts), libkalburator v0.57 (`GenericSqliteBackend`, `LogicalCalendar`, `generateMappings`), CTest. Plugins are git submodules; `CategoryMappingStore` + `PalmRuntime` are main-repo.

**Iteration build:** `cmake -S . -B build-c -DCMAKE_BUILD_TYPE=Debug -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=/home/clinton/dev/libkalburator` then `cmake --build build-c -j"$(nproc)"`.

**Spec:** `docs/superpowers/specs/2026-05-27-subproject-c-canonical-hub-design.md`.

---

## File structure

| File | Repo | Responsibility |
|------|------|----------------|
| `src/palm/calendar/categorymappingstore.{h,cpp}` | main | add `slotForName(db, name)` reverse lookup |
| `src/runtime/palmruntime.{h,cpp}` | main | own/register hub; build LogicalCalendars + generateMappings; drop per-slot RawFiles loop |
| `src/plugins/<d>/palm<d>backend.{h,cpp}` ×4 | submodule | add domain-level collection |
| `src/plugins/<d>/<transcoder>.{h,cpp}` + `<stage>.{h,cpp}` + `<d>domainextension.{h,cpp}` ×4 | submodule | thread `CategoryMappingStore*`; map slot⇄`CATEGORIES`/frontmatter |
| `tests/runtime/*`, `tests/plugins/*` | main | assert hub receives records; category round-trip |

---

### Task 1: CategoryMappingStore — name→slot reverse lookup

**Files:**
- Modify: `src/palm/calendar/categorymappingstore.h`, `.cpp`
- Test: `tests/palm/tst_categorymappingstore.cpp` (create if absent; otherwise add cases)

- [ ] **Step 1: Failing test**

Add to the store's test (create `tests/palm/tst_categorymappingstore.cpp` with the standard `QTEST_GUILESS_MAIN` skeleton if it doesn't exist; register it in `tests/CMakeLists.txt` mirroring a sibling test):
```cpp
void slotForName_resolvesAndFallsBack()
{
    WildPalms::PalmCalendar::CategoryMappingStore s;
    s.setSlotName(QStringLiteral("DatebookDB"), 3, QStringLiteral("Work"));
    QCOMPARE(s.slotForName(QStringLiteral("DatebookDB"), QStringLiteral("Work")), 3);
    // Unknown name -> Unfiled (0)
    QCOMPARE(s.slotForName(QStringLiteral("DatebookDB"), QStringLiteral("Nope")), 0);
    // "Unfiled" -> 0
    QCOMPARE(s.slotForName(QStringLiteral("DatebookDB"), QStringLiteral("Unfiled")), 0);
    // Case-sensitive exact match is fine; empty -> 0
    QCOMPARE(s.slotForName(QStringLiteral("DatebookDB"), QString()), 0);
}
```

- [ ] **Step 2: Run, expect FAIL** — `ctest --test-dir build-c -R categorymappingstore` → fails to compile (`slotForName` undeclared).

- [ ] **Step 3: Implement.** In `categorymappingstore.h`, after `sixteenSlotNames`:
```cpp
    /// Reverse of slotName: the slot whose stored name equals `name`
    /// (exact match) for dbName. "Unfiled"/empty/unknown -> 0 (Unfiled).
    int slotForName(const QString &dbName, const QString &name) const;
```
In `categorymappingstore.cpp`:
```cpp
int CategoryMappingStore::slotForName(const QString &dbName, const QString &name) const
{
    if (name.isEmpty() || name == QLatin1String(UnfiledName))
        return UnfiledSlot;
    const auto db = m_slots.constFind(dbName);
    if (db != m_slots.constEnd()) {
        for (auto it = db->constBegin(); it != db->constEnd(); ++it)
            if (it.value() == name)
                return it.key();
    }
    return UnfiledSlot;
}
```

- [ ] **Step 4: Run, expect PASS.**

- [ ] **Step 5: Commit (main repo).** `git add src/palm/calendar/categorymappingstore.* tests/palm/tst_categorymappingstore.cpp tests/CMakeLists.txt && git commit -m "feat(palm): CategoryMappingStore::slotForName reverse lookup (C)"`

---

### Task 2: Stand up the hub backend in PalmRuntime

**Files:**
- Modify: `src/runtime/palmruntime.h` (member), `src/runtime/palmruntime.cpp` (ctor + a `createHub()` helper)

- [ ] **Step 1: Add the hub member.** In `palmruntime.h`, after `m_baselineStore`:
```cpp
    // C: canonical local hub — one GenericSqliteBackend, one canon collection
    // per domain, registered as "wp-hub". Primary in every LogicalCalendar.
    std::unique_ptr<Kalburator::Universal::GenericSqliteBackend> m_hub;
```
Add include `#include <genericsqlitebackend.h>` near the other libkalburator includes in `palmruntime.cpp`. (Confirm the namespace: `grep -n "namespace" ../libkalburator/src/universal/genericsqlitebackend.h` — adjust `Kalburator::Universal` to the actual namespace, e.g. `Kalburator::Sinks` or `Kalburator::Sync`, used by the class.)

- [ ] **Step 2: Construct + register the hub in the ctor**, after `m_registry` is built and `registerStandardContributions` runs, before plugin load:
```cpp
    m_hub = std::make_unique<Kalburator::Universal::GenericSqliteBackend>(
        QDir(profilePath).filePath(QStringLiteral(".state/hub.db")));
    m_registry->registerBackendInstance(QStringLiteral("wp-hub"), m_hub.get());
```

- [ ] **Step 3: Create one canon collection per domain.** Add a private helper `void PalmRuntime::ensureHubCollections()` called once after construction (e.g. end of ctor), creating a collection per domain with its canon shape:
```cpp
void PalmRuntime::ensureHubCollections()
{
    using Kalburator::Shape::Shape;
    using Kalburator::Shape::DomainId;
    using Kalburator::Shape::EncodingId;
    const std::pair<const char *, const char *> domains[] = {
        { "calendar", "calendar" }, { "contacts", "contacts" },
        { "todo", "todo" },         { "note", "note" },
    };
    for (const auto &[colId, dom] : domains) {
        Kalburator::Sync::CollectionInfo info;
        info.id = QString::fromLatin1(colId);
        info.name = QString::fromLatin1(colId);
        info.type = QString::fromLatin1(dom);
        m_hub->createCollection(
            info, Shape{ DomainId{QString::fromLatin1(dom)}, EncodingId{QStringLiteral("canon")} });
    }
}
```
Declare `ensureHubCollections()` + `m_hub` access in the header.

- [ ] **Step 4: Build** — `cmake --build build-c` → compiles (hub registered; no mapping uses it yet).

- [ ] **Step 5: Commit (main repo).** `git commit -am "feat(runtime): stand up GenericSqliteBackend hub with per-domain canon collections (C)"`

---

### Task 3: Domain-level collection on each Palm backend (4 submodules)

For EACH plugin backend, add a domain-level collection whose id is `"palm:<domain>"` (calendar/contacts/todo/note), returning ALL records unfiltered, shaped `(<domain>, palm)`. Worked example = **calendar**; repeat for the other three with their domain string + db name.

**Files (calendar):** `src/plugins/calendar/palmcalendarbackend.{h,cpp}`
**Test:** `tests/plugins/calendar/tst_palmcalendarbackend.cpp` (add a case)

- [ ] **Step 1 (calendar): Failing test** — domain collection returns all records across categories:
```cpp
void domainCollection_returnsAllRecordsUnfiltered()
{
    // Arrange a mock device with records in slots 0 and 3 (see existing
    // fixtures in this test file for MockPalmDatabaseAccess setup).
    // ... build backend `b` with two DatebookDB records, categories 0 and 3 ...
    const auto all = b.loadRecords(QStringLiteral("palm:calendar"));
    QCOMPARE(all.size(), 2);                       // both, regardless of slot
    QVERIFY(!b.shapeFor(QStringLiteral("palm:calendar")).domain.toString().isEmpty());
}
```
(Model the arrange block on the existing per-slot tests already in this file.)

- [ ] **Step 2: Run, expect FAIL** (domain collection yields nothing today).

- [ ] **Step 3: Implement.** In `palmcalendarbackend.cpp`:
  - `availableCollections()` — append a domain-level entry first:
```cpp
    Kalburator::Sync::CollectionInfo domain;
    domain.id   = QStringLiteral("palm:calendar");
    domain.name = QStringLiteral("Calendar");
    domain.type = QStringLiteral("calendar");
    out.append(domain);
```
  - `loadRecords(collectionId)` — handle the domain id (no slot filter) before the per-slot path:
```cpp
    if (collectionId == QStringLiteral("palm:calendar")) {
        QList<Kalburator::Sync::BackendRecord> out;
        if (!m_palmBackend) return out;
        for (const auto &pr : m_palmBackend->loadPalmRecords(QStringLiteral("DatebookDB"))) {
            Kalburator::Sync::BackendRecord br;
            br.id = idForPalmRecord(pr.recordId);
            br.data = pr.toWireBytes();
            br.type = QStringLiteral("calendar");
            br.lastModified = pr.lastModified;
            br.contentHash = pr.contentHash();
            out.append(br);
        }
        return out;
    }
```
  - `shapeFor(collectionId)` — return `(calendar, palm)` for `"palm:calendar"` (same shape the per-slot collections report; reuse the existing per-slot branch by treating the domain id like a slot collection for shape purposes).
  - `createRecord`/`updateRecord`/`deleteRecord` — these already operate on the underlying DatebookDB by record id, not slot, so they need no domain-specific branch; confirm they don't reject an unknown collection id (if they validate `slotFromCollectionId(collectionId) >= 0`, relax to also accept `"palm:calendar"`). The category byte in the written record comes from the transcoder (Task 5), not collection context.

- [ ] **Step 4: Run, expect PASS.**

- [ ] **Step 5 (contacts/todo/memo): repeat Steps 1-4** with:
  - contacts: id `"palm:contacts"`, db `"AddressDB"`, type/domain `contacts`, file `src/plugins/contacts/palmcontactsbackend.cpp`.
  - todo: id `"palm:todo"`, db `"ToDoDB"`, domain `todo`, file `src/plugins/todos/todoblobbackend.cpp`.
  - memo: id `"palm:note"`, db `"MemoDB"`, domain `note`, file `src/plugins/memo/memoblobbackend.cpp`.
  (Each backend's loadRecords/availableCollections mirror the calendar shape; use that backend's existing per-slot code as the template for record construction.)

- [ ] **Step 6: Commit each submodule** (`git -C src/plugins/<d> commit -am "feat: domain-level palm:<domain> collection (C)"`).

---

### Task 4: finishConnect — LogicalCalendars + generateMappings; drop RawFiles defaults

**Files:** `src/runtime/palmruntime.cpp` (`finishConnect`, lines ~416-490)

- [ ] **Step 1: Remove the per-slot RawFiles default loop.** Delete the `for (const auto &palmCol : palmCollections)` block that creates `rawfiles-…` backends + `default-…` mappings (the block ending at `m_engine->setSyncMappings(m_mappings)`), but keep the AppInfo/category-slot capture above it and the `registerBackendInstance(id, ownedBackend)` for the Palm backend.

- [ ] **Step 2: Build per-domain LogicalCalendars after all Palm backends are registered.** Add includes `#include <logicalcalendar.h>` and `#include <syncmappinggenerator.h>`. After the plugin loop, build LCs mapping each registered Palm backend to the hub:
```cpp
    using Kalburator::Sync::LogicalCalendar;
    using Kalburator::Sync::CalendarBackendBinding;
    using Kalburator::Sync::BackendRole;
    QList<LogicalCalendar> lcs;
    // (palmBackendId, hubCollectionId, palmDomainCollectionId)
    const std::tuple<QString, QString, QString> wiring[] = {
        { QStringLiteral("calendar"), QStringLiteral("calendar"), QStringLiteral("palm:calendar") },
        { QStringLiteral("contacts"), QStringLiteral("contacts"), QStringLiteral("palm:contacts") },
        { QStringLiteral("memo"),     QStringLiteral("note"),     QStringLiteral("palm:note") },
        { QStringLiteral("todo"),     QStringLiteral("todo"),     QStringLiteral("palm:todo") },
    };
    for (const auto &[palmId, hubCol, palmCol] : wiring) {
        if (!m_registry->backendInstance(palmId)) continue;  // backend not connected
        LogicalCalendar lc;
        lc.id = QStringLiteral("wp-%1").arg(hubCol);
        lc.domain = Kalburator::Shape::DomainId{hubCol};
        lc.displayName = hubCol;
        lc.syncEnabled = true;
        CalendarBackendBinding hubB;
        hubB.backendId = QStringLiteral("wp-hub");
        hubB.calendarId = hubCol;
        hubB.role = BackendRole::Primary;
        lc.bindings.append(hubB);
        CalendarBackendBinding palmB;
        palmB.backendId = palmId;
        palmB.calendarId = palmCol;
        palmB.role = BackendRole::Sync1;
        palmB.syncOrder = 1;
        lc.bindings.append(palmB);
        lcs.append(lc);
    }
    m_mappings = Kalburator::Sync::generateMappings(lcs, Kalburator::Sync::SyncTopology::Star);
    m_engine->setSyncMappings(m_mappings);
```
(Confirm the actual registered Palm backend ids via `grep registerBackendInstance` in `finishConnect` — adjust `palmId` strings to match what `id` holds for each plugin. The `id` is the plugin's backend id from the plugin loop.)

- [ ] **Step 3: Build** — `cmake --build build-c` → compiles.

- [ ] **Step 4: Commit (main repo).** `git commit -am "feat(runtime): domain-level Palm<->hub Star mappings via generateMappings; drop per-slot RawFiles defaults (C)"`

---

### Task 5: Category-as-field — calendar (worked example, submodule)

**Files:** `src/plugins/calendar/{icstranscoder,palmtoicstransformation,calendardomainextension}.{h,cpp}`
**Test:** `tests/plugins/calendar/tst_calendar_canon_roundtrip.cpp`

- [ ] **Step 1: Failing test** — a slot-3 ("Work") Palm event round-trips its category name through canon. Extend the existing roundtrip test: seed a `CategoryMappingStore` with slot 3 = "Work", build the `CalendarPalmShapes` with that store, encode a category-3 Palm record, and assert the produced ical contains `CATEGORIES:Work`; decode back and assert category byte == 3.

- [ ] **Step 2: Run, expect FAIL** (no CATEGORIES emitted today; `categories` is in `icsToPalmLoss` as Dropped).

- [ ] **Step 3: Thread the store + emit/read categories.**
  - `icstranscoder.h`: change signatures to take the store + db name:
```cpp
QByteArray encodePalmToIcs(const WildPalms::PalmSync::PalmRecord &record,
                           const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                           const QString &dbName);
std::optional<WildPalms::PalmSync::PalmRecord>
decodeIcsToPalm(const QByteArray &icsBytes,
                const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                const QString &dbName);
```
  - `icstranscoder.cpp`: in `encodePalmToIcs`, after building the `KCalendarCore::Event`, set categories from the slot name:
```cpp
    if (cats && record.category != 0) {
        const QString nm = cats->slotName(dbName, record.category);
        if (!nm.isEmpty()) event->setCategories(QStringList{nm});
    }
```
   In `decodeIcsToPalm`, derive the slot from the event's categories (replacing the old `slotHint` param):
```cpp
    int slot = 0;
    if (cats && !event->categories().isEmpty())
        slot = cats->slotForName(dbName, event->categories().first());
    // ...set the decoded PalmRecord's category = slot...
```
  - `palmtoicstransformation.{h,cpp}`: give both stages a borrowed `const CategoryMappingStore*` member (ctor arg) and pass `(store, "DatebookDB")` into the transcoder calls. The `IcsToPalmStage` no longer passes `slotHint=-1`.
  - `calendardomainextension.{h,cpp}`: `CalendarPalmShapes` gains a ctor taking `const CategoryMappingStore*` and builds its edges with stages constructed from it:
```cpp
    explicit CalendarPalmShapes(const WildPalms::PalmCalendar::CategoryMappingStore *cats);
    // edges(): std::make_shared<PalmToIcsStage>(m_cats), std::make_shared<IcsToPalmStage>(m_cats)
```
  - `calendarbackendplugin.cpp`: `shapeContributions()` passes the plugin's store:
```cpp
    return { std::make_shared<CalendarPalmShapes>(m_categoryStore.get()) };
```
  - Remove `categories` from `icsToPalmLoss()` (it is no longer dropped).

- [ ] **Step 4: Run, expect PASS** (the roundtrip test + existing tests in the file).

- [ ] **Step 5: Commit (calendar submodule).** `git -C src/plugins/calendar commit -am "feat: Palm category slot<->CATEGORIES name via CategoryMappingStore (C)"`

---

### Task 6: Category-as-field — contacts (submodule)

**Files:** `src/plugins/contacts/{contactsvcardtranscoder,palmtovcardtransformation,contactsdomainextension}.{h,cpp}`; test `tests/plugins/contacts/tst_contacts_canon_roundtrip.cpp`.

- [ ] Apply the **Task 5 pattern** with contacts specifics: transcoder fns `encodePalmToVcard` / `decodeVcardToPalm` gain `(const CategoryMappingStore *cats, const QString &dbName)` (db `"AddressDB"`); use KContacts `Addressee::setCategories(QStringList)` / `addr.categories()`; `ContactsPalmShapes` ctor takes the store and constructs `PalmToVCardStage`/`VCardToPalmStage` from it; `contactsbackendplugin.cpp` `shapeContributions()` passes `m_categoryStore.get()`; remove `categories` from the vcard→palm loss profile if present. Write the slot-3 round-trip test first (FAIL → implement → PASS). Commit the submodule.

---

### Task 7: Category-as-field — todo (submodule)

**Files:** `src/plugins/todos/{todoicstranscoder,palmtovtodotransformation,tododomainextension}.{h,cpp}`; test `tests/plugins/todos/tst_todo_canon_roundtrip.cpp`.

- [ ] Apply the **Task 5 pattern** with todo specifics: transcoder fns `encodePalmToIcs` / `decodeIcsToPalm` (in `todoicstranscoder.h`) gain `(const CategoryMappingStore *cats, const QString &dbName)` (db `"ToDoDB"`); use `KCalendarCore::Todo::setCategories` / `todo->categories()`; `TodoPalmShapes` ctor takes the store and constructs `PalmToVTodoStage`/`VTodoToPalmStage` from it; `todobackendplugin.cpp` passes `m_categoryStore.get()`; remove `categories` from the vtodo→palm loss profile if present. Round-trip test first. Commit the submodule.

---

### Task 8: Category-as-field — memo via frontmatter (submodule)

**Files:** `src/plugins/memo/{<memomarkdown transcoder>,palmnotetransformation,notedomainextension}.{h,cpp}`; test `tests/plugins/memo/tst_memo_note_roundtrip.cpp`.

Memo already carries the category **slot** in YAML frontmatter (`palmnotetransformation.h:12`). Shift it to the **name** so it matches the cross-domain model.

- [ ] **Step 1: Failing test** — a slot-3 ("Work") memo round-trips with frontmatter `category: Work` and decodes back to slot 3 (seed the store slot 3 = "Work").
- [ ] **Step 2: FAIL.**
- [ ] **Step 3: Implement.** Thread `const CategoryMappingStore*` into `NotePalmShapes` → `PalmToCanonStage`/`CanonToPalmStage` (same ctor-injection pattern). In the memomarkdown encode, write `category: <slotName>` into the frontmatter (instead of/in addition to the slot index); in decode, read the `category:` frontmatter value and resolve via `slotForName(\"MemoDB\", name)`. `memobackendplugin.cpp` `shapeContributions()` passes `m_categoryStore.get()`.
- [ ] **Step 4: PASS.**
- [ ] **Step 5: Commit (memo submodule).**

---

### Task 9: Update runtime/e2e tests to assert the hub

**Files:** `tests/runtime/tst_palm_runtime_hotsync.cpp`, `tst_palm_runtime_modes.cpp`, `tst_runtime_stress.cpp`, `tst_runtime_caldav_e2e.cpp`, `tst_runtime_carddav_e2e.cpp`, `tst_palm_mass_delete_guard_e2e.cpp`.

These assert against the old `rawfiles/<plugin>/<slot>/` outputs, which no longer exist.

- [ ] **Step 1:** For each test that asserted a RawFiles mirror received records, re-point the assertion to the hub: after `hotSync()`, read the hub via `runtime.backendRegistry().backendInstance("wp-hub")` → `loadRecords("<domain>")` and assert the expected records/count landed in canon. (Add a small `PalmRuntime` test accessor for the hub if `backendRegistry()` doesn't already expose enough — `backendRegistry()` returns the `BackendRegistry&`, so `backendInstance("wp-hub")` is reachable.)
- [ ] **Step 2:** The caldav/carddav e2e tests assumed `Palm↔remote` directly; under C the Palm goes to the hub only. Mark the direct-remote assertions `QSKIP("hub<->remote routing lands in the next sub-project (C-remote)")` with a comment, or convert them to assert `Palm→hub` then leave the remote leg for the next sub-project. Do NOT delete them.
- [ ] **Step 3: Run** `ctest --test-dir build-c` → all green (skips allowed).
- [ ] **Step 4: Commit (main repo).**

---

### Task 10: Re-pin verification + superproject commit + push

**Files:** superproject gitlinks (`src/plugins/*`), no `CMakeLists.txt` change (pin already `v0.57`).

- [ ] **Step 1: Fresh FetchContent build + full suite** (real delivery path):
```bash
rm -rf build-ccheck
cmake -S . -B build-ccheck -DCMAKE_BUILD_TYPE=Debug 2>&1 | grep -iE "fetching v0.57|Configuring done|CMake Error"
cmake --build build-ccheck -j"$(nproc)" 2>&1 | grep -cE "error:" | xargs echo "errors:"
ctest --test-dir build-ccheck 2>&1 | tail -4
```
Expected: `errors: 0`; suite green.
- [ ] **Step 2: Push the four plugin submodules** (`git -C src/plugins/<d> push`).
- [ ] **Step 3: Stage gitlinks + commit superproject:**
```bash
git add src/plugins/calendar src/plugins/contacts src/plugins/memo src/plugins/todos
git commit -m "feat(sync): canonical hub + domain-level Star + category-as-field (sub-project C)

Stands up the per-domain GenericSqliteBackend hub; Palm<->hub is one
domain-level Star mapping per domain (one device read/session). Palm category
travels as the canonical CATEGORIES field (name via CategoryMappingStore) /
memo frontmatter. Per-slot RawFiles defaults removed. Remote + views land in
later sub-projects (interim: remote Palm->hub only, views empty until D).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```
- [ ] **Step 4: Clean scratch** (`rm -rf build-ccheck build-c`) and push the superproject branch.

---

## Self-Review

- **Spec coverage:** hub standup (T2), domain Palm collection (T3), LC+generateMappings+drop-defaults (T4), category-as-field cal/contacts/todo (T5-7) + memo frontmatter (T8), store reverse helper (T1), test re-pointing (T9), verify/commit/push (T10). All spec §3 in-scope items covered; §8 open items resolved (8.1 store threading = ctor injection T5; 8.2 GenericSqliteBackend linkage = T2 Step 1 namespace check; 8.3 remote mappings = T4 Step 1 drop + T9 Step 2 skip; 8.4 memo frontmatter key = T8 `category:`).
- **Placeholder scan:** the per-domain Tasks 6/7 reference Task 5's pattern with full concrete specifics (function names, KDE APIs, db strings) rather than repeating the whole block — borderline vs. the "repeat the code" rule, accepted because the structure is identical and each domain's deltas (transcoder fn names, `setCategories` API, db name) are stated explicitly. Several steps say "confirm X via grep, adjust" for facts that vary by submodule internals (registered backend ids, GenericSqliteBackend namespace) — these are real verification steps with the grep given, not vague placeholders.
- **Type/name consistency:** `slotForName`, `wp-hub`, `palm:<domain>`, `CalendarPalmShapes(store)` consistent across tasks. Hub collection ids (`calendar/contacts/todo/note`) consistent T2↔T4.
- **Known softness (flag for executor):** Tasks 5-8 edit transcoder bodies (`icstranscoder.cpp` etc.) whose full current bodies were not read while planning; the executor must read each `encode*/decode*` body and place the `setCategories`/`categories()` calls at the right point (after Event/Addressee/Todo construction). The signature changes + call sites are specified; the surrounding body is not reproduced.
