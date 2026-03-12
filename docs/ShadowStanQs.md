# ShadowPlan Sync Granularity Rationale

Advisory from ShadowStan developers to WildPalms sync engine designers. This document proposes the record-level granularity needed for each ShadowPlan database type and explains why.

## The Problem: Hierarchical Records Are Order-Dependent

ShadowPlan's `ShadP-*` list databases use a **stack-based level encoding** where the meaning of each record depends on the records that precede it:

```
Record 0: [list header]
Record 1: "Groceries"      level=3  → has children, PUSH (more siblings follow)
Record 2: "  Milk"         level=1  → sibling follows
Record 3: "  Bread"        level=0  → POP (back to Groceries' depth)
Record 4: "Work Tasks"     level=2  → has children, NO PUSH (last sibling)
Record 5: "  Send report"  level=0  → POP + end
```

The `level` byte on each record encodes tree structure relative to the record before it:
- `level=3`: child follows, push onto stack (current node has more siblings)
- `level=2`: child follows, no push (current node is last child)
- `level=1`: sibling follows (same depth)
- `level=0`: pop from stack (return to shallower depth)

This means:
- **Inserting** a record in the middle changes the meaning of subsequent records
- **Deleting** a record can orphan or re-parent its descendants
- **Reordering** records produces a corrupted tree
- **Merging** independently-modified record sets (Palm added records 2-3, PC added records 4-5) requires re-encoding the entire level structure

### Concrete failure example

Palm state: A(level=3) → B(level=1) → C(level=0)
This encodes: root has children A, B, C at the same depth.

Now suppose during a per-record sync:
- Palm deleted record B (the middle sibling)
- PC modified record C's title

A naive per-record sync would delete B and update C, producing:
A(level=3) → C(level=0)

But A's `level=3` means "child follows, push" — so C is now interpreted as A's *child*, not its sibling. The tree structure is silently corrupted.

## Proposed Granularity by Database Type

### `ShadP-*` (task lists): **Whole-file sync**

We propose that `ShadP-*` databases use whole-file replacement rather than per-record diffing. When a conflict exists (both Palm and PC modified the same list), the conduit should present both complete trees to the user for resolution.

**Why whole-file works here:**
- Lists are typically small (tens to low hundreds of records)
- The level encoding makes the entire record sequence a single semantic unit
- ShadowStan already round-trips `.pdb` files faithfully (unknown bytes, trailing data, record metadata all preserved)
- The conflict display (Section 5 of WildPalmsPrep.md) can show tree diffs meaningfully

**What the engine needs to support:**
- A sync mode where the entire database is treated as one atomic unit for change detection and conflict resolution
- Change detection based on the PDB header's `modificationDate` or a content hash of all records, rather than per-record dirty flags
- Conflict resolution that presents "Palm version" vs "PC version" of the complete list

**What about per-record dirty flags?** ShadowPlan on Palm does set per-record dirty flags when individual nodes are edited. These could be used as *hints* for a tree-aware merge algorithm in the future — "these specific nodes changed" — but the actual sync unit must remain the complete ordered record set to preserve structural integrity.

### `ShadTags` (tag definitions): **Per-record sync**

Tag records are independent 52-byte entries (32-byte name + 4-byte timestamp ID + 16 reserved). No record depends on any other. Per-record sync is safe and desirable — if Palm adds a tag and PC renames a different tag, both changes should merge cleanly.

**Record identity:** The 4-byte timestamp ID (bytes 32-35) is a stable unique identifier assigned at creation time. Use this as the pairing key.

### `ShadViews` (named views): **Per-record sync**

View records are independent 66-byte entries. Each view is self-contained with its own name, column mask, and filter reference. No ordering dependency.

**Record identity:** The 4-byte `created` timestamp (bytes 32-35) is the stable unique identifier. ShadowPlan uses this as the view reference stored in list headers.

### `ShadFilters` (custom filters): **Per-record sync**

Filter records are independent entries with a 32-byte name header and variable-length rule data. Each filter is self-contained.

**Record identity:** The 4-byte `created` timestamp in the header is the stable unique identifier. Views reference filters by this timestamp.

### `ShadCat` (categories): **Whole-file sync**

The category database is a single record containing NUL-terminated `filename=category` pairs. It's a flat map with no per-entry identity. Treating it as one atomic unit is simplest.

The record is small (typically under 1KB) and infrequently modified. Whole-file sync with last-writer-wins or user-prompted conflict resolution is appropriate.

## Summary

| Database | Granularity | Record Identity | Rationale |
|---|---|---|---|
| `ShadP-*` | Whole-file | N/A (atomic unit) | Level encoding makes records order-dependent |
| `ShadTags` | Per-record | `created` timestamp (bytes 32-35) | Independent records with stable IDs |
| `ShadViews` | Per-record | `created` timestamp (bytes 32-35) | Independent records with stable IDs |
| `ShadFilters` | Per-record | `created` timestamp (bytes 32-35) | Independent records with stable IDs |
| `ShadCat` | Whole-file | N/A (single record) | Flat map in one record, no per-entry identity |

## What ShadowStan Will Provide

The ShadowStan conduit will implement `recordsEqual()`, `palmToBackend()`, and `backendToPalm()` for all five database types. For per-record databases (tags, views, filters), these work naturally with the existing sync algorithms. For whole-file databases (lists, categories), the conduit will need the engine to support an atomic sync mode.

We're building the record manipulation logic as portable utilities in `libs/shadow` (no WildPalms dependency) so the same encode/decode/compare functions can be reused by other integrations.

## Open Question: Creator ID Claiming

ShadowPlan uses two creator IDs:
- **`"Shad"`** — the ShadowPlan application itself (Palm app creator)
- **`"Coog"`** — all ShadowPlan data databases (ShadP-*, ShadTags, ShadViews, ShadFilters, ShadCat)

The conduit metadata currently specifies `"X-WildPalms-PalmCreatorId": "Shad"`, but the databases we actually sync all have creator `"Coog"`.

**Questions for WildPalms:**
1. Does `X-WildPalms-PalmCreatorId` match against the *application* creator or the *database* creator field?
2. Should we specify `"Coog"` instead (since that's what the databases have)?
3. Or does WildPalms match purely by `X-WildPalms-PalmDatabases` name patterns, making the creator ID informational only?
4. If a conduit needs to claim databases with *different* creator IDs (e.g., if ShadCache uses `"CooD"` while ShadP uses `"Coog"`), does the field accept an array?

## Future: Tree-Aware Merge

Whole-file sync is the safe starting point. A future enhancement could implement tree-aware merging:

1. Use per-record dirty flags to identify which nodes changed on each side
2. Decode both trees into in-memory `ShadowNode` hierarchies
3. Diff the trees structurally (node identity via `createdDate` timestamp)
4. Apply non-conflicting changes to a merged tree
5. Present node-level conflicts for user resolution
6. Re-encode the merged tree with correct level bytes

This is significantly more complex but would allow automatic merging of non-overlapping edits (e.g., Palm edited node A's title while PC edited node B's priority). ShadowStan's `ShadowCodec` already handles encode/decode, and `ShadowNode::createdDate` provides stable per-node identity.

We mention this to inform the engine design — if the atomic-sync mode is designed extensibly, a future tree-aware merge could plug in without engine changes.
