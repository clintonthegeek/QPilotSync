# QPilotSync Project Vision

## Overview

QPilotSync is a modern Qt6-based synchronization application for Palm Pilot devices, bringing classic Palm organizers into the 2020s with support for modern data formats and contemporary desktop environments.

## Goals

### Primary Objectives

1. **Modern Palm Synchronization**: Enable Palm Pilot devices to sync with contemporary Linux/KDE systems
2. **Standard Data Formats**: Use industry-standard formats (iCalendar `.ics`, vCard `.vcf`) for maximum interoperability
3. **Clean Architecture**: Build on proven patterns from PlanStanLite and KPilot, modernized for Qt6
4. **Extensibility**: Design for future expansion to Akonadi, CalDAV, CardDAV, and other backends

### Non-Goals (For Now)

- Akonadi integration (future enhancement)
- Mobile/Android support
- Cloud sync services
- Custom Palm applications

## Core Features

### Phase 1: Foundation
- Device detection and connection (USB/serial)
- Read-only sync: Palm → PC
- Calendar export to `.ics` files
- Contacts export to `.vcf` files
- Basic UI with sync log

### Phase 2: Bidirectional Sync
- Two-way synchronization
- Conflict detection and resolution
- Three-way merge (HH, PC, backup state)
- Change tracking and dirty detection

### Phase 3: Polish
- Comprehensive UI (inspired by KPilot)
- File installer (`.pdb`/`.prc` files to Palm)
- Backup/restore functionality
- Conduit configuration
- Multiple device profiles

### Future Enhancements
- Akonadi backend integration
- Remote sync (CalDAV/CardDAV)
- Memo sync
- Task sync
- Multiple backend support (like PlanStanLite)

## Target Users

- Palm Pilot enthusiasts keeping classic devices alive
- Users with legacy Palm data to migrate
- Retro computing hobbyists
- Anyone needing offline-first PIM with long battery life

## Success Criteria

1. Successfully sync calendar and contacts bidirectionally
2. Zero data loss during sync operations
3. Handles conflicts gracefully
4. Works with Palm OS 3.x through 5.x devices
5. Clean, maintainable codebase for future contributors

## Data Format Strategy

### Palm → Standard Formats Mapping

**Calendar (Datebook/Calendar DB → iCalendar)**
- Use KCalendarCore for parsing/serialization
- Map Palm events to `VEVENT` components
- Handle repeat rules, alarms, exceptions
- Preserve categories as iCal categories
- Handle timezone conversions

**Contacts (AddressDB/Contacts → vCard)**
- Export as vCard 3.0/4.0
- Map all 19 classic fields (or 39 extended fields)
- Preserve custom fields in vCard extension fields
- Handle categories

**Memos (MemoDB → Text/Markdown)**
- Simple text export
- Consider future org-mode integration

**Tasks (ToDoDB → iCalendar VTODO)**
- Map to `VTODO` components
- Preserve priority, due date, completion status

## Technical Philosophy

1. **Proven Patterns**: Adopt successful architectures from PlanStanLite and KPilot
2. **Separation of Concerns**: Daemon vs GUI, Model vs View, Data vs Presentation
3. **Testability**: Design for testing without physical devices
4. **Incremental Development**: Build in layers, validate each step
5. **Minimal Dependencies**: Use only essential libraries (Qt6, KCalendarCore, pilot-link)
6. **Standard Compliance**: Follow RFC 5545 (iCalendar), RFC 6350 (vCard)

## Project Constraints

### What We Can Use
- **PlanStanLite**: Architecture patterns, CMake setup, documentation (READ ONLY)
- **pilot-link**: Build and link, no modifications (READ ONLY)
- **KPilot**: Conceptual reference only, no code reuse (Qt4 incompatible)

### What We Must Build
- All Qt6 UI code
- Palm backend (implementing PlanStanLite's backend interface)
- Sync engine and conflict resolution
- Data mapping layer (Palm ↔ iCalendar/vCard)
- Configuration management
- Device connection manager

## Risks and Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Palm data structure complexity | High | Use pilot-link's pack/unpack functions |
| USB communication reliability | High | Implement KPilot's tickle mechanism |
| Sync conflict edge cases | Medium | Start with conservative conflict resolution |
| Qt4→Qt6 knowledge gap | Medium | Study Qt6 best practices, use modern patterns |
| Testing without devices | Medium | Build virtual link for filesystem testing |
| Build system complexity | Low | Copy proven CMake patterns from PlanStanLite |

## Development Principles

1. **Documentation First**: Write docs before code
2. **Test Early**: Create test infrastructure from day one
3. **Incremental Validation**: Validate each component before integration
4. **Read-Only Phase**: Perfect read-only sync before attempting writes
5. **Backup Everything**: Never write to Palm without backup
6. **User Safety**: Warn before destructive operations

## Long-Term Vision

QPilotSync should become:
- The reference implementation for modern Palm syncing
- A bridge between vintage computing and modern workflows
- A showcase for Qt6 application architecture
- A foundation for expanded retro device support

Beyond Palm Pilots, the architecture could adapt to:
- Apple Newton synchronization
- Psion organizers
- Other vintage PDAs

## Success Metrics

### Technical Metrics
- Zero data corruption incidents
- >95% field mapping accuracy (Palm ↔ iCalendar/vCard)
- <5 second sync time for typical datasets (100 events, 200 contacts)
- Support for Palm OS 3.0-5.4 devices

### Community Metrics
- Active user base
- Community contributions
- Documentation coverage >80%
- Issue response time <48 hours

---

**Document Version**: 1.0
**Last Updated**: 2026-01-05
**Next Review**: After Phase 1 completion
