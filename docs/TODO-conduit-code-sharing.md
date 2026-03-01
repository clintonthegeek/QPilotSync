# Conduit Code Sharing: Future Refactoring Opportunities

Identified during the category normalization work (Feb 2025). These are
patterns duplicated across conduits that could be centralized in
`WildPalmsCore` for future development.

## 1. Windows-1252 Text Encoding (High Value, ~120 lines)

Three identical copies of `decodePalmText()`, `encodePalmText()`, and the
`cp1252_to_unicode[32]` lookup table exist across mappers (todomapper.cpp,
memomapper.cpp, contactmapper.cpp) plus a fourth in categoryinfo.cpp.

**Proposal**: Create `palm/palmtextcodec.h` in WildPalmsCore with static
`PalmTextCodec::decode(const char*)` and `PalmTextCodec::encode(const QString&)`.

## 2. Category Comparison Normalization (Medium Value, ~60 lines)

Every conduit's `recordsEqual()` has identical "Unfiled" normalization:

```cpp
if (name.compare("Unfiled", Qt::CaseInsensitive) == 0)
    name.clear();
```

**Proposal**: Add `SyncConduitBase::categoriesMatch(int palmIndex, const QString &backendName)`
helper now that category handling lives in the base class.

## 3. Filename Sanitization (Low Value, ~30 lines)

TodoMapper, MemoMapper, and CalendarMapper all do the same regex-based
filename sanitization (replace invalid chars, collapse spaces, trim
underscores).

**Proposal**: `PalmUtils::sanitizeFilename(const QString&, int maxLength)`
as a free function in WildPalmsCore.

## 4. Record Attribute Packing (Low Value)

Every mapper's `pack*()` builds attributes identically:

```cpp
int attr = 0;
if (data.isPrivate) attr |= PilotRecord::AttrSecret;
if (data.isDirty)   attr |= PilotRecord::AttrDirty;
if (data.isDeleted) attr |= PilotRecord::AttrDeleted;
```

Could add a `PilotRecord::buildAttributes(bool secret, bool dirty, bool deleted)`
convenience, but the current code is clear enough.

## 5. Mapper Base Pattern (Future Consideration)

All mappers share a structural pattern:
- Internal struct with `recordId`, `category`, `categoryName`, `isPrivate`, etc.
- Static `unpack*(PilotRecord*) -> Struct`
- Static `pack*(Struct) -> PilotRecord*`
- Static conversion to/from external format (iCal, vCard, Markdown)

A `PalmRecordMapper<T>` template could formalize this when adding more
conduits. Not worth the design effort for four conduits.
