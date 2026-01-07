# Major Challenges and Mitigation Strategies

## 1. Qt4 → Qt6 Code Translation

### Challenge
KPilot is written in Qt4/KDE4. We cannot directly reuse the code in Qt6/KF6. Many APIs have changed, and some KDE4 libraries don't have direct Qt6 equivalents.

### Impact
- Cannot copy-paste KPilot UI code
- Must reimplement all UI from scratch
- Signal/slot syntax has changed
- QObject parent/child memory management patterns differ

### Mitigation
1. **Conceptual Reuse**: Study KPilot's architecture, not its code
2. **Modern Patterns**: Use Qt6 best practices (lambda slots, smart pointers)
3. **PlanStanLite as Template**: Copy proven Qt6 patterns from PlanStanLite
4. **Incremental Validation**: Build small, test each component
5. **Qt6 Documentation**: Study Qt6 examples extensively

### Risk Level: **Medium**
We have good architectural guidance, just need to translate concepts to modern Qt.

---

## 2. Palm Data Structure Complexity

### Challenge
Palm databases have intricate binary formats with:
- Variable-length fields
- Bit-packed flags
- Category indices
- Custom fields
- Extended record formats (Palm OS 5.x)
- Attachment blobs (photos)

### Impact
- Easy to corrupt data with incorrect parsing
- Off-by-one errors can scramble entire database
- Endianness issues (Palm is big-endian)
- Need to handle both classic and extended formats

### Mitigation
1. **Use pilot-link Functions**: Never hand-parse binary data
2. **pilot-link Pack/Unpack**: Use `pack_Appointment()`, `unpack_Address()`, etc.
3. **Validation**: Checksum and validate all parsed data
4. **Read-Only Phase**: Perfect reading before attempting writes
5. **Extensive Testing**: Test with real Palm databases
6. **Backup First**: Always backup before write operations

### Risk Level: **High → Low** (with pilot-link)
pilot-link abstracts away most complexity. Just use their functions correctly.

---

## 3. Sync Algorithm Complexity

### Challenge
Three-way synchronization is algorithmically complex:
- Must track three states: Palm, PC, Backup
- Detecting deletions requires backup comparison
- Conflict resolution has many edge cases
- Clock skew can cause false conflicts
- Record ID mapping between systems

### Impact
- Easy to create duplicate records
- Easy to lose data if logic is wrong
- User frustration from unexpected conflicts
- Hard to test all edge cases

### Mitigation
1. **Start Conservative**: When uncertain, ask user (don't auto-resolve)
2. **Extensive Logging**: Log every decision made during sync
3. **ID Mapping Database**: Store Palm ↔ PC ID mappings in SQLite
4. **Timestamp Tolerance**: Allow small clock differences (±1 minute)
5. **Test Scenarios**: Create comprehensive test matrix
6. **Read-Only First**: Perfect one-way sync before attempting two-way
7. **Backup Everything**: Never overwrite without backup

### Test Scenarios:
```
Created on both sides (conflict)
Modified on both sides (conflict)
Created on Palm, not on PC
Created on PC, not on Palm
Modified on Palm, unchanged on PC
Modified on PC, unchanged on Palm
Deleted on Palm, unchanged on PC
Deleted on PC, unchanged on Palm
Deleted on both sides
Unchanged on both sides
```

### Risk Level: **High**
This is the hardest part. Requires careful design and extensive testing.

---

## 4. Data Format Mapping (Palm ↔ iCalendar/vCard)

### Challenge
Palm and modern formats have mismatches:

**Timezone Handling:**
- Palm: Stores local time only (no timezone)
- iCalendar: Requires UTC + TZID or floating time
- Risk: Events shift when timezone changes

**Repeat Rules:**
- Palm: Limited repeat types (daily, weekly, monthly, yearly)
- iCalendar: Complex RRULE with BYDAY, BYMONTH, etc.
- Not all iCal rules map to Palm

**Categories:**
- Palm: 16 categories max per database
- iCalendar: Unlimited categories
- Mapping requires truncation or special handling

**Custom Fields:**
- Palm: 4-9 custom fields with custom labels
- vCard: X- extension properties
- Need bidirectional label mapping

**Attachment Support:**
- Palm OS 5: JPEG photos in contacts
- vCard: BASE64-encoded PHOTO property
- Large data handling

### Impact
- Data loss if mapping is incomplete
- User confusion from changed data
- Sync failures from invalid formats

### Mitigation
1. **Conservative Mapping**: Only map fields we fully understand
2. **Timezone Strategy**: Use floating time for Palm events (no TZID)
3. **Repeat Simplification**: Downgrade complex iCal RRULEs to nearest Palm equivalent
4. **Category Overflow**: Create "Overflow" category for >16 categories
5. **Custom Field Registry**: Maintain mapping table for custom field labels
6. **Validation**: Parse generated iCal/vCard to ensure validity
7. **Loss Warning**: Warn user about lossy mappings

### Field Mapping Tables:
Document complete field mapping in `docs/FIELD_MAPPING.md`

### Risk Level: **Medium**
Well-defined problem, just needs careful implementation.

---

## 5. USB Communication Reliability

### Challenge
USB communication with Palm devices is notoriously flaky:
- USB visor kernel module issues
- Device detection timing
- Permissions (udev rules)
- USB sleep/suspend interference
- Hot-plug race conditions

### Impact
- Sync failures mid-operation
- Device not detected
- Permission denied errors
- Incomplete transfers
- User frustration

### Mitigation
1. **Tickle Mechanism**: Keep device awake (from KPilot)
2. **Retry Logic**: Retry failed operations with exponential backoff
3. **Timeout Handling**: Detect hung connections, allow cancel
4. **User Guide**: Document udev rules and permissions setup
5. **Diagnostic Mode**: Provide connection troubleshooting tool
6. **Serial Fallback**: Support old-school serial cables (more reliable)
7. **Status Feedback**: Show connection state clearly in UI

### Required udev Rule:
```
SUBSYSTEM=="usb", ATTR{idVendor}=="0830", MODE="0666"
```

### Risk Level: **High**
USB issues are the #1 complaint for Palm sync tools. Need robust error handling.

---

## 6. Build System Complexity

### Challenge
Building pilot-link alongside Qt6 application:
- pilot-link uses autotools (old-school)
- Qt6 uses CMake
- Need to integrate both
- Cross-platform compatibility (Linux focus for now)
- Dependency version management

### Impact
- Build failures on different systems
- Hard to set up development environment
- CI/CD complexity
- User installation difficulty

### Mitigation
1. **ExternalProject_Add**: Use CMake to drive pilot-link build
2. **In-Tree Build**: Build pilot-link in `lib/pilot-link/`
3. **Cached Build**: Don't rebuild pilot-link every time
4. **Version Pin**: Use specific pilot-link version (0.12.5)
5. **Build Script**: Provide `scripts/build-pilot-link.sh`
6. **Documentation**: Clear build instructions in README
7. **Dev Container**: Consider Docker image for development

### Example CMake:
```cmake
ExternalProject_Add(pilot-link
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/lib/pilot-link
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_BINARY_DIR}/pilot-link-install
    BUILD_COMMAND make
    INSTALL_COMMAND make install
)
```

### Risk Level: **Low**
Well-understood problem, just needs initial setup.

---

## 7. Testing Without Physical Devices

### Challenge
Not everyone has a Palm Pilot available for testing:
- Devices are vintage (20-25 years old)
- Battery replacement difficult
- USB cables scarce
- HotSync button can fail
- CI/CD needs automated tests

### Impact
- Hard to develop without device
- Can't test every code path
- CI/CD can't validate device communication
- Contributors need devices

### Mitigation
1. **Virtual Link**: Implement `KPilotLocalLink` for filesystem testing
2. **Mock Databases**: Create test `.pdb` files
3. **Unit Tests**: Test sync algorithm without device
4. **Mapper Tests**: Test Palm ↔ iCal conversion in isolation
5. **Device Recordings**: Record DLP sessions, replay in tests
6. **Community Testing**: Beta testers with devices
7. **Emulator Research**: Investigate POSE (Palm OS Emulator)

### Test Strategy:
- 80% tests don't need device (mappers, sync algorithm, UI)
- 20% tests need real device (integration, USB handling)
- Manual testing for release validation

### Risk Level: **Medium**
Can work around with good architecture.

---

## 8. Conflict Resolution UI/UX

### Challenge
How to present conflicts to users:
- Users may not understand "three-way merge"
- Too many prompts are annoying
- Wrong default can lose data
- Different conflict types need different UIs

### Impact
- User confusion
- Accidental data loss
- Sync abandonment
- Support burden

### Mitigation
1. **Smart Defaults**: Use heuristics to auto-resolve when safe
2. **Batch UI**: Show all conflicts together, not one-by-one
3. **Side-by-Side Diff**: Visual comparison of conflicting records
4. **"Always" Options**: Let user set policy for future conflicts
5. **Undo Support**: Allow rollback of conflict resolution
6. **Conservative Mode**: Default to "ask user" when starting out
7. **User Education**: In-app help explaining conflicts

### Conflict UI Mockup:
```
┌─────────────────────────────────────────────────────┐
│ Conflict: Meeting with Bob                         │
├─────────────────────────────────────────────────────┤
│ Palm:     2026-01-10 10:00-11:00                   │
│ Computer: 2026-01-10 14:00-15:00                   │
│ Last Sync: 2026-01-10 10:00-11:00                  │
├─────────────────────────────────────────────────────┤
│ ○ Keep Palm version                                 │
│ ○ Keep Computer version                             │
│ ● Keep both (create duplicate)                      │
│ □ Always prefer Palm for conflicts                  │
└─────────────────────────────────────────────────────┘
```

### Risk Level: **Medium**
Requires user testing and iteration.

---

## 9. Character Encoding

### Challenge
Palm uses various character encodings:
- Palm OS 1-3: CP1252 (Windows Latin-1)
- Palm OS 4+: Unicode option
- Regional variations (Japanese, Chinese)
- Qt6 expects UTF-8

### Impact
- Garbled international characters
- Loss of accented characters
- Chinese/Japanese sync failures

### Mitigation
1. **Auto-Detection**: Try to detect Palm encoding
2. **User Setting**: Let user specify encoding
3. **Qt Conversion**: Use QTextCodec/QStringConverter
4. **Validation**: Check for invalid UTF-8 after conversion
5. **Fallback**: Replace unconvertible characters with '?'
6. **Testing**: Test with various languages

### Risk Level: **Low**
Qt has good encoding support.

---

## 10. Long-Term Maintenance

### Challenge
Palm Pilots are obsolete hardware:
- Devices breaking down
- Shrinking user base
- Developer motivation
- Dependency rot

### Impact
- Project abandonment risk
- Bitrot from Qt/KDE updates
- Lost tribal knowledge

### Mitigation
1. **Excellent Documentation**: Document everything
2. **Clean Architecture**: Make code maintainable
3. **Test Coverage**: Automated tests prevent regression
4. **Community Building**: Engage Palm enthusiast community
5. **Modular Design**: Components useful beyond Palm (vCard/iCal libraries)
6. **Open Source**: Multiple maintainers possible

### Risk Level: **Low** (but inevitable long-term)
Accept that this is a niche project.

---

## Priority Risk Matrix

| Challenge | Risk | Priority | Phase |
|-----------|------|----------|-------|
| Sync algorithm | High | Critical | Phase 3 |
| USB communication | High | High | Phase 1 |
| Qt4→Qt6 translation | Medium | High | Phase 1 |
| Data mapping | Medium | High | Phase 2 |
| Conflict UI | Medium | Medium | Phase 3 |
| Testing w/o device | Medium | Medium | Phase 1 |
| Palm data structures | Medium→Low | Low | Phase 2 |
| Build system | Low | Medium | Phase 1 |
| Character encoding | Low | Low | Phase 2 |
| Long-term maintenance | Low | Low | Future |

---

**Document Version**: 1.0
**Last Updated**: 2026-01-05
**Next Review**: After Phase 1 completion
