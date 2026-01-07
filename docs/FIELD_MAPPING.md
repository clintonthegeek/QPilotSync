# Palm ↔ iCalendar/vCard Field Mapping Reference

This document defines how Palm Pilot data structures map to modern iCalendar and vCard formats.

---

## Calendar: Palm Datebook ↔ iCalendar VEVENT

### Classic Datebook (Palm OS 1.x - 4.x)

**Palm Structure: `Appointment_t`** (from `pi-datebook.h`)

| Palm Field | Type | iCalendar Property | Notes |
|------------|------|-------------------|-------|
| `begin` | `struct tm` | `DTSTART` | Convert to UTC or floating time |
| `end` | `struct tm` | `DTEND` | Convert to UTC or floating time |
| `description` | `char*` | `SUMMARY` | First line of description |
| `note` | `char*` | `DESCRIPTION` | Full notes field |
| `alarm` | `int` | `VALARM` | Minutes before event |
| `repeat` | `RepeatType` | `RRULE` | See repeat mapping below |
| `repeatEnd` | `struct tm` | `RRULE:UNTIL` | End date for repeats |
| `repeatFrequency` | `int` | `RRULE:INTERVAL` | Every N days/weeks/months |
| `repeatDay[]` | `int[7]` | `RRULE:BYDAY` | For weekly repeats |
| `repeatWeekstart` | `int` | `RRULE:WKST` | Week start day |
| `exceptions` | `int` | `EXDATE` | Exception dates |
| `untimed` | `bool` | `DTSTART` (DATE) | All-day event if true |

**Repeat Type Mapping:**

| Palm `RepeatType` | iCalendar `RRULE:FREQ` | Example |
|-------------------|------------------------|---------|
| `repeatNone` (0) | (no RRULE) | Single event |
| `repeatDaily` (1) | `FREQ=DAILY` | `RRULE:FREQ=DAILY;INTERVAL=1` |
| `repeatWeekly` (2) | `FREQ=WEEKLY` | `RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR` |
| `repeatMonthlyByDay` (3) | `FREQ=MONTHLY;BYMONTHDAY` | `RRULE:FREQ=MONTHLY;BYMONTHDAY=15` |
| `repeatMonthlyByDate` (4) | `FREQ=MONTHLY;BYDAY` | `RRULE:FREQ=MONTHLY;BYDAY=2MO` (2nd Monday) |
| `repeatYearly` (5) | `FREQ=YEARLY` | `RRULE:FREQ=YEARLY` |

**Timezone Handling:**
- **Problem**: Palm stores local time only, no timezone info
- **Solution**: Use floating time in iCalendar (no TZID)
- **Alternative**: Assume system timezone and convert to UTC+TZID

**Category Mapping:**
- Palm: 16 categories max per database
- iCalendar: `CATEGORIES` property (comma-separated)
- Category names stored in AppInfo block

**Alarm Mapping:**
- Palm: `alarm` field = minutes before event
- iCalendar:
```
BEGIN:VALARM
ACTION:DISPLAY
TRIGGER:-PT{alarm}M
DESCRIPTION:Reminder
END:VALARM
```

### Extended Calendar (Palm OS 5.x)

**Palm Structure: `CalendarEvent_t`** (from `pi-calendar.h`)

Additional fields beyond classic Datebook:

| Palm Field | iCalendar Property | Notes |
|------------|-------------------|-------|
| `location` | `LOCATION` | Event location |
| `timezone` | `DTSTART;TZID` | Full timezone support |
| `blob` | `ATTACH` | Attachments (photos, files) |

---

## Contacts: Palm Address ↔ vCard

### Classic Address Book (Palm OS 1.x - 4.x)

**Palm Structure: `Address_t`** (from `pi-address.h`)

**19 Standard Fields:**

| Palm Field | Index | vCard 3.0 Property | Notes |
|------------|-------|-------------------|-------|
| `entry[0]` | `entryLastname` | `N:surname` | Last name |
| `entry[1]` | `entryFirstname` | `N:given` | First name |
| `entry[2]` | `entryCompany` | `ORG` | Company/organization |
| `entry[3]` | `entryPhone1` | `TEL;TYPE=WORK` | Work phone |
| `entry[4]` | `entryPhone2` | `TEL;TYPE=HOME` | Home phone |
| `entry[5]` | `entryPhone3` | `TEL;TYPE=FAX` | Fax |
| `entry[6]` | `entryPhone4` | `TEL;TYPE=PAGER` | Other |
| `entry[7]` | `entryPhone5` | `TEL;TYPE=CELL` | Mobile |
| `entry[8]` | `entryAddress` | `ADR;TYPE=WORK` | Work address |
| `entry[9]` | `entryCity` | `ADR;TYPE=WORK:locality` | Work city |
| `entry[10]` | `entryState` | `ADR;TYPE=WORK:region` | Work state |
| `entry[11]` | `entryZip` | `ADR;TYPE=WORK:postal-code` | Work ZIP |
| `entry[12]` | `entryCountry` | `ADR;TYPE=WORK:country` | Work country |
| `entry[13]` | `entryTitle` | `TITLE` | Job title |
| `entry[14]` | `entryCustom1` | `X-PALM-CUSTOM1` | Custom field 1 |
| `entry[15]` | `entryCustom2` | `X-PALM-CUSTOM2` | Custom field 2 |
| `entry[16]` | `entryCustom3` | `X-PALM-CUSTOM3` | Custom field 3 |
| `entry[17]` | `entryCustom4` | `X-PALM-CUSTOM4` | Custom field 4 |
| `entry[18]` | `entryNote` | `NOTE` | Notes field |

**Additional Metadata:**

| Palm Field | vCard Property | Notes |
|------------|---------------|-------|
| `showPhone` | `TEL;PREF` | Preferred phone field |
| `phoneLabel[]` | `TEL;TYPE=X-{label}` | Custom phone labels |

**vCard Example:**
```
BEGIN:VCARD
VERSION:3.0
N:Smith;John;;;
FN:John Smith
ORG:Acme Corp
TITLE:Software Engineer
TEL;TYPE=WORK:555-1234
TEL;TYPE=CELL;PREF:555-5678
ADR;TYPE=WORK:;;123 Main St;Springfield;IL;62701;USA
EMAIL:john@example.com
NOTE:Prefers email contact
CATEGORIES:Business,Tech
END:VCARD
```

### Extended Contacts (Palm OS 5.x)

**Palm Structure: `struct Contact`** (from `pi-contact.h`)

**39 Fields total**, including:

| Palm Field | vCard Property | Notes |
|------------|---------------|-------|
| `name.prefix` | `N:honorific-prefix` | Mr., Dr., etc. |
| `name.first` | `N:given` | First name |
| `name.middle` | `N:additional` | Middle name |
| `name.last` | `N:surname` | Last name |
| `name.suffix` | `N:honorific-suffix` | Jr., PhD, etc. |
| `email[0]` | `EMAIL;TYPE=WORK` | Work email |
| `email[1]` | `EMAIL;TYPE=HOME` | Personal email |
| `email[2]` | `EMAIL;TYPE=OTHER` | Other email |
| `website` | `URL` | Website |
| `birthday` | `BDAY` | Birthday (ISO 8601) |
| `anniversary` | `X-ANNIVERSARY` | Anniversary |
| `photo` | `PHOTO;ENCODING=b;TYPE=JPEG` | JPEG photo (base64) |
| `im[0]` | `X-AIM` | AIM handle |
| `im[1]` | `X-MSN` | MSN handle |

**Photo Handling:**
- Palm: JPEG blob in contact record
- vCard: Base64-encoded PHOTO property
- Size limit: Recommend <100KB

---

## Tasks: Palm ToDo ↔ iCalendar VTODO

**Palm Structure: `ToDo_t`** (from `pi-todo.h`)

| Palm Field | iCalendar Property | Notes |
|------------|-------------------|-------|
| `description` | `SUMMARY` | Task description |
| `note` | `DESCRIPTION` | Detailed notes |
| `priority` | `PRIORITY` | 1 (high) to 5 (low) |
| `complete` | `STATUS` | `COMPLETED` or `NEEDS-ACTION` |
| `due` | `DUE` | Due date |
| `indefinite` | (no DUE) | No due date if true |

**Priority Mapping:**

| Palm Priority | iCalendar PRIORITY | Description |
|---------------|-------------------|-------------|
| 1 | 1 | High/Urgent |
| 2 | 3 | High |
| 3 | 5 | Medium (default) |
| 4 | 7 | Low |
| 5 | 9 | Very Low |

**Example VTODO:**
```
BEGIN:VTODO
UID:todo-12345
SUMMARY:Finish report
DESCRIPTION:Quarterly sales report for Q4
PRIORITY:3
DUE:20260115T170000
STATUS:NEEDS-ACTION
CATEGORIES:Work
END:VTODO
```

---

## Memos: Palm Memo ↔ Markdown

**Palm Structure: `Memo_t`** (from `pi-memo.h`)

| Palm Field | File Format | Notes |
|------------|------------|-------|
| `text` | Markdown file with YAML frontmatter | UTF-8 encoded |
| Category | Directory name + frontmatter field | One directory per category |

**Storage Format: Markdown with YAML Frontmatter**

Memos are stored as Markdown files (`.md`) with YAML frontmatter metadata:

```
memos/
├── Business/
│   ├── meeting-notes.md
│   └── ideas.md
├── Personal/
│   └── shopping-list.md
└── Unfiled/
    └── random-thought.md
```

**File Format Example:**
```markdown
---
title: Meeting Notes
category: Business
created: 2026-01-05T14:30:00
modified: 2026-01-05T14:30:00
palm-uid: 12345
---

Meeting with Bob about Q1 goals...

## Action Items
- Review budget
- Schedule follow-up
```

**Frontmatter Fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `title` | string | Yes | First line of memo or generated title |
| `category` | string | Yes | Palm category name |
| `created` | ISO 8601 | Yes | Creation timestamp |
| `modified` | ISO 8601 | Yes | Last modification timestamp |
| `palm-uid` | integer | No | Palm record UID for sync tracking |

**Filename Generation:**
- Sanitize title for filesystem: `title.lower().replace(' ', '-').replace('/', '-')`
- Limit to 50 characters
- Ensure uniqueness by appending number if needed
- Example: `meeting-notes.md`, `meeting-notes-2.md`

---

## Categories: Palm AppInfo ↔ iCalendar/vCard CATEGORIES

**Palm Structure: `CategoryAppInfo_t`** (from `pi-appinfo.h`)

```c
struct CategoryAppInfo {
    int renamed[16];        // Flags for renamed categories
    char name[16][16];      // Category names (15 chars max)
    int lastUniqueID[16];   // Unique IDs for categories
    unsigned char ID[16];   // Category IDs
};
```

**Mapping:**
- Palm category 0 = "Unfiled" (default)
- Palm categories 1-15 = User-defined
- iCalendar: `CATEGORIES:Business,Personal,Tech`
- Multiple categories allowed in iCal, Palm allows only one per record

**Handling Overflow:**
- When importing iCal with >15 categories:
  - Map first 15 categories
  - Concatenate remainder into 16th category "Other"
  - Or: Use primary category only, store rest in note

---

## Conflict Resolution Metadata

Store sync metadata in X- properties:

```
X-PALM-UID:12345
X-PALM-CATEGORY:3
X-PALM-MODIFIED:20260105T143000Z
X-PALM-SYNCED:20260105T143000Z
```

This allows:
- Tracking Palm record IDs
- Detecting modifications
- Preserving Palm-specific data

---

## Data Loss Scenarios

### Palm → iCalendar
- **Private flag**: Map to `CLASS:PRIVATE` vs `CLASS:PUBLIC`
- **Repeat exceptions**: iCalendar supports, ensure proper mapping
- **Custom labels**: Store in X- properties if needed

### iCalendar → Palm
- **Complex RRULEs**: Simplify to nearest Palm repeat type
- **Timezones**: Convert to floating time or system timezone
- **Multiple alarms**: Palm supports one alarm only (use earliest)
- **Attachments**: Palm OS 5+ only, ignore on older devices

### vCard → Palm
- **Multiple addresses**: Palm classic has one address (choose WORK or HOME)
- **Multiple emails**: Palm classic has no email field (use note)
- **Photos**: Palm OS 5+ only
- **Extended character sets**: Ensure proper encoding

---

## Testing Checklist

For each data type, test:
- [ ] Simple record (minimal fields)
- [ ] Complex record (all fields)
- [ ] Unicode/international characters
- [ ] Empty fields
- [ ] Maximum field lengths
- [ ] Repeating events (all types)
- [ ] Exceptions and modifications
- [ ] Categories
- [ ] Private/public flags
- [ ] Alarms
- [ ] All-day events
- [ ] Multi-day events

---

**Document Version**: 1.0
**Last Updated**: 2026-01-05
**Next Review**: During Phase 2 implementation
