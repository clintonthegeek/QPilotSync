# Phase E.7 — Typed adapters for Contacts/Memos/Todos

**Status:** Design approved 2026-04-23. Ready for implementation plan.

**Parent spec:** `2026-04-21-phase-e-plugin-abi-rewrite-design.md`
(§"Directory layout" lines 537–540, §"Sub-phases" row E.7 line 585).
This document refines the sub-phase's design; for the overall Phase-E
architecture see the parent.

**Dependencies:** E.4 (`PalmBackend` on real pilot-link) — satisfied.
Parallel to E.5 (landed) and E.6 (landed).

**Exit gate:** WP `ctest` passes. Per-field round-trips, golden-bytes
fixtures, KDE PIM converters, and adapter tests against
`MockPalmDatabaseAccess` all green.

---

## Intent

Land a typed layer over `PalmBackend` for Address, Memo, and ToDo
databases so that:

1. The E.9 (Memo), E.11 (ToDo), and E.12 (Contacts) plugin rewrites
   have a clean, tested Palm-side surface to build on.
2. WP's existing Qt widgets (`contactview`, `memoview`, `taskview`)
   have a decode-free way to read/write Palm records once those
   plugins are rewritten.
3. The codec surface is portable enough that if libkalburator ever
   grows typed Contact/Memo/Todo sync upstream (analogous to today's
   `ICalendarSync`), the codecs migrate with near-zero friction and
   the adapters retire.

## Two-tier split

The sub-phase introduces two distinct surfaces with different
stability profiles, separated by directory so the seam is visible.

### `src/palm/codecs/` — stable-ish surface

- POD value types: `Contact`, `Memo`, `Todo`.
- Pure encode/decode free functions: `QByteArray` ↔ POD via pisock's
  `pack_*` / `unpack_*`.
- Optional KDE PIM converters (`Contact` ↔ `KContacts::Addressee`;
  `Todo` ↔ `KCalendarCore::Todo::Ptr`). No memo converter — no KDE
  type for memos.
- Dependencies: Qt6 Core, pisock, KF6::Contacts (only the optional
  Addressee converter TU links it), KF6::CalendarCore (only the
  optional Todo converter TU links it).
- **Must not** depend on `PalmBackend`, `IPalmDatabaseAccess`,
  `WildPalmsCore`, `Qt::Widgets`, or `KF6::XmlGui`.

### `src/palm/adapters/` — transient convenience surface

- Stateless free functions in `WildPalms::Palm::Adapters` namespace.
- Each function borrows a `PalmBackend*` and (where category names
  are needed) a `CategoryMappingStore*`; returns typed PODs.
- No QObject, no signals, no caching, no lifecycle.
- Dependencies: `src/palm/codecs/`, `src/palm/palmbackend.h`,
  `src/palm/calendar/categorymappingstore.h`.
- Namespace and a one-line header banner make the transience
  explicit: "WP-internal convenience. 3rd-party use OK but this
  layer may move upstream to libkalburator in a future phase."

### Why split this way

The plugin ABI landing in E.8 (`IPlugin` / `IBackendPlugin` /
`IPluginAction`) hands a plugin a `PalmDeviceConnection*` and an
`ISyncHost*`. It does **not** mention codecs or adapters. A 3rd-party
plugin may:

- use codecs directly for typed records on its own terms, or
- use adapters for the "give me all contacts" convenience path, or
- call `libpisock` entirely on its own.

All three are supported. If adapters ever migrate upstream, the
plugin ABI is unaffected, and 3rd-party code that binds to adapters
gets a documented migration path rather than a surprise break.

## POD shape

PODs hold **content only**. Metadata lives on `PalmRecord`.

```cpp
struct Contact {
    QString lastName, firstName, company, title;
    QString phone[5];            // five Palm phone slots
    QStringList phoneLabels;     // five labels
    int     showPhone;           // 0..4, preferred-phone index
    QString address, city, state, zip, country;
    QString custom[4];
    QString note;
    bool    isPrivate;
};

struct Memo {
    QString text;
    bool    isPrivate;
};

struct Todo {
    QString   description;
    QString   note;
    bool      hasIndefiniteDue;
    QDateTime due;               // valid when !hasIndefiniteDue
    int       priority;          // 1..5 (Palm-native range)
    bool      isComplete;
    bool      isPrivate;
};
```

Fields deliberately **not** on the PODs (they belong on `PalmRecord`):
`recordId`, `category` (slot number 0..15), `categoryName` (resolved
via `CategoryMappingStore`), `isDirty`, `isDeleted`.

This shape is honest to the device: five hardcoded phone slots, a
`showPhone` display-preference field, priority 1..5 rather than
iCal's 1..9. Callers who want a richer projection reach for the KDE
PIM converter.

## Codec API

```cpp
// src/palm/codecs/contactcodec.h
namespace WildPalms::Palm::Codecs {

struct Contact { /* ... as above ... */ };

QByteArray          encodeContact(const Contact &c);
std::optional<Contact> decodeContact(QByteArrayView bytes);

} // namespace
```

Analogous for `memocodec.h` and `todocodec.h`. Each codec TU
`#include`s the corresponding pisock header (`pi-address.h`,
`pi-memo.h`, `pi-todo.h`) and calls the pack/unpack reference
routines directly. No byte-level reimplementation.

### KDE PIM converters

```cpp
// src/palm/codecs/kde_pim_convert.h
namespace WildPalms::Palm::Codecs {

KContacts::Addressee     toAddressee(const Contact &c);
Contact                  fromAddressee(const KContacts::Addressee &a);

KCalendarCore::Todo::Ptr toKCalTodo(const Todo &t);
Todo                     fromKCalTodo(const KCalendarCore::Todo::Ptr &t);

} // namespace
```

Mapping is necessarily lossy in places (documented in-header):

- **Phone slots → Addressee phones.** Palm label strings that match
  standard vCard types map to typed `PhoneNumber`s; others become
  `Other` with the original label preserved in a `X-PALM-LABEL`
  parameter.
- **`showPhone`.** Preserved as `X-PALM-SHOW-PHONE` custom field on
  Addressee; round-trips.
- **`custom1..4`.** Preserved as `X-PALM-CUSTOM-{1..4}` custom
  fields; round-trip.
- **Palm priority 1..5 → iCal priority.** Direct 1-to-1: 1→1, 2→2,
  3→3, 4→4, 5→5. iCal's 6..9 are unused by Palm and map back to 5
  on reverse conversion.

## Adapter API

```cpp
// src/palm/adapters/palmcontactsadapter.h
// WP-internal convenience. 3rd-party use OK but this layer may move
// upstream to libkalburator in a future phase.

namespace WildPalms::Palm::Adapters {

struct ContactRow {
    std::uint32_t     id;              // PalmRecord::recordId
    int               categorySlot;    // 0..15
    QString           categoryName;    // resolved via CategoryMappingStore
    Codecs::Contact   content;
};

QList<ContactRow>
readAllContacts(PalmBackend *pb,
                const CategoryMappingStore *cats);

std::optional<ContactRow>
readContact(PalmBackend *pb,
            const CategoryMappingStore *cats,
            std::uint32_t id);

std::uint32_t
writeContact(PalmBackend *pb,
             int categorySlot,
             const Codecs::Contact &c);

void
deleteContact(PalmBackend *pb, std::uint32_t id);

} // namespace
```

Analogous shapes for `palmmemosadapter.h` and `palmtodosadapter.h`.

**Error handling.** Decode failures on individual records are
skipped with a `qWarning` logged against category
`wildpalms.palm.adapter`. The adapter never throws; a corrupt record
does not break a list-read of healthy siblings.

## Directory & CMake layout

```
src/palm/
├── codecs/                                 # NEW
│   ├── CMakeLists.txt                      # WildPalmsPalmCodecs static lib
│   ├── contactcodec.{h,cpp}
│   ├── memocodec.{h,cpp}
│   ├── todocodec.{h,cpp}
│   └── kde_pim_convert.{h,cpp}
├── adapters/                               # NEW
│   ├── CMakeLists.txt                      # WildPalmsPalmAdapters static lib
│   ├── palmcontactsadapter.{h,cpp}
│   ├── palmmemosadapter.{h,cpp}
│   └── palmtodosadapter.{h,cpp}
├── calendar/      # E.6, unchanged
├── conflict/      # E.5, unchanged
├── device/        # E.4, unchanged
└── (E.3 PalmBackend / PalmRecord at src/palm/)
```

**Linkage:**

- `WildPalmsPalmCodecs` links `Qt6::Core`, `pisock`, `KF6::Contacts`
  (via `kde_pim_convert.cpp`), `KF6::CalendarCore` (via
  `kde_pim_convert.cpp`).
- `WildPalmsPalmAdapters` links `WildPalmsPalmCodecs`,
  `WildPalmsPalmSync` (for `PalmBackend` + `PalmRecord`),
  `WildPalmsPalmCalendar` (for `CategoryMappingStore`), `Qt6::Core`.

## Testing

```
tests/palmadapters/
├── CMakeLists.txt
├── tst_contactcodec.cpp          # byte ↔ POD
├── tst_memocodec.cpp
├── tst_todocodec.cpp
├── tst_kde_pim_convert.cpp       # POD ↔ KDE PIM
├── tst_palmcontactsadapter.cpp   # adapter vs. MockPalmDatabaseAccess
├── tst_palmmemosadapter.cpp
├── tst_palmtodosadapter.cpp
└── fixtures/
    ├── address_phone_bitfield.bin   # golden bytes
    ├── address_showphone_nonzero.bin
    ├── memo_utf8_newline.bin
    ├── todo_indefinite_due.bin
    └── todo_due_priority1.bin
```

**Coverage targets:**

- **Per-field round-trips** (`encode(decode(bytes)) == bytes` via
  constructed PODs): every field in each POD exercised. ~8 tests
  per domain.
- **Golden-bytes fixtures**: 3–5 per domain covering tricky
  encodings — Address phone-label bitfield packing, non-zero
  `showPhone`, Todo `hasIndefiniteDue` vs. real due, Memo with
  UTF-8 + embedded newline.
- **KDE PIM round-trips**: Contact ↔ Addressee (approximate
  equality, documented lossy fields allowed); Todo ↔
  `KCalendarCore::Todo` (near-strict). ~5 tests per converter.
- **Adapter tests**: preload `MockPalmDatabaseAccess` with raw
  bytes; exercise `readAll` / `readOne` / `write` / `delete`;
  assert round-trip through `PalmBackend`. ~6 tests per adapter.

**Target total:** ~55 tests across the sub-phase. Legacy mapper
tests at `tests/test_contactmapper.cpp`,
`tests/test_memomapper.cpp`, `tests/test_todomapper.cpp` remain
untouched and continue to pass.

## Non-goals

This sub-phase explicitly does **not**:

- Replace, wrap, or reimplement any part of `libpisock`. Every codec
  calls pisock's `pack_*` / `unpack_*` directly. Pilot-link remains
  READ-ONLY per `PROJECT_VISION.md` line 105.
- Touch `src/plugins/{contacts,memo,todos}/`. The legacy mappers
  keep running inside their plugins until E.9 (Memo), E.11 (ToDo),
  and E.12 (Contacts) delete them as part of the plugin rewrites.
- Parse Palm AppInfo blocks. `CategoryMappingStore` is populated by
  callers; real `dlp_ReadAppBlock` + `unpack_CategoryAppInfo`
  handling defers to E.10/E.17.
- Introduce `QObject`, signals, caching, or any lifecycle to the
  adapter layer. It is stateless free functions.
- Add entries to E.8's plugin ABI. `IBackendPlugin::createBackends`
  signature is unchanged by this sub-phase.
- Migrate or modify `PalmBackend`, `PalmRecord`,
  `IPalmDatabaseAccess`, `CategoryMappingStore`, or
  `PalmCalendarBackend`.

## Risks & mitigations

- **KDE PIM converter scope creep.** The Contact ↔ Addressee
  mapping has many edge cases (phone-label semantics, address-type
  semantics, multiple emails). Mitigation: ship the converter with
  documented lossy fields and one golden round-trip test per lossy
  field; do not chase every edge. E.12 can refine during the
  Contacts plugin rewrite.
- **Fixture drift vs. pisock version.** Golden bytes are captured
  against the vendored `pilot-link/` tree. If pisock ever bumps,
  fixtures may need regeneration. Mitigation: keep fixtures small
  (3–5 per domain), generate them from a one-shot `cpp` helper
  checked into `tests/palmadapters/fixtures/`. Regeneration is a
  one-line rebuild.
- **Dependency creep in codecs.** `kde_pim_convert.cpp` pulls
  KF6::Contacts + KF6::CalendarCore into `WildPalmsPalmCodecs`.
  Mitigation: the converter TU is optional in spirit — a 3rd-party
  plugin that doesn't want KDE PIM can still link
  `WildPalmsPalmCodecs` and ignore the converter header. If the
  link cost becomes a problem, split the converter into its own
  static lib in a follow-up.

## Cross-references

- Parent Phase-E spec:
  `docs/superpowers/specs/2026-04-21-phase-e-plugin-abi-rewrite-design.md`
  §"WP-side class layout" line 164, §"Directory layout" line 537,
  §"Sub-phases" row E.7 line 585.
- E.3 foundation (`PalmBackend`, `PalmRecord`,
  `MockPalmDatabaseAccess`):
  `docs/superpowers/plans/2026-04-21-phase-e3-palm-backend-scaffold.md`.
- E.4 pilot-link wiring (`PilotLinkPalmDatabaseAccess`):
  `docs/superpowers/plans/2026-04-21-phase-e4-palm-backend-pilot-link.md`.
- E.6 precedent (fresh codec + typed backend split):
  `docs/superpowers/plans/2026-04-21-phase-e6-palm-calendar-backend.md`.
- Pilot-link invariance: `docs/PROJECT_VISION.md` line 105;
  `docs/plans/2026-04-20-libkalburator-integration-design.md` line 357.
- Legacy mapper sources (untouched by E.7, deleted by E.9/E.11/E.12):
  `src/plugins/contacts/contactmapper.{h,cpp}`,
  `src/plugins/memo/memomapper.{h,cpp}`,
  `src/plugins/todos/todomapper.{h,cpp}`.
- Legacy mapper tests (untouched, still pass):
  `tests/test_contactmapper.cpp`, `tests/test_memomapper.cpp`,
  `tests/test_todomapper.cpp`.
