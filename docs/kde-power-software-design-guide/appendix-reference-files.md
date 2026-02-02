# Appendix: Reference Files

Quick reference to source code implementations in the analyzed KDE projects.

---

## Main Window Implementations

| Pattern | Project | File |
|---------|---------|------|
| KXmlGuiWindow | Dolphin | `dolphin/src/dolphinmainwindow.h` |
| KXmlGuiWindow | Kdenlive | `kdenlive/src/mainwindow.h` |
| KParts::MainWindow | Okular | `okular/shell/shell.h` |
| KateMDI::MainWindow | Kate | `kate/apps/lib/katemainwindow.h` |
| Sublime::MainWindow | KDevelop | `kdevelop/kdevplatform/sublime/mainwindow.h` |

---

## XMLGUI Files

| Project | File | Notable Features |
|---------|------|------------------|
| Dolphin | `dolphin/src/dolphinui.rc` | State-based actions, hamburger menu |
| Kate | `kate/apps/lib/data/kateui.rc` | Sessions menu, plugin merge points |
| Okular | `okular/shell/shell.rc` | Minimal shell, part merging |
| Kdenlive | `kdenlive/src/kdenliveui.rc` | Extensive toolbars, timeline tools |
| KDevelop | `kdevelop/app/kdevui.rc` | Area switching, plugin integration |

---

## MDI/Toolview Systems

| Component | Project | File |
|-----------|---------|------|
| KateMDI Framework | Kate | `kate/apps/lib/katemdi.h` |
| Sidebar | Kate | `kate/apps/lib/katemdi.h` (Sidebar class) |
| ToolView | Kate | `kate/apps/lib/katemdi.h` (ToolView class) |
| Sublime Areas | KDevelop | `kdevelop/kdevplatform/sublime/area.h` |
| Sublime Controller | KDevelop | `kdevelop/kdevplatform/sublime/controller.h` |
| IdealDockWidget | KDevelop | `kdevelop/kdevplatform/sublime/idealdockwidget.h` |

---

## Dock Widget Patterns

| Component | Project | File |
|-----------|---------|------|
| Custom Dock Widget | Dolphin | `dolphin/src/dolphindockwidget.h` |
| Panel Base Class | Dolphin | `dolphin/src/panels/panel.h` |
| Information Panel | Dolphin | `dolphin/src/panels/information/informationpanel.h` |
| Folders Panel | Dolphin | `dolphin/src/panels/folders/folderspanel.h` |
| Terminal Panel | Dolphin | `dolphin/src/panels/terminal/terminalpanel.h` |
| KDDockWidgets Setup | Kdenlive | `kdenlive/src/kddocksetup.cpp` |

---

## Plugin Architecture

| Component | Project | File |
|-----------|---------|------|
| Plugin Manager | Kate | `kate/apps/lib/katepluginmanager.h` |
| Plugin Info Struct | Kate | `kate/apps/lib/katepluginmanager.h` |
| Example Plugin | Kate | `kate/addons/filebrowser/katefilebrowserplugin.cpp` |
| Plugin View | Kate | `kate/addons/filetree/katefiletreepluginview.cpp` |
| Plugin Config Page | Kate | `kate/addons/filetree/katefiletreeconfigpage.cpp` |
| Plugin Controller | KDevelop | `kdevelop/kdevplatform/shell/plugincontroller.h` |
| Plugin Metadata Keys | KDevelop | `kdevelop/kdevplatform/shell/plugincontroller.cpp` |

---

## Session Management

| Component | Project | File |
|-----------|---------|------|
| Session Class | Kate | `kate/apps/lib/session/katesession.h` |
| Session Manager | Kate | `kate/apps/lib/session/katesessionmanager.h` |
| Session Controller | KDevelop | `kdevelop/kdevplatform/shell/sessioncontroller.h` |
| Session Lock | KDevelop | `kdevelop/kdevplatform/shell/sessionlock.h` |

---

## Settings Dialogs

| Component | Project | File |
|-----------|---------|------|
| Settings Dialog | Dolphin | `dolphin/src/settings/dolphinsettingsdialog.h` |
| Settings Page Base | Dolphin | `dolphin/src/settings/settingspagebase.h` |
| Interface Settings | Dolphin | `dolphin/src/settings/interface/interfacesettingspage.h` |
| Config Dialog | Kate | `kate/apps/lib/kateconfigdialog.h` |
| Config Dialog | KDevelop | `kdevelop/kdevplatform/shell/configdialog.h` |
| Settings Dialog | Kdenlive | `kdenlive/src/dialogs/kdenlivesettingsdialog.h` |
| KCFG File | Kdenlive | `kdenlive/src/kdenlivesettings.kcfg` |

---

## Document Management

| Component | Project | File |
|-----------|---------|------|
| Document Manager | Kate | `kate/apps/lib/katedocmanager.h` |
| View Manager | Kate | `kate/apps/lib/kateviewmanager.h` |
| View Space | Kate | `kate/apps/lib/kateviewspace.h` |
| Project Manager | Kdenlive | `kdenlive/src/project/projectmanager.h` |
| Project Settings | Kdenlive | `kdenlive/src/project/dialogs/projectsettings.h` |
| Bin (Project Files) | Kdenlive | `kdenlive/src/bin/bin.h` |

---

## Status Bars and Progress

| Component | Project | File |
|-----------|---------|------|
| Status Bar | Dolphin | `dolphin/src/statusbar/dolphinstatusbar.h` |
| Status Bar | KDevelop | `kdevelop/kdevplatform/shell/statusbar.h` |
| Progress Manager | KDevelop | `kdevelop/kdevplatform/shell/progresswidget/progressmanager.h` |
| Progress Dialog | KDevelop | `kdevelop/kdevplatform/shell/progresswidget/progressdialog.h` |
| IStatus Interface | KDevelop | `kdevelop/kdevplatform/interfaces/istatus.h` |

---

## Views and Display

| Component | Project | File |
|-----------|---------|------|
| View Container | Dolphin | `dolphin/src/dolphinviewcontainer.h` |
| View Action Handler | Dolphin | `dolphin/src/views/dolphinviewactionhandler.h` |
| Item List View | Dolphin | `dolphin/src/kitemviews/kitemlistview.h` |
| Timeline Widget | Kdenlive | `kdenlive/src/timeline2/view/timelinewidget.h` |
| Timeline Controller | Kdenlive | `kdenlive/src/timeline2/view/timelinecontroller.h` |
| Monitor | Kdenlive | `kdenlive/src/monitor/monitor.h` |
| Part | Okular | `okular/part/part.h` |

---

## KConfig Framework

| Component | File |
|-----------|------|
| KConfig | `kconfig/src/core/kconfig.h` |
| KConfigGroup | `kconfig/src/core/kconfiggroup.h` |
| KSharedConfig | `kconfig/src/core/ksharedconfig.h` |
| KCoreConfigSkeleton | `kconfig/src/core/kcoreconfigskeleton.h` |
| KConfigSkeleton (GUI) | `kconfig/src/gui/kconfigskeleton.h` |
| INI Backend | `kconfig/src/core/kconfigini.cpp` |

---

## CMake Examples

| Project | File | Notable Patterns |
|---------|------|------------------|
| Dolphin | `dolphin/CMakeLists.txt` | Full framework deps |
| Kate | `kate/CMakeLists.txt` | TextEditor integration |
| KDevelop | `kdevelop/CMakeLists.txt` | Extensive framework usage |
| Kate Lib | `kate/apps/lib/CMakeLists.txt` | Library linking pattern |
| Kate Plugin | `kate/addons/filebrowser/CMakeLists.txt` | Plugin build pattern |

---

## Application Entry Points

| Project | File | Notable Features |
|---------|------|------------------|
| Kate | `kate/apps/kate/main.cpp` | Session handling, DBus |
| Dolphin | `dolphin/src/main.cpp` | Single/Multi instance |
| KDevelop | `kdevelop/app/main.cpp` | Session selection |
| Okular | `okular/shell/main.cpp` | Part loading |
| Kdenlive | `kdenlive/src/main.cpp` | MLT initialization |

---

## Additional Resources

### KDE Human Interface Guidelines
- https://develop.kde.org/hig/

### KDE API Documentation
- https://api.kde.org/

### KDE Frameworks Source
- https://invent.kde.org/frameworks

### Qt Documentation
- https://doc.qt.io/qt-6/

---

## Project-Specific Patterns

### Kate-Specific

| Pattern | Files |
|---------|-------|
| KTextEditor Integration | `kate/apps/lib/kateapp.h`, `kate/apps/lib/katemainwindow.h` |
| Output View | `kate/apps/lib/kateoutputview.h` |
| Tab Bar | `kate/apps/lib/katetabbar.h` |
| LSP Client Plugin | `kate/addons/lspclient/` |

### Dolphin-Specific

| Pattern | Files |
|---------|-------|
| URL Navigation | `dolphin/src/dolphinurlnavigator.h` |
| Places Panel | `dolphin/src/panels/places/placespanel.h` |
| Selection Mode | `dolphin/src/selectionmode/` |
| Context Menu | `dolphin/src/dolphincontextmenu.h` |

### Kdenlive-Specific

| Pattern | Files |
|---------|-------|
| Asset Panel | `kdenlive/src/assets/assetpanel.h` |
| Effect Stack | `kdenlive/src/effects/effectstack/` |
| Audio Mixer | `kdenlive/src/audiomixer/` |
| Render Dialog | `kdenlive/src/dialogs/renderwidget.h` |
| Scopes | `kdenlive/src/scopes/` |

### KDevelop-Specific

| Pattern | Files |
|---------|-------|
| Language Support | `kdevelop/kdevplatform/language/` |
| Project Model | `kdevelop/kdevplatform/project/` |
| Debug Interfaces | `kdevelop/kdevplatform/debugger/` |
| VCS Integration | `kdevelop/kdevplatform/vcs/` |

### KOrganizer-Specific

| Pattern | Files |
|---------|-------|
| Calendar View | `korganizer/src/views/` |
| PIM Integration | `korganizer/src/kontactplugin/` |
| Prefs System | `korganizer/src/prefs/` |
