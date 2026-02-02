# KDE/Qt6 Power Software Design Guide

A comprehensive reference for building professional QWidget-based desktop applications using Qt6 and the KDE Frameworks, derived from analysis of Kate, KDevelop, KOrganizer, Okular, Dolphin, Kdenlive, and libktorrent.

---

## Document Index

| Document | Description |
|----------|-------------|
| [01 - Overview](01-overview.md) | Philosophy, reference applications, and when to use which patterns |
| [02 - UI Design Principles](02-ui-design-principles.md) | Aesthetics, layout patterns, theming, accessibility |
| [03 - Main Window Architecture](03-main-window-architecture.md) | KXmlGuiWindow, KParts, KateMDI, Sublime patterns |
| [04 - Menu and Toolbar System](04-menu-and-toolbar-system.md) | XMLGUI, standard menus, context menus, hamburger menus |
| [05 - KDE Frameworks Reference](05-kde-frameworks-reference.md) | KIO, KConfig, KParts, KActionCollection, KI18n |
| [06 - Plugin Architecture](06-plugin-architecture.md) | KPluginFactory, metadata, discovery, extension points |
| [07 - UI Components](07-ui-components.md) | Dock widgets, sidebars, toolviews, status bars, panels |
| [08 - User Workflows](08-user-workflows.md) | Sessions, documents, navigation, undo/redo |
| [09 - Settings Dialogs](09-settings-dialogs.md) | KPageDialog, KConfigSkeleton, settings pages |
| [10 - Best Practices](10-best-practices.md) | Code organization, CMake, testing, common pitfalls |
| [Appendix - Reference Files](appendix-reference-files.md) | Quick-reference to source implementations |

---

## Quick Start by Application Type

### Text Editors and IDEs
Start with: [03 - Main Window Architecture](03-main-window-architecture.md) (KateMDI section), [06 - Plugin Architecture](06-plugin-architecture.md)

**Reference:** Kate, KDevelop

### Document Viewers
Start with: [03 - Main Window Architecture](03-main-window-architecture.md) (KParts section), [05 - KDE Frameworks Reference](05-kde-frameworks-reference.md)

**Reference:** Okular

### File Managers
Start with: [03 - Main Window Architecture](03-main-window-architecture.md) (KXmlGuiWindow section), [07 - UI Components](07-ui-components.md)

**Reference:** Dolphin

### Media Applications
Start with: [03 - Main Window Architecture](03-main-window-architecture.md) (KDDockWidgets section), [07 - UI Components](07-ui-components.md)

**Reference:** Kdenlive

### PIM/Productivity Applications
Start with: [08 - User Workflows](08-user-workflows.md), [09 - Settings Dialogs](09-settings-dialogs.md)

**Reference:** KOrganizer

---

## Version Requirements

This guide targets:

- **Qt:** 6.5 or later
- **KDE Frameworks:** 6.0 or later (KF6)
- **C++ Standard:** C++17 or later

All code examples use KF6 APIs. For KF5 compatibility, consult the KDE API documentation for namespace and class name differences.

---

## Scope

This guide focuses exclusively on **QWidget-based** application development. QML/Kirigami patterns are explicitly out of scope.

The patterns documented here are suitable for:
- Complex multi-panel interfaces
- Power-user focused applications
- Applications requiring deep system integration
- Professional productivity software

---

## How to Use This Guide

1. **New to KDE development?** Read [01 - Overview](01-overview.md) first to understand the philosophy and available patterns.

2. **Starting a new application?** Read [03 - Main Window Architecture](03-main-window-architecture.md) to choose your base class, then [04 - Menu and Toolbar System](04-menu-and-toolbar-system.md) for UI structure.

3. **Adding features?** Consult specific documents:
   - Plugin system: [06 - Plugin Architecture](06-plugin-architecture.md)
   - Settings: [09 - Settings Dialogs](09-settings-dialogs.md)
   - File operations: [05 - KDE Frameworks Reference](05-kde-frameworks-reference.md)

4. **Looking for code examples?** Check [Appendix - Reference Files](appendix-reference-files.md) for direct links to implementations.

---

## Contributing

This guide was derived from source code analysis of KDE applications. Updates should reflect changes in the upstream projects and evolving best practices.
