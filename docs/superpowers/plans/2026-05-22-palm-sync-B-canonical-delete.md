# Sub-Project B — Canonical deleteRecord — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `PalmCalendarBackend::deleteRecord` and `MemoBlobBackend::deleteRecord` actually delete records on the Palm device by switching them to the dbName-aware `deletePalmRecord` pattern that contacts/todos already use.

**Architecture:** Mirror the contacts/todos pattern. Each plugin already knows its canonical Palm DB name ("DatebookDB" for calendar, "MemoDB" for memo); the plugin's `deleteRecord` decodes the numeric record id from the encoded BackendRecord id and calls `m_palmBackend->deletePalmRecord(canonicalDbName, numericId)`. The decoder `PalmBackend::decodeRecordId` stays untouched (other callers depend on its current behavior).

**Tech Stack:** Qt6, C++17.

**Spec:** `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` §4.2

**Build / test commands:**
- `cmake --build build-dev -j$(nproc)`
- `ctest --test-dir build-dev --output-on-failure -j$(nproc)`

**File inventory:**

Modified:
- `src/plugins/calendar/palmcalendarbackend.cpp` — rewrite `deleteRecord` at line 197.
- `src/plugins/memo/memoblobbackend.cpp` — rewrite `deleteRecord` at line 185.
- `tests/palmcalendar/tst_palmcalendarbackend.cpp` — add delete-path test if missing.
- `tests/plugins/memo/tst_memoblobbackend.cpp` — add delete-path test.

**Dependency:** none. Plan B is independent of Plan A (hash stability). Both can be implemented in either order. Spec puts B after A because the spec's downstream sub-projects (C, D, E) need both A and B landed.

**Safety:** this change makes calendar and memo destructive on the delete path. The mass-delete guard (libkalburator `v0.54-mass-delete-guard`, already shipped) is the safety net. Sub-project E (Plan E) proves it works end-to-end. **Do not ship Plan B without Plan E's verification on real data.**

**Cross-repo:** none. No libkalburator changes; no PlanStan gate.

---

## Task 1: Calendar — failing delete test

**Files:**
- Modify: `tests/palmcalendar/tst_palmcalendarbackend.cpp`

- [ ] **Step 1: Read the existing test fixture pattern**

```bash
sed -n '1,60p' /home/clinton/dev/WildPalms/tests/palmcalendar/tst_palmcalendarbackend.cpp
```

Note the headers, the stub `IPalmDatabaseAccess` / `MockPalmDatabaseAccess` usage, the backend construction pattern. Match it.

- [ ] **Step 2: Find the slots block and add a new slot**

```bash
grep -n "private slots:" /home/clinton/dev/WildPalms/tests/palmcalendar/tst_palmcalendarbackend.cpp
```

In the `private slots:` block, after the existing test slots, add:

```cpp
    void deleteRecord_usesDatebookDBCanonicalName();
```

- [ ] **Step 3: Add the test body**

Append before the `QTEST_*_MAIN` line (use the existing main macro — likely `QTEST_GUILESS_MAIN` or a project-specific variant):

```cpp
void TstPalmCalendarBackend::deleteRecord_usesDatebookDBCanonicalName()
{
    // Seed the mock device with one calendar record.
    auto device = std::make_shared<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = 12345;
    pr.category = 0;
    pr.data     = QByteArrayLiteral("test event");
    device->writeRecord(QStringLiteral("DatebookDB"), pr);

    auto palmBackend = std::make_shared<WildPalms::PalmSync::PalmBackend>(device);
    WildPalms::CalendarPlugin::PalmCalendarBackend backend;
    backend.setPalmBackend(palmBackend.get());

    // Encode the recordId the way the plugin does, then delete.
    const QString encoded = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("DatebookDB"), 12345);
    QVERIFY(backend.deleteRecord(encoded));

    // Verify the device no longer has the record.
    const auto remaining = device->readAllRecords(QStringLiteral("DatebookDB"));
    QCOMPARE(remaining.size(), 0);
}
```

Adjust the `setPalmBackend` call to match whatever the existing test fixture does to wire the Palm backend into the calendar plugin backend (might be a ctor argument or another method). Read the existing test to confirm.

- [ ] **Step 4: Build, expect test to fail**

```bash
cmake --build build-dev --target tst_palmcalendarbackend
ctest --test-dir build-dev -R tst_palmcalendarbackend --output-on-failure 2>&1 | tail -15
```

Expected: `deleteRecord_usesDatebookDBCanonicalName` FAILS with "remaining.size() != 0" — the current `m_palmBackend->deleteRecord(encoded)` silently returns false because `decodeRecordId` produces a non-canonical name the mock device rejects.

If the test passes against the current code, the mock device is too forgiving — adjust the mock to enforce dbName matching, OR write the test to call `m_palmBackend->deletePalmRecord` directly with a wrong name and verify it fails, then verify the new `deleteRecord` succeeds.

- [ ] **Step 5: Commit**

```bash
git add tests/palmcalendar/tst_palmcalendarbackend.cpp
git commit -m "test: failing calendar deleteRecord test (red phase)"
```

---

## Task 2: Calendar — fix deleteRecord

**Files:**
- Modify: `src/plugins/calendar/palmcalendarbackend.cpp`

- [ ] **Step 1: Rewrite the method**

In `src/plugins/calendar/palmcalendarbackend.cpp`, find `PalmCalendarBackend::deleteRecord` at line 197:

```cpp
bool PalmCalendarBackend::deleteRecord(const QString &recordId)
{
    return m_palmBackend && m_palmBackend->deleteRecord(recordId);
}
```

Replace with:

```cpp
bool PalmCalendarBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    std::uint32_t rid = 0;
    if (!decodeId(recordId, &rid)) return false;
    return m_palmBackend->deletePalmRecord(QStringLiteral("DatebookDB"), rid);
}
```

`decodeId` is the existing local helper at line 33 — it extracts the numeric id without depending on `PalmBackend::decodeRecordId`'s dbName output.

- [ ] **Step 2: Build + run the test**

```bash
cmake --build build-dev --target tst_palmcalendarbackend
ctest --test-dir build-dev -R tst_palmcalendarbackend --output-on-failure
```

Expected: all calendar backend tests PASS, including the new `deleteRecord_usesDatebookDBCanonicalName`.

- [ ] **Step 3: Commit**

```bash
git add src/plugins/calendar/palmcalendarbackend.cpp
git commit -m "plugins: calendar — canonical-dbName deleteRecord

PalmCalendarBackend::deleteRecord now uses the dbName-aware
deletePalmRecord(\"DatebookDB\", rid) pattern that contacts/todos use,
instead of m_palmBackend->deleteRecord(recordId) which routes through
decodeRecordId and produces a non-canonical name the device rejects.
Calendar deletes now actually delete on the Palm. The mass-delete
guard (libkalburator v0.54) is the safety net against bulk deletion."
```

---

## Task 3: Memo — failing delete test

**Files:**
- Modify: `tests/plugins/memo/tst_memoblobbackend.cpp`

- [ ] **Step 1: Read the existing fixture pattern**

```bash
sed -n '1,60p' /home/clinton/dev/WildPalms/tests/plugins/memo/tst_memoblobbackend.cpp
```

Note the imports, mock device wiring, backend construction. Memo's structure may differ from calendar's; match what's already there.

- [ ] **Step 2: Add a new test slot**

In the `private slots:` block, add:

```cpp
    void deleteRecord_usesMemoDBCanonicalName();
```

- [ ] **Step 3: Add the test body**

Append before the `QTEST_*_MAIN` line:

```cpp
void TstMemoBlobBackend::deleteRecord_usesMemoDBCanonicalName()
{
    // Seed the mock device with one memo record.
    auto device = std::make_shared<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    WildPalms::PalmSync::PalmRecord pr;
    pr.recordId = 7777;
    pr.category = 0;
    pr.data     = QByteArrayLiteral("memo body");
    device->writeRecord(QStringLiteral("MemoDB"), pr);

    auto palmBackend = std::make_shared<WildPalms::PalmSync::PalmBackend>(device);
    WildPalms::Memo::MemoBlobBackend backend;
    backend.setPalmBackend(palmBackend.get());

    const QString encoded = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral("MemoDB"), 7777);
    QVERIFY(backend.deleteRecord(encoded));

    const auto remaining = device->readAllRecords(QStringLiteral("MemoDB"));
    QCOMPARE(remaining.size(), 0);
}
```

Match the namespace + class names against what the existing memo test does (the `WildPalms::Memo::MemoBlobBackend` may be wrapped differently — read the existing test first).

- [ ] **Step 4: Build, expect failure**

```bash
cmake --build build-dev --target tst_memoblobbackend
ctest --test-dir build-dev -R tst_memoblobbackend --output-on-failure 2>&1 | tail -15
```

Expected: `deleteRecord_usesMemoDBCanonicalName` FAILS.

- [ ] **Step 5: Commit**

```bash
git add tests/plugins/memo/tst_memoblobbackend.cpp
git commit -m "test: failing memo deleteRecord test (red phase)"
```

---

## Task 4: Memo — fix deleteRecord

**Files:**
- Modify: `src/plugins/memo/memoblobbackend.cpp`

- [ ] **Step 1: Rewrite the method**

In `src/plugins/memo/memoblobbackend.cpp`, find `MemoBlobBackend::deleteRecord` at line 185:

```cpp
bool MemoBlobBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    return m_palmBackend->deleteRecord(recordId);
}
```

Replace with:

```cpp
bool MemoBlobBackend::deleteRecord(const QString &recordId)
{
    if (!m_palmBackend) return false;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!WildPalms::PalmSync::PalmBackend::decodeRecordId(
            recordId, &dbName, &numericId)) {
        return false;
    }
    return m_palmBackend->deletePalmRecord(QStringLiteral("MemoDB"), numericId);
}
```

(Memo doesn't have a local `decodeId` helper like calendar — it uses `PalmBackend::decodeRecordId` directly, per the existing readRecord/updateRecord paths at lines 152, 177, 200. We discard the decoded dbName and use the canonical "MemoDB" string.)

- [ ] **Step 2: Build + run the test**

```bash
cmake --build build-dev --target tst_memoblobbackend tst_memobackendplugin
ctest --test-dir build-dev -R "tst_memo" --output-on-failure 2>&1 | tail -10
```

Expected: all memo tests PASS, including the new delete-path test.

- [ ] **Step 3: Commit**

```bash
git add src/plugins/memo/memoblobbackend.cpp
git commit -m "plugins: memo — canonical-dbName deleteRecord

MemoBlobBackend::deleteRecord now uses deletePalmRecord(\"MemoDB\", rid)
instead of m_palmBackend->deleteRecord(recordId), which routed through
decodeRecordId and produced a non-canonical name. Memo deletes now
actually delete on the Palm. The mass-delete guard (libkalburator
v0.54) is the safety net."
```

---

## Task 5: Full ctest + regression check

**Files:** none modified

- [ ] **Step 1: Run the full WildPalms ctest suite**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 80+ tests pass. Two new tests added (calendar + memo delete-path).

- [ ] **Step 2: Confirm grep cleanliness**

```bash
grep -rn "m_palmBackend->deleteRecord" /home/clinton/dev/WildPalms/src/plugins/
```

Expected: empty output. All plugin backends now use `deletePalmRecord(canonicalName, rid)`.

```bash
grep -rn "deletePalmRecord" /home/clinton/dev/WildPalms/src/plugins/
```

Expected: 4 hits (one per plugin — calendar/contacts/memo/todos), each using the canonical dbName.

---

## Verification checklist

- [ ] `PalmCalendarBackend::deleteRecord` uses `deletePalmRecord("DatebookDB", rid)`.
- [ ] `MemoBlobBackend::deleteRecord` uses `deletePalmRecord("MemoDB", rid)`.
- [ ] `PalmBackend::decodeRecordId` is NOT touched.
- [ ] Two new tests pass (calendar + memo delete-path).
- [ ] Full ctest baseline (80+) holds.
- [ ] Spec §4.2 requirements satisfied.

**Out of scope for this plan:**
- Other plugin types (Plucker, WebCalendar) — not Palm-side, no `deleteRecord` against pilot-link.
- Touching `decodeRecordId` — Plan B's spec §9 explicitly says "no" (other callers may rely on current behavior; a qWarning was considered but skipped to avoid log noise).

**Next sub-project:** Plan C — Default mapping policy = LastWriteWins (`docs/superpowers/plans/2026-05-22-palm-sync-C-default-policy.md`).
