# Overview: Philosophy and Reference Applications

## KDE Design Philosophy

KDE applications are built with a specific design philosophy that prioritizes:

1. **Power User Focus**: Rich feature sets that reward expertise
2. **Configurability**: Extensive customization without overwhelming defaults
3. **Integration**: Seamless interaction with other KDE applications and the desktop
4. **Consistency**: Familiar patterns across all applications
5. **Accessibility**: Usable by people with diverse needs

This philosophy manifests in several technical decisions:
- Rich context menus with comprehensive options
- Configurable keyboard shortcuts for nearly every action
- Session persistence and restoration
- Plugin architectures for extensibility
- Deep integration with KDE services (KIO, KDED, etc.)

---

## QWidget vs QML: When to Use QWidget

This guide focuses on QWidget-based development. Choose QWidget when:

| Use QWidget When... | Use QML/Kirigami When... |
|---------------------|--------------------------|
| Building complex multi-panel interfaces | Building mobile-first applications |
| Deep integration with existing KDE frameworks | Creating animated, fluid UIs |
| Maximum performance for large datasets | Convergent desktop/mobile experiences |
| Requiring precise control over rendering | Rapid prototyping |
| Embedding third-party C++ widgets | Modern touch-first interfaces |

Applications analyzed in this guide demonstrate that QWidget remains the choice for professional desktop software requiring sophisticated layouts and deep framework integration.

---

## Reference Applications

### Kate - Advanced Text Editor

**Architecture Pattern:** KateMDI (Multi-Document Interface with Toolviews)

**Key Characteristics:**
- Sidebar-based toolview system with collapsible panels
- Multi-document editing with split views
- Extensive plugin ecosystem (KTextEditor plugins)
- Session management for workspace persistence
- Deep integration with LSP (Language Server Protocol)

**Study Kate for:**
- Toolview/sidebar patterns
- Split view document handling
- Text editor plugin architecture
- Session persistence

**Key Files:**
```
kate/apps/lib/katemdi.h           - MDI framework
kate/apps/lib/katemainwindow.h    - Main window implementation
kate/apps/lib/kateviewspace.h     - View management
kate/apps/lib/session/            - Session management
kate/addons/                      - Plugin implementations
```

---

### KDevelop - Integrated Development Environment

**Architecture Pattern:** Sublime (Area-based View Management)

**Key Characteristics:**
- Area-based workspace (Code, Debug, Review, etc.)
- Per-area view configuration
- Sophisticated plugin system with dependency tracking
- Project-centric session management
- Background task processing

**Study KDevelop for:**
- Complex IDE architecture
- Area/perspective switching
- Plugin dependency management
- Background processing with progress reporting

**Key Files:**
```
kdevelop/kdevplatform/sublime/           - Area framework
kdevelop/kdevplatform/shell/             - Shell implementation
kdevelop/kdevplatform/interfaces/        - Plugin interfaces
kdevelop/plugins/                        - Plugin implementations
```

---

### Okular - Universal Document Viewer

**Architecture Pattern:** KParts Shell

**Key Characteristics:**
- Part-based document embedding
- Multiple backend support (PDF, EPUB, images, etc.)
- Annotation and form filling
- Presentation mode
- Accessible document rendering

**Study Okular for:**
- KParts architecture
- Document viewer patterns
- Backend/generator system
- Annotation persistence

**Key Files:**
```
okular/shell/shell.h              - KParts::MainWindow shell
okular/part/part.h                - Main part implementation
okular/core/                      - Document model
okular/generators/                - Format backends
```

---

### Dolphin - File Manager

**Architecture Pattern:** KXmlGuiWindow with Panels

**Key Characteristics:**
- URL-based navigation with breadcrumbs
- Detachable panel system
- Multiple view modes (icons, compact, details)
- Integrated terminal
- Context menu extensibility

**Study Dolphin for:**
- Panel/dock widget patterns
- KIO integration for file operations
- View container architecture
- Navigation and history management

**Key Files:**
```
dolphin/src/dolphinmainwindow.h   - Main window
dolphin/src/dolphindockwidget.h   - Custom dock widget
dolphin/src/panels/               - Panel implementations
dolphin/src/views/                - View implementations
dolphin/src/dolphinui.rc          - XMLGUI definition
```

---

### Kdenlive - Video Editor

**Architecture Pattern:** KDDockWidgets with QML Timeline

**Key Characteristics:**
- Advanced dock widget system (KDDockWidgets library)
- Multi-track timeline with QML rendering
- Dual monitor system (clip/project)
- Effect stack with keyframe animation
- Multi-sequence project support

**Study Kdenlive for:**
- Complex multi-panel professional interfaces
- KDDockWidgets integration
- Hybrid QWidget/QML architecture
- Monitor/preview patterns
- Project management

**Key Files:**
```
kdenlive/src/mainwindow.h         - Main window
kdenlive/src/kddocksetup.cpp      - Custom dock factory
kdenlive/src/monitor/             - Monitor implementations
kdenlive/src/timeline2/           - Timeline implementation
kdenlive/src/bin/                 - Project bin
```

---

### KOrganizer - Personal Information Manager

**Architecture Pattern:** KParts with Multi-View Calendar

**Key Characteristics:**
- Multiple calendar views (day, week, month, agenda)
- Integration with Akonadi for data storage
- Kontact integration
- Reminder and alarm system
- iCal import/export

**Study KOrganizer for:**
- Calendar view patterns
- Akonadi/PIM integration
- Multi-view switching
- Data model integration

**Key Files:**
```
korganizer/src/                   - Main application
korganizer/plugins/               - View plugins
korganizer/src/prefs/             - Settings system
```

---

### libktorrent - BitTorrent Library

**Architecture Pattern:** KDE Library with KIO Integration

**Key Characteristics:**
- Clean separation of library and UI
- KIO slave for torrent protocol
- Plugin-based extension system
- DBus interface for external control

**Study libktorrent for:**
- Library design patterns
- KIO protocol handlers
- Signal/slot based APIs
- Background service architecture

**Key Files:**
```
libktorrent/src/                  - Core library
libktorrent/src/interfaces/       - Public interfaces
```

---

## Choosing Your Architecture

| Application Type | Recommended Base | Reference |
|-----------------|------------------|-----------|
| Simple single-document app | `KXmlGuiWindow` | Dolphin |
| Document viewer/editor | `KParts::MainWindow` | Okular |
| IDE/text editor with toolviews | `KateMDI::MainWindow` | Kate |
| Complex IDE with areas | `Sublime::MainWindow` | KDevelop |
| Professional multi-panel | KDDockWidgets | Kdenlive |
| Library/service | Custom `QObject` | libktorrent |

---

## Framework Tiers

KDE Frameworks are organized into tiers based on dependencies:

**Tier 1 - Foundation (Qt-only dependencies):**
- KConfig, KCoreAddons, KI18n, KArchive, KCodecs

**Tier 2 - Core (Tier 1 + Qt dependencies):**
- KCompletion, KJobWidgets, KWidgetsAddons, KWindowSystem

**Tier 3 - Integration (Tier 2 + system/runtime dependencies):**
- KIO, KParts, KTextEditor, KNotifications, KXMLGui

**Tier 4 - Application (Full KDE runtime):**
- Akonadi, KActivities

For minimal applications, stick to Tier 1-2. Most desktop applications will use Tier 3 for KIO and KXMLGui integration.

---

## Getting Started Checklist

1. **Choose your main window base class** based on application type
2. **Set up CMake** with ECM and KDE macros
3. **Create your XMLGUI file** for menus and toolbars
4. **Implement core functionality** using appropriate KDE frameworks
5. **Add settings** with KConfig/KConfigSkeleton
6. **Consider plugins** for extensibility
7. **Test integration** with KDE desktop environment
