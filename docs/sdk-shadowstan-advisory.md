# WildPalms SDK Advisory for ShadowStan Developers

This document covers WildPalms SDK accommodations, confirmed capabilities, and prescribed changes for the ShadowStan conduit plugin.

## 1. Linking Fix: Remove usb/bluetooth Workaround

**What changed:** WildPalms SDK 0.1.1+ now links `libusb` and `libbluetooth` as PRIVATE dependencies of `libWildPalmsCore.so`. These symbols are fully resolved within the shared library — DT_NEEDED entries for `libusb-0.1.so.4` and `libbluetooth.so.3` are present.

**Prescribed change in ShadowStan:**

In `tests/wildpalms-conduit/CMakeLists.txt`, remove the `usb` and `bluetooth` workaround:

```cmake
# Before (remove this):
target_link_libraries(tst_pdbsyncbackend PRIVATE
    Qt6::Test
    WildPalms::Core
    ShadowStan::Pdb
    usb        # Workaround: WildPalms::Core has unresolved usb/bluetooth symbols
    bluetooth  # not declared in its CMake config
)

# After:
target_link_libraries(tst_pdbsyncbackend PRIVATE
    Qt6::Test
    WildPalms::Core
    ShadowStan::Pdb
)
```

Similarly, the conduit plugin itself (`src/wildpalms-conduit/CMakeLists.txt`) should not need explicit `usb` or `bluetooth` — verify and remove if present.

## 2. Wildcard Database Names: Confirmed Working

WildPalms fully supports glob patterns in `palmDatabaseNames()`. Your `ShadP-*` pattern is handled correctly:

1. Engine calls `KPilotDeviceLink::listDatabases()` once per sync session.
2. `expandDatabaseName("ShadP-*")` converts to regex and matches against the device list.
3. `sync(context)` is called once per matched database with `context->palmDatabase` set to the specific name (e.g., `"ShadP-Main"`).

Your current dispatch in `ShadowPlanConduit::sync()` using `context->palmDatabase.startsWith("ShadP-")` is correct.

**No changes needed.** This is documented in `docs/sdk-plugin-guide.md` for reference.

## 3. Suggestion: Batch PdbSyncBackend Record Operations

`PdbSyncBackend::createRecord()`, `updateRecord()`, and `deleteRecord()` each perform a full read-modify-write cycle on the PDB file. During a per-record sync of `ShadTags`, `ShadViews`, or `ShadFilters` with many changes, this means N full PDB file reads and writes.

**Suggested improvement:** Buffer record operations and flush once at the end of the sync cycle.

Option A — Add a flush/commit pattern:

```cpp
class PdbSyncBackend : public Sync::SyncBackend {
    // ...
    void beginBatch(const QString &collectionId);
    void commitBatch();
private:
    std::optional<PdbDatabase> m_pendingDb;
    QString m_pendingCollection;
};
```

Where `createRecord()`/`updateRecord()`/`deleteRecord()` modify `m_pendingDb` in memory, and `commitBatch()` writes once.

Option B — Override `sync()` for companion databases to do bulk operations directly, bypassing the backend's single-record methods. Since you already override `sync()` for `ShadP-*`, extending this to companion databases would be natural:

```cpp
SyncResult ShadowPlanConduit::sync(SyncContext *context) {
    if (context->palmDatabase.startsWith("ShadP-") ||
        context->palmDatabase == "ShadCat") {
        return wholeFileSync(context, pdbPath);
    }
    // Per-record databases still use default algorithm
    return SyncConduitBase::sync(context);
}
```

The default per-record algorithm handles companion databases well since they're small (typically <100 records), so this is a performance optimization, not a correctness issue.

## 4. Suggestion: Add wholeFileSync Test Coverage

The conduit test suite (`tests/wildpalms-conduit/`) currently covers `PdbSyncBackend` record operations but has no tests for:

- `wholeFileSync()` hash-based change detection
- The `sync()` dispatch logic (routing ShadP-* to whole-file, others to per-record)
- Conflict registration when both sides change a whole-file database
- Baseline hash storage and retrieval across sync sessions

**Suggested test cases:**

```cpp
// In a new tst_shadowplanconduit.cpp or tst_wholefilesync.cpp:

void testWholeFileSyncNoChanges();      // Same hash both sides → no-op
void testWholeFileSyncPalmOnly();       // Palm changed, PC same → overwrite PC
void testWholeFileSyncPCOnly();         // PC changed, Palm same → write to Palm
void testWholeFileSyncBothChanged();    // Both changed → conflict registered
void testSyncDispatchRouting();         // ShadP-* → wholeFileSync, ShadTags → per-record
void testBaselineHashPersistence();     // Hash survives across mock sync sessions
```

These tests can use mock `KPilotLink` and `SyncBackend` instances since `SyncContext::deviceLink` is now the abstract `KPilotLink*` type (SDK change from Task 1 — this was done specifically to enable testability).

## 5. Creator ID Clarification

Per your open question in `wildpalms-sync-granularity.md`:

> Does `X-WildPalms-PalmCreatorId` match against the application creator or the database creator field?

**Answer:** `X-WildPalms-PalmCreatorId` is currently **informational only**. The engine matches conduits to databases exclusively via `X-WildPalms-PalmDatabases` name patterns. The creator ID field exists for documentation and potential future use (e.g., auto-discovery of unclaimed databases by creator).

**Recommendation:** Set it to `"Coog"` since that's what the actual data databases use. The `"Shad"` creator belongs to the ShadowPlan application itself, not the databases you sync.

```json
{
    "X-WildPalms-PalmCreatorId": "Coog",
    "X-WildPalms-PalmDatabases": ["ShadTags", "ShadViews", "ShadFilters", "ShadCat", "ShadP-*"]
}
```

## 6. SDK Version Requirement

The ShadowStan conduit should require WildPalms SDK 0.1.1+ (the version that includes the usb/bluetooth fix and `KPilotLink` interface promotion):

```cmake
find_package(WildPalms 0.1.1 REQUIRED)
```

The `SameMinorVersion` compatibility policy ensures this finds 0.1.1, 0.1.2, etc. but not 0.2.0+.
