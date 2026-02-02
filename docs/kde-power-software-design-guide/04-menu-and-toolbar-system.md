# Menu and Toolbar System

KDE applications use XMLGUI for defining menus and toolbars, allowing user customization while maintaining consistent defaults.

---

## XMLGUI Overview

XMLGUI separates UI structure (XML) from behavior (C++). Benefits include:

- User-customizable toolbars and shortcuts
- Consistent keyboard shortcut management
- Plugin GUI merging
- Centralized action collection

### File Structure

```
myapp/
├── src/
│   ├── mainwindow.cpp
│   └── mainwindow.h
└── data/
    └── myappui.rc          # XMLGUI definition
```

---

## XMLGUI File Format

### Basic Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="myapp" version="1" translationDomain="myapp">

  <MenuBar>
    <!-- Menu definitions -->
  </MenuBar>

  <ToolBar name="mainToolBar">
    <!-- Toolbar definitions -->
  </ToolBar>

  <ActionProperties>
    <!-- Action property overrides -->
  </ActionProperties>

</gui>
```

**Important Attributes:**
- `name`: Application identifier (must match `setComponentName()`)
- `version`: Increment when structure changes to update user configurations
- `translationDomain`: Domain for i18n (usually same as `name`)

### Complete Example (Dolphin-style)

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="dolphin" version="48" translationDomain="dolphin">

  <MenuBar>
    <Menu name="file">
      <text>&amp;File</text>
      <Action name="new_menu"/>
      <Action name="new_tab"/>
      <Action name="file_close"/>
      <Action name="undo"/>
      <Action name="redo"/>
      <Separator/>
      <Action name="renamefile"/>
      <Action name="movetotrash"/>
      <Action name="deletefile"/>
      <Separator/>
      <Action name="show_target"/>
      <Action name="properties"/>
      <Separator/>
      <Action name="file_quit"/>
    </Menu>

    <Menu name="edit">
      <text>&amp;Edit</text>
      <Action name="edit_select_all"/>
      <Action name="invert_selection"/>
      <Separator/>
      <Action name="edit_find"/>
      <Action name="duplicate"/>
    </Menu>

    <Menu name="view">
      <text>&amp;View</text>
      <Action name="view_zoom_in"/>
      <Action name="view_zoom_reset"/>
      <Action name="view_zoom_out"/>
      <Separator/>
      <Menu name="view_mode">
        <text>&amp;View Mode</text>
        <Action name="icons"/>
        <Action name="compact"/>
        <Action name="details"/>
      </Menu>
      <Separator/>
      <Action name="sort"/>
      <Action name="additional_info"/>
      <Separator/>
      <Action name="show_hidden_files"/>
      <Action name="view_properties"/>
      <Separator/>
      <Action name="split_view"/>
      <Action name="split_stacked"/>
      <Action name="close_active_view"/>
      <Separator/>
      <Action name="view_redisplay"/>
    </Menu>

    <Menu name="go">
      <text>&amp;Go</text>
      <Action name="go_back"/>
      <Action name="go_forward"/>
      <Action name="go_up"/>
      <Action name="go_home"/>
      <Separator/>
      <Action name="closed_tabs"/>
    </Menu>

    <Menu name="tools">
      <text>&amp;Tools</text>
      <Action name="open_preferred_search_tool"/>
      <Action name="open_terminal"/>
      <Action name="open_terminal_here"/>
      <Action name="change_remote_encoding"/>
      <Separator/>
      <Action name="compare_files"/>
    </Menu>

    <Menu name="settings">
      <text>&amp;Settings</text>
      <Merge name="StandardToolBarMenuHandler"/>
      <Action name="options_show_menubar"/>
      <Separator/>
      <Action name="panels"/>
      <Separator/>
      <Action name="options_configure_keybinding"/>
      <Action name="options_configure_toolbars"/>
      <Action name="options_configure"/>
    </Menu>

    <Menu name="help">
      <text>&amp;Help</text>
    </Menu>
  </MenuBar>

  <ToolBar name="mainToolBar" noMerge="1">
    <text>Main Toolbar</text>
    <Action name="go_back"/>
    <Action name="go_forward"/>
    <Separator/>
    <Action name="icons"/>
    <Action name="compact"/>
    <Action name="details"/>
    <Separator/>
    <Action name="edit_find"/>
    <Action name="split_view"/>
    <Spacer/>
    <Action name="hamburger_menu"/>
  </ToolBar>

  <!-- State-based action visibility -->
  <State name="has_selection">
    <enable>
      <Action name="renamefile"/>
      <Action name="movetotrash"/>
      <Action name="deletefile"/>
      <Action name="duplicate"/>
    </enable>
  </State>

  <State name="has_no_selection">
    <disable>
      <Action name="renamefile"/>
      <Action name="movetotrash"/>
      <Action name="deletefile"/>
      <Action name="duplicate"/>
    </disable>
  </State>

</gui>
```

---

## Standard Menu Structure

KDE applications follow a consistent menu order:

| Menu | Purpose | Standard Actions |
|------|---------|------------------|
| **File** | Document operations | New, Open, Save, Close, Quit |
| **Edit** | Content manipulation | Undo, Redo, Cut, Copy, Paste, Select All |
| **View** | Display options | Zoom, View modes, Panels, Toolbars |
| **Go** | Navigation | Back, Forward, Up, Home, Bookmarks |
| **Tools** | External tools | Application-specific utilities |
| **Settings** | Configuration | Toolbars, Shortcuts, Preferences |
| **Help** | Documentation | Handbook, About, What's This |

### Menu Text Conventions

```xml
<!-- Use & for keyboard accelerators -->
<Menu name="file">
  <text>&amp;File</text>
</Menu>

<!-- Action text includes accelerator -->
<Action name="file_save"/>  <!-- Text set in C++: i18n("&Save") -->
```

---

## Creating Actions in C++

### Using KActionCollection

```cpp
#include <KActionCollection>
#include <KStandardAction>
#include <KLocalizedString>

void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    // Standard actions (automatically get icons, shortcuts, text)
    KStandardAction::openNew(this, &MainWindow::newFile, ac);
    KStandardAction::open(this, &MainWindow::openFile, ac);
    KStandardAction::save(this, &MainWindow::saveFile, ac);
    KStandardAction::saveAs(this, &MainWindow::saveFileAs, ac);
    KStandardAction::quit(qApp, &QCoreApplication::quit, ac);

    KStandardAction::undo(this, &MainWindow::undo, ac);
    KStandardAction::redo(this, &MainWindow::redo, ac);
    KStandardAction::cut(this, &MainWindow::cut, ac);
    KStandardAction::copy(this, &MainWindow::copy, ac);
    KStandardAction::paste(this, &MainWindow::paste, ac);
    KStandardAction::selectAll(this, &MainWindow::selectAll, ac);

    KStandardAction::zoomIn(this, &MainWindow::zoomIn, ac);
    KStandardAction::zoomOut(this, &MainWindow::zoomOut, ac);
    KStandardAction::actualSize(this, &MainWindow::zoomReset, ac);

    KStandardAction::preferences(this, &MainWindow::showSettings, ac);
    KStandardAction::configureToolbars(this, &MainWindow::configureToolbars, ac);
    KStandardAction::keyBindings(this, &MainWindow::configureShortcuts, ac);

    // Custom actions
    QAction *myAction = ac->addAction(QStringLiteral("my_custom_action"));
    myAction->setText(i18n("&My Action"));
    myAction->setIcon(QIcon::fromTheme(QStringLiteral("document-properties")));
    myAction->setToolTip(i18n("Perform my action"));
    myAction->setWhatsThis(i18n("Detailed description of what my action does"));
    ac->setDefaultShortcut(myAction, QKeySequence(Qt::CTRL | Qt::Key_M));
    connect(myAction, &QAction::triggered, this, &MainWindow::onMyAction);

    // Checkable action
    QAction *toggleAction = ac->addAction(QStringLiteral("toggle_feature"));
    toggleAction->setText(i18n("Enable &Feature"));
    toggleAction->setCheckable(true);
    toggleAction->setChecked(m_featureEnabled);
    connect(toggleAction, &QAction::toggled, this, &MainWindow::setFeatureEnabled);
}
```

### Action Categories

Organize actions into categories for the shortcut configuration dialog:

```cpp
void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();

    // Create categories
    KActionCategory *fileCategory = new KActionCategory(i18n("File"), ac);
    KActionCategory *viewCategory = new KActionCategory(i18n("View"), ac);
    KActionCategory *toolsCategory = new KActionCategory(i18n("Tools"), ac);

    // Add actions to categories
    QAction *newAction = fileCategory->addAction(QStringLiteral("file_new"));
    newAction->setText(i18n("&New"));

    QAction *zoomAction = viewCategory->addAction(QStringLiteral("zoom_in"));
    zoomAction->setText(i18n("Zoom &In"));
}
```

### Kdenlive's Category Pattern

```cpp
// Store categories for reuse
QMap<QString, KActionCategory *> m_categoryMap;

KActionCategory *getOrCreateCategory(const QString &name)
{
    if (!m_categoryMap.contains(name)) {
        m_categoryMap[name] = new KActionCategory(
            i18n(qPrintable(name)), actionCollection());
    }
    return m_categoryMap[name];
}

void setupAction(const QString &category, const QString &name,
                 const QString &text, const QIcon &icon)
{
    KActionCategory *cat = getOrCreateCategory(category);
    QAction *action = cat->addAction(name);
    action->setText(text);
    action->setIcon(icon);
}
```

---

## State-Based Action Management

Enable/disable actions based on application state:

### Using XMLGUI States

```xml
<!-- In your .rc file -->
<State name="has_selection">
  <enable>
    <Action name="edit_cut"/>
    <Action name="edit_copy"/>
    <Action name="delete"/>
  </enable>
</State>

<State name="has_no_selection">
  <disable>
    <Action name="edit_cut"/>
    <Action name="edit_copy"/>
    <Action name="delete"/>
  </disable>
</State>

<State name="read_only">
  <disable>
    <Action name="file_save"/>
    <Action name="edit_cut"/>
    <Action name="edit_paste"/>
  </disable>
</State>
```

### Activating States in C++

```cpp
void MainWindow::onSelectionChanged(bool hasSelection)
{
    if (hasSelection) {
        stateChanged(QStringLiteral("has_selection"));
    } else {
        stateChanged(QStringLiteral("has_no_selection"));
    }
}

void MainWindow::setReadOnly(bool readOnly)
{
    if (readOnly) {
        stateChanged(QStringLiteral("read_only"));
    } else {
        stateChanged(QStringLiteral("read_only"),
                     KXMLGUIClient::StateReverse);
    }
}
```

### Manual State Management

```cpp
void MainWindow::updateActions()
{
    bool hasSelection = !m_view->selectedItems().isEmpty();
    bool canPaste = QApplication::clipboard()->mimeData()->hasUrls();
    bool isModified = m_document->isModified();

    actionCollection()->action(QStringLiteral("edit_cut"))->setEnabled(hasSelection);
    actionCollection()->action(QStringLiteral("edit_copy"))->setEnabled(hasSelection);
    actionCollection()->action(QStringLiteral("edit_paste"))->setEnabled(canPaste);
    actionCollection()->action(QStringLiteral("file_save"))->setEnabled(isModified);
}
```

---

## Context Menus

### Basic Context Menu

```cpp
void MyWidget::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    menu.addAction(actionCollection()->action(QStringLiteral("edit_copy")));
    menu.addAction(actionCollection()->action(QStringLiteral("edit_paste")));
    menu.addSeparator();
    menu.addAction(actionCollection()->action(QStringLiteral("properties")));

    menu.exec(event->globalPos());
}
```

### Dynamic Context Menu (Dolphin Style)

```cpp
void MyView::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    QModelIndex index = indexAt(pos);

    if (index.isValid()) {
        // Item-specific actions
        menu.addAction(m_openAction);
        menu.addAction(m_renameAction);
        menu.addSeparator();

        // Add "Open With" submenu
        QMenu *openWithMenu = new QMenu(i18n("Open With"), &menu);
        populateOpenWithMenu(openWithMenu, index);
        menu.addMenu(openWithMenu);

        menu.addSeparator();
        menu.addAction(m_cutAction);
        menu.addAction(m_copyAction);
        menu.addSeparator();
        menu.addAction(m_trashAction);
        menu.addAction(m_deleteAction);
        menu.addSeparator();
        menu.addAction(m_propertiesAction);
    } else {
        // Background actions
        menu.addAction(m_pasteAction);
        menu.addAction(m_selectAllAction);
        menu.addSeparator();
        menu.addAction(m_newFolderAction);
        menu.addSeparator();
        menu.addAction(m_propertiesAction);
    }

    menu.exec(mapToGlobal(pos));
}
```

### KFileItemActions Integration

For file manager context menus:

```cpp
#include <KFileItemActions>
#include <KFileItemListProperties>

void FileView::showContextMenu(const QPoint &pos, const KFileItemList &items)
{
    QMenu menu(this);

    KFileItemListProperties props(items);

    // Standard file actions
    m_fileItemActions = new KFileItemActions(this);
    m_fileItemActions->setItemListProperties(props);
    m_fileItemActions->setParentWidget(this);

    // Add "Open" action
    m_fileItemActions->insertOpenWithActionsTo(nullptr, &menu, QStringList());

    menu.addSeparator();

    // Add service menu actions (right-click service menus)
    m_fileItemActions->addActionsTo(&menu);

    menu.exec(mapToGlobal(pos));
}
```

---

## Hamburger Menu

Modern KDE applications include a hamburger menu for compact interfaces:

### XMLGUI Setup

```xml
<ToolBar name="mainToolBar" noMerge="1">
  <text>Main Toolbar</text>
  <Action name="go_back"/>
  <Action name="go_forward"/>
  <Spacer/>
  <Action name="hamburger_menu"/>
</ToolBar>
```

### Code Setup

```cpp
#include <KHamburgerMenu>
#include <KToolBar>

void MainWindow::setupActions()
{
    // Create hamburger menu
    auto *hamburgerMenu = KStandardAction::hamburgerMenu(
        nullptr, nullptr, actionCollection());

    // Configure what appears in the menu
    hamburgerMenu->setMenuBar(menuBar());
    hamburgerMenu->setShowMenuBarAction(
        actionCollection()->action(KStandardAction::name(
            KStandardAction::ShowMenubar)));

    // Connect menu about to show
    connect(hamburgerMenu, &KHamburgerMenu::aboutToShowMenu, this, [this]() {
        // Update menu state before showing
        updateMenuState();
    });
}

void MainWindow::setupGUI()
{
    KXmlGuiWindow::setupGUI(Default, QStringLiteral("myappui.rc"));

    // Initially hide menu bar, show hamburger
    menuBar()->hide();
    toolBar()->show();
}
```

---

## Plugin GUI Merging

Plugins can contribute to menus and toolbars:

### Merge Points in Main XMLGUI

```xml
<Menu name="tools">
  <text>&amp;Tools</text>
  <Action name="builtin_tool"/>
  <Separator/>
  <!-- Plugin actions merge here -->
  <DefineGroup name="tools_operations"/>
  <Separator/>
  <Merge/>  <!-- Additional plugin merge point -->
</Menu>
```

### Plugin XMLGUI

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="myplugin" version="1" translationDomain="myplugin">

  <MenuBar>
    <Menu name="tools">
      <Action name="plugin_action" group="tools_operations"/>
    </Menu>
  </MenuBar>

  <ToolBar name="mainToolBar">
    <Action name="plugin_action"/>
  </ToolBar>

</gui>
```

### Registering Plugin GUI

```cpp
// Plugin code
MyPlugin::MyPlugin(QObject *parent)
    : KTextEditor::Plugin(parent)
{
}

QObject *MyPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    auto *view = new MyPluginView(this, mainWindow);
    return view;
}

MyPluginView::MyPluginView(MyPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , KXMLGUIClient()
    , m_mainWindow(mainWindow)
{
    KXMLGUIClient::setComponentName(QStringLiteral("myplugin"),
                                    i18n("My Plugin"));
    setXMLFile(QStringLiteral("mypluginui.rc"));

    // Setup actions
    QAction *action = actionCollection()->addAction(
        QStringLiteral("plugin_action"));
    action->setText(i18n("Plugin Action"));
    connect(action, &QAction::triggered, this, &MyPluginView::doAction);

    // Register with main window's GUI factory
    m_mainWindow->guiFactory()->addClient(this);
}

MyPluginView::~MyPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);
}
```

---

## Toolbar Configuration

### Allowing User Customization

```cpp
void MainWindow::configureToolbars()
{
    KConfigGroup config(KSharedConfig::openConfig(), QStringLiteral("MainWindow"));
    saveMainWindowSettings(config);

    KEditToolBar dlg(guiFactory(), this);
    connect(&dlg, &KEditToolBar::newToolBarConfig, this, [this]() {
        createGUI(QStringLiteral("myappui.rc"));
        applyMainWindowSettings(
            KSharedConfig::openConfig()->group(QStringLiteral("MainWindow")));
    });
    dlg.exec();
}
```

### Configuring Shortcuts

```cpp
void MainWindow::configureShortcuts()
{
    KShortcutsDialog dlg(KShortcutsEditor::AllActions,
                         KShortcutsEditor::LetterShortcutsAllowed, this);
    dlg.addCollection(actionCollection());

    // Add plugin collections
    for (KXMLGUIClient *client : guiFactory()->clients()) {
        if (client != this) {
            dlg.addCollection(client->actionCollection(), client->componentName());
        }
    }

    dlg.configure();
}
```

---

## Best Practices

1. **Always use KStandardAction** for common operations
2. **Increment version** in .rc file when changing structure
3. **Use DefineGroup** for plugin merge points
4. **Provide keyboard shortcuts** for all important actions
5. **Use consistent naming**: `file_*`, `edit_*`, `view_*`, etc.
6. **Include tooltips and What's This** for all actions
7. **Test with** `KDE_DEBUG=1` to see XMLGUI parsing errors
