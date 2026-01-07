# Data Loss Handling: Architecture & UX Options

This document explores approaches to warn users about and handle data loss during format mapping. These patterns apply to QPilotSync, PlanStanLite, and any system that bridges incompatible data formats.

---

## The Problem Space

When mapping between formats with different capabilities, we face several categories of data loss:

### 1. **Feature Gaps** (Unsupported Capabilities)
- Palm has no photo field (classic Address)
- Palm has one address per contact, vCard allows many
- Palm has 5 phone fields, modern contacts have unlimited
- Palm alarms are single, iCal supports multiple

### 2. **Precision Loss** (Downgrading Rich Data)
- Complex RRULE → Simple Palm repeat type
- UTC+TZID → Floating time (no timezone)
- Rich text → Plain text
- Multiple categories → Single category

### 3. **Capacity Limits** (Truncation)
- Memo: ~32KB max
- Contact fields: ~255 chars each
- Note fields: Similar limits
- 16 categories max per database

### 4. **Semantic Loss** (Meaning Changes)
- "Every 3rd Thursday" → "Monthly by day" (close but not exact)
- Email in phone field (Palm classic workaround)
- Custom field mappings that lose original context

---

## Architectural Approaches

### Option A: **Mapper Result Objects** (Recommended)

Instead of mappers returning just the converted data, return a result object that includes conversion metadata:

```cpp
struct MapperResult<T> {
    T data;                              // The converted data
    bool isLossy;                        // Was any data lost?
    QList<DataLossWarning> warnings;     // Specific warnings
    QMap<QString, QVariant> overflow;    // Data that couldn't be mapped
};

struct DataLossWarning {
    enum Severity { Info, Warning, Error };
    enum Category { Truncated, Unsupported, Downgraded, Overflow };

    Severity severity;
    Category category;
    QString field;           // Which field was affected
    QString originalValue;   // What the original value was
    QString resultValue;     // What it became (or empty if dropped)
    QString message;         // Human-readable explanation
};
```

**Usage:**
```cpp
MapperResult<PilotRecord*> result = ContactMapper::packContact(contact);

if (result.isLossy) {
    for (const auto& warning : result.warnings) {
        qDebug() << warning.field << ":" << warning.message;
    }
}
```

**Pros:**
- Warnings are generated at the point of knowledge (the mapper)
- UI layer can decide how to present them
- Supports batch processing with aggregated warnings
- Can be logged, displayed, or silently recorded

**Cons:**
- More complex mapper API
- Need to handle result objects everywhere

---

### Option B: **Signal-Based Warnings**

Mappers emit signals during conversion:

```cpp
class ContactMapper : public QObject {
    Q_OBJECT
signals:
    void dataLossWarning(const QString &field, const QString &message);
    void conversionProgress(int current, int total);
};
```

**Pros:**
- Simpler mapper return values
- Real-time feedback during long operations
- Easy to connect to logging or UI

**Cons:**
- Harder to associate warnings with specific records in batch
- QObject overhead for mappers

---

### Option C: **Two-Phase Conversion** (Validate-Then-Convert)

Separate validation from conversion:

```cpp
class ContactMapper {
public:
    // Phase 1: Check what will be lost
    static ValidationResult validate(const Contact &contact);

    // Phase 2: Perform conversion (caller has acknowledged warnings)
    static PilotRecord* packContact(const Contact &contact);
};

struct ValidationResult {
    bool canConvert;                     // false = critical incompatibility
    QList<DataLossWarning> warnings;     // What will be lost
    QMap<QString, QString> suggestions;  // "Use custom field 1 for second email"
};
```

**Pros:**
- Clear separation of concerns
- UI can show preview before committing
- Supports "dry run" mode

**Cons:**
- Two passes over data
- Validation and conversion logic can drift apart

---

### Option D: **Overflow Sidecars**

Store unmappable data in companion files:

```
contacts/
├── john_smith.vcf           # Palm-compatible subset
├── john_smith.overflow.json # Everything else
```

```json
{
  "palm_record_id": 12345,
  "unmapped_fields": {
    "PHOTO": "base64...",
    "additional_emails": ["john2@example.com"],
    "social_profiles": [...]
  }
}
```

**Pros:**
- No data is truly lost
- Can reconstruct full record if syncing to richer target
- Clear audit trail

**Cons:**
- More files to manage
- Sidecar can become stale
- Adds complexity to sync logic

---

## UX Approaches

### UX Option 1: **Pre-Flight Warning Dialog**

Before import/sync, analyze all records and show a summary:

```
┌─────────────────────────────────────────────────────┐
│ Import Preview: 15 contacts                         │
├─────────────────────────────────────────────────────┤
│ ⚠ 3 contacts will lose data:                        │
│                                                     │
│   John Smith                                        │
│   • Photo will be dropped (Palm classic)            │
│   • 2 of 4 email addresses will be dropped          │
│                                                     │
│   Jane Doe                                          │
│   • Home address will be dropped (using Work)       │
│                                                     │
│   Acme Corp                                         │
│   • Note truncated from 45KB to 32KB                │
│                                                     │
│ ☐ Don't show this warning again                     │
│                                                     │
│        [Cancel]  [View Details...]  [Import Anyway] │
└─────────────────────────────────────────────────────┘
```

**Pros:**
- User knows what they're getting into
- Can cancel before any changes
- Shows aggregate impact

**Cons:**
- Extra step for every operation
- Users may learn to click through without reading

---

### UX Option 2: **Inline Annotations in Exported Files**

When exporting from Palm, add comments about what was preserved:

```markdown
---
id: 12345
category: Business
palm_export_note: "Full Palm record preserved"
---

Meeting notes...
```

When importing to Palm, add notes about what was lost:

```markdown
---
id: 12345
category: Business
import_warnings:
  - "Photo dropped (unsupported on Palm classic)"
  - "Second email moved to Note field"
---
```

**Pros:**
- Persistent record of transformations
- User can review at leisure
- Git-trackable

**Cons:**
- Clutters files
- May not be seen if user doesn't open files

---

### UX Option 3: **Sync Report**

Generate a detailed report after sync:

```
═══════════════════════════════════════════════════════
 QPilotSync Report - 2026-01-07 14:30
═══════════════════════════════════════════════════════

IMPORTED TO PALM: 15 contacts, 8 events, 3 todos

WARNINGS:
─────────────────────────────────────────────────────
CONTACTS (3 with data loss):
  • John Smith: Photo dropped
  • Jane Doe: Extra emails moved to Note
  • Acme Corp: Note truncated (45KB → 32KB)

CALENDAR (2 with modifications):
  • Team Meeting: Complex recurrence simplified
    Original: RRULE:FREQ=MONTHLY;BYDAY=2TH,4TH
    Converted: RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TH
    ⚠ May not match exactly on months with 5 weeks

  • Birthday: Yearly repeat, no changes

FULL REPORT: ~/.qpilotsync/reports/2026-01-07_143000.log
═══════════════════════════════════════════════════════
```

**Pros:**
- Complete record of what happened
- User can review after the fact
- Supports automation (exit codes, parseable logs)

**Cons:**
- User may not read reports
- After-the-fact (data already changed)

---

### UX Option 4: **Smart Defaults with Override**

Make intelligent decisions automatically, but allow per-record override:

```cpp
struct MappingPolicy {
    // Recurrence handling
    enum RecurrenceMode {
        Simplify,           // Convert to nearest Palm type
        Expand,             // Create multiple single events
        AskUser             // Prompt for each complex rule
    };

    // Truncation handling
    enum TruncationMode {
        SilentTruncate,     // Just truncate
        TruncateWithMarker, // Add "...[truncated]"
        SplitRecords,       // Create multiple records
        AskUser
    };

    // Photo handling
    enum PhotoMode {
        Drop,               // Just remove it
        MoveToNote,         // "Photo: see john_smith.jpg"
        StoreExternal       // Save to companion file
    };

    RecurrenceMode recurrence = Simplify;
    TruncationMode truncation = TruncateWithMarker;
    PhotoMode photos = MoveToNote;
};
```

**Pros:**
- Sensible behavior out of the box
- Power users can customize
- Can be saved as profiles

**Cons:**
- Complexity in settings UI
- May still surprise users

---

## Special Case: Recurrence Expansion

Complex iCal RRULEs that Palm can't represent natively:

```
RRULE:FREQ=MONTHLY;BYDAY=1MO,3WE;COUNT=24
(First Monday and third Wednesday of each month, 24 occurrences)
```

### Expansion Strategy Options:

**A. Simplify (Lossy)**
- Convert to nearest Palm type
- Warning: "Pattern simplified, may not match exactly"

**B. Expand to Instances**
- Generate 24 individual events
- Each event gets X-EXPANDED-FROM:original-uid
- Warning: "Created 24 events from recurring pattern"
- **Problem**: Editing one doesn't update others

**C. Hybrid Approach**
- Create Palm repeating event for the "closest fit"
- Add exception dates for mismatches
- Create individual events for exceptions
- **Complex but most accurate**

**D. User Choice Per Pattern**
```
┌─────────────────────────────────────────────────────┐
│ Complex Recurrence Detected                         │
├─────────────────────────────────────────────────────┤
│ Event: Team Standup                                 │
│ Pattern: First Monday and third Wednesday monthly   │
│                                                     │
│ Palm cannot represent this exactly. Choose:         │
│                                                     │
│ ○ Simplify to "Every 2 weeks on Mon, Wed"           │
│   (May drift over time)                             │
│                                                     │
│ ● Create 24 separate events                         │
│   (Exact dates, but no automatic recurrence)        │
│                                                     │
│ ○ Skip this event (don't sync to Palm)              │
│                                                     │
│ ☐ Remember this choice for similar patterns         │
│                                                     │
│                          [Cancel]  [Apply]          │
└─────────────────────────────────────────────────────┘
```

---

## Recommended Architecture

Based on the analysis, I recommend a **layered approach**:

### Layer 1: Mapper Results (Required)
All mappers return `MapperResult<T>` with warnings embedded. This is non-negotiable for traceability.

### Layer 2: Policy Engine (Configurable)
A `MappingPolicy` object controls automatic decisions. Defaults are conservative (warn, don't silently drop).

### Layer 3: UI Handlers (Pluggable)
- **CLI mode**: Print warnings to stderr, continue
- **GUI mode**: Show dialog for severe warnings, aggregate minor ones
- **Batch mode**: Log to report file, exit with warning code

### Layer 4: Audit Trail (Persistent)
- Log all transformations to `~/.qpilotsync/audit.log`
- Store overflow data in sidecar files (optional)
- Include X-QPILOTSYNC-WARNINGS in exported files

---

## Implementation Priority

1. **MapperResult structure** - Foundation for everything else
2. **Warning generation in mappers** - Detect and report issues
3. **Basic logging** - Ensure nothing is silently lost
4. **Pre-flight validation** - Catch issues before commit
5. **Sync report generation** - Post-sync summary
6. **Policy engine** - User-configurable handling
7. **Sidecar files** - Zero-loss option for power users

---

## Applicability to PlanStanLite

This architecture applies directly to PlanStanLite for:
- CalDAV ↔ Local file mapping
- Multi-calendar merging
- Backend capability differences (some backends don't support alarms, attachments, etc.)

The `MapperResult` pattern would work well in PlanStanLite's backend abstraction layer.

---

**Document Version**: 1.0
**Last Updated**: 2026-01-07
**Status**: Design Options (Pending Decision)
