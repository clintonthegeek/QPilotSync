# Palm OS Categories: Architecture, Behaviour, and pilot-link Handling

## Overview

Categories are a core organizational feature of Palm OS, providing a simple
tagging system that lets users group records (memos, contacts, tasks, etc.)
into up to 16 named buckets per application. This document covers how
categories work at the OS level, which ROM apps use them, and how pilot-link
exposes them to desktop software.

---

## 1. The Category System Architecture

### Per-Database, Not Shared

**Categories are defined per-database, not globally.** Each Palm database
(MemoDB, ToDoDB, AddressDB, etc.) maintains its own independent list of 16
categories. There is no shared category registry across applications.

This means:
- "Business" in Memo Pad and "Business" in To Do List are coincidentally
  named but completely independent.
- Renaming a category in one app has zero effect on any other app.
- Each database can use all 16 slots however it wants.

### Where Categories Live: The AppInfo Block

Every Palm database has an optional **AppInfo block** -- a blob of
application-specific metadata stored at the beginning of the database. For
all standard ROM applications, the AppInfo block begins with a
`CategoryAppInfo` structure that defines the 16 category slots.

The AppInfo block is read/written as a unit via the DLP protocol:
- `dlp_ReadAppBlock()` -- read from device
- `dlp_WriteAppBlock()` -- write to device

### The CategoryAppInfo Structure

Defined in `pilot-link/include/pi-appinfo.h`:

```c
typedef struct CategoryAppInfo {
    unsigned int renamed[16];     /* Boolean: has this category been renamed? */
    char name[16][16];            /* 16 categories, 15 chars + null each     */
    unsigned char ID[16];         /* Unique sync-tracking IDs                */
    unsigned char lastUniqueID;   /* Next ID to assign                       */
} CategoryAppInfo_t;
```

**Constraints:**
- Exactly **16 category slots** (indices 0-15). This is a hard limit.
- **15 characters** maximum per category name (16 bytes including null).
- **Slot 0 is always "Unfiled"** -- it cannot be renamed or deleted.
- Slots 1-15 are user-configurable. An empty name (`name[i][0] == '\0'`)
  means the slot is unused and available.

### Binary Layout (278 bytes)

```
Offset   Size    Field
------   ------  -----
0        2       Renamed flags (16-bit bitmap, one bit per category)
2        256     Category names (16 slots x 16 bytes each)
258      16      Category IDs (one byte per slot)
274      1       lastUniqueID
275      3       Padding/gapfill (zeroed)
```

Total: **278 bytes** at the start of every AppInfo block.

### Category IDs (Sync Tracking)

Each category slot has a unique 1-byte ID used for sync tracking:

- **ID 0**: Reserved for "Unfiled" (slot 0)
- **IDs 1-127**: Assigned by the Palm device
- **IDs 128-255**: Assigned by the desktop/PC side

These IDs allow the sync engine to track category identity across renames.
If the user renames "Personal" to "Home" on the Palm, the ID stays the
same, so the desktop sync can detect it was a rename rather than a
delete+create.

The `renamed[i]` boolean flags indicate which categories have been renamed
since the last sync, so the conduit knows which names to update on the
other side.

### How Records Reference Categories

Each database record carries a 1-byte **attributes** field. The category is
encoded in the **lower 4 bits** (the nibble):

```
Bit 7: Deleted    (0x80)
Bit 6: Dirty      (0x40)
Bit 5: Busy       (0x20)
Bit 4: Secret     (0x10)
Bit 3: Archived   (0x08)
Bits 2-0: (unused in some contexts)

Lower nibble (bits 3-0): Category index (0-15)
```

From `pilot-link/libpisock/pi-file.c`:

```c
/* Reading: split attributes byte into flags and category */
*recattrs = entp->attrs & 0xf0;    /* Upper nibble: flags   */
*category = entp->attrs & 0x0f;    /* Lower nibble: category */

/* Writing: combine flags and category into attributes byte */
entp->attrs = (recattrs & 0xf0) | (category & 0x0f);
```

**Important**: The category is a 4-bit index (0-15), not a name. To get
the human-readable name, you look up `appinfo.category.name[index]`.

---

## 2. Which ROM Apps Support Categories

### The Standard Category Dialog

Palm OS provides a **system-level category editing UI**. All apps that
support categories use the same dialog (or a very similar one), which is
why the "Edit Categories" experience looks identical in Memo Pad, To Do
List, Address Book, Notepad, and Expense. This is an OS API, not something
each app re-implements.

The dialog provides:
- A list of current category names
- "New" button (finds first empty slot, assigns next unique ID)
- "Rename" button (updates name, sets renamed flag)
- "Delete" button (clears the name, moves affected records to Unfiled)

### Per-Application Support

| Application      | Database      | Categories? | Notes |
|------------------|---------------|:-----------:|-------|
| **Memo Pad**     | `MemoDB`      | Yes | Standard categories. AppInfo also stores `sortByAlpha`. |
| **To Do List**   | `ToDoDB`      | Yes | Standard categories. AppInfo also stores `sortByPriority`. |
| **Address Book** | `AddressDB`   | Yes | Standard categories. AppInfo also stores custom field labels, phone labels, sort order. |
| **Notepad**      | `NotepadDB`   | Yes | The simple drawing/handwriting app. Standard categories. |
| **Expense**      | `ExpenseDB`   | Yes | Standard categories. AppInfo also stores sort order and custom currencies. |
| **Date Book**    | `DatebookDB`  | **No (UI)** | See below. |

### Date Book: The Notable Exception

The stock Date Book application on **Palm OS 4.1 does not expose categories
in its user interface**. There is no category picker, no "Edit Categories"
menu item, and no way to assign a category to an event through the app.

However, **the database structure supports categories**. The
`AppointmentAppInfo_t` structure in pilot-link includes a full
`CategoryAppInfo` field:

```c
typedef struct AppointmentAppInfo {
    struct CategoryAppInfo category;  /* Standard 278-byte category block */
    int startOfWeek;                  /* 0=Sunday, 1=Monday               */
} AppointmentAppInfo_t;
```

This means:
- The `DatebookDB` database **does** have a CategoryAppInfo block.
- Records **do** have category nibbles in their attributes bytes.
- But the stock Date Book app ignores all of this -- all events are
  effectively Unfiled (category 0).

Third-party replacements like **DateBk6** (by Pimlico Software) *do* use
Datebook categories, which is why the infrastructure exists in the
database. DateBk6 added a full category UI to the calendar.

**For WildPalms**: We should read/write Datebook categories correctly at
the data layer (they're in the database), but we shouldn't be surprised if
stock Palm OS Date Book shows everything as uncategorized.

### Third-Party Applications

The category system is a **public Palm OS API**. Any application can use
it by:

1. Including a `CategoryAppInfo` block at the start of its AppInfo data
2. Using the standard category editing APIs (`CategoryEdit`, `CategorySelect`, etc.)
3. Storing the category index in the lower nibble of each record's
   attributes byte

Most well-written Palm apps follow this convention. The Palm OS SDK
provides these functions:

- `CategoryInitialize()` -- set up default categories for a new database
- `CategoryEdit()` -- show the standard category editor dialog
- `CategorySelect()` -- show the category picker popup
- `CategoryGetName()` -- look up a category name by index
- `CategoryFind()` -- find a category index by name
- `CategorySetName()` -- rename a category
- `CategoryGetNext()` -- iterate through categories

These are documented in the *Palm OS Reference* (Chapter: Categories).

---

## 3. How pilot-link Handles Categories

### Unpacking (Device -> Desktop)

From `pilot-link/libpisock/appinfo.c`:

```c
int unpack_CategoryAppInfo(CategoryAppInfo_t *ai,
                           const unsigned char *record, size_t len)
{
    /* Minimum size check: 2 + 256 + 16 + 4 = 278 bytes */
    if (len < 2 + 16 * 16 + 16 + 4)
        return 0;

    /* Renamed flags: 16-bit bitmap */
    int rec = get_short(record);
    for (i = 0; i < 16; i++)
        ai->renamed[i] = (rec & (1 << i)) ? 1 : 0;
    record += 2;

    /* Category names: 16 x 16 bytes */
    for (i = 0; i < 16; i++) {
        memcpy(ai->name[i], record, 16);
        record += 16;
    }

    /* Category IDs: 16 bytes */
    memcpy(ai->ID, record, 16);
    record += 16;

    /* lastUniqueID: 1 byte + 3 bytes padding */
    ai->lastUniqueID = get_byte(record);

    return 278;  /* bytes consumed */
}
```

### Packing (Desktop -> Device)

```c
int pack_CategoryAppInfo(const CategoryAppInfo_t *ai,
                         unsigned char *record, size_t len)
{
    /* If record is NULL, just return the size needed */
    if (!record) return 278;

    /* Renamed flags bitmap */
    int rec = 0;
    for (i = 0; i < 16; i++)
        if (ai->renamed[i]) rec |= (1 << i);
    set_short(record, rec);
    record += 2;

    /* Category names */
    for (i = 0; i < 16; i++) {
        memcpy(record, ai->name[i], 16);
        record += 16;
    }

    /* Category IDs */
    memcpy(record, ai->ID, 16);
    record += 16;

    /* lastUniqueID + 3 bytes gapfill */
    set_byte(record, ai->lastUniqueID);
    record++;
    set_byte(record, 0);
    set_short(record + 1, 0);

    return 278;
}
```

### Per-App AppInfo Structures

Each app wraps `CategoryAppInfo` inside its own AppInfo structure. The
category block is always the **first field** (or nearly so -- some have a
`type` enum before it):

```c
/* Memo Pad */
typedef struct MemoAppInfo {
    memoType type;                       /* enum: memo_v1     */
    struct CategoryAppInfo category;     /* 278 bytes         */
    int sortByAlpha;                     /* sort preference   */
} MemoAppInfo_t;

/* To Do List */
typedef struct ToDoAppInfo {
    todoType type;                       /* enum: todo_v1     */
    struct CategoryAppInfo category;     /* 278 bytes         */
    int dirty;
    int sortByPriority;
} ToDoAppInfo_t;

/* Address Book */
typedef struct AddressAppInfo {
    addressType type;
    struct CategoryAppInfo category;
    char labels[22][16];                 /* Custom field labels */
    int labelRenamed[22];
    char phoneLabels[8][16];
    int country;
    int sortByCompany;
} AddressAppInfo_t;

/* Date Book */
typedef struct AppointmentAppInfo {
    struct CategoryAppInfo category;     /* Note: no type field! */
    int startOfWeek;
} AppointmentAppInfo_t;

/* Expense */
typedef struct ExpenseAppInfo {
    struct CategoryAppInfo category;     /* Note: no type field! */
    enum ExpenseSort sortOrder;
    struct ExpenseCustomCurrency currencies[4];
} ExpenseAppInfo_t;

/* Notepad (drawings) */
typedef struct NotePadAppInfo {
    int dirty, sortByPriority;
    struct CategoryAppInfo category;     /* Note: dirty/sort come first! */
} NotePadAppInfo_t;
```

**Important**: The offset of `CategoryAppInfo` within the AppInfo block
varies by application. For Datebook and Expense it's at offset 0. For
Memo, ToDo, and Address it's after a type enum (typically 4 bytes). For
Notepad it's after two ints (8 bytes). When writing back a modified
AppInfo block, you must preserve the app-specific fields before and after
the category data.

### DLP Protocol

Records are read/written with explicit category parameters:

```c
/* Reading a record by index */
int dlp_ReadRecordByIndex(int sd, int dbhandle, int recindex,
    pi_buffer_t *buffer, recordid_t *recuid, int *attr, int *category);

/* Writing a record */
int dlp_WriteRecord(int sd, int dbhandle, int attr, recordid_t recuid,
    int category, const void *data, size_t size, recordid_t *newuid);
```

The DLP layer handles splitting/combining the attributes byte
automatically. When you call `dlp_ReadRecordByIndex`, it gives you the
record flags and category as separate values. When you call
`dlp_WriteRecord`, you pass them separately and the DLP layer combines
them.

---

## 4. Category Lifecycle During Sync

### Reading from Palm

1. Open the database (`dlp_OpenDB`)
2. Read the AppInfo block (`dlp_ReadAppBlock`)
3. Unpack the `CategoryAppInfo` from the beginning of the AppInfo data
4. For each record read, extract the category index from the attributes byte
5. Look up the category name: `appinfo.category.name[record_category]`

### Writing to Palm

1. If new categories were created on the desktop:
   - Find empty slots (1-15) in the CategoryAppInfo
   - Assign the name and a new unique ID (>= 128 for desktop-created)
   - Increment `lastUniqueID`
2. Pack the modified CategoryAppInfo back into the AppInfo block
3. Write the AppInfo block (`dlp_WriteAppBlock`)
4. For each record, set the category index in the attributes byte
5. Write the record (`dlp_WriteRecord`)

### Handling Renamed Categories

When a category is renamed:
1. The `renamed[i]` flag is set to 1
2. The `name[i]` is updated to the new name
3. The `ID[i]` stays the same
4. All records in that category keep their category index -- they
   automatically get the new name since it's a lookup

### Handling Deleted Categories

When a category is deleted:
1. The `name[i]` is cleared (set to empty string)
2. All records that were in that category are moved to Unfiled (index 0)
3. The slot becomes available for reuse

---

## 5. Practical Notes for WildPalms

### Reading Categories Correctly

The WildPalms `CategoryInfo` class (`src/palm/categoryinfo.h`) wraps
`CategoryAppInfo_t` and handles:
- Parsing from raw AppInfo data
- Name lookup by index and reverse lookup by name
- Creating new categories with proper ID assignment
- Packing modified categories back for writing

Category names use **Windows-1252 encoding** on the Palm, which
`CategoryInfo` handles via encode/decode helpers.

### The "category: 2" Problem

When a category name lookup fails (e.g., categories weren't loaded, or
the AppInfo parse had an error), the memo mapper falls back to writing
the raw category index number into the frontmatter:

```yaml
category: 2       # <-- Index, not name. Something went wrong.
```

vs. the correct output:

```yaml
category: Personal   # <-- Resolved name
```

This fallback is in `MemoMapper::memoToMarkdown()` at the line:
```cpp
} else if (memo.category > 0) {
    markdown += QStringLiteral("category: %1\n").arg(memo.category);
}
```

### AppInfo Block Offsets

When writing back modified categories, the category data must be written
at the correct offset within the AppInfo block. The approach used by the
conduits is to:
1. Save the entire original AppInfo block
2. Overwrite just the CategoryAppInfo portion
3. Write the whole block back

This preserves app-specific data (sort preferences, custom labels, etc.)
that follows the category block.

### Date Book Categories in Practice

Since stock Palm OS Date Book doesn't expose categories in the UI, all
events synced from a stock Palm will have category 0 (Unfiled). If a
user has DateBk6 or another category-aware calendar replacement, events
may have real categories. WildPalms should handle both cases gracefully.
