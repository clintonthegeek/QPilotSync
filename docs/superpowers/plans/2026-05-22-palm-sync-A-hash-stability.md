# Sub-Project A — Hash Stability — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `PalmRecord::contentHash()` returning a deterministic SHA-256 over stable identity fields (recordId + category + data), and migrate all four Palm plugin backends to use it instead of `sha256Hex(toWireBytes())`.

**Architecture:** The current `sha256Hex(br.data)` (where `br.data = pr.toWireBytes()`) includes mutable per-sync metadata (the dirty/busy attributes byte and `lastModified` timestamp). Two reads of the same unchanged Palm record produce different hashes → engine's `perRecordDiff` misclassifies everything as a conflict. The fix is one new method on `PalmRecord` + four call-site migrations in the plugins. `toWireBytes()` is preserved unchanged for transport.

**Tech Stack:** Qt6, C++17, QCryptographicHash.

**Spec:** `docs/superpowers/specs/2026-05-22-palm-sync-honesty-design.md` §4.1

**Build / test commands:**
- `cmake --build build-dev -j$(nproc)`
- `ctest --test-dir build-dev --output-on-failure -j$(nproc)`
- `ctest --test-dir build-dev -R <name> --output-on-failure`

**File inventory:**

New:
- `tests/palmsync/tst_palmrecord_contenthash.cpp` — invariant tests for the new method.

Modified:
- `src/palm/sync/palmrecord.h` — add `contentHash()`; add `#include <QCryptographicHash>`.
- `src/plugins/calendar/palmcalendarbackend.cpp` — migrate 3 call sites (125, 147, 222); remove local `sha256Hex` helper (line 21).
- `src/plugins/contacts/palmcontactsbackend.cpp` — migrate 3 call sites (122, 141, 214); remove local `sha256Hex` helper (line 19).
- `src/plugins/memo/memoblobbackend.cpp` — migrate the 1 call site (line 47, which hashes `bytes` = `encode(m).toUtf8()`, not wire bytes — needs special handling, see Task 4).
- `src/plugins/todos/todoblobbackend.cpp` — migrate 3 call sites (124, 146, 231); remove local `sha256Hex` helper (line 20).
- `tests/palmsync/CMakeLists.txt` — register the new test.

**Cross-repo:** none directly. Sub-project A does NOT change libkalburator. Baseline schema is unchanged (the engine still stores contentHash strings; we just compute them differently on the Palm side). PlanStan gate not required.

**Note on memo (line 47):** `MemoBlobBackend::palmRecordToMarkdown` hashes the Markdown-encoded bytes, not Palm wire bytes. The Markdown encoding IS deterministic by design (it's the canonical content representation). For consistency with the spec's stable-hash invariant, we still switch memo to `pr.contentHash()` so all plugins use the same algorithm. Tested in Task 4.

---

## Task 1: PalmRecord::contentHash() — failing test

**Files:**
- Create: `tests/palmsync/tst_palmrecord_contenthash.cpp`
- Modify: `tests/palmsync/CMakeLists.txt`

- [ ] **Step 1: Write the test file**

```cpp
// tests/palmsync/tst_palmrecord_contenthash.cpp
#include <QtTest/QtTest>

#include "../../src/palm/sync/palmrecord.h"

using WildPalms::PalmSync::PalmRecord;

class TstPalmRecordContentHash : public QObject
{
    Q_OBJECT
private slots:
    void hashStableAcrossAttributeChanges();
    void hashStableAcrossLastModifiedChanges();
    void hashChangesWhenDataChanges();
    void hashChangesWhenCategoryChanges();
    void hashChangesWhenRecordIdChanges();
    void hashEmptyRecordIsDeterministic();
};

void TstPalmRecordContentHash::hashStableAcrossAttributeChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    a.attributes = 0x00;
    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-22T10:00:00Z"), Qt::ISODate);

    const QString h0 = a.contentHash();

    a.attributes = PalmRecord::AttrDirty;
    QCOMPARE(a.contentHash(), h0);

    a.attributes = PalmRecord::AttrDirty | PalmRecord::AttrBusy;
    QCOMPARE(a.contentHash(), h0);

    a.attributes = PalmRecord::AttrSecret | PalmRecord::AttrArchived;
    QCOMPARE(a.contentHash(), h0);
}

void TstPalmRecordContentHash::hashStableAcrossLastModifiedChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-22T10:00:00Z"), Qt::ISODate);

    const QString h0 = a.contentHash();

    a.lastModified = QDateTime::fromString(QStringLiteral("2026-05-23T10:00:00Z"), Qt::ISODate);
    QCOMPARE(a.contentHash(), h0);

    a.lastModified = QDateTime();   // invalid
    QCOMPARE(a.contentHash(), h0);
}

void TstPalmRecordContentHash::hashChangesWhenDataChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.data = QByteArrayLiteral("hello!");
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashChangesWhenCategoryChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.category = 4;
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashChangesWhenRecordIdChanges()
{
    PalmRecord a;
    a.recordId = 42;
    a.category = 3;
    a.data     = QByteArrayLiteral("hello");
    const QString h0 = a.contentHash();

    a.recordId = 43;
    QVERIFY(a.contentHash() != h0);
}

void TstPalmRecordContentHash::hashEmptyRecordIsDeterministic()
{
    PalmRecord a, b;
    QCOMPARE(a.contentHash(), b.contentHash());
    QVERIFY(!a.contentHash().isEmpty());
}

QTEST_GUILESS_MAIN(TstPalmRecordContentHash)
#include "tst_palmrecord_contenthash.moc"
```

- [ ] **Step 2: Register the test in CMakeLists**

In `tests/palmsync/CMakeLists.txt`, find the entry that registers `tst_palmrecord_wirebytes` and add a parallel entry for `tst_palmrecord_contenthash` (same helper or same `add_executable` block — match exactly).

- [ ] **Step 3: Build, expect compile failure**

```bash
cmake --build build-dev --target tst_palmrecord_contenthash 2>&1 | tail -10
```

Expected: compile error — `'class WildPalms::PalmSync::PalmRecord' has no member named 'contentHash'`. If you get a different error, fix the test before proceeding.

- [ ] **Step 4: Commit**

```bash
git add tests/palmsync/tst_palmrecord_contenthash.cpp tests/palmsync/CMakeLists.txt
git commit -m "test: failing tests for PalmRecord::contentHash (red phase)"
```

The Co-Authored-By trailer is auto-added by the commit harness on this machine — do NOT include it manually.

---

## Task 2: PalmRecord::contentHash() — implementation

**Files:**
- Modify: `src/palm/sync/palmrecord.h`

- [ ] **Step 1: Add the include + method**

In `src/palm/sync/palmrecord.h`, add the include near the existing Qt includes (after `#include <QIODevice>`):

```cpp
#include <QCryptographicHash>
```

Then add the new method inside the `PalmRecord` struct, right after the existing `static PalmRecord fromWireBytes(...)` (around line 95, before the closing `};`):

```cpp
    /// Returns a deterministic SHA-256 over the stable identity
    /// fields (recordId + category + data). Does NOT include
    /// attributes or lastModified — those carry per-sync metadata
    /// (dirty/busy bits, mtime) that change between reads of
    /// otherwise-unchanged records and would corrupt the engine's
    /// content-equality diff.
    ///
    /// Used by sync-plugin backends as the BackendRecord::contentHash.
    /// Wire-bytes serialization (toWireBytes) stays unchanged for
    /// in-process transport; this is purely the identity hash.
    QString contentHash() const
    {
        QByteArray buf;
        QDataStream ds(&buf, QIODevice::WriteOnly);
        ds.setVersion(QDataStream::Qt_6_0);
        ds << static_cast<quint32>(recordId)
           << static_cast<quint8>(category)
           << data;
        return QString::fromLatin1(
            QCryptographicHash::hash(buf, QCryptographicHash::Sha256).toHex());
    }
```

- [ ] **Step 2: Build + run the tests**

```bash
cmake --build build-dev --target tst_palmrecord_contenthash
ctest --test-dir build-dev -R tst_palmrecord_contenthash --output-on-failure
```

Expected: all 6 cases PASS. Paste the per-test PASS output in your report.

- [ ] **Step 3: Run sibling palmrecord tests for regressions**

```bash
ctest --test-dir build-dev -R "tst_palmrecord" --output-on-failure 2>&1 | tail -10
```

Expected: all palmrecord-related tests pass (including existing `tst_palmrecord_wirebytes` and `tst_palmrecord_bridge`).

- [ ] **Step 4: Commit**

```bash
git add src/palm/sync/palmrecord.h
git commit -m "palm: PalmRecord::contentHash — deterministic SHA over stable identity"
```

---

## Task 3: Migrate calendar + contacts + todos plugin backends

**Files:**
- Modify: `src/plugins/calendar/palmcalendarbackend.cpp`
- Modify: `src/plugins/contacts/palmcontactsbackend.cpp`
- Modify: `src/plugins/todos/todoblobbackend.cpp`

These three follow the same pattern: replace the local `sha256Hex(br.data)` calls with `pr.contentHash()`. Memo is different (Task 4) because it hashes Markdown bytes, not Palm wire bytes.

- [ ] **Step 1: Calendar — remove local helper + migrate sites**

In `src/plugins/calendar/palmcalendarbackend.cpp`:

1. Delete the local helper at lines 21-30 (the `sha256Hex` function — likely a small helper using QCryptographicHash). Use:

```bash
grep -n "sha256Hex" /home/clinton/dev/WildPalms/src/plugins/calendar/palmcalendarbackend.cpp
```

to confirm the exact line range, then delete those lines.

2. At each of the three call sites (lines around 125, 147, 222 — but the line numbers will shift after deletion; grep again after the deletion), the existing code looks like:

```cpp
br.data         = pr.toWireBytes();
br.contentHash  = sha256Hex(br.data);
```

Replace with:

```cpp
br.data         = pr.toWireBytes();
br.contentHash  = pr.contentHash();
```

(Order matters: `br.data` is still set to wire bytes for transport; `contentHash` no longer derives from `br.data`.)

3. Remove the now-unused `#include <QCryptographicHash>` if it was only used by the deleted helper. Search:

```bash
grep -n "QCryptographicHash" /home/clinton/dev/WildPalms/src/plugins/calendar/palmcalendarbackend.cpp
```

If only the include remains, delete it.

- [ ] **Step 2: Contacts — same pattern**

In `src/plugins/contacts/palmcontactsbackend.cpp`:

1. Delete the local `sha256Hex` helper at line 19 (and the QCryptographicHash include if no longer needed).
2. At each of the 3 call sites (around lines 122, 141, 214), replace `sha256Hex(br.data)` with `pr.contentHash()`.

- [ ] **Step 3: Todos — same pattern**

In `src/plugins/todos/todoblobbackend.cpp`:

1. Delete the local `sha256Hex` helper at line 20.
2. At each of the 3 call sites (around lines 124, 146, 231), replace `sha256Hex(br.data)` with `pr.contentHash()`.

- [ ] **Step 4: Build + run each plugin's backend tests**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -10
ctest --test-dir build-dev -R "tst_palmcalendarbackend|tst_palmcontactsbackend|tst_palmtodosadapter|tst_todoblobbackend|tst_contactsblobbackend|tst_calendarbackendplugin|tst_contactsbackendplugin|tst_todobackendplugin" --output-on-failure 2>&1 | tail -20
```

Expected: all pass. Some tests may have been hashing wire bytes via the OLD `sha256Hex` helper indirectly — if any test fails because its expected-hash literal no longer matches, that's a test that needs updating to use `pr.contentHash()` instead of a baked-in hex string. Update the test by computing the expected hash from a known PalmRecord rather than hard-coding it.

- [ ] **Step 5: Commit**

```bash
git add src/plugins/calendar/palmcalendarbackend.cpp \
    src/plugins/contacts/palmcontactsbackend.cpp \
    src/plugins/todos/todoblobbackend.cpp
git commit -m "plugins: calendar/contacts/todos — use PalmRecord::contentHash

Replaces sha256Hex(br.data) (which hashed pr.toWireBytes()
including the mutable dirty bit and lastModified) with
pr.contentHash() (deterministic over recordId+category+data).
Local sha256Hex helpers removed. Wire-bytes transport unchanged."
```

---

## Task 4: Migrate memo plugin backend

**Files:**
- Modify: `src/plugins/memo/memoblobbackend.cpp`

Memo's hash currently uses `sha256Hex(bytes)` where `bytes` is the encoded Markdown — not wire bytes. The bug doesn't affect memo as severely (the encoded Markdown is deterministic), but for the spec's "all plugins use the same algorithm" property, switch memo too.

- [ ] **Step 1: Locate the call site**

```bash
grep -n "sha256Hex" /home/clinton/dev/WildPalms/src/plugins/memo/memoblobbackend.cpp
```

Expected: helper at line 18, single call site around line 47.

- [ ] **Step 2: Replace the call site**

The existing code (around line 47):

```cpp
const QByteArray bytes = WildPalms::Memo::encode(m).toUtf8();
Kalburator::Sync::BackendRecord br;
br.id   = WildPalms::PalmSync::PalmBackend::encodeRecordId(
    QStringLiteral("MemoDB"), pr.recordId);
br.type = QStringLiteral("memos");
br.data = bytes;
br.contentHash = sha256Hex(bytes);
br.lastModified = pr.lastModified;
br.isDeleted = pr.isDeleted();
return br;
```

Replace `br.contentHash = sha256Hex(bytes);` with:

```cpp
br.contentHash = pr.contentHash();
```

The `bytes` variable stays — it's still used to populate `br.data`. We just no longer hash it.

- [ ] **Step 3: Delete the local helper**

Delete the `sha256Hex` function at line 18 (and surrounding lines for the function body — likely 4-6 lines including the closing brace). Remove the `#include <QCryptographicHash>` if no other code in the file uses it (`grep QCryptographicHash` after the helper deletion to confirm).

- [ ] **Step 4: Build + run memo tests**

```bash
cmake --build build-dev --target tst_memoblobbackend tst_memobackendplugin 2>&1 | tail -5
ctest --test-dir build-dev -R "tst_memo" --output-on-failure 2>&1 | tail -10
```

Expected: all memo-related tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/plugins/memo/memoblobbackend.cpp
git commit -m "plugins: memo — use PalmRecord::contentHash for consistency"
```

---

## Task 5: Cross-plugin smoke test + full ctest

**Files:** none modified

- [ ] **Step 1: Run the full WildPalms ctest suite**

```bash
cmake --build build-dev -j$(nproc) 2>&1 | tail -5
ctest --test-dir build-dev --output-on-failure -j$(nproc) 2>&1 | tail -15
```

Expected: 80+ tests pass. Note any pre-existing failures unrelated to this change (e.g. flaky `tst_providerlifecycle` was previously known).

- [ ] **Step 2: Verify hash determinism manually**

Quick sanity: construct a PalmRecord with known fields and verify the hash matches an explicit SHA-256 of the same buffer:

```bash
cat > /tmp/contenthash_verify.cpp << 'EOF'
#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QString>
#include <QtCore>
int main() {
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << static_cast<quint32>(42)
       << static_cast<quint8>(3)
       << QByteArrayLiteral("hello");
    qDebug() << QCryptographicHash::hash(buf, QCryptographicHash::Sha256).toHex();
    return 0;
}
EOF
g++ -std=c++17 $(pkg-config --cflags Qt6Core) /tmp/contenthash_verify.cpp $(pkg-config --libs Qt6Core) -o /tmp/contenthash_verify
/tmp/contenthash_verify
```

Compare against `tst_palmrecord_contenthash`'s output of the same record (recordId=42, category=3, data="hello"). They should match.

- [ ] **Step 3: No commit — this is verification only**

---

## Verification checklist

- [ ] `PalmRecord::contentHash()` exists and is documented.
- [ ] All four plugin backends call `pr.contentHash()` (not `sha256Hex`).
- [ ] No `sha256Hex` helpers remain in plugin .cpp files (`grep -rn sha256Hex src/plugins/` returns nothing).
- [ ] `tst_palmrecord_contenthash` passes (6 cases).
- [ ] Full ctest baseline (80+) holds.
- [ ] Spec §4.1 requirements satisfied.

**Out of scope for this plan (covered elsewhere):**
- Baseline migration (release note for end users) — Plan A's commit messages mention the concern; a release note is part of the eventual aggregate ship.
- Default conflict policy change (Sub-project C) — independent plan.
- deleteRecord canonicalization (Sub-project B) — independent plan.

**Next sub-project:** Plan B — Canonical deleteRecord (`docs/superpowers/plans/2026-05-22-palm-sync-B-canonical-delete.md`).
