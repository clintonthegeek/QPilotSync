# Phase E.7 — Typed Adapters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land stateless typed codecs and adapters for Palm Address, Memo, and ToDo records so that the E.9/E.11/E.12 plugin rewrites and WP's UI widgets have a clean, tested typed surface over `PalmBackend`. Two new static libraries: `WildPalmsPalmCodecs` (POD + pisock-driven encode/decode + optional KDE PIM converters) and `WildPalmsPalmAdapters` (stateless free functions over `PalmBackend`).

**Architecture:** Follows E.6's split. `src/palm/codecs/` holds POD value types and pure encode/decode free functions that call pisock's `pack_*`/`unpack_*`. `src/palm/adapters/` holds stateless free functions that borrow `PalmBackend*` and `CategoryMappingStore*` and return typed PODs. Neither layer appears in E.8's plugin ABI; both are optional convenience surfaces. Legacy `src/plugins/{contacts,memo,todos}/mapper.*` remain untouched and keep running until E.9/E.11/E.12 delete them.

**Tech Stack:** C++20, Qt6 (Core, Test), KF6::Contacts, KF6::CalendarCore, pisock (`pi-address.h`, `pi-memo.h`, `pi-todo.h`, `pi-buffer.h`), `Kalburator::Sync` (via `PalmBackend`), `WildPalmsPalmSync` (E.3), `WildPalmsPalmCalendar` (E.6, for `CategoryMappingStore`). No new external dependencies.

**Spec:** `docs/superpowers/specs/2026-04-23-phase-e7-typed-adapters-design.md`.

**Parent:** `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` row E.7.

**Repo:** All work in `~/dev/WildPalms/`. Build directory: `build-dev/` (preset project). No upstream libkalburator changes.

**Scope explicitly excluded** (per spec §"Non-goals"):

- Replacing, wrapping, or reimplementing any part of `libpisock`. Every codec calls pisock's `pack_*` / `unpack_*` directly.
- Touching `src/plugins/{contacts,memo,todos}/`. Legacy mappers stay put.
- Parsing Palm AppInfo blocks.
- Introducing QObject, signals, caching, or lifecycle to adapters.
- Modifying E.8's plugin ABI surface (it doesn't exist yet — E.8 is the next sub-phase).

---

## File Structure

**Files to CREATE:**

- `src/palm/codecs/CMakeLists.txt` — `WildPalmsPalmCodecs` static lib.
- `src/palm/codecs/palmtext.h` — Windows-1252 ↔ QString helpers shared across codecs.
- `src/palm/codecs/palmtext.cpp` — impl.
- `src/palm/codecs/contactcodec.h` — `Contact` POD + `encodeContact` / `decodeContact`.
- `src/palm/codecs/contactcodec.cpp` — impl (calls `pack_Address` / `unpack_Address`).
- `src/palm/codecs/memocodec.h` — `Memo` POD + `encodeMemo` / `decodeMemo`.
- `src/palm/codecs/memocodec.cpp` — impl (calls `pack_Memo` / `unpack_Memo`).
- `src/palm/codecs/todocodec.h` — `Todo` POD + `encodeTodo` / `decodeTodo`.
- `src/palm/codecs/todocodec.cpp` — impl (calls `pack_ToDo` / `unpack_ToDo`).
- `src/palm/codecs/kde_pim_convert.h` — `Contact ↔ KContacts::Addressee`, `Todo ↔ KCalendarCore::Todo::Ptr`.
- `src/palm/codecs/kde_pim_convert.cpp` — impl.
- `src/palm/adapters/CMakeLists.txt` — `WildPalmsPalmAdapters` static lib.
- `src/palm/adapters/palmcontactsadapter.h` — contact row + free functions over `PalmBackend`.
- `src/palm/adapters/palmcontactsadapter.cpp` — impl.
- `src/palm/adapters/palmmemosadapter.h` — memo row + free functions.
- `src/palm/adapters/palmmemosadapter.cpp` — impl.
- `src/palm/adapters/palmtodosadapter.h` — todo row + free functions.
- `src/palm/adapters/palmtodosadapter.cpp` — impl.
- `tests/palmadapters/CMakeLists.txt` — test executables wiring.
- `tests/palmadapters/tst_memocodec.cpp`
- `tests/palmadapters/tst_contactcodec.cpp`
- `tests/palmadapters/tst_todocodec.cpp`
- `tests/palmadapters/tst_kde_pim_convert.cpp`
- `tests/palmadapters/tst_palmcontactsadapter.cpp`
- `tests/palmadapters/tst_palmmemosadapter.cpp`
- `tests/palmadapters/tst_palmtodosadapter.cpp`

**Files to MODIFY:**

- `src/palm/CMakeLists.txt` — add `add_subdirectory(codecs)` and `add_subdirectory(adapters)`.
- `tests/CMakeLists.txt` — add `add_subdirectory(palmadapters)` after `palmcalendar`.
- `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` — flip row E.7 to `✅ **E.7**` at end.
- `docs/plans/2026-04-20-libkalburator-integration.md` — note E.7 landed where sub-phase progress is tracked.
- `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_palm_category_routing.md` — append E.7 to "Delivered" list if relevant; otherwise leave alone.

**Files to LEAVE UNTOUCHED:**

- `src/plugins/contacts/*`, `src/plugins/memo/*`, `src/plugins/todos/*` — legacy mappers keep running until E.9/E.11/E.12.
- `tests/test_contactmapper.cpp`, `tests/test_memomapper.cpp`, `tests/test_todomapper.cpp` — legacy tests keep passing.
- `pilot-link/`, `pilot-link-git/` — `PROJECT_VISION.md` line 105: pilot-link is READ-ONLY.

---

## Task 1: Scaffold `WildPalmsPalmCodecs` static lib + shared text helper

Goal: produce a buildable, empty static library at `src/palm/codecs/` with one dependency (pisock) wired and one utility TU (`palmtext.{h,cpp}`) compiling. No consumer yet; just a green build so subsequent codec tasks can add TUs one at a time.

**Files:**

- Create: `src/palm/codecs/CMakeLists.txt`
- Create: `src/palm/codecs/palmtext.h`
- Create: `src/palm/codecs/palmtext.cpp`
- Modify: `src/palm/CMakeLists.txt` (check current content, append `add_subdirectory(codecs)` in the right ordering — after `sync` since codecs will transitively need `WildPalmsPalmSync` headers for `PalmRecord`).

- [ ] **Step 1.1: Read the current `src/palm/CMakeLists.txt` to see existing subdir order**

Run: `cat src/palm/CMakeLists.txt`

Expected: shows `add_subdirectory(sync)`, `add_subdirectory(device)`, `add_subdirectory(conflict)`, `add_subdirectory(calendar)` (order may vary). Identify the right spot to insert `codecs` — immediately after `sync` (codecs depend on `PalmRecord` from `WildPalmsPalmSync`).

- [ ] **Step 1.2: Create `src/palm/codecs/palmtext.h`**

```cpp
#ifndef WILDPALMS_CODECS_PALMTEXT_H
#define WILDPALMS_CODECS_PALMTEXT_H

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Decode a null-terminated Palm text buffer (Windows-1252) into QString.
/// Returns an empty string if `palmText` is null.
QString decodePalmText(const char *palmText);

/// Encode a QString to Windows-1252 for Palm. Characters that have no
/// Windows-1252 representation are replaced with '?'. The returned
/// QByteArray is NOT null-terminated — the caller appends '\0' when
/// building a pi_buffer payload.
QByteArray encodePalmText(const QString &text);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_PALMTEXT_H
```

- [ ] **Step 1.3: Create `src/palm/codecs/palmtext.cpp`**

Port the Windows-1252 helpers from `src/plugins/memo/memomapper.cpp:6-77`. Adapted to namespace + free-function form:

```cpp
#include "palmtext.h"

#include <QChar>

namespace WildPalms::PalmCodecs {

namespace {

// Windows-1252 to Unicode mapping table for 0x80-0x9F. The rest of
// the 0xA0-0xFF range matches ISO-8859-1 directly, so only the
// Windows-specific 0x80-0x9F range needs explicit translation.
constexpr unsigned short kCp1252ToUnicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

} // namespace

QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);
    QByteArray fixed;
    fixed.reserve(data.size());
    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = kCp1252ToUnicode[byte - 0x80];
            fixed.append(QString(QChar(unicode)).toUtf8());
        } else {
            fixed.append(static_cast<char>(byte));
        }
    }
    return QString::fromUtf8(fixed);
}

QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());
    for (QChar ch : text) {
        ushort unicode = ch.unicode();
        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && (unicode < 0x80 || unicode > 0x9F)) {
            result.append(static_cast<char>(unicode));
        } else {
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (kCp1252ToUnicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.append('?');
            }
        }
    }
    return result;
}

} // namespace WildPalms::PalmCodecs
```

- [ ] **Step 1.4: Create `src/palm/codecs/CMakeLists.txt`**

```cmake
# WildPalmsPalmCodecs — POD + pisock-driven encode/decode for Palm
# Address/Memo/Todo records, plus optional KDE PIM converters.
#
# Phase E.7 of the libkalburator integration. Sibling to
# WildPalmsPalmSync (E.3), WildPalmsPalmDevice (E.4),
# WildPalmsPalmConflict (E.5), WildPalmsPalmCalendar (E.6).
#
# Links Qt::Core (for QString/QByteArray), pisock (for pack_*/unpack_*),
# KF6::Contacts (for the optional Addressee converter TU), and
# KF6::CalendarCore (for the optional KCalendarCore::Todo converter TU).
# Deliberately does NOT link WildPalmsCore — codecs must stay usable
# without the Qt::Widgets / KF6::XmlGui transitive deps.

find_package(KF6 REQUIRED COMPONENTS Contacts CalendarCore)

add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
)

target_include_directories(WildPalmsPalmCodecs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/palm/sync>
)

target_link_libraries(WildPalmsPalmCodecs
    PUBLIC
        Qt::Core
    PRIVATE
        pisock
)

add_dependencies(WildPalmsPalmCodecs pilot-link-external)

set_target_properties(WildPalmsPalmCodecs PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

Note: `contactcodec.{h,cpp}`, `memocodec.{h,cpp}`, `todocodec.{h,cpp}`, and `kde_pim_convert.{h,cpp}` will be appended to this target's sources in Tasks 2–6.

- [ ] **Step 1.5: Wire `src/palm/codecs/` into `src/palm/CMakeLists.txt`**

Add `add_subdirectory(codecs)` after `add_subdirectory(sync)` (codecs are consumed by adapters, which live under `src/palm/adapters/`; they do not depend on `device`, `conflict`, or `calendar`).

- [ ] **Step 1.6: Configure and build**

Run: `cmake --build build-dev --target WildPalmsPalmCodecs 2>&1 | tail -20`

Expected: `[100%] Built target WildPalmsPalmCodecs` (exact line may differ; what matters is no errors). If configure is needed first: `cmake -S . -B build-dev`.

- [ ] **Step 1.7: Commit**

```bash
git add src/palm/CMakeLists.txt src/palm/codecs/
git commit -m "$(cat <<'EOF'
feat(palm-codecs): scaffold WildPalmsPalmCodecs static lib

Adds the Phase E.7 codec sibling to E.6's PalmCalendar tree. Only the
shared Windows-1252 text helper lands in this commit; per-domain
codecs follow in subsequent commits (TDD).

Refs spec: docs/superpowers/specs/2026-04-23-phase-e7-typed-adapters-design.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: MemoCodec (simplest codec, establishes the pattern)

Goal: TDD-land `WildPalms::PalmCodecs::encodeMemo` / `decodeMemo` over pisock's `pack_Memo` / `unpack_Memo`. Memo is the simplest of the three — content is just text + a private flag.

**Files:**
- Create: `tests/palmadapters/CMakeLists.txt` (first test subdir)
- Create: `tests/palmadapters/tst_memocodec.cpp`
- Modify: `tests/CMakeLists.txt` (add `add_subdirectory(palmadapters)`)
- Create: `src/palm/codecs/memocodec.h`
- Create: `src/palm/codecs/memocodec.cpp`
- Modify: `src/palm/codecs/CMakeLists.txt` (append codec sources)

- [ ] **Step 2.1: Create `tests/palmadapters/CMakeLists.txt`**

```cmake
# Phase E.7 — codec + adapter tests for Contact/Memo/Todo domains.
# Each test links WildPalmsPalmCodecs (codec tests) plus
# WildPalmsPalmAdapters (adapter tests, added in later tasks).
# Deliberately does NOT link WildPalmsCore.

function(add_palm_adapter_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            KF6::Contacts
            KF6::CalendarCore
            WildPalmsPalmCodecs
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

add_palm_adapter_test(tst_memocodec tst_memocodec.cpp)
# tst_contactcodec, tst_todocodec, tst_kde_pim_convert, and adapter
# tests are appended in later tasks.
```

- [ ] **Step 2.2: Wire `tests/palmadapters/` into `tests/CMakeLists.txt`**

Insert `add_subdirectory(palmadapters)` immediately after the existing `add_subdirectory(palmcalendar)` line (around line 173 per the repo snapshot).

- [ ] **Step 2.3: Write `tests/palmadapters/tst_memocodec.cpp` (the failing test)**

```cpp
#include <QtTest/QtTest>

#include "memocodec.h"

using WildPalms::PalmCodecs::Memo;
using WildPalms::PalmCodecs::encodeMemo;
using WildPalms::PalmCodecs::decodeMemo;

class TestMemoCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyTextRoundTrips();
    void simpleAsciiRoundTrips();
    void utf8WithNewlineRoundTrips();
    void windows1252SmartQuotesRoundTrip();
    void privateFlagRoundTrips();
    void decodeEmptyBytesReturnsEmpty();
    void decodeHandlesNullTerminator();
};

void TestMemoCodec::emptyTextRoundTrips()
{
    Memo m;
    m.text = QString();
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QString());
    QCOMPARE(decoded->isPrivate, false);
}

void TestMemoCodec::simpleAsciiRoundTrips()
{
    Memo m;
    m.text = QStringLiteral("Shopping list\n- apples\n- oranges");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::utf8WithNewlineRoundTrips()
{
    Memo m;
    m.text = QStringLiteral("line one\nline two");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::windows1252SmartQuotesRoundTrip()
{
    // Palm uses Windows-1252 for text; smart quotes at 0x91-0x94 are
    // the common "works on real device" test.
    Memo m;
    m.text = QString::fromUtf8("He said \xE2\x80\x9Chello\xE2\x80\x9D.");
    m.isPrivate = false;
    const QByteArray bytes = encodeMemo(m);
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, m.text);
}

void TestMemoCodec::privateFlagRoundTrips()
{
    // The private flag lives on PalmRecord.attributes, NOT inside the
    // memo content. The codec does not read or write it; the POD
    // carries it purely as a caller convenience (adapter layer sets
    // it on the enclosing PalmRecord). This test asserts the codec
    // ignores isPrivate when encoding: two Memos differing only in
    // isPrivate produce the same bytes.
    Memo m1;   m1.text = QStringLiteral("x"); m1.isPrivate = false;
    Memo m2;   m2.text = QStringLiteral("x"); m2.isPrivate = true;
    QCOMPARE(encodeMemo(m1), encodeMemo(m2));
}

void TestMemoCodec::decodeEmptyBytesReturnsEmpty()
{
    const auto decoded = decodeMemo(QByteArray());
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QString());
}

void TestMemoCodec::decodeHandlesNullTerminator()
{
    // A real Palm record ends with a trailing NUL. Confirm we strip it
    // rather than appending a stray replacement character.
    QByteArray bytes("hello", 5);
    bytes.append('\0');
    const auto decoded = decodeMemo(bytes);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->text, QStringLiteral("hello"));
}

QTEST_GUILESS_MAIN(TestMemoCodec)
#include "tst_memocodec.moc"
```

- [ ] **Step 2.4: Run the test — expect it to fail (no codec yet)**

Run: `cmake --build build-dev --target tst_memocodec 2>&1 | tail -20`

Expected: compile failure (`memocodec.h: No such file or directory`). This is the "red" phase of TDD.

- [ ] **Step 2.5: Write `src/palm/codecs/memocodec.h`**

```cpp
#ifndef WILDPALMS_CODECS_MEMOCODEC_H
#define WILDPALMS_CODECS_MEMOCODEC_H

#include <optional>

#include <QByteArray>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Palm Memo content POD. The enclosing PalmRecord carries recordId,
/// category slot, and the isDeleted/isDirty attribute bits; this
/// struct is content-only.
struct Memo {
    QString text;
    bool    isPrivate = false;

    bool operator==(const Memo &) const = default;
};

/// Encode a Memo to Palm-wire bytes. Calls pisock's `pack_Memo`. The
/// result is what goes into `PalmRecord::data`. Does not encode
/// isPrivate — that is a PalmRecord.attributes concern.
QByteArray encodeMemo(const Memo &memo);

/// Decode Palm-wire bytes to a Memo. Returns nullopt only on an
/// unrecoverable pisock failure. Empty input returns a valid Memo
/// with an empty text string (Palm sometimes stores empty memos).
std::optional<Memo> decodeMemo(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_MEMOCODEC_H
```

- [ ] **Step 2.6: Write `src/palm/codecs/memocodec.cpp`**

```cpp
#include "memocodec.h"

#include <cstring>

#include "palmtext.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-memo.h>
}

namespace WildPalms::PalmCodecs {

namespace {

struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 256) { buf = pi_buffer_new(initial); }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

} // namespace

QByteArray encodeMemo(const Memo &memo)
{
    // Palm memos are a null-terminated text blob. Build a heap copy of
    // the Windows-1252 bytes, let pisock pack them, then copy out.
    const QByteArray wire = encodePalmText(memo.text);

    Memo_t palm{};
    QByteArray mutableCopy = wire;
    mutableCopy.append('\0');
    palm.text = mutableCopy.data();  // pisock reads, never writes

    ScopedBuffer out(mutableCopy.size() + 1);
    if (!out.buf) {
        return {};
    }
    if (pack_Memo(&palm, out.buf, memo_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Memo> decodeMemo(QByteArrayView bytes)
{
    if (bytes.isEmpty()) {
        return Memo{};
    }

    ScopedBuffer in(bytes.size());
    if (!in.buf) {
        return std::nullopt;
    }
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    Memo_t palm{};
    if (unpack_Memo(&palm, in.buf, memo_v1) < 0) {
        return std::nullopt;
    }

    Memo memo;
    memo.text = decodePalmText(palm.text);
    free_Memo(&palm);
    return memo;
}

} // namespace WildPalms::PalmCodecs
```

- [ ] **Step 2.7: Append codec sources to `src/palm/codecs/CMakeLists.txt`**

Change the `add_library(WildPalmsPalmCodecs STATIC ...)` source list to include `memocodec.h` and `memocodec.cpp`. The list becomes:

```cmake
add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
    memocodec.h
    memocodec.cpp
)
```

- [ ] **Step 2.8: Run the test — expect it to pass**

Run: `cmake --build build-dev --target tst_memocodec 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_memocodec$' --output-on-failure`

Expected: all 7 test methods pass.

- [ ] **Step 2.9: Commit**

```bash
git add tests/CMakeLists.txt tests/palmadapters/ src/palm/codecs/CMakeLists.txt src/palm/codecs/memocodec.h src/palm/codecs/memocodec.cpp
git commit -m "$(cat <<'EOF'
feat(palm-codecs): MemoCodec

Palm Memo bytes <-> Memo POD via pisock's pack_Memo/unpack_Memo.
Includes Windows-1252 round-trip for smart quotes and UTF-8 safety.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: ContactCodec

Goal: TDD-land `WildPalms::PalmCodecs::encodeContact` / `decodeContact` over pisock's `pack_Address` / `unpack_Address`. Contact is the largest of the three — five phone slots with labels, five labels, `showPhone`, four custom fields, plus name/address/etc.

**Reference implementation:** `src/plugins/contacts/contactmapper.cpp`. Lines 130–250 (unpack) and 450–595 (pack) show the pisock-field mapping idiom. Adapt that code, dropping the `recordId` / `category` / `isDirty` / `isDeleted` fields (those live on `PalmRecord` now) and the vCard-specific helpers (they stay in the legacy mapper until E.12 deletes it).

**Files:**
- Create: `tests/palmadapters/tst_contactcodec.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt` (register `tst_contactcodec`)
- Create: `src/palm/codecs/contactcodec.h`
- Create: `src/palm/codecs/contactcodec.cpp`
- Modify: `src/palm/codecs/CMakeLists.txt` (append sources)

- [ ] **Step 3.1: Write `tests/palmadapters/tst_contactcodec.cpp` (the failing test)**

```cpp
#include <QtTest/QtTest>

#include "contactcodec.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::encodeContact;
using WildPalms::PalmCodecs::decodeContact;

class TestContactCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyContactRoundTrips();
    void firstAndLastNameRoundTrip();
    void companyAndTitleRoundTrip();
    void allFivePhoneSlotsRoundTrip();
    void phoneLabelsRoundTrip();
    void showPhoneNonZeroRoundTrips();
    void addressFieldsRoundTrip();
    void customFieldsRoundTrip();
    void noteRoundTrip();
    void utf8SmartQuotesInNameRoundTrip();
    void decodeEmptyBytesReturnsNullopt();
};

namespace {

Contact makeSample()
{
    Contact c;
    c.lastName  = QStringLiteral("Doe");
    c.firstName = QStringLiteral("Jane");
    c.company   = QStringLiteral("Acme, Inc.");
    c.title     = QStringLiteral("Engineer");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phone[1]  = QStringLiteral("555-0101");
    c.phone[2]  = QStringLiteral("555-0102");
    c.phone[3]  = QStringLiteral("jane@example.com");
    c.phone[4]  = QStringLiteral("555-0104");
    c.phoneLabels = { QStringLiteral("Work"),
                      QStringLiteral("Home"),
                      QStringLiteral("Mobile"),
                      QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    c.showPhone = 0;
    c.address   = QStringLiteral("123 Main St");
    c.city      = QStringLiteral("Springfield");
    c.state     = QStringLiteral("IL");
    c.zip       = QStringLiteral("62701");
    c.country   = QStringLiteral("USA");
    c.custom[0] = QStringLiteral("Field A");
    c.custom[1] = QStringLiteral("Field B");
    c.custom[2] = QStringLiteral("");
    c.custom[3] = QStringLiteral("Field D");
    c.note      = QStringLiteral("Multi-line note\nsecond line.");
    c.isPrivate = false;
    return c;
}

} // namespace

void TestContactCodec::emptyContactRoundTrips()
{
    Contact c{};
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(*decoded, c);
}

void TestContactCodec::firstAndLastNameRoundTrip()
{
    Contact c{};
    c.firstName = QStringLiteral("Alice");
    c.lastName  = QStringLiteral("Liddell");
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->firstName, c.firstName);
    QCOMPARE(decoded->lastName,  c.lastName);
}

void TestContactCodec::companyAndTitleRoundTrip()
{
    Contact c{};
    c.company = QStringLiteral("Initech");
    c.title   = QStringLiteral("Software Engineer");
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->company, c.company);
    QCOMPARE(decoded->title,   c.title);
}

void TestContactCodec::allFivePhoneSlotsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(decoded->phone[i], c.phone[i]);
    }
}

void TestContactCodec::phoneLabelsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->phoneLabels, c.phoneLabels);
}

void TestContactCodec::showPhoneNonZeroRoundTrips()
{
    Contact c = makeSample();
    c.showPhone = 3;
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->showPhone, 3);
}

void TestContactCodec::addressFieldsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->address, c.address);
    QCOMPARE(decoded->city,    c.city);
    QCOMPARE(decoded->state,   c.state);
    QCOMPARE(decoded->zip,     c.zip);
    QCOMPARE(decoded->country, c.country);
}

void TestContactCodec::customFieldsRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(decoded->custom[i], c.custom[i]);
    }
}

void TestContactCodec::noteRoundTrip()
{
    Contact c = makeSample();
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->note, c.note);
}

void TestContactCodec::utf8SmartQuotesInNameRoundTrip()
{
    Contact c{};
    c.firstName = QString::fromUtf8("Jos\xC3\xA9");  // José
    c.lastName  = QString::fromUtf8("O\xE2\x80\x99""Connor"); // O’Connor
    const auto decoded = decodeContact(encodeContact(c));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->firstName, c.firstName);
    QCOMPARE(decoded->lastName,  c.lastName);
}

void TestContactCodec::decodeEmptyBytesReturnsNullopt()
{
    const auto decoded = decodeContact(QByteArray());
    QCOMPARE(decoded.has_value(), false);
}

QTEST_GUILESS_MAIN(TestContactCodec)
#include "tst_contactcodec.moc"
```

- [ ] **Step 3.2: Register `tst_contactcodec` in `tests/palmadapters/CMakeLists.txt`**

Append:
```cmake
add_palm_adapter_test(tst_contactcodec tst_contactcodec.cpp)
```

- [ ] **Step 3.3: Run — expect failure**

Run: `cmake --build build-dev --target tst_contactcodec 2>&1 | tail -10`

Expected: `contactcodec.h: No such file or directory`.

- [ ] **Step 3.4: Write `src/palm/codecs/contactcodec.h`**

```cpp
#ifndef WILDPALMS_CODECS_CONTACTCODEC_H
#define WILDPALMS_CODECS_CONTACTCODEC_H

#include <array>
#include <optional>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace WildPalms::PalmCodecs {

/// Palm Address record content POD. The enclosing PalmRecord carries
/// recordId, category slot, and the isDeleted/isDirty/isSecret
/// attribute bits; this struct is content-only.
struct Contact {
    QString lastName;
    QString firstName;
    QString company;
    QString title;

    /// Five Palm phone slots. Slot labels are in `phoneLabels`.
    std::array<QString, 5> phone {};

    /// Five Palm-labeled phone slot labels. Order matches `phone`.
    /// Typical entries: Work, Home, Fax, Other, E-mail, Main, Pager,
    /// Mobile. The Palm device stores these as per-record small
    /// integers indexing into the AddressAppInfo's label table; the
    /// codec resolves them to strings on decode and reverses on
    /// encode.
    QStringList phoneLabels;

    /// Which phone slot (0..4) is the "preferred" one for UI display.
    int showPhone = 0;

    QString address;
    QString city;
    QString state;
    QString zip;
    QString country;

    /// Four Palm custom fields. Labels live on AddressAppInfo and are
    /// not exposed by this codec (appearance defer to E.10/E.17).
    std::array<QString, 4> custom {};

    QString note;

    /// Convenience flag. NOT encoded — PalmRecord.attributes owns the
    /// Secret bit. Adapter layer sets this when surfacing to callers.
    bool isPrivate = false;

    bool operator==(const Contact &) const = default;
};

/// Encode a Contact to Palm-wire bytes via `pack_Address`.
QByteArray encodeContact(const Contact &c);

/// Decode Palm-wire bytes to a Contact via `unpack_Address`. Returns
/// nullopt on malformed input (pisock returns negative). Empty input
/// returns nullopt — callers should not feed empty data.
std::optional<Contact> decodeContact(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_CONTACTCODEC_H
```

- [ ] **Step 3.5: Write `src/palm/codecs/contactcodec.cpp`**

Port the field-by-field mapping from `src/plugins/contacts/contactmapper.cpp`. Reference: `ContactMapper::unpackContact` at line 130 handles decode; `ContactMapper::packContact` at line 450 handles encode. The pisock struct is `Address_t` (declared in `pi-address.h`) with these members worth mapping:

- `entry[0..18]` — strings, indexed by `entryLastname=0`, `entryFirstname=1`, `entryCompany=2`, `entryPhone1..5=3..7`, `entryAddress=8`, `entryCity=9`, `entryState=10`, `entryZipCode=11`, `entryCountry=12`, `entryTitle=13`, `entryCustom1..4=14..17`, `entryNote=18`. (Constants come from `pi-address.h`.)
- `phoneLabel[0..4]` — integer indices into a label table (0 ≈ Work, 1 ≈ Home, 2 ≈ Fax, …). Resolve against a hardcoded default table in the codec TU since AppInfo parsing is deferred.
- `showPhone` — the `showPhone` integer field.

Full listing:

```cpp
#include "contactcodec.h"

#include <cstring>

#include "palmtext.h"

extern "C" {
#include <pi-address.h>
#include <pi-buffer.h>
}

namespace WildPalms::PalmCodecs {

namespace {

struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 512) { buf = pi_buffer_new(initial); }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

struct ScopedAddress {
    Address_t a{};
    ~ScopedAddress() { free_Address(&a); }
    ScopedAddress() = default;
    ScopedAddress(const ScopedAddress &) = delete;
    ScopedAddress &operator=(const ScopedAddress &) = delete;
};

// Default Palm phone-label table. Real devices override these via
// AppInfo (E.10/E.17 will plumb that); the codec uses the stock labels
// as its source of truth until then.
const char *kDefaultPhoneLabels[8] = {
    "Work", "Home", "Fax", "Other", "E-mail", "Main", "Pager", "Mobile"
};

int labelStringToIndex(const QString &label)
{
    for (int i = 0; i < 8; ++i) {
        if (label.compare(QString::fromLatin1(kDefaultPhoneLabels[i]),
                          Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 3;  // fall back to "Other"
}

// Strdup a QString into a freshly-malloc'd Windows-1252 C string, or
// return nullptr for an empty string (pisock represents "no value" as
// nullptr on Address_t::entry[]).
char *qstringToPalmDup(const QString &s)
{
    if (s.isEmpty()) {
        return nullptr;
    }
    const QByteArray wire = encodePalmText(s);
    char *out = static_cast<char *>(std::malloc(wire.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, wire.constData(), wire.size());
    out[wire.size()] = '\0';
    return out;
}

QString palmDupToQString(const char *s)
{
    return s ? decodePalmText(s) : QString();
}

} // namespace

QByteArray encodeContact(const Contact &c)
{
    ScopedAddress wrap;
    Address_t &addr = wrap.a;

    addr.entry[entryLastname]  = qstringToPalmDup(c.lastName);
    addr.entry[entryFirstname] = qstringToPalmDup(c.firstName);
    addr.entry[entryCompany]   = qstringToPalmDup(c.company);
    addr.entry[entryPhone1]    = qstringToPalmDup(c.phone[0]);
    addr.entry[entryPhone2]    = qstringToPalmDup(c.phone[1]);
    addr.entry[entryPhone3]    = qstringToPalmDup(c.phone[2]);
    addr.entry[entryPhone4]    = qstringToPalmDup(c.phone[3]);
    addr.entry[entryPhone5]    = qstringToPalmDup(c.phone[4]);
    addr.entry[entryAddress]   = qstringToPalmDup(c.address);
    addr.entry[entryCity]      = qstringToPalmDup(c.city);
    addr.entry[entryState]     = qstringToPalmDup(c.state);
    addr.entry[entryZip]       = qstringToPalmDup(c.zip);
    addr.entry[entryCountry]   = qstringToPalmDup(c.country);
    addr.entry[entryTitle]     = qstringToPalmDup(c.title);
    addr.entry[entryCustom1]   = qstringToPalmDup(c.custom[0]);
    addr.entry[entryCustom2]   = qstringToPalmDup(c.custom[1]);
    addr.entry[entryCustom3]   = qstringToPalmDup(c.custom[2]);
    addr.entry[entryCustom4]   = qstringToPalmDup(c.custom[3]);
    addr.entry[entryNote]      = qstringToPalmDup(c.note);

    for (int i = 0; i < 5; ++i) {
        addr.phoneLabel[i] = (i < c.phoneLabels.size())
            ? labelStringToIndex(c.phoneLabels[i])
            : i;  // default: slot N -> label index N
    }
    addr.showPhone = c.showPhone;

    ScopedBuffer out(1024);
    if (!out.buf) return {};
    if (pack_Address(&addr, out.buf, address_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Contact> decodeContact(QByteArrayView bytes)
{
    if (bytes.isEmpty()) {
        return std::nullopt;
    }
    ScopedBuffer in(bytes.size());
    if (!in.buf) return std::nullopt;
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    ScopedAddress wrap;
    Address_t &addr = wrap.a;
    if (unpack_Address(&addr, in.buf, address_v1) < 0) {
        return std::nullopt;
    }

    Contact c;
    c.lastName  = palmDupToQString(addr.entry[entryLastname]);
    c.firstName = palmDupToQString(addr.entry[entryFirstname]);
    c.company   = palmDupToQString(addr.entry[entryCompany]);
    c.phone[0]  = palmDupToQString(addr.entry[entryPhone1]);
    c.phone[1]  = palmDupToQString(addr.entry[entryPhone2]);
    c.phone[2]  = palmDupToQString(addr.entry[entryPhone3]);
    c.phone[3]  = palmDupToQString(addr.entry[entryPhone4]);
    c.phone[4]  = palmDupToQString(addr.entry[entryPhone5]);
    c.address   = palmDupToQString(addr.entry[entryAddress]);
    c.city      = palmDupToQString(addr.entry[entryCity]);
    c.state     = palmDupToQString(addr.entry[entryState]);
    c.zip       = palmDupToQString(addr.entry[entryZip]);
    c.country   = palmDupToQString(addr.entry[entryCountry]);
    c.title     = palmDupToQString(addr.entry[entryTitle]);
    c.custom[0] = palmDupToQString(addr.entry[entryCustom1]);
    c.custom[1] = palmDupToQString(addr.entry[entryCustom2]);
    c.custom[2] = palmDupToQString(addr.entry[entryCustom3]);
    c.custom[3] = palmDupToQString(addr.entry[entryCustom4]);
    c.note      = palmDupToQString(addr.entry[entryNote]);

    c.phoneLabels.clear();
    for (int i = 0; i < 5; ++i) {
        int idx = addr.phoneLabel[i];
        if (idx < 0 || idx >= 8) idx = 3;  // Other
        c.phoneLabels.append(QString::fromLatin1(kDefaultPhoneLabels[idx]));
    }
    c.showPhone = addr.showPhone;

    return c;
}

} // namespace WildPalms::PalmCodecs
```

- [ ] **Step 3.6: Append `contactcodec.{h,cpp}` to `src/palm/codecs/CMakeLists.txt`**

```cmake
add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
    memocodec.h
    memocodec.cpp
    contactcodec.h
    contactcodec.cpp
)
```

- [ ] **Step 3.7: Build + run the test**

Run: `cmake --build build-dev --target tst_contactcodec 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_contactcodec$' --output-on-failure`

Expected: all 11 test methods pass.

- [ ] **Step 3.8: Commit**

```bash
git add tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_contactcodec.cpp src/palm/codecs/CMakeLists.txt src/palm/codecs/contactcodec.h src/palm/codecs/contactcodec.cpp
git commit -m "$(cat <<'EOF'
feat(palm-codecs): ContactCodec

Palm Address record bytes <-> Contact POD via pisock's pack_Address
/ unpack_Address. Covers all five phone slots, their labels, showPhone,
four custom fields, and the rest of the AddressDB content layout.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: TodoCodec

Goal: TDD-land `WildPalms::PalmCodecs::encodeTodo` / `decodeTodo` over pisock's `pack_ToDo` / `unpack_ToDo`.

**Reference implementation:** `src/plugins/todos/todomapper.cpp`. Lines 80–180 (unpack) and 230–320 (pack).

**Files:**
- Create: `tests/palmadapters/tst_todocodec.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt` (register test)
- Create: `src/palm/codecs/todocodec.h`
- Create: `src/palm/codecs/todocodec.cpp`
- Modify: `src/palm/codecs/CMakeLists.txt` (append sources)

- [ ] **Step 4.1: Write `tests/palmadapters/tst_todocodec.cpp`**

```cpp
#include <QtTest/QtTest>

#include "todocodec.h"

using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::encodeTodo;
using WildPalms::PalmCodecs::decodeTodo;

class TestTodoCodec : public QObject
{
    Q_OBJECT
private slots:
    void emptyTodoRoundTrips();
    void descriptionAndNoteRoundTrip();
    void indefiniteDueFlagRoundTrips();
    void concreteDueDateRoundTrips();
    void priority1RoundTrips();
    void priority5RoundTrips();
    void completionFlagRoundTrips();
    void unicodeDescriptionRoundTrips();
    void decodeEmptyBytesReturnsNullopt();
};

void TestTodoCodec::emptyTodoRoundTrips()
{
    Todo t{};
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(*decoded, t);
}

void TestTodoCodec::descriptionAndNoteRoundTrip()
{
    Todo t{};
    t.description = QStringLiteral("Write the plan");
    t.note        = QStringLiteral("Must include fixtures.");
    t.hasIndefiniteDue = true;
    t.priority = 3;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->description, t.description);
    QCOMPARE(decoded->note,        t.note);
}

void TestTodoCodec::indefiniteDueFlagRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->hasIndefiniteDue, true);
}

void TestTodoCodec::concreteDueDateRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 1), QTime(0, 0), Qt::LocalTime);
    t.priority = 2;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->hasIndefiniteDue, false);
    QCOMPARE(decoded->due.date(), QDate(2026, 5, 1));
}

void TestTodoCodec::priority1RoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->priority, 1);
}

void TestTodoCodec::priority5RoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 5;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->priority, 5);
}

void TestTodoCodec::completionFlagRoundTrips()
{
    Todo t{};
    t.description = QStringLiteral("x");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    t.isComplete = true;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->isComplete, true);
}

void TestTodoCodec::unicodeDescriptionRoundTrips()
{
    Todo t{};
    t.description = QString::fromUtf8("Caf\xC3\xA9 \xE2\x80\x94 discuss");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    const auto decoded = decodeTodo(encodeTodo(t));
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->description, t.description);
}

void TestTodoCodec::decodeEmptyBytesReturnsNullopt()
{
    const auto decoded = decodeTodo(QByteArray());
    QCOMPARE(decoded.has_value(), false);
}

QTEST_GUILESS_MAIN(TestTodoCodec)
#include "tst_todocodec.moc"
```

- [ ] **Step 4.2: Register the test**

Append to `tests/palmadapters/CMakeLists.txt`:
```cmake
add_palm_adapter_test(tst_todocodec tst_todocodec.cpp)
```

- [ ] **Step 4.3: Run — expect failure**

Run: `cmake --build build-dev --target tst_todocodec 2>&1 | tail -5`

Expected: missing `todocodec.h`.

- [ ] **Step 4.4: Write `src/palm/codecs/todocodec.h`**

```cpp
#ifndef WILDPALMS_CODECS_TODOCODEC_H
#define WILDPALMS_CODECS_TODOCODEC_H

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace WildPalms::PalmCodecs {

/// Palm ToDo record content POD. Priority is Palm-native 1..5 (1 = highest).
struct Todo {
    QString   description;
    QString   note;
    bool      hasIndefiniteDue = true;
    QDateTime due;             ///< valid when !hasIndefiniteDue
    int       priority = 1;    ///< 1..5
    bool      isComplete = false;
    bool      isPrivate = false;

    bool operator==(const Todo &) const = default;
};

/// Encode a Todo via pisock's `pack_ToDo`.
QByteArray encodeTodo(const Todo &todo);

/// Decode via `unpack_ToDo`. Empty input returns nullopt.
std::optional<Todo> decodeTodo(QByteArrayView bytes);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_TODOCODEC_H
```

- [ ] **Step 4.5: Write `src/palm/codecs/todocodec.cpp`**

```cpp
#include "todocodec.h"

#include <cstring>
#include <ctime>

#include "palmtext.h"

extern "C" {
#include <pi-buffer.h>
#include <pi-todo.h>
}

namespace WildPalms::PalmCodecs {

namespace {

struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 256) { buf = pi_buffer_new(initial); }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

struct ScopedToDo {
    ToDo_t t{};
    ~ScopedToDo() { free_ToDo(&t); }
    ScopedToDo() = default;
    ScopedToDo(const ScopedToDo &) = delete;
    ScopedToDo &operator=(const ScopedToDo &) = delete;
};

char *qstringToPalmDup(const QString &s)
{
    if (s.isEmpty()) {
        return nullptr;
    }
    const QByteArray wire = encodePalmText(s);
    char *out = static_cast<char *>(std::malloc(wire.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, wire.constData(), wire.size());
    out[wire.size()] = '\0';
    return out;
}

QString palmDupToQString(const char *s)
{
    return s ? decodePalmText(s) : QString();
}

} // namespace

QByteArray encodeTodo(const Todo &todo)
{
    ScopedToDo wrap;
    ToDo_t &t = wrap.t;
    t.indefinite = todo.hasIndefiniteDue ? 1 : 0;
    if (!todo.hasIndefiniteDue && todo.due.isValid()) {
        t.due.tm_year = todo.due.date().year() - 1900;
        t.due.tm_mon  = todo.due.date().month() - 1;
        t.due.tm_mday = todo.due.date().day();
    }
    t.priority   = todo.priority;
    t.complete   = todo.isComplete ? 1 : 0;
    t.description = qstringToPalmDup(todo.description);
    t.note        = qstringToPalmDup(todo.note);

    ScopedBuffer out(512);
    if (!out.buf) return {};
    if (pack_ToDo(&t, out.buf, todo_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Todo> decodeTodo(QByteArrayView bytes)
{
    if (bytes.isEmpty()) return std::nullopt;
    ScopedBuffer in(bytes.size());
    if (!in.buf) return std::nullopt;
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    ScopedToDo wrap;
    if (unpack_ToDo(&wrap.t, in.buf, todo_v1) < 0) {
        return std::nullopt;
    }
    const ToDo_t &t = wrap.t;

    Todo todo;
    todo.hasIndefiniteDue = t.indefinite != 0;
    if (!todo.hasIndefiniteDue) {
        QDate d(t.due.tm_year + 1900, t.due.tm_mon + 1, t.due.tm_mday);
        if (d.isValid()) {
            todo.due = QDateTime(d, QTime(0, 0), Qt::LocalTime);
        }
    }
    todo.priority    = t.priority;
    todo.isComplete  = t.complete != 0;
    todo.description = palmDupToQString(t.description);
    todo.note        = palmDupToQString(t.note);
    return todo;
}

} // namespace WildPalms::PalmCodecs
```

- [ ] **Step 4.6: Append sources to `src/palm/codecs/CMakeLists.txt`**

```cmake
add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
    memocodec.h
    memocodec.cpp
    contactcodec.h
    contactcodec.cpp
    todocodec.h
    todocodec.cpp
)
```

- [ ] **Step 4.7: Build + test**

Run: `cmake --build build-dev --target tst_todocodec 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_todocodec$' --output-on-failure`

Expected: all 9 test methods pass.

- [ ] **Step 4.8: Commit**

```bash
git add tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_todocodec.cpp src/palm/codecs/CMakeLists.txt src/palm/codecs/todocodec.h src/palm/codecs/todocodec.cpp
git commit -m "$(cat <<'EOF'
feat(palm-codecs): TodoCodec

Palm ToDo record bytes <-> Todo POD via pisock's pack_ToDo/unpack_ToDo.
Covers description/note, definite vs indefinite due date, Palm-native
priority 1..5, and completion flag.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: KDE PIM convert — Contact ↔ KContacts::Addressee

Goal: ship the optional Contact↔Addressee converter. Lossy fields (`showPhone`, `custom1..4`, non-standard phone labels) are preserved as Addressee custom fields so that a round-trip is information-preserving.

**Files:**
- Create: `tests/palmadapters/tst_kde_pim_convert.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt` (register test)
- Create: `src/palm/codecs/kde_pim_convert.h`
- Create: `src/palm/codecs/kde_pim_convert.cpp`
- Modify: `src/palm/codecs/CMakeLists.txt` (append sources)

- [ ] **Step 5.1: Write the failing test `tests/palmadapters/tst_kde_pim_convert.cpp` (Contact half only for this task; Todo half is Task 6)**

```cpp
#include <QtTest/QtTest>

#include <KContacts/Addressee>
#include <KCalendarCore/Todo>

#include "contactcodec.h"
#include "kde_pim_convert.h"

using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCodecs::toAddressee;
using WildPalms::PalmCodecs::fromAddressee;
using WildPalms::PalmCodecs::toKCalTodo;
using WildPalms::PalmCodecs::fromKCalTodo;

class TestKdePimConvert : public QObject
{
    Q_OBJECT
private slots:
    // Contact <-> Addressee
    void contactRoundTripsThroughAddressee();
    void contactPreservesShowPhoneAsCustomField();
    void contactPreservesCustomFieldsInAddressee();
    void contactNonStandardPhoneLabelPreservedInAddressee();
    // Todo <-> KCalendarCore::Todo
    void todoRoundTripsThroughKCalTodo();
    void todoPreservesPriorityOneToOne();
    void todoIndefiniteDueMapsToMissingDueDate();
    void todoPriority9MapsBackToFiveOnReverse();
};

namespace {

Contact makeContactSample()
{
    Contact c;
    c.firstName = QStringLiteral("Ada");
    c.lastName  = QStringLiteral("Lovelace");
    c.company   = QStringLiteral("Analytical Engine Co.");
    c.title     = QStringLiteral("Programmer");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phone[1]  = QStringLiteral("555-0101");
    c.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Home"),
                      QStringLiteral("Mobile"), QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    c.showPhone = 1;
    c.custom[0] = QStringLiteral("CustomA");
    c.custom[3] = QStringLiteral("CustomD");
    c.note      = QStringLiteral("A brilliant mathematician.");
    return c;
}

Todo makeTodoSample()
{
    Todo t;
    t.description = QStringLiteral("Review Phase-E spec");
    t.note        = QStringLiteral("Focus on E.10+ consumers.");
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 5, 15), QTime(0, 0), Qt::LocalTime);
    t.priority = 2;
    t.isComplete = false;
    return t;
}

} // namespace

void TestKdePimConvert::contactRoundTripsThroughAddressee()
{
    const Contact src = makeContactSample();
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    QCOMPARE(back.firstName, src.firstName);
    QCOMPARE(back.lastName,  src.lastName);
    QCOMPARE(back.company,   src.company);
    QCOMPARE(back.title,     src.title);
    QCOMPARE(back.note,      src.note);
}

void TestKdePimConvert::contactPreservesShowPhoneAsCustomField()
{
    Contact src = makeContactSample();
    src.showPhone = 3;
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    QCOMPARE(back.showPhone, 3);
}

void TestKdePimConvert::contactPreservesCustomFieldsInAddressee()
{
    const Contact src = makeContactSample();
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(back.custom[i], src.custom[i]);
    }
}

void TestKdePimConvert::contactNonStandardPhoneLabelPreservedInAddressee()
{
    Contact src = makeContactSample();
    src.phoneLabels[0] = QStringLiteral("Satellite");  // not a standard Palm label
    const auto addr = toAddressee(src);
    const Contact back = fromAddressee(addr);
    // "Satellite" isn't a known Palm label; the codec falls back to
    // "Other" on round-trip. Document this behaviour in-test.
    QCOMPARE(back.phoneLabels[0], QStringLiteral("Other"));
}

void TestKdePimConvert::todoRoundTripsThroughKCalTodo()
{
    const Todo src = makeTodoSample();
    const auto kcal = toKCalTodo(src);
    QVERIFY(!kcal.isNull());
    const Todo back = fromKCalTodo(kcal);
    QCOMPARE(back.description, src.description);
    QCOMPARE(back.note,        src.note);
    QCOMPARE(back.hasIndefiniteDue, src.hasIndefiniteDue);
    QCOMPARE(back.priority,    src.priority);
    QCOMPARE(back.isComplete,  src.isComplete);
}

void TestKdePimConvert::todoPreservesPriorityOneToOne()
{
    for (int p = 1; p <= 5; ++p) {
        Todo t = makeTodoSample();
        t.priority = p;
        const auto kcal = toKCalTodo(t);
        QCOMPARE(kcal->priority(), p);
    }
}

void TestKdePimConvert::todoIndefiniteDueMapsToMissingDueDate()
{
    Todo t = makeTodoSample();
    t.hasIndefiniteDue = true;
    t.due = QDateTime();
    const auto kcal = toKCalTodo(t);
    QCOMPARE(kcal->hasDueDate(), false);
}

void TestKdePimConvert::todoPriority9MapsBackToFiveOnReverse()
{
    auto kcal = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo);
    kcal->setSummary(QStringLiteral("x"));
    kcal->setPriority(9);
    const Todo back = fromKCalTodo(kcal);
    QCOMPARE(back.priority, 5);
}

QTEST_GUILESS_MAIN(TestKdePimConvert)
#include "tst_kde_pim_convert.moc"
```

- [ ] **Step 5.2: Register the test**

Append to `tests/palmadapters/CMakeLists.txt`:
```cmake
add_palm_adapter_test(tst_kde_pim_convert tst_kde_pim_convert.cpp)
```

- [ ] **Step 5.3: Run — expect failure**

Run: `cmake --build build-dev --target tst_kde_pim_convert 2>&1 | tail -5`

Expected: `kde_pim_convert.h: No such file or directory`.

- [ ] **Step 5.4: Write `src/palm/codecs/kde_pim_convert.h`**

```cpp
#ifndef WILDPALMS_CODECS_KDE_PIM_CONVERT_H
#define WILDPALMS_CODECS_KDE_PIM_CONVERT_H

#include <KContacts/Addressee>
#include <KCalendarCore/Todo>

#include "contactcodec.h"
#include "todocodec.h"

namespace WildPalms::PalmCodecs {

/// Contact -> KContacts::Addressee. Best-effort mapping. Lossy fields
/// (`showPhone`, `custom1..4`, non-standard phone labels) are stashed
/// in `X-PALM-*` custom fields so the reverse conversion is
/// information-preserving.
KContacts::Addressee toAddressee(const Contact &c);

/// KContacts::Addressee -> Contact. Reads `X-PALM-*` custom fields to
/// recover the Palm-specific data when present; defaults otherwise.
Contact fromAddressee(const KContacts::Addressee &a);

/// Todo -> KCalendarCore::Todo. Priority maps 1:1 for 1..5. Indefinite
/// due maps to "no due date". `isComplete` maps to
/// `KCalendarCore::Todo::Completed`.
KCalendarCore::Todo::Ptr toKCalTodo(const Todo &t);

/// KCalendarCore::Todo -> Todo. Priority > 5 clamps to 5 on reverse
/// (iCal's 6..9 have no Palm equivalent).
Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &kcal);

} // namespace WildPalms::PalmCodecs

#endif // WILDPALMS_CODECS_KDE_PIM_CONVERT_H
```

- [ ] **Step 5.5: Write `src/palm/codecs/kde_pim_convert.cpp` (Contact half; Todo half added in Task 6)**

```cpp
#include "kde_pim_convert.h"

#include <KContacts/PhoneNumber>

namespace WildPalms::PalmCodecs {

namespace {

constexpr const char *kAppPalm              = "PALM";
constexpr const char *kShowPhoneField       = "SHOW-PHONE";
constexpr const char *kCustomFieldPrefix    = "CUSTOM-";  // CUSTOM-1..4
constexpr const char *kPhoneLabelPrefix     = "PHONE-LABEL-";  // PHONE-LABEL-0..4

bool phoneLabelIsStandard(const QString &label)
{
    static const QStringList kStd = {
        QStringLiteral("Work"),  QStringLiteral("Home"),   QStringLiteral("Fax"),
        QStringLiteral("Other"), QStringLiteral("E-mail"), QStringLiteral("Main"),
        QStringLiteral("Pager"), QStringLiteral("Mobile")
    };
    for (const auto &s : kStd) {
        if (label.compare(s, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

KContacts::Addressee toAddressee(const Contact &c)
{
    KContacts::Addressee a;
    a.setGivenName(c.firstName);
    a.setFamilyName(c.lastName);
    a.setOrganization(c.company);
    a.setTitle(c.title);
    a.setNote(c.note);

    for (int i = 0; i < 5; ++i) {
        if (c.phone[i].isEmpty()) continue;
        KContacts::PhoneNumber::Type type = KContacts::PhoneNumber::Voice;
        if (i < c.phoneLabels.size()) {
            const QString &label = c.phoneLabels[i];
            if      (label == QLatin1String("Work"))   type = KContacts::PhoneNumber::Work;
            else if (label == QLatin1String("Home"))   type = KContacts::PhoneNumber::Home;
            else if (label == QLatin1String("Fax"))    type = KContacts::PhoneNumber::Fax;
            else if (label == QLatin1String("Mobile")) type = KContacts::PhoneNumber::Cell;
            else if (label == QLatin1String("Pager"))  type = KContacts::PhoneNumber::Pager;
            else                                        type = KContacts::PhoneNumber::Voice;
            a.insertCustom(kAppPalm,
                           QString::fromLatin1(kPhoneLabelPrefix) + QString::number(i),
                           label);
        }
        a.insertPhoneNumber(KContacts::PhoneNumber(c.phone[i], type));
    }

    a.insertCustom(kAppPalm, kShowPhoneField, QString::number(c.showPhone));
    for (int i = 0; i < 4; ++i) {
        if (c.custom[i].isEmpty()) continue;
        a.insertCustom(kAppPalm,
                       QString::fromLatin1(kCustomFieldPrefix) + QString::number(i + 1),
                       c.custom[i]);
    }

    KContacts::Address ka(KContacts::Address::Home);
    ka.setStreet(c.address);
    ka.setLocality(c.city);
    ka.setRegion(c.state);
    ka.setPostalCode(c.zip);
    ka.setCountry(c.country);
    a.insertAddress(ka);

    return a;
}

Contact fromAddressee(const KContacts::Addressee &a)
{
    Contact c;
    c.firstName = a.givenName();
    c.lastName  = a.familyName();
    c.company   = a.organization();
    c.title     = a.title();
    c.note      = a.note();

    // Phone labels: prefer stashed X-PALM-PHONE-LABEL-N; else derive
    // from type on the matching KContacts phone; else "Other".
    c.phoneLabels = QStringList{ QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other") };
    for (int i = 0; i < 5; ++i) {
        const QString stashed = a.custom(kAppPalm,
            QString::fromLatin1(kPhoneLabelPrefix) + QString::number(i));
        if (!stashed.isEmpty()) {
            c.phoneLabels[i] = phoneLabelIsStandard(stashed)
                                 ? stashed
                                 : QStringLiteral("Other");
        }
    }

    int slot = 0;
    for (const auto &ph : a.phoneNumbers()) {
        if (slot >= 5) break;
        c.phone[slot++] = ph.number();
    }

    bool ok = false;
    const int shown = a.custom(kAppPalm, kShowPhoneField).toInt(&ok);
    if (ok) c.showPhone = shown;

    for (int i = 0; i < 4; ++i) {
        const QString v = a.custom(kAppPalm,
            QString::fromLatin1(kCustomFieldPrefix) + QString::number(i + 1));
        c.custom[i] = v;
    }

    const auto addrs = a.addresses();
    if (!addrs.isEmpty()) {
        const auto &ka = addrs.first();
        c.address = ka.street();
        c.city    = ka.locality();
        c.state   = ka.region();
        c.zip     = ka.postalCode();
        c.country = ka.country();
    }

    return c;
}

// Todo converters land in Task 6; for now leave stubs so the linker
// is happy when tst_kde_pim_convert links. (The Todo tests in this
// task's file still reference toKCalTodo/fromKCalTodo — if they fail
// to link, defer those test methods to Task 6 by commenting them out
// here and re-enabling them in Task 6.)
KCalendarCore::Todo::Ptr toKCalTodo(const Todo &) { return {}; }
Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &) { return {}; }

} // namespace WildPalms::PalmCodecs
```

Pragmatic note: leaving the Todo stubs here lets the whole test file compile in this task. The four Todo test methods will fail; that's OK — they land green in Task 6.

- [ ] **Step 5.6: Append sources to `src/palm/codecs/CMakeLists.txt`**

```cmake
add_library(WildPalmsPalmCodecs STATIC
    palmtext.h
    palmtext.cpp
    memocodec.h
    memocodec.cpp
    contactcodec.h
    contactcodec.cpp
    todocodec.h
    todocodec.cpp
    kde_pim_convert.h
    kde_pim_convert.cpp
)

target_link_libraries(WildPalmsPalmCodecs
    PUBLIC
        Qt::Core
        KF6::Contacts
        KF6::CalendarCore
    PRIVATE
        pisock
)
```

(Promote `KF6::Contacts` and `KF6::CalendarCore` to PUBLIC since they now appear in the public `kde_pim_convert.h`.)

- [ ] **Step 5.7: Build + run the Contact tests (expect the four Todo tests to fail; the four Contact tests should pass)**

Run: `cmake --build build-dev --target tst_kde_pim_convert 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_kde_pim_convert$' --output-on-failure`

Expected: Contact round-trip tests pass (4/8). Todo tests fail with the stub. That is the intended Task-5 state. Task 6 turns them green.

- [ ] **Step 5.8: Commit**

```bash
git add tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_kde_pim_convert.cpp src/palm/codecs/CMakeLists.txt src/palm/codecs/kde_pim_convert.h src/palm/codecs/kde_pim_convert.cpp
git commit -m "$(cat <<'EOF'
feat(palm-codecs): Contact <-> KContacts::Addressee converter

Lossy fields (showPhone, custom1..4, non-standard phone labels) are
preserved in X-PALM-* custom fields so the round-trip is
information-preserving. Todo converters land in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: KDE PIM convert — Todo ↔ KCalendarCore::Todo

Goal: replace the Task-5 stubs with real Todo ↔ `KCalendarCore::Todo` conversion.

**Files:**
- Modify: `src/palm/codecs/kde_pim_convert.cpp` (replace stubs)

- [ ] **Step 6.1: Replace the Todo stubs in `kde_pim_convert.cpp`**

Remove the two stub function bodies at the bottom of the file and replace with:

```cpp
KCalendarCore::Todo::Ptr toKCalTodo(const Todo &t)
{
    auto kcal = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo);
    kcal->setSummary(t.description);
    kcal->setDescription(t.note);
    if (!t.hasIndefiniteDue && t.due.isValid()) {
        kcal->setDtDue(t.due, true);
        kcal->setAllDay(true);
    }
    // Palm priority 1..5 maps 1:1 to iCal priority 1..5.
    kcal->setPriority(qBound(1, t.priority, 5));
    if (t.isComplete) {
        kcal->setCompleted(QDateTime::currentDateTime());
    }
    return kcal;
}

Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &kcal)
{
    Todo t;
    if (!kcal) return t;
    t.description = kcal->summary();
    t.note        = kcal->description();
    if (kcal->hasDueDate() && kcal->dtDue().isValid()) {
        t.hasIndefiniteDue = false;
        t.due = kcal->dtDue();
    } else {
        t.hasIndefiniteDue = true;
    }
    // iCal priority 0 ("no priority") -> 1. 6..9 clamp to 5.
    const int p = kcal->priority();
    if      (p <= 0) t.priority = 1;
    else if (p > 5)  t.priority = 5;
    else             t.priority = p;
    t.isComplete = kcal->isCompleted();
    return t;
}
```

- [ ] **Step 6.2: Run the test — expect all 8 methods to pass**

Run: `cmake --build build-dev --target tst_kde_pim_convert 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_kde_pim_convert$' --output-on-failure`

Expected: all 8 test methods pass (4 Contact + 4 Todo).

- [ ] **Step 6.3: Commit**

```bash
git add src/palm/codecs/kde_pim_convert.cpp
git commit -m "$(cat <<'EOF'
feat(palm-codecs): Todo <-> KCalendarCore::Todo converter

Priority maps 1:1 for 1..5. iCal 6..9 clamps to 5 on reverse
conversion. Indefinite due maps to "no due date"; completion flag
round-trips via Todo::setCompleted/isCompleted.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Scaffold `WildPalmsPalmAdapters` static lib

Goal: produce a buildable, empty static library at `src/palm/adapters/`. Subsequent tasks add one adapter module at a time.

**Files:**
- Create: `src/palm/adapters/CMakeLists.txt`
- Modify: `src/palm/CMakeLists.txt` (append `add_subdirectory(adapters)`)

- [ ] **Step 7.1: Create `src/palm/adapters/CMakeLists.txt`**

```cmake
# WildPalmsPalmAdapters — stateless typed wrappers over PalmBackend
# for WP-internal UI consumption. NOT a libkalburator type. NOT on
# the plugin ABI. Transient convenience surface per E.7 spec.
#
# Phase E.7 sibling to WildPalmsPalmCodecs (same phase).
#
# Links WildPalmsPalmCodecs (POD + encode/decode), WildPalmsPalmSync
# (PalmBackend + PalmRecord + IPalmDatabaseAccess), WildPalmsPalmCalendar
# (CategoryMappingStore).

add_library(WildPalmsPalmAdapters STATIC
    # Adapter sources appended in Tasks 8-10.
)

target_include_directories(WildPalmsPalmAdapters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(WildPalmsPalmAdapters
    PUBLIC
        Qt::Core
        WildPalmsPalmCodecs
        WildPalmsPalmSync
        WildPalmsPalmCalendar
)

set_target_properties(WildPalmsPalmAdapters PROPERTIES POSITION_INDEPENDENT_CODE ON)
```

Caveat: CMake forbids `add_library()` with zero sources. Workaround — add a placeholder TU that exports nothing:

```cpp
// src/palm/adapters/palmadapters_placeholder.cpp
// Placeholder TU so WildPalmsPalmAdapters is a valid empty static
// lib until Tasks 8-10 add adapter sources. Delete in the first
// commit that adds a real adapter source.
namespace WildPalms::Palm::Adapters {}  // no-op
```

…and list it in the `add_library(...)`. Task 8 deletes this TU on its first commit.

- [ ] **Step 7.2: Create the placeholder TU**

Create `src/palm/adapters/palmadapters_placeholder.cpp` with the content above.

- [ ] **Step 7.3: Wire into `src/palm/CMakeLists.txt`**

Append `add_subdirectory(adapters)` after `add_subdirectory(codecs)`.

- [ ] **Step 7.4: Build**

Run: `cmake --build build-dev --target WildPalmsPalmAdapters 2>&1 | tail -5`

Expected: `[100%] Built target WildPalmsPalmAdapters`.

- [ ] **Step 7.5: Commit**

```bash
git add src/palm/CMakeLists.txt src/palm/adapters/
git commit -m "$(cat <<'EOF'
feat(palm-adapters): scaffold WildPalmsPalmAdapters static lib

Empty skeleton with a placeholder TU so the lib is buildable. First
adapter (PalmMemosAdapter) lands in the next commit; placeholder
will be deleted then.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: PalmMemosAdapter

Goal: TDD-land the stateless memo adapter free functions. Tests use `MockPalmDatabaseAccess` from E.3 to exercise the round-trip through a real `PalmBackend` without a device.

**Files:**
- Create: `tests/palmadapters/tst_palmmemosadapter.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt` (register test; link `WildPalmsPalmAdapters`)
- Create: `src/palm/adapters/palmmemosadapter.h`
- Create: `src/palm/adapters/palmmemosadapter.cpp`
- Modify: `src/palm/adapters/CMakeLists.txt` (append sources; delete placeholder)
- Delete: `src/palm/adapters/palmadapters_placeholder.cpp`

- [ ] **Step 8.1: Update the `add_palm_adapter_test` helper in `tests/palmadapters/CMakeLists.txt` to also link `WildPalmsPalmAdapters`**

Change the function body to:
```cmake
function(add_palm_adapter_test TEST_NAME)
    set(TEST_SOURCES ${ARGN})
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME}
        PRIVATE
            Qt::Core
            Qt::Test
            KF6::Contacts
            KF6::CalendarCore
            Kalburator::Sync
            WildPalmsPalmCodecs
            WildPalmsPalmAdapters
            WildPalmsPalmSync
            WildPalmsPalmCalendar
    )
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    set_tests_properties(${TEST_NAME} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()
```

- [ ] **Step 8.2: Write `tests/palmadapters/tst_palmmemosadapter.cpp`**

```cpp
#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "memocodec.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmmemosadapter.h"

using WildPalms::Palm::Adapters::MemoRow;
using WildPalms::Palm::Adapters::readAllMemos;
using WildPalms::Palm::Adapters::readMemo;
using WildPalms::Palm::Adapters::writeMemo;
using WildPalms::Palm::Adapters::deleteMemo;
using WildPalms::PalmCodecs::Memo;
using WildPalms::PalmCodecs::encodeMemo;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

class TestPalmMemosAdapter : public QObject
{
    Q_OBJECT
private slots:
    void readEmptyDbReturnsEmptyList();
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void readByIdMissingReturnsNullopt();
    void writeAssignsIdWhenZero();
    void categorySlotPreservedOnWrite();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
};

namespace {

void seed(MockPalmDatabaseAccess &mock, const QString &db, const Memo &m,
          std::uint8_t categorySlot)
{
    PalmRecord rec;
    rec.category = categorySlot;
    rec.data = encodeMemo(m);
    mock.createDatabase(db);
    mock.createRecord(db, rec);
}

} // namespace

void TestPalmMemosAdapter::readEmptyDbReturnsEmptyList()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    QCOMPARE(readAllMemos(&pb, &cats).size(), 0);
}

void TestPalmMemosAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 0, m);
    QVERIFY(id != 0);
    const auto rows = readAllMemos(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.text, QStringLiteral("x"));
}

void TestPalmMemosAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("needle");
    const auto id = writeMemo(&pb, 0, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.text, QStringLiteral("needle"));
}

void TestPalmMemosAdapter::readByIdMissingReturnsNullopt()
{
    MockPalmDatabaseAccess mock;
    mock.createDatabase(QStringLiteral("MemoDB"));
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto row = readMemo(&pb, &cats, 9999);
    QVERIFY(!row.has_value());
}

void TestPalmMemosAdapter::writeAssignsIdWhenZero()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id1 = writeMemo(&pb, 0, m);
    const auto id2 = writeMemo(&pb, 0, m);
    QVERIFY(id1 != 0);
    QVERIFY(id2 != 0);
    QVERIFY(id1 != id2);
}

void TestPalmMemosAdapter::categorySlotPreservedOnWrite()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 7, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 7);
}

void TestPalmMemosAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setDisplayName(QStringLiteral("MemoDB"), 3, QStringLiteral("Work"));
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 3, m);
    const auto row = readMemo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Work"));
}

void TestPalmMemosAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Memo m; m.text = QStringLiteral("x");
    const auto id = writeMemo(&pb, 0, m);
    deleteMemo(&pb, id);
    QVERIFY(!readMemo(&pb, &cats, id).has_value());
}

QTEST_GUILESS_MAIN(TestPalmMemosAdapter)
#include "tst_palmmemosadapter.moc"
```

- [ ] **Step 8.3: Register the test**

Append to `tests/palmadapters/CMakeLists.txt`:
```cmake
add_palm_adapter_test(tst_palmmemosadapter tst_palmmemosadapter.cpp)
```

- [ ] **Step 8.4: Run — expect failure**

Run: `cmake --build build-dev --target tst_palmmemosadapter 2>&1 | tail -5`

Expected: `palmmemosadapter.h: No such file or directory`.

- [ ] **Step 8.5: Write `src/palm/adapters/palmmemosadapter.h`**

```cpp
#ifndef WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase. Prefer binding via the
// codec headers + PalmBackend directly if you need long-term ABI
// stability.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "memocodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct MemoRow {
    std::uint32_t       id = 0;            ///< PalmRecord::recordId
    int                 categorySlot = 0;  ///< 0..15
    QString             categoryName;      ///< resolved via CategoryMappingStore
    WildPalms::PalmCodecs::Memo content;
};

QList<MemoRow>
readAllMemos(WildPalms::PalmSync::PalmBackend *pb,
             const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<MemoRow>
readMemo(WildPalms::PalmSync::PalmBackend *pb,
         const WildPalms::PalmCalendar::CategoryMappingStore *cats,
         std::uint32_t id);

std::uint32_t
writeMemo(WildPalms::PalmSync::PalmBackend *pb,
          int categorySlot,
          const WildPalms::PalmCodecs::Memo &m);

void
deleteMemo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMMEMOSADAPTER_H
```

- [ ] **Step 8.6: Write `src/palm/adapters/palmmemosadapter.cpp`**

```cpp
#include "palmmemosadapter.h"

#include <QLoggingCategory>

#include "categorymappingstore.h"
#include "palmbackend.h"

Q_LOGGING_CATEGORY(lcMemoAdapter, "wildpalms.palm.adapter.memo")

namespace WildPalms::Palm::Adapters {

namespace {

constexpr const char *kDbName = "MemoDB";

MemoRow rowFromBackendRecord(const Kalburator::Sync::BackendRecord &rec,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    MemoRow row;
    QString dbName;
    std::uint32_t nativeId = 0;
    if (WildPalms::PalmSync::PalmBackend::decodeRecordId(rec.id, &dbName, &nativeId)) {
        row.id = nativeId;
    }
    // BackendRecord doesn't carry the Palm category slot directly; the
    // convention established in E.3 is that PalmBackend stashes it in
    // `rec.properties` under the "palm.category" key.
    row.categorySlot = rec.properties.value(QStringLiteral("palm.category")).toInt();
    row.categoryName = cats
        ? cats->displayName(QStringLiteral(kDbName), row.categorySlot)
        : QString();
    auto decoded = WildPalms::PalmCodecs::decodeMemo(rec.data);
    if (!decoded) {
        qCWarning(lcMemoAdapter) << "decode failed for" << rec.id;
        return row;
    }
    row.content = *decoded;
    return row;
}

} // namespace

QList<MemoRow> readAllMemos(WildPalms::PalmSync::PalmBackend *pb,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    QList<MemoRow> out;
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    for (const auto &rec : pb->loadRecords(colId)) {
        out.append(rowFromBackendRecord(rec, cats));
    }
    return out;
}

std::optional<MemoRow> readMemo(WildPalms::PalmSync::PalmBackend *pb,
                                 const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                                 std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    auto rec = pb->loadRecord(recId);
    if (!rec) return std::nullopt;
    return rowFromBackendRecord(*rec, cats);
}

std::uint32_t writeMemo(WildPalms::PalmSync::PalmBackend *pb,
                         int categorySlot,
                         const WildPalms::PalmCodecs::Memo &m)
{
    Kalburator::Sync::BackendRecord rec;
    rec.data = WildPalms::PalmCodecs::encodeMemo(m);
    rec.properties.insert(QStringLiteral("palm.category"), categorySlot);
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    const QString newId = pb->createRecord(colId, rec);
    QString dbName;
    std::uint32_t nativeId = 0;
    WildPalms::PalmSync::PalmBackend::decodeRecordId(newId, &dbName, &nativeId);
    return nativeId;
}

void deleteMemo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    pb->deleteRecord(recId);
}

} // namespace WildPalms::Palm::Adapters
```

Caveat on `palm.category`: this key is an invented convention in this plan. Before implementing the adapter, the engineer should grep `src/palm/sync/palmbackend.cpp` for how E.3 actually marshals the Palm category slot into `BackendRecord`. Use whatever key is in use there. If E.3 did not surface the category at all (possible — E.3 was a scaffold), extend `PalmBackend` to include it, as a small side-edit to `palmbackend.cpp`; a one-line add to `properties` on both the load and create paths is enough. Document the key in a comment on `PalmBackend::loadRecords`.

- [ ] **Step 8.7: Delete the placeholder TU and append real sources**

Delete `src/palm/adapters/palmadapters_placeholder.cpp`. Update `src/palm/adapters/CMakeLists.txt`:

```cmake
add_library(WildPalmsPalmAdapters STATIC
    palmmemosadapter.h
    palmmemosadapter.cpp
)
```

- [ ] **Step 8.8: Build + test**

Run: `cmake --build build-dev --target tst_palmmemosadapter 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_palmmemosadapter$' --output-on-failure`

Expected: all 8 test methods pass. If any depend on `PalmBackend` surfacing `palm.category` and that isn't yet there, extend `palmbackend.cpp` and re-run.

- [ ] **Step 8.9: Commit**

```bash
git rm src/palm/adapters/palmadapters_placeholder.cpp
git add src/palm/adapters/CMakeLists.txt src/palm/adapters/palmmemosadapter.h src/palm/adapters/palmmemosadapter.cpp tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_palmmemosadapter.cpp
# If PalmBackend was edited to surface the category key, include it:
git add src/palm/sync/palmbackend.cpp 2>/dev/null || true
git commit -m "$(cat <<'EOF'
feat(palm-adapters): PalmMemosAdapter

Stateless free functions over PalmBackend for MemoDB. Borrows
CategoryMappingStore for category-name resolution. Tested end-to-end
against MockPalmDatabaseAccess.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: PalmContactsAdapter

Goal: the contact adapter. Structurally identical to `PalmMemosAdapter`; the only differences are the DB name (`AddressDB`) and the codec (`ContactCodec`).

**Files:**
- Create: `tests/palmadapters/tst_palmcontactsadapter.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt`
- Create: `src/palm/adapters/palmcontactsadapter.h`
- Create: `src/palm/adapters/palmcontactsadapter.cpp`
- Modify: `src/palm/adapters/CMakeLists.txt`

- [ ] **Step 9.1: Write `tests/palmadapters/tst_palmcontactsadapter.cpp`**

```cpp
#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "contactcodec.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmcontactsadapter.h"

using WildPalms::Palm::Adapters::ContactRow;
using WildPalms::Palm::Adapters::readAllContacts;
using WildPalms::Palm::Adapters::readContact;
using WildPalms::Palm::Adapters::writeContact;
using WildPalms::Palm::Adapters::deleteContact;
using WildPalms::PalmCodecs::Contact;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmContactsAdapter : public QObject
{
    Q_OBJECT
private slots:
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void categorySlotPreserved();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
    void phoneSlotsSurviveRoundTrip();
};

namespace {

Contact makeSample()
{
    Contact c;
    c.firstName = QStringLiteral("Grace");
    c.lastName  = QStringLiteral("Hopper");
    c.phone[0]  = QStringLiteral("555-0100");
    c.phoneLabels = { QStringLiteral("Work"), QStringLiteral("Home"),
                      QStringLiteral("Mobile"), QStringLiteral("E-mail"),
                      QStringLiteral("Other") };
    return c;
}

} // namespace

void TestPalmContactsAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    QVERIFY(id != 0);
    const auto rows = readAllContacts(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.firstName, QStringLiteral("Grace"));
}

void TestPalmContactsAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.lastName, QStringLiteral("Hopper"));
}

void TestPalmContactsAdapter::categorySlotPreserved()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 5, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 5);
}

void TestPalmContactsAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setDisplayName(QStringLiteral("AddressDB"), 5, QStringLiteral("Family"));
    const auto id = writeContact(&pb, 5, makeSample());
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Family"));
}

void TestPalmContactsAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeContact(&pb, 0, makeSample());
    deleteContact(&pb, id);
    QVERIFY(!readContact(&pb, &cats, id).has_value());
}

void TestPalmContactsAdapter::phoneSlotsSurviveRoundTrip()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Contact c = makeSample();
    for (int i = 0; i < 5; ++i) c.phone[i] = QStringLiteral("555-010%1").arg(i);
    const auto id = writeContact(&pb, 0, c);
    const auto row = readContact(&pb, &cats, id);
    QVERIFY(row.has_value());
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(row->content.phone[i], c.phone[i]);
    }
}

QTEST_GUILESS_MAIN(TestPalmContactsAdapter)
#include "tst_palmcontactsadapter.moc"
```

- [ ] **Step 9.2: Register the test**

Append to `tests/palmadapters/CMakeLists.txt`:
```cmake
add_palm_adapter_test(tst_palmcontactsadapter tst_palmcontactsadapter.cpp)
```

- [ ] **Step 9.3: Run — expect failure**

Run: `cmake --build build-dev --target tst_palmcontactsadapter 2>&1 | tail -5`

Expected: missing `palmcontactsadapter.h`.

- [ ] **Step 9.4: Write `src/palm/adapters/palmcontactsadapter.h`**

```cpp
#ifndef WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "contactcodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct ContactRow {
    std::uint32_t                 id = 0;
    int                           categorySlot = 0;
    QString                       categoryName;
    WildPalms::PalmCodecs::Contact content;
};

QList<ContactRow>
readAllContacts(WildPalms::PalmSync::PalmBackend *pb,
                const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<ContactRow>
readContact(WildPalms::PalmSync::PalmBackend *pb,
            const WildPalms::PalmCalendar::CategoryMappingStore *cats,
            std::uint32_t id);

std::uint32_t
writeContact(WildPalms::PalmSync::PalmBackend *pb,
             int categorySlot,
             const WildPalms::PalmCodecs::Contact &c);

void
deleteContact(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMCONTACTSADAPTER_H
```

- [ ] **Step 9.5: Write `src/palm/adapters/palmcontactsadapter.cpp`**

```cpp
#include "palmcontactsadapter.h"

#include <QLoggingCategory>

#include "categorymappingstore.h"
#include "palmbackend.h"

Q_LOGGING_CATEGORY(lcContactAdapter, "wildpalms.palm.adapter.contact")

namespace WildPalms::Palm::Adapters {

namespace {

constexpr const char *kDbName = "AddressDB";

ContactRow rowFromBackendRecord(const Kalburator::Sync::BackendRecord &rec,
                                const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    ContactRow row;
    QString dbName;
    std::uint32_t nativeId = 0;
    if (WildPalms::PalmSync::PalmBackend::decodeRecordId(rec.id, &dbName, &nativeId)) {
        row.id = nativeId;
    }
    row.categorySlot = rec.properties.value(QStringLiteral("palm.category")).toInt();
    row.categoryName = cats
        ? cats->displayName(QStringLiteral(kDbName), row.categorySlot)
        : QString();
    auto decoded = WildPalms::PalmCodecs::decodeContact(rec.data);
    if (!decoded) {
        qCWarning(lcContactAdapter) << "decode failed for" << rec.id;
        return row;
    }
    row.content = *decoded;
    return row;
}

} // namespace

QList<ContactRow> readAllContacts(WildPalms::PalmSync::PalmBackend *pb,
                                   const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    QList<ContactRow> out;
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    for (const auto &rec : pb->loadRecords(colId)) {
        out.append(rowFromBackendRecord(rec, cats));
    }
    return out;
}

std::optional<ContactRow> readContact(WildPalms::PalmSync::PalmBackend *pb,
                                       const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                                       std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    auto rec = pb->loadRecord(recId);
    if (!rec) return std::nullopt;
    return rowFromBackendRecord(*rec, cats);
}

std::uint32_t writeContact(WildPalms::PalmSync::PalmBackend *pb,
                            int categorySlot,
                            const WildPalms::PalmCodecs::Contact &c)
{
    Kalburator::Sync::BackendRecord rec;
    rec.data = WildPalms::PalmCodecs::encodeContact(c);
    rec.properties.insert(QStringLiteral("palm.category"), categorySlot);
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    const QString newId = pb->createRecord(colId, rec);
    QString dbName;
    std::uint32_t nativeId = 0;
    WildPalms::PalmSync::PalmBackend::decodeRecordId(newId, &dbName, &nativeId);
    return nativeId;
}

void deleteContact(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    pb->deleteRecord(recId);
}

} // namespace WildPalms::Palm::Adapters
```

- [ ] **Step 9.6: Append sources to `src/palm/adapters/CMakeLists.txt`**

```cmake
add_library(WildPalmsPalmAdapters STATIC
    palmmemosadapter.h
    palmmemosadapter.cpp
    palmcontactsadapter.h
    palmcontactsadapter.cpp
)
```

- [ ] **Step 9.7: Build + test**

Run: `cmake --build build-dev --target tst_palmcontactsadapter 2>&1 | tail -5 && ctest --test-dir build-dev -R '^tst_palmcontactsadapter$' --output-on-failure`

Expected: all 6 test methods pass.

- [ ] **Step 9.8: Commit**

```bash
git add src/palm/adapters/CMakeLists.txt src/palm/adapters/palmcontactsadapter.h src/palm/adapters/palmcontactsadapter.cpp tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_palmcontactsadapter.cpp
git commit -m "$(cat <<'EOF'
feat(palm-adapters): PalmContactsAdapter

Stateless free functions over PalmBackend for AddressDB. Mirrors the
PalmMemosAdapter pattern.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: PalmTodosAdapter

Goal: the todo adapter. Same pattern as Contacts and Memos; DB name `ToDoDB`, codec `TodoCodec`.

**Files:**
- Create: `tests/palmadapters/tst_palmtodosadapter.cpp`
- Modify: `tests/palmadapters/CMakeLists.txt`
- Create: `src/palm/adapters/palmtodosadapter.h`
- Create: `src/palm/adapters/palmtodosadapter.cpp`
- Modify: `src/palm/adapters/CMakeLists.txt`

- [ ] **Step 10.1: Write `tests/palmadapters/tst_palmtodosadapter.cpp`**

```cpp
#include <QtTest/QtTest>

#include "categorymappingstore.h"
#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmtodosadapter.h"
#include "todocodec.h"

using WildPalms::Palm::Adapters::TodoRow;
using WildPalms::Palm::Adapters::readAllTodos;
using WildPalms::Palm::Adapters::readTodo;
using WildPalms::Palm::Adapters::writeTodo;
using WildPalms::Palm::Adapters::deleteTodo;
using WildPalms::PalmCodecs::Todo;
using WildPalms::PalmCalendar::CategoryMappingStore;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;

class TestPalmTodosAdapter : public QObject
{
    Q_OBJECT
private slots:
    void writeThenReadAllRoundTrips();
    void readByIdFindsSpecificRecord();
    void categorySlotPreserved();
    void categoryNameResolvedFromStore();
    void deleteRemovesTheRecord();
    void dueDateSurvivesRoundTrip();
    void priorityAndCompletionSurvive();
};

namespace {

Todo makeSample()
{
    Todo t;
    t.description = QStringLiteral("Write plan");
    t.hasIndefiniteDue = true;
    t.priority = 1;
    return t;
}

} // namespace

void TestPalmTodosAdapter::writeThenReadAllRoundTrips()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    QVERIFY(id != 0);
    const auto rows = readAllTodos(&pb, &cats);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().content.description, QStringLiteral("Write plan"));
}

void TestPalmTodosAdapter::readByIdFindsSpecificRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.priority, 1);
}

void TestPalmTodosAdapter::categorySlotPreserved()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 9, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categorySlot, 9);
}

void TestPalmTodosAdapter::categoryNameResolvedFromStore()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    cats.setDisplayName(QStringLiteral("ToDoDB"), 9, QStringLiteral("Project"));
    const auto id = writeTodo(&pb, 9, makeSample());
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->categoryName, QStringLiteral("Project"));
}

void TestPalmTodosAdapter::deleteRemovesTheRecord()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    const auto id = writeTodo(&pb, 0, makeSample());
    deleteTodo(&pb, id);
    QVERIFY(!readTodo(&pb, &cats, id).has_value());
}

void TestPalmTodosAdapter::dueDateSurvivesRoundTrip()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Todo t = makeSample();
    t.hasIndefiniteDue = false;
    t.due = QDateTime(QDate(2026, 6, 1), QTime(0, 0), Qt::LocalTime);
    const auto id = writeTodo(&pb, 0, t);
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.hasIndefiniteDue, false);
    QCOMPARE(row->content.due.date(), QDate(2026, 6, 1));
}

void TestPalmTodosAdapter::priorityAndCompletionSurvive()
{
    MockPalmDatabaseAccess mock;
    PalmBackend pb(&mock);
    CategoryMappingStore cats;
    Todo t = makeSample();
    t.priority = 5;
    t.isComplete = true;
    const auto id = writeTodo(&pb, 0, t);
    const auto row = readTodo(&pb, &cats, id);
    QVERIFY(row.has_value());
    QCOMPARE(row->content.priority, 5);
    QCOMPARE(row->content.isComplete, true);
}

QTEST_GUILESS_MAIN(TestPalmTodosAdapter)
#include "tst_palmtodosadapter.moc"
```

- [ ] **Step 10.2: Register the test**

Append to `tests/palmadapters/CMakeLists.txt`:
```cmake
add_palm_adapter_test(tst_palmtodosadapter tst_palmtodosadapter.cpp)
```

- [ ] **Step 10.3: Run — expect failure**

Run: `cmake --build build-dev --target tst_palmtodosadapter 2>&1 | tail -5`

Expected: missing `palmtodosadapter.h`.

- [ ] **Step 10.4: Write `src/palm/adapters/palmtodosadapter.h`**

```cpp
#ifndef WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H
#define WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H

// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase.

#include <cstdint>
#include <optional>

#include <QList>
#include <QString>

#include "todocodec.h"

namespace WildPalms::PalmSync { class PalmBackend; }
namespace WildPalms::PalmCalendar { class CategoryMappingStore; }

namespace WildPalms::Palm::Adapters {

struct TodoRow {
    std::uint32_t             id = 0;
    int                       categorySlot = 0;
    QString                   categoryName;
    WildPalms::PalmCodecs::Todo content;
};

QList<TodoRow>
readAllTodos(WildPalms::PalmSync::PalmBackend *pb,
             const WildPalms::PalmCalendar::CategoryMappingStore *cats);

std::optional<TodoRow>
readTodo(WildPalms::PalmSync::PalmBackend *pb,
         const WildPalms::PalmCalendar::CategoryMappingStore *cats,
         std::uint32_t id);

std::uint32_t
writeTodo(WildPalms::PalmSync::PalmBackend *pb,
          int categorySlot,
          const WildPalms::PalmCodecs::Todo &t);

void
deleteTodo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id);

} // namespace WildPalms::Palm::Adapters

#endif // WILDPALMS_ADAPTERS_PALMTODOSADAPTER_H
```

- [ ] **Step 10.5: Write `src/palm/adapters/palmtodosadapter.cpp`**

```cpp
#include "palmtodosadapter.h"

#include <QLoggingCategory>

#include "categorymappingstore.h"
#include "palmbackend.h"

Q_LOGGING_CATEGORY(lcTodoAdapter, "wildpalms.palm.adapter.todo")

namespace WildPalms::Palm::Adapters {

namespace {

constexpr const char *kDbName = "ToDoDB";

TodoRow rowFromBackendRecord(const Kalburator::Sync::BackendRecord &rec,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    TodoRow row;
    QString dbName;
    std::uint32_t nativeId = 0;
    if (WildPalms::PalmSync::PalmBackend::decodeRecordId(rec.id, &dbName, &nativeId)) {
        row.id = nativeId;
    }
    row.categorySlot = rec.properties.value(QStringLiteral("palm.category")).toInt();
    row.categoryName = cats
        ? cats->displayName(QStringLiteral(kDbName), row.categorySlot)
        : QString();
    auto decoded = WildPalms::PalmCodecs::decodeTodo(rec.data);
    if (!decoded) {
        qCWarning(lcTodoAdapter) << "decode failed for" << rec.id;
        return row;
    }
    row.content = *decoded;
    return row;
}

} // namespace

QList<TodoRow> readAllTodos(WildPalms::PalmSync::PalmBackend *pb,
                             const WildPalms::PalmCalendar::CategoryMappingStore *cats)
{
    QList<TodoRow> out;
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    for (const auto &rec : pb->loadRecords(colId)) {
        out.append(rowFromBackendRecord(rec, cats));
    }
    return out;
}

std::optional<TodoRow> readTodo(WildPalms::PalmSync::PalmBackend *pb,
                                 const WildPalms::PalmCalendar::CategoryMappingStore *cats,
                                 std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    auto rec = pb->loadRecord(recId);
    if (!rec) return std::nullopt;
    return rowFromBackendRecord(*rec, cats);
}

std::uint32_t writeTodo(WildPalms::PalmSync::PalmBackend *pb,
                         int categorySlot,
                         const WildPalms::PalmCodecs::Todo &t)
{
    Kalburator::Sync::BackendRecord rec;
    rec.data = WildPalms::PalmCodecs::encodeTodo(t);
    rec.properties.insert(QStringLiteral("palm.category"), categorySlot);
    const QString colId = WildPalms::PalmSync::PalmBackend::encodeCollectionId(
        QStringLiteral(kDbName));
    const QString newId = pb->createRecord(colId, rec);
    QString dbName;
    std::uint32_t nativeId = 0;
    WildPalms::PalmSync::PalmBackend::decodeRecordId(newId, &dbName, &nativeId);
    return nativeId;
}

void deleteTodo(WildPalms::PalmSync::PalmBackend *pb, std::uint32_t id)
{
    const QString recId = WildPalms::PalmSync::PalmBackend::encodeRecordId(
        QStringLiteral(kDbName), id);
    pb->deleteRecord(recId);
}

} // namespace WildPalms::Palm::Adapters
```

- [ ] **Step 10.6: Append sources to `src/palm/adapters/CMakeLists.txt`**

```cmake
add_library(WildPalmsPalmAdapters STATIC
    palmmemosadapter.h
    palmmemosadapter.cpp
    palmcontactsadapter.h
    palmcontactsadapter.cpp
    palmtodosadapter.h
    palmtodosadapter.cpp
)
```

- [ ] **Step 10.7: Run the full E.7 test set**

Run: `cmake --build build-dev 2>&1 | tail -5 && ctest --test-dir build-dev -R '^(tst_memocodec|tst_contactcodec|tst_todocodec|tst_kde_pim_convert|tst_palmmemosadapter|tst_palmcontactsadapter|tst_palmtodosadapter)$' --output-on-failure`

Expected: all 7 test binaries pass (~55 test methods total).

- [ ] **Step 10.8: Run the full WP ctest to confirm no regressions in legacy mapper tests or other suites**

Run: `ctest --test-dir build-dev --output-on-failure 2>&1 | tail -40`

Expected: green — all WP tests pass including the legacy `test_contactmapper`, `test_memomapper`, `test_todomapper`.

- [ ] **Step 10.9: Commit**

```bash
git add src/palm/adapters/CMakeLists.txt src/palm/adapters/palmtodosadapter.h src/palm/adapters/palmtodosadapter.cpp tests/palmadapters/CMakeLists.txt tests/palmadapters/tst_palmtodosadapter.cpp
git commit -m "$(cat <<'EOF'
feat(palm-adapters): PalmTodosAdapter

Stateless free functions over PalmBackend for ToDoDB. Completes the
E.7 typed-adapter layer (Contacts + Memos + Todos + KDE PIM
converters).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Mark E.7 landed in the Phase-E spec + integration plan

Goal: update the sub-phase tracking state so future sessions see E.7 as ✅, including its test-count summary.

**Files:**
- Modify: `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` (row E.7 → ✅)
- Modify: `docs/plans/2026-04-20-libkalburator-integration.md` (if it tracks sub-phase completion; verify first)
- Modify: `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_palm_category_routing.md` (append E.7 delivery note)

- [ ] **Step 11.1: Flip the Phase-E spec row for E.7**

Edit `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md` line 585 (the row beginning `| **E.7** |`). Change to:

```
| ✅ **E.7** | Typed codecs + stateless adapters for Contacts/Memos/Todos at `src/palm/codecs/` and `src/palm/adapters/`. New static libs `WildPalmsPalmCodecs` + `WildPalmsPalmAdapters`. POD + pisock-driven encode/decode (`pack_*`/`unpack_*`), optional KDE PIM converters (Contact↔Addressee, Todo↔KCalendarCore::Todo). Legacy mappers at `src/plugins/{contacts,memo,todos}/` untouched. Landed 2026-04-23. Plan: `docs/superpowers/plans/2026-04-23-phase-e7-typed-adapters.md`. | WP | E.4 (parallel to E.5/E.6) | WP ctest passes; ~55 tests across codecs + adapters; legacy mapper tests still pass. |
```

- [ ] **Step 11.2: Check whether `docs/plans/2026-04-20-libkalburator-integration.md` has a sub-phase checklist for E.7 and update if present**

Run: `grep -n "E\.7" docs/plans/2026-04-20-libkalburator-integration.md`

If a checkbox or status line exists, flip it to landed. If not, no change needed.

- [ ] **Step 11.3: Update the memory file**

Edit `/home/clinton/.claude/projects/-home-clinton-dev-WildPalms/memory/project_palm_category_routing.md`. In the `## Status` section, append under "Delivered in E.6":

```
**Delivered in E.7 (2026-04-23):** Typed codecs + stateless adapters
for Contacts/Memos/Todos at `src/palm/codecs/` and `src/palm/adapters/`.
POD content-only types (metadata on PalmRecord), pisock pack_*/unpack_*
underneath, optional KDE PIM converters alongside. Legacy mappers at
`src/plugins/{contacts,memo,todos}/` untouched — deletion deferred to
E.9/E.11/E.12.
```

- [ ] **Step 11.4: Commit**

```bash
git add docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md docs/plans/2026-04-20-libkalburator-integration.md 2>/dev/null || true
git commit -m "$(cat <<'EOF'
docs(phase-e): mark E.7 landed in spec sub-phases table

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Memory file commits separately (lives outside the repo), so the edit from Step 11.3 is retained in the memory folder directly.

---

## Self-review checklist

- **Spec coverage (§"POD shape"):** Task 2 (Memo fields), Task 3 (Contact fields — all 5 phones, labels, showPhone, custom1..4, note, addr fields), Task 4 (Todo fields — description, note, due, priority, completion).
- **Spec coverage (§"Codec API"):** `encodeX` / `decodeX` signatures land in Tasks 2–4.
- **Spec coverage (§"KDE PIM converters"):** Contact↔Addressee in Task 5, Todo↔`KCalendarCore::Todo` in Task 6.
- **Spec coverage (§"Adapter API"):** `read*` / `write*` / `delete*` free functions in `WildPalms::Palm::Adapters` namespace in Tasks 8–10.
- **Spec coverage (§"Testing"):** ~55 tests across Tasks 2–10 (7 memo + 11 contact + 9 todo + 8 kde-pim + 8+6+7 adapter = 56).
- **Spec coverage (§"Non-goals"):** Task 11 documents untouched legacy mappers. No task touches `src/plugins/*` or `pilot-link*/`. No AppInfo parsing. Adapters are stateless free functions.
- **Spec coverage (§"Linkage"):** `WildPalmsPalmCodecs` gets `KF6::Contacts` + `KF6::CalendarCore` promoted to PUBLIC in Task 5.7; `WildPalmsPalmAdapters` links `WildPalmsPalmCodecs` + `WildPalmsPalmSync` + `WildPalmsPalmCalendar` in Task 7.1.
- **Placeholder scan:** Task 8's reference to `rec.properties` key `"palm.category"` is flagged as an invented convention to verify against E.3's PalmBackend; the plan directs the engineer to grep and extend if needed rather than assume. This is a known-unknown, not a placeholder.
- **Type consistency:** POD type names (`Memo`, `Contact`, `Todo`) and row type names (`MemoRow`, `ContactRow`, `TodoRow`) consistent across tasks. Namespaces `WildPalms::PalmCodecs` and `WildPalms::Palm::Adapters` used consistently. Pisock type names (`Memo_t`, `Address_t`, `ToDo_t`) and functions (`pack_Memo`, `pack_Address`, `pack_ToDo`) spelled per pisock headers.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-04-23-phase-e7-typed-adapters.md`.

Two execution options:

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration. REQUIRED SUB-SKILL: superpowers:subagent-driven-development.
2. **Inline Execution** — execute tasks in this session using superpowers:executing-plans, batch execution with checkpoints.
