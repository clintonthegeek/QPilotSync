# Main Window Architecture

KDE applications use several main window base classes, each suited to different application types. This document covers the primary patterns.

---

## KXmlGuiWindow - The Standard Choice

`KXmlGuiWindow` is the most common base class for KDE applications. It provides:

- XMLGUI for menu/toolbar management
- Action collection integration
- Toolbar and menu customization
- Session management hooks
- State configuration persistence

### Basic Structure

```cpp
// mainwindow.h
#include <KXmlGuiWindow>

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void saveProperties(KConfigGroup &config) override;
    void readProperties(const KConfigGroup &config) override;

private:
    void setupActions();
    void setupDockWidgets();
    void loadSettings();
    void saveSettings();

    QWidget *m_centralWidget;
    // Dock widgets, views, etc.
};
```

### Implementation Pattern (Dolphin Style)

```cpp
// mainwindow.cpp
#include "mainwindow.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStandardAction>

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    // Set object name for session management
    setObjectName(QStringLiteral("MainWindow"));

    // Set component name for XMLGUI
    setComponentName(QStringLiteral("myapp"),
                     QGuiApplication::applicationDisplayName());

    // Enable state config saving (toolbar positions, etc.)
    setStateConfigGroup(QStringLiteral("MainWindow"));

    // Create central widget
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    // Setup UI
    setupActions();
    setupDockWidgets();

    // Load XMLGUI and create GUI
    setupGUI(Default, QStringLiteral("myappui.rc"));

    // Restore window geometry
    loadSettings();
}

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    // Standard actions
    KStandardAction::quit(qApp, &QCoreApplication::quit, ac);
    KStandardAction::preferences(this, &MainWindow::showSettings, ac);
    KStandardAction::configureToolbars(this, &MainWindow::configureToolbars, ac);
    KStandardAction::keyBindings(this, &MainWindow::configureShortcuts, ac);

    // Custom actions
    QAction *myAction = ac->addAction(QStringLiteral("my_action"));
    myAction->setText(i18n("My Action"));
    myAction->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    ac->setDefaultShortcut(myAction, QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(myAction, &QAction::triggered, this, &MainWindow::onMyAction);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    KXmlGuiWindow::closeEvent(event);
}

void MainWindow::saveProperties(KConfigGroup &config)
{
    // Save session-specific state (open documents, etc.)
    config.writeEntry("CurrentFile", m_currentFile);
}

void MainWindow::readProperties(const KConfigGroup &config)
{
    // Restore session-specific state
    QString file = config.readEntry("CurrentFile", QString());
    if (!file.isEmpty()) {
        openFile(file);
    }
}

void MainWindow::loadSettings()
{
    KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("MainWindow"));
    restoreGeometry(config.readEntry("Geometry", QByteArray()));
    restoreState(config.readEntry("State", QByteArray()));
}

void MainWindow::saveSettings()
{
    KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("MainWindow"));
    config.writeEntry("Geometry", saveGeometry());
    config.writeEntry("State", saveState());
    config.sync();
}
```

### D-Bus Integration

For applications needing inter-process communication:

```cpp
// mainwindow.h
class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.myapp.MainWindow")

public:
    // D-Bus exposed methods
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void activateWindow();
};

// In constructor
new MainWindowAdaptor(this);  // Auto-generated from XML
QDBusConnection::sessionBus().registerObject(
    QStringLiteral("/MainWindow"),
    this);
```

---

## KParts::MainWindow - For Document Embedding

When your application embeds document viewers/editors (like Okular embedding PDF support):

### Shell Application Pattern

```cpp
// shell.h
#include <KParts/MainWindow>
#include <KParts/ReadWritePart>

class Shell : public KParts::MainWindow
{
    Q_OBJECT

public:
    explicit Shell(QWidget *parent = nullptr);
    ~Shell() override;

    void openDocument(const QUrl &url);

protected:
    void setupActions();
    bool queryClose() override;

private Q_SLOTS:
    void fileOpen();
    void optionsConfigureToolbars();
    void applyNewToolbarConfig();

private:
    void setupGUI();
    bool openNewTab(const QUrl &url);

    KPluginFactory *m_partFactory;
    QVector<KParts::ReadWritePart *> m_parts;
    QTabWidget *m_tabWidget;
};
```

### Loading Parts

```cpp
// shell.cpp
#include <KPluginFactory>
#include <KPluginMetaData>

Shell::Shell(QWidget *parent)
    : KParts::MainWindow(parent)
    , m_partFactory(nullptr)
{
    setObjectName(QStringLiteral("shell"));

    // Find and load the part plugin
    const KPluginMetaData metaData(QStringLiteral("kf6/parts/mypart"));
    m_partFactory = KPluginFactory::loadFactory(metaData).plugin;

    if (!m_partFactory) {
        KMessageBox::error(this, i18n("Could not find the viewer component."));
        return;
    }

    // Create tab widget for multiple documents
    m_tabWidget = new QTabWidget(this);
    setCentralWidget(m_tabWidget);

    setupActions();
    setupGUI(Default, QStringLiteral("shellui.rc"));
}

bool Shell::openNewTab(const QUrl &url)
{
    // Create a new part instance
    KParts::ReadWritePart *part = m_partFactory->create<KParts::ReadWritePart>(this);

    if (!part) {
        return false;
    }

    // Connect part signals
    connect(part, &KParts::ReadOnlyPart::completed, this, [this, part]() {
        // Update tab title when document loads
        int index = m_tabWidget->indexOf(part->widget());
        m_tabWidget->setTabText(index, part->url().fileName());
    });

    // Add to tab widget
    int index = m_tabWidget->addTab(part->widget(), i18n("New Document"));
    m_tabWidget->setCurrentIndex(index);
    m_parts.append(part);

    // Merge part GUI into shell
    createGUI(part);

    // Open the URL
    return part->openUrl(url);
}

void Shell::setupActions()
{
    KStandardAction::open(this, &Shell::fileOpen, actionCollection());
    KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());
}
```

### Part GUI Merging

When a part is activated, its GUI merges with the shell:

```cpp
void Shell::onTabChanged(int index)
{
    // Unplug previous part
    if (m_currentPart) {
        guiFactory()->removeClient(m_currentPart);
    }

    // Get new current part
    QWidget *widget = m_tabWidget->widget(index);
    KParts::ReadWritePart *part = findPartForWidget(widget);

    if (part) {
        // Plug new part's GUI
        createGUI(part);
        m_currentPart = part;
    }
}
```

---

## KateMDI::MainWindow - Toolview-Based Interface

Kate's MDI framework provides a sophisticated toolview system for editor-like applications.

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│ Menu Bar                                                │
├─────────────────────────────────────────────────────────┤
│ Toolbar                                                 │
├─────┬───────────────────────────────────┬───────────────┤
│     │                                   │               │
│  S  │                                   │   Sidebar     │
│  i  │      Central Widget               │   (Right)     │
│  d  │      (Document Views)             │               │
│  e  │                                   │               │
│  b  │                                   │               │
│  a  │                                   │               │
│  r  ├───────────────────────────────────┤               │
│     │   Bottom Bar (Search/Replace)     │               │
│(Left)├───────────────────────────────────┴───────────────┤
│     │   Bottom Sidebar (Terminal, Output, etc.)         │
└─────┴───────────────────────────────────────────────────┘
```

### Core Components

**Sidebar** - Container for multiple toolviews on one edge:

```cpp
// Based on kate/apps/lib/katemdi.h
namespace KateMDI {

class Sidebar : public QSplitter
{
    Q_OBJECT

public:
    explicit Sidebar(Qt::Orientation orientation, MainWindow *mainWin,
                     QWidget *parent = nullptr);

    ToolView *addToolView(const QIcon &icon, const QString &text,
                          const QString &identifier, ToolView *widget);
    bool removeToolView(ToolView *widget);

    void setToolViewVisible(ToolView *widget, bool visible);
    bool isToolViewVisible(ToolView *widget) const;

    void saveSession(KConfigGroup &config);
    void restoreSession(const KConfigGroup &config);

private:
    MainWindow *m_mainWindow;
    QList<ToolView *> m_toolviews;
    KMultiTabBar *m_tabBar;
};
```

**ToolView** - Individual tool panel:

```cpp
class ToolView : public QFrame
{
    Q_OBJECT

public:
    QString identifier() const { return m_identifier; }
    MainWindow *mainWindow() const { return m_mainWindow; }

    void setToolVisible(bool visible);
    bool toolVisible() const;

    void setTabButtonVisible(bool visible);
    bool tabButtonVisible() const;

private:
    friend class Sidebar;
    friend class MainWindow;

    ToolView(MainWindow *mainwin, Sidebar *sidebar,
             QWidget *parent, const QString &identifier);

    QString m_identifier;
    MainWindow *m_mainWindow;
    Sidebar *m_sidebar;
    bool m_toolVisible;
};
```

### Using KateMDI

```cpp
// Based on kate/apps/lib/katemainwindow.h
class MyMainWindow : public KateMDI::MainWindow
{
    Q_OBJECT

public:
    explicit MyMainWindow();

private:
    void setupToolViews();
    void setupCentralWidget();

    KateMDI::ToolView *m_fileTreeToolView;
    KateMDI::ToolView *m_terminalToolView;
    KateMDI::ToolView *m_outputToolView;
};

void MyMainWindow::setupToolViews()
{
    // Left sidebar - file tree
    m_fileTreeToolView = createToolView(
        nullptr,  // plugin (or nullptr for built-in)
        QStringLiteral("kate_private_plugin_katefiletreeplugin"),
        KMultiTabBar::Left,
        QIcon::fromTheme(QStringLiteral("folder")),
        i18n("Files"));

    QWidget *fileTree = new FileTreeWidget(m_fileTreeToolView);
    m_fileTreeToolView->layout()->addWidget(fileTree);

    // Bottom sidebar - terminal
    m_terminalToolView = createToolView(
        nullptr,
        QStringLiteral("kate_private_plugin_katekonsoleplugin"),
        KMultiTabBar::Bottom,
        QIcon::fromTheme(QStringLiteral("utilities-terminal")),
        i18n("Terminal"));

    // Right sidebar - information
    m_outputToolView = createToolView(
        nullptr,
        QStringLiteral("kate_private_plugin_output"),
        KMultiTabBar::Right,
        QIcon::fromTheme(QStringLiteral("dialog-messages")),
        i18n("Output"));
}
```

### Bottom View Bar Pattern

Kate uses a special bottom bar container for view-specific bars (find, replace):

```cpp
// Bottom view bar container (per-view bars like search/replace)
QWidget *m_bottomViewBarContainer;
KateContainerStackedLayout *m_bottomContainerStack;
QHash<KTextEditor::View *, BarState> m_bottomViewBarMapping;

void MyMainWindow::addWidgetToViewBar(KTextEditor::View *view, QWidget *bar)
{
    // Add to stacked layout
    m_bottomContainerStack->addWidget(bar);

    // Map view to its bar
    m_bottomViewBarMapping[view] = {bar, false};

    // Show when view is active
    connect(view, &KTextEditor::View::focusIn, this, [this, view, bar]() {
        m_bottomContainerStack->setCurrentWidget(bar);
    });
}
```

---

## Sublime::MainWindow - Area-Based IDE Architecture

KDevelop uses the Sublime framework for workspace areas (Code, Debug, Review).

### Area Concept

```
Area: "Code"                    Area: "Debug"
┌─────────────────────┐         ┌─────────────────────┐
│ Project │ Editor    │         │ Variables │ Editor  │
│ Files   │           │         │           │         │
│         │           │         ├───────────┤         │
│         │           │         │ Call Stack│         │
├─────────┴───────────┤         ├───────────┴─────────┤
│ Build Output        │         │ Debug Console       │
└─────────────────────┘         └─────────────────────┘
```

### Core Classes

```cpp
// Based on kdevelop/kdevplatform/sublime/
namespace Sublime {

class Area : public QObject
{
    Q_OBJECT

public:
    explicit Area(Controller *controller, const QString &name);

    QString objectName() const;
    QString title() const;

    // View management
    void addView(View *view, AreaIndex *index);
    void removeView(View *view);
    QList<View *> views() const;

    // Tool view management
    void addToolView(View *view, Position position);
    void removeToolView(View *view);
    QList<View *> toolViews() const;

    // Persistence
    void save(KConfigGroup &config) const;
    void load(const KConfigGroup &config);

Q_SIGNALS:
    void viewAdded(Sublime::AreaIndex *, Sublime::View *);
    void viewRemoved(Sublime::AreaIndex *, Sublime::View *);
    void toolViewAdded(Sublime::View *, Sublime::Position);
    void toolViewRemoved(Sublime::View *, Sublime::Position);
};

class MainWindow : public KParts::MainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(Controller *controller, Qt::WindowFlags flags = {});

    // Area management
    void setArea(Area *area);
    Area *area() const;
    QList<Area *> areas() const;

    // View access
    QList<View *> toolDocks() const;
    View *activeView() const;
    View *activeToolView() const;

protected:
    void loadSettings();
    void saveSettings();

private:
    void reconstruct();  // Rebuild UI when area changes
};

}
```

### Switching Areas

```cpp
void MyIDE::switchToDebugArea()
{
    Sublime::Area *debugArea = m_controller->area(
        QStringLiteral("debug"));

    if (debugArea) {
        m_mainWindow->setArea(debugArea);
    }
}

// Areas can be pre-configured
void MyIDE::setupAreas()
{
    Sublime::Controller *controller = m_mainWindow->controller();

    // Code area
    Sublime::Area *codeArea = new Sublime::Area(controller,
        QStringLiteral("code"));
    codeArea->setTitle(i18n("Code"));
    // Add default tool views for code area
    codeArea->addToolView(m_projectView, Sublime::Left);
    codeArea->addToolView(m_buildOutputView, Sublime::Bottom);

    // Debug area
    Sublime::Area *debugArea = new Sublime::Area(controller,
        QStringLiteral("debug"));
    debugArea->setTitle(i18n("Debug"));
    // Add default tool views for debug area
    debugArea->addToolView(m_variablesView, Sublime::Left);
    debugArea->addToolView(m_callStackView, Sublime::Left);
    debugArea->addToolView(m_debugConsoleView, Sublime::Bottom);
}
```

---

## KDDockWidgets - Advanced Docking

Kdenlive uses KDDockWidgets for its complex multi-panel interface, providing more flexibility than QDockWidget.

### Setup

```cpp
// mainwindow.cpp
#include <kddockwidgets/Config.h>
#include <kddockwidgets/qtwidgets/DockWidget.h>
#include <kddockwidgets/qtwidgets/MainWindow.h>

void MainWindow::setupDocking()
{
    // Configure KDDockWidgets behavior
    auto flags = KDDockWidgets::Config::self().flags();
    flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
    flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
    flags |= KDDockWidgets::Config::Flag_TabsHaveCloseButton;
    KDDockWidgets::Config::self().setFlags(flags);

    // Use custom widget factory for theming
    KDDockWidgets::Config::self().setViewFactory(
        new CustomWidgetFactory());
    KDDockWidgets::Config::self().setLayoutSpacing(0);

    // Create main dock window
    m_dockWindow = new KDDockWidgets::QtWidgets::MainWindow(
        QStringLiteral("MainWindow"), {}, this);
    setCentralWidget(m_dockWindow);
}
```

### Custom Widget Factory

```cpp
// Based on kdenlive/src/kddocksetup.cpp
class CustomWidgetFactory : public KDDockWidgets::QtWidgets::ViewFactory
{
public:
    KDDockWidgets::QtWidgets::TabBar *createTabBar(
        KDDockWidgets::Core::TabBar *controller,
        KDDockWidgets::Core::View *parent) const override
    {
        auto *tabBar = new CustomTabBar(controller, parent);
        tabBar->setDocumentMode(true);
        return tabBar;
    }

    KDDockWidgets::QtWidgets::TitleBar *createTitleBar(
        KDDockWidgets::Core::TitleBar *controller,
        KDDockWidgets::Core::View *parent) const override
    {
        return new CustomTitleBar(controller, parent);
    }

    KDDockWidgets::QtWidgets::Separator *createSeparator(
        KDDockWidgets::Core::Separator *controller,
        KDDockWidgets::Core::View *parent) const override
    {
        return new CustomSeparator(controller, parent);
    }
};
```

### Adding Dock Widgets

```cpp
KDDockWidgets::QtWidgets::DockWidget *MainWindow::addDock(
    const QString &title,
    const QString &objectName,
    QWidget *widget,
    KDDockWidgets::Location area,
    KDDockWidgets::QtWidgets::DockWidget *relativeTo,
    const QSize &preferredSize)
{
    auto *dock = new KDDockWidgets::QtWidgets::DockWidget(objectName);
    dock->setTitle(title);
    dock->setWidget(widget);
    dock->setObjectName(objectName);

    // Register toggle action
    KActionCategory *guiActions = actionCollection()->category(
        QStringLiteral("interface"));
    guiActions->addAction(objectName, dock->toggleAction());

    // Add to main window
    if (area == KDDockWidgets::Location_None && relativeTo) {
        // Add as tab
        relativeTo->addDockWidgetAsTab(dock, preferredSize);
    } else {
        m_dockWindow->addDockWidget(dock, area, relativeTo, preferredSize);
    }

    return dock;
}
```

### Layout Example (Kdenlive-style)

```cpp
void MainWindow::setupDefaultLayout()
{
    // Timeline at bottom (full width)
    m_timelineDock = addDock(i18n("Timeline"), QStringLiteral("timeline"),
        m_timelineWidget, KDDockWidgets::Location_OnBottom, nullptr);

    // Project bin on top
    m_projectBinDock = addDock(i18n("Project Bin"), QStringLiteral("project_bin"),
        m_projectBin, KDDockWidgets::Location_OnTop, m_timelineDock);

    // Monitors on right side of project bin
    m_clipMonitorDock = addDock(i18n("Clip Monitor"), QStringLiteral("clip_monitor"),
        m_clipMonitor, KDDockWidgets::Location_OnRight, m_projectBinDock);

    m_projectMonitorDock = addDock(i18n("Project Monitor"), QStringLiteral("project_monitor"),
        m_projectMonitor, KDDockWidgets::Location_OnRight, m_clipMonitorDock);

    // Effect stack on right side of timeline
    m_effectStackDock = addDock(i18n("Effect Stack"), QStringLiteral("effect_stack"),
        m_effectStack, KDDockWidgets::Location_OnRight, m_timelineDock);

    // Tab additional panels with project bin
    m_effectListDock = addDock(i18n("Effects"), QStringLiteral("effect_list"),
        m_effectList, KDDockWidgets::Location_None, m_projectBinDock);

    m_compositionListDock = addDock(i18n("Compositions"), QStringLiteral("compositions"),
        m_compositionList, KDDockWidgets::Location_None, m_projectBinDock);
}
```

---

## Choosing Your Main Window Base

| Base Class | Best For | Examples |
|------------|----------|----------|
| `KXmlGuiWindow` | Standard apps with dock widgets | Dolphin |
| `KParts::MainWindow` | Document viewer/editor shells | Okular |
| `KateMDI::MainWindow` | Text editors with toolviews | Kate |
| `Sublime::MainWindow` | IDEs with multiple workspaces | KDevelop |
| KDDockWidgets | Complex multi-panel professional apps | Kdenlive |

### Decision Tree

```
Need to embed multiple document types?
├─ Yes → KParts::MainWindow
└─ No
   ├─ Need workspace/area switching?
   │  └─ Yes → Sublime::MainWindow
   └─ No
      ├─ Need advanced docking (tabs, nesting)?
      │  └─ Yes → KDDockWidgets
      └─ No
         ├─ Need sidebars with toolviews?
         │  └─ Yes → KateMDI::MainWindow
         └─ No → KXmlGuiWindow
```
