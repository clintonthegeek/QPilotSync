# QPilotSync Development Roadmap

## Overview

This roadmap outlines the incremental development plan for QPilotSync, broken into manageable phases with clear deliverables and validation criteria.

---

## Phase 1: Foundation & Device Communication (Weeks 1-2)

### Goals
- Establish project structure
- Build pilot-link library
- Create minimal Qt6 application
- Achieve basic device connection and info reading

### Tasks

#### 1.1 Project Setup
- [ ] Create directory structure (`src/`, `lib/`, `docs/`, `tests/`, `build/`)
- [ ] Write root `CMakeLists.txt` (based on PlanStanLite pattern)
- [ ] Set up Git repository (if not already)
- [ ] Create `.gitignore` for build artifacts
- [ ] Document build requirements in `README.md`

#### 1.2 Build pilot-link
- [ ] Configure pilot-link build as CMake ExternalProject
- [ ] Create `lib/CMakeLists.txt` to drive pilot-link build
- [ ] Build libpisock.so successfully
- [ ] Verify pilot-link headers accessible
- [ ] Create test program linking to pilot-link
- [ ] Document any build quirks

#### 1.3 Basic Qt6 Application
- [ ] Create minimal Qt6 Widgets application
- [ ] Set up MainWindow with menu bar
- [ ] Create basic status bar
- [ ] Add "About" dialog
- [ ] Implement application icon/resources
- [ ] Verify builds on target platform (Linux)

#### 1.4 Device Detection
- [ ] Implement `KPilotLink` abstract interface (from KPilot design)
- [ ] Implement `KPilotDeviceLink` using pilot-link
- [ ] Create device detection state machine
- [ ] Show connection status in UI
- [ ] Display "Waiting for device..." message
- [ ] Handle connection errors gracefully

#### 1.5 Read User Info
- [ ] Call `dlp_ReadUserInfo()` after connection
- [ ] Display user name, user ID, device name in UI
- [ ] Read and display system info (`dlp_ReadSysInfo()`)
- [ ] Show Palm OS version, memory info
- [ ] Log all operations to console

#### 1.6 Logging System
- [ ] Create `LogWidget` (based on KPilot's design)
- [ ] Show timestamped log messages
- [ ] Support log levels (Info, Warning, Error)
- [ ] Save logs to file
- [ ] Clear log function

### Deliverables
- Buildable Qt6 application
- Successfully connects to Palm device
- Displays device information
- Comprehensive build documentation

### Validation Criteria
- [ ] Application builds without errors
- [ ] pilot-link links successfully
- [ ] Can detect Palm device when HotSync pressed
- [ ] Shows correct user info from device
- [ ] Clean UI with no crashes

### Success Metric
"Can connect to a Palm Pilot and read basic info"

---

## Phase 2: Read-Only Sync (Weeks 3-4)

### Goals
- Read Palm databases
- Export to iCalendar and vCard files
- No write operations to Palm

### Tasks

#### 2.1 Database Reading Infrastructure
- [ ] Implement `PilotDatabase` abstraction
- [ ] Implement `PilotSerialDatabase` (reads from device)
- [ ] Implement `PilotLocalDatabase` (reads .pdb files)
- [ ] Test database enumeration (`dlp_ReadDBList()`)
- [ ] List all databases in UI

#### 2.2 Data Structures
- [ ] Implement `PilotRecord` base class
- [ ] Implement `PilotDateEntry` (calendar events)
- [ ] Implement `PilotAddress` (contacts)
- [ ] Implement `PilotToDo` (tasks)
- [ ] Implement `PilotMemo` (notes)
- [ ] Use pilot-link's pack/unpack functions

#### 2.3 Calendar Conduit (Read-Only)
- [ ] Create `CalendarConduit` class
- [ ] Read "DatebookDB" from Palm
- [ ] Implement `PalmToICalMapper`
- [ ] Map Palm events to KCalendarCore::Event
- [ ] Handle repeat rules
- [ ] Handle alarms
- [ ] Handle exceptions
- [ ] Map categories
- [ ] Export to .ics files (one per event)
- [ ] Test with various event types

#### 2.4 Contacts Conduit (Read-Only)
- [ ] Create `ContactsConduit` class
- [ ] Read "AddressDB" from Palm
- [ ] Implement `PalmToVCardMapper`
- [ ] Map Palm addresses to vCard 3.0
- [ ] Handle all 19 address fields
- [ ] Handle custom fields
- [ ] Handle categories
- [ ] Export to .vcf files (one per contact)
- [ ] Test with various contact types

#### 2.5 Local Storage Backend
- [ ] Implement `LocalBackend` (based on PlanStanLite)
- [ ] Create directory structure for calendars and contacts
- [ ] Write .ics files with proper naming (UID.ics)
- [ ] Write .vcf files with proper naming
- [ ] Handle file conflicts (existing files)

#### 2.6 Sync UI
- [ ] Create "Sync" menu with options
- [ ] Implement "Sync from Palm" action
- [ ] Show progress dialog during sync
- [ ] Update log with sync progress
- [ ] Show summary after sync (X events, Y contacts)
- [ ] Handle errors gracefully

### Deliverables
- Working calendar read-only sync
- Working contacts read-only sync
- Files exported to local directory
- User can view exported .ics/.vcf files in other apps

### Validation Criteria
- [ ] All events from Palm exported correctly
- [ ] All contacts from Palm exported correctly
- [ ] Exported files open in other calendar/contact apps
- [ ] No data corruption or loss
- [ ] Repeat rules preserved
- [ ] Categories preserved
- [ ] International characters work

### Success Metric
"Can extract all Palm data and view it on PC"

---

## Phase 3: Bidirectional Sync Foundation (Weeks 5-6)

### Goals
- Implement backup state tracking
- Create ID mapping database
- Build sync comparison logic
- NO actual two-way sync yet (still read-only to Palm)

### Tasks

#### 3.1 Backup State System
- [ ] Create `BackupDatabase` class
- [ ] Store last-synced state for each record
- [ ] Use .pdb file format for backup
- [ ] Implement backup directory structure
- [ ] Track sync timestamps

#### 3.2 ID Mapping
- [ ] Create SQLite database for ID mappings
- [ ] Schema: `mapping (palm_id, pc_uid, type, last_sync)`
- [ ] Implement ID lookup functions
- [ ] Handle orphaned mappings (deleted records)
- [ ] Persist mappings across syncs

#### 3.3 Change Detection
- [ ] Implement `DirtyChanges` structure (from PlanStanLite)
- [ ] Detect created records (in Palm, not in backup)
- [ ] Detect modified records (different from backup)
- [ ] Detect deleted records (in backup, not in Palm)
- [ ] Do same for PC side

#### 3.4 Three-Way Comparison
- [ ] Implement sync decision algorithm
- [ ] Load Palm records
- [ ] Load PC records
- [ ] Load backup records
- [ ] Compare three sets
- [ ] Generate sync actions (copy-to-palm, copy-to-pc, conflict)
- [ ] Log all decisions

#### 3.5 Conflict Detection
- [ ] Detect same-record edits on both sides
- [ ] Detect dual-creates with similar data
- [ ] Flag all conflicts for user review
- [ ] Create conflict report

#### 3.6 Dry-Run Mode
- [ ] Implement "Preview Sync" mode
- [ ] Show what would change without executing
- [ ] Display planned operations in UI
- [ ] Allow user to review before committing

### Deliverables
- Backup state tracking working
- ID mapping database
- Sync algorithm logic
- Dry-run preview

### Validation Criteria
- [ ] Correctly identifies new records on both sides
- [ ] Correctly identifies modifications
- [ ] Correctly identifies deletions
- [ ] Detects all conflict scenarios
- [ ] Dry-run shows accurate preview

### Success Metric
"Can analyze what needs to sync without writing anything"

---

## Phase 4: Bidirectional Sync Implementation (Weeks 7-8)

### Goals
- Implement write-to-Palm operations
- Execute sync decisions
- Handle conflicts
- Achieve full two-way sync

### Tasks

#### 4.1 Write Operations to Palm
- [ ] Implement Palm record creation
- [ ] Implement Palm record updates
- [ ] Implement Palm record deletion
- [ ] Verify written records readable
- [ ] Handle write errors

#### 4.2 Sync Execution
- [ ] Execute "copy to Palm" operations
- [ ] Execute "copy to PC" operations
- [ ] Execute deletes on both sides
- [ ] Update ID mappings after writes
- [ ] Update backup state after sync

#### 4.3 Conflict Resolution UI
- [ ] Design conflict resolution dialog
- [ ] Show side-by-side comparison
- [ ] Offer resolution options (Palm wins, PC wins, both)
- [ ] Allow "always prefer" settings
- [ ] Batch conflict resolution
- [ ] Apply user choices

#### 4.4 Rollback Support
- [ ] Create pre-sync snapshot
- [ ] Implement undo mechanism (QUndoStack)
- [ ] Allow post-sync rollback
- [ ] Restore from backup on error

#### 4.5 Sync Modes
- [ ] Implement HotSync (two-way, modified only)
- [ ] Implement FullSync (two-way, all records)
- [ ] Implement Copy PC to Palm
- [ ] Implement Copy Palm to PC
- [ ] User can select mode before sync

#### 4.6 Error Handling
- [ ] Handle mid-sync disconnects
- [ ] Recover from partial syncs
- [ ] Validate all writes
- [ ] Verify checksums
- [ ] Atomic sync transactions

### Deliverables
- Full bidirectional sync working
- Conflict resolution UI
- Multiple sync modes
- Robust error handling

### Validation Criteria
- [ ] Changes on Palm appear on PC
- [ ] Changes on PC appear on Palm
- [ ] Deletions propagate correctly
- [ ] Conflicts detected and resolved
- [ ] No data loss or corruption
- [ ] Can recover from errors

### Success Metric
"Full two-way sync with zero data loss"

---

## Phase 5: Polish & Features (Weeks 9-12)

### Goals
- Implement file installer
- Add backup/restore
- Create comprehensive UI
- Configuration management
- Documentation

### Tasks

#### 5.1 File Installer
- [ ] Create file installer widget (like KPilot)
- [ ] Drag-and-drop .pdb/.prc files
- [ ] Queue files for installation
- [ ] Install during next HotSync
- [ ] Show installation progress
- [ ] Handle installation errors

#### 5.2 Backup/Restore
- [ ] Full device backup to directory
- [ ] Fast backup (modified only)
- [ ] Scheduled automatic backups
- [ ] Restore from backup
- [ ] Backup verification
- [ ] Backup management (delete old)

#### 5.3 Configuration Dialog
- [ ] Device settings page
- [ ] Sync settings page
- [ ] Conduit enable/disable
- [ ] Conflict resolution preferences
- [ ] Storage location settings
- [ ] Save/load configuration

#### 5.4 Enhanced UI
- [ ] Main window with multiple tabs
- [ ] Sync log viewer
- [ ] File installer tab
- [ ] Device info panel
- [ ] Status indicators
- [ ] Keyboard shortcuts
- [ ] Polish visual design

#### 5.5 Multiple Device Support
- [ ] Device profiles
- [ ] Switch between devices
- [ ] Separate backups per device
- [ ] Separate ID mappings per device

#### 5.6 Utilities
- [ ] Database viewer (browse Palm DBs)
- [ ] Category editor
- [ ] Diagnostic tool (connection testing)
- [ ] Log viewer/export

### Deliverables
- Complete, polished application
- All major features implemented
- Professional UI
- Comprehensive configuration

### Validation Criteria
- [ ] All features working smoothly
- [ ] UI intuitive and responsive
- [ ] Configuration persists
- [ ] Backup/restore reliable
- [ ] File installer works
- [ ] No major bugs

### Success Metric
"Production-ready application suitable for daily use"

---

## Phase 6: Testing & Documentation (Week 13+)

### Goals
- Comprehensive testing
- User documentation
- Developer documentation
- Release preparation

### Tasks

#### 6.1 Testing
- [ ] Unit tests for all mappers
- [ ] Unit tests for sync algorithm
- [ ] Integration tests with mock device
- [ ] Manual testing with real devices
- [ ] Test various Palm OS versions
- [ ] Stress testing (large databases)
- [ ] Edge case testing

#### 6.2 Documentation
- [ ] User guide (installation, usage)
- [ ] Troubleshooting guide
- [ ] Developer documentation
- [ ] API documentation (Doxygen)
- [ ] Build instructions
- [ ] Contribution guidelines

#### 6.3 Packaging
- [ ] Create .desktop file
- [ ] Create application icon
- [ ] Package as AppImage
- [ ] Package as Flatpak (optional)
- [ ] Create AUR package (Arch)
- [ ] Debian/Ubuntu package (optional)

#### 6.4 Release
- [ ] Version tagging (SemVer)
- [ ] Release notes
- [ ] GitHub release
- [ ] Announce on Palm forums
- [ ] Create website/landing page

### Deliverables
- Tested, documented software
- Installation packages
- Public release

### Success Metric
"Others can successfully use QPilotSync"

---

## Future Enhancements (Post-1.0)

### Potential Features
- Akonadi integration (sync to KDE PIM)
- CalDAV/CardDAV support
- Memo sync
- Expense sync
- Email sync (if anyone still uses Palm email!)
- Photo management (Palm Zire 72, Tungsten series)
- Expansion card support (VFS)
- Network HotSync (sync over WiFi with compatible Palms)
- Web interface (manage syncs remotely)
- Multi-platform support (Windows, macOS via POSE)

### Code Quality Improvements
- Increase test coverage to >80%
- CI/CD pipeline
- Automated releases
- Fuzzing for parser robustness
- Performance profiling
- Memory leak detection

---

## Milestones

| Milestone | Completion Date | Deliverable |
|-----------|----------------|-------------|
| M1: First Build | Week 1 | App compiles and runs |
| M2: Device Connection | Week 2 | Can read Palm info |
| M3: Read-Only Sync | Week 4 | Export Palm data to files |
| M4: Sync Foundation | Week 6 | Sync algorithm logic complete |
| M5: Two-Way Sync | Week 8 | Full bidirectional sync |
| M6: Feature Complete | Week 12 | All planned features done |
| M7: Release 1.0 | Week 14 | Public release |

---

## Development Methodology

### Iterative Approach
1. Build smallest testable increment
2. Validate thoroughly
3. Document learnings
4. Iterate

### Risk Management
- Start with highest-risk items (USB communication, sync algorithm)
- Build safety nets (backups, dry-run mode, undo)
- Test aggressively at each phase

### Definition of Done
For each task:
- [ ] Code written and reviewed
- [ ] Unit tests passing
- [ ] Manual testing completed
- [ ] Documentation updated
- [ ] No known bugs

---

**Document Version**: 1.0
**Last Updated**: 2026-01-05
**Next Review**: After each phase completion
