# KDE/Qt Keyboard Handling Best Practices

A reference guide for keyboard handling in KDE/Qt applications, distilled from
analysis of KOrganizer, Dolphin, and Kate source code. Applicable to any
KDE Frameworks (KF6) application.

---

## 1. KActionCollection — The Central Registry

Every keyboard shortcut in a KDE application should be registered through
`KActionCollection`. This is the single source of truth that enables:

- User-configurable shortcuts via the **Configure Shortcuts** dialog
- Discoverability through menus and tooltips
- Conflict detection across the entire application
- Persistence of user customizations in `~/.config/<app>rc`

### Registering Actions

```cpp
KActionCollection *ac = actionCollection();  // from KXmlGuiWindow

// Create and register an action
auto *action = ac->addAction(QStringLiteral("incidence_new_event"));
action->setText(i18n("New &Event..."));
action->setIcon(QIcon::fromTheme(QStringLiteral("appointment-new")));
ac->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::Key_E));
connect(action, &QAction::triggered, this, &MyWindow::onNewEvent);
```

**PlanStanLite example** — `mainwindow.cpp:156-160`:
```cpp
m_actionNewCollection = ac->addAction(QStringLiteral("collection_new"));
m_actionNewCollection->setText(i18n("&New Collection..."));
m_actionNewCollection->setIcon(QIcon::fromTheme(QStringLiteral("folder-new")));
ac->setDefaultShortcut(m_actionNewCollection, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
connect(m_actionNewCollection, &QAction::triggered, this, &MainWindow::onActionNewCollection);
```

### `setDefaultShortcut()` vs `setDefaultShortcuts()`

- **`setDefaultShortcut(action, keySequence)`** — assign one default binding
- **`setDefaultShortcuts(action, {keySeq1, keySeq2})`** — assign multiple
  bindings (e.g., both `Ctrl+S` and `F5` for Save)

Always use the `KActionCollection` versions of these methods (not
`QAction::setShortcut()`), because only KActionCollection-managed shortcuts
are visible to the Configure Shortcuts dialog.

### KStandardAction Helpers

For common operations, use `KStandardAction` — it provides the correct icon,
text, shortcut, and action name automatically:

```cpp
KStandardAction::quit(this, &MainWindow::onQuit, ac);
KStandardAction::preferences(this, &MainWindow::onPreferences, ac);
KStandardAction::undo(nullptr, nullptr, ac);  // connected later
KStandardAction::redo(nullptr, nullptr, ac);
KStandardAction::openRecent(this, &MainWindow::openFile, ac);
```

Standard action names (e.g., `file_quit`, `edit_undo`) are automatically
recognized by KXMLGUI and placed in the correct menu position.

**KOrganizer example** (`actionmanager.cpp:549-556`) uses `KStandardAction`
for Open, Save, Undo/Redo, and Quit. **Dolphin** (`dolphinmainwindow.cpp:1834-1843`)
uses it for Copy, Paste, SelectAll, and Find.

---

## 2. QShortcut vs KActionCollection — When to Use Each

### KActionCollection (the default for nearly everything)

Use KActionCollection for:
- All menu-visible operations
- All toolbar operations
- Any shortcut the user should be able to remap
- Application-wide or window-wide shortcuts

### QShortcut (rare — widget-local editing only)

Use `QShortcut` only when:
- The shortcut is truly local to a specific widget (e.g., Tab column cycling
  in an inline editor)
- The shortcut would conflict with a global action in a different context
- The shortcut is part of a low-level editing interaction, not a user-facing
  "command"

**PlanStanLite example** — `ganttsubwindow.cpp:174-187` uses `QShortcut` for
task hierarchy operations scoped to the Gantt view:

```cpp
// Keyboard shortcuts scoped to this window
auto *scLinkTasks = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F2), this);
connect(scLinkTasks, &QShortcut::activated, this, &GanttSubWindow::onLinkTasks);

auto *scPromote = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
connect(scPromote, &QShortcut::activated, this, &GanttSubWindow::onPromote);
```

**The problem:** These shortcuts are invisible to **Settings > Configure
Shortcuts**, cannot be remapped by the user, and don't appear in any menu.
The recommended approach is to register them as KActionCollection actions with
`WidgetWithChildrenShortcut` context (see below).

### Shortcut Context Options

`QAction::ShortcutContext` controls when a shortcut is active:

| Context | Scope | Use Case |
|---------|-------|----------|
| `WindowShortcut` | Active when the window has focus (default) | Most actions |
| `ApplicationShortcut` | Active across all windows | Global operations |
| `WidgetShortcut` | Active only when the specific widget has focus | Inline editors |
| `WidgetWithChildrenShortcut` | Active when the widget or any child has focus | **Panel-specific actions** |

To scope a KActionCollection action to a specific widget:

```cpp
auto *action = ac->addAction(QStringLiteral("gantt_promote"));
action->setText(i18n("&Promote Task"));
ac->setDefaultShortcut(action, QKeySequence(Qt::ALT | Qt::Key_Left));

// Scope to the Gantt subwindow widget
action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
ganttWidget->addAction(action);

connect(action, &QAction::triggered, this, &GanttSubWindow::onPromote);
```

This gives you the best of both worlds: user-configurable, discoverable, and
properly scoped.

---

## 3. Context-Dependent Shortcuts

### Enable/Disable Pattern (KOrganizer)

Actions always exist in the collection but are enabled/disabled based on
current selection state. This is the simplest and most common approach.

**KOrganizer** (`actionmanager.cpp:1352-1368`):
```cpp
void ActionManager::enableIncidenceActions(bool enabled)
{
    m_actionDeleteIncidence->setEnabled(enabled);
    m_actionEditIncidence->setEnabled(enabled);
    m_actionCutIncidence->setEnabled(enabled);
    m_actionCopyIncidence->setEnabled(enabled);
    // ...
}
```

**PlanStanLite** (`mainwindow.cpp:422-429`):
```cpp
void MainWindow::updateActionStates()
{
    bool hasCollection = (m_collectionController != nullptr);
    m_actionSaveCollection->setEnabled(hasCollection);
    m_actionCloseCollection->setEnabled(hasCollection);
    // ...
}
```

### Action Handler Pattern (Dolphin)

A single handler object switches which view processes actions. When the active
view changes, the handler redirects action signals to the new view.

**Dolphin** (`dolphinviewactionhandler.h:52`):
```cpp
class DolphinViewActionHandler : public QObject {
    void setCurrentView(DolphinView *view);
    // Actions trigger slots on m_currentView
};
```

This avoids duplicating actions across views while keeping a single set of
shortcuts active.

### Visitor Pattern for Context-Specific Text

**KOrganizer** uses `ActionStringsVisitor` (`actionmanager.cpp:1249-1350`)
to update action text based on the incidence type — "Delete Event" vs
"Delete Task" vs "Delete Journal" — while keeping the same action and shortcut.

### When NOT to Change Shortcuts Per Context

**Prefer enable/disable over rebinding.** Changing a shortcut's key sequence
based on context confuses users and breaks the Configure Shortcuts dialog.
If an action doesn't apply in a given context, disable it; don't repurpose
the key.

---

## 4. Focus Management Between Panels

### Focus Panel Actions Pattern (Dolphin)

**Dolphin** (`dolphinmainwindow.cpp:2492-2576`) creates explicit focus actions
for each panel:

```cpp
auto *action = ac->addAction(QStringLiteral("focus_terminal_panel"));
action->setText(i18n("Focus Terminal Panel"));
ac->setDefaultShortcut(action, QKeySequence(Qt::Key_F4));
connect(action, &QAction::triggered, terminalPanel, [terminalPanel]() {
    terminalPanel->setFocus(Qt::ShortcutFocusReason);
});
```

Each dockable panel gets a dedicated `focus_<name>_panel` action. This lets
users jump between panels with configurable shortcuts.

### ShortcutFocusReason

Always use `Qt::ShortcutFocusReason` when setting focus from a keyboard
shortcut:

```cpp
widget->setFocus(Qt::ShortcutFocusReason);
```

This tells Qt (and assistive technologies) that focus moved because of a
keyboard action, enabling proper focus ring display and screen reader
announcements.

### Tab Order

Use `setTabOrder()` for logical panel traversal:

```cpp
QWidget::setTabOrder(collectionExplorer, mainView);
QWidget::setTabOrder(mainView, editor);
QWidget::setTabOrder(editor, proactiveView);
```

### Focus Policies

| Policy | Use For |
|--------|---------|
| `Qt::StrongFocus` | Interactive widgets that should receive both Tab and click focus |
| `Qt::TabFocus` | Widgets reachable by Tab but not click (rare) |
| `Qt::ClickFocus` | Widgets focusable by click but not Tab (custom paint widgets) |
| `Qt::NoFocus` | Display-only labels and decorations |

---

## 5. Standard Keyboard Patterns Every View Should Support

These are the baseline keyboard interactions KDE users expect in any list/tree
view:

| Key | Action | Notes |
|-----|--------|-------|
| **Up/Down** | Navigate items | Arrow keys in lists and trees |
| **Left/Right** | Expand/collapse tree nodes | Only in tree views |
| **Enter/Return** | Activate (open, edit) selected item | The primary action for the selection |
| **Space** | Toggle (checkbox, expand/collapse) | Secondary activation |
| **Delete** | Delete selected items | With confirmation dialog |
| **F2** | Rename/edit in place | Inline editing trigger |
| **Shift+F10** | Open context menu | Equivalent to right-click; also **Menu** key |
| **Escape** | Cancel / close / deselect | Cancel inline edit, close popup, clear selection |
| **Ctrl+A** | Select all | QAbstractItemView provides this with `ExtendedSelection` |
| **Home/End** | Jump to first/last item | Standard list navigation |

**Shift+F10 / Menu key context menu** is particularly important. Many widgets
use `setContextMenuPolicy(Qt::CustomContextMenu)` with
`customContextMenuRequested`, which only fires on right-click. To support
keyboard context menus:

```cpp
// In keyPressEvent or eventFilter:
if (key == Qt::Key_F10 && modifiers == Qt::ShiftModifier) {
    QPoint pos = visualRect(currentIndex()).center();
    emit customContextMenuRequested(pos);
    return true;
}
```

Or connect to the `QAction::triggered` signal of a dedicated context menu
action.

---

## 6. KXMLGUI Setup

### The `.rc` File Structure

KXMLGUI defines menus and toolbars in an XML resource file:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="planstanlite" version="7">

  <MenuBar>
    <Menu name="file">
      <text>&amp;File</text>
      <Action name="collection_new"/>
      <Action name="collection_open"/>
      <Action name="file_open_recent"/>
      <Separator/>
      <Action name="collection_save"/>
      <Separator/>
      <Action name="file_quit"/>
    </Menu>

    <Menu name="settings">
      <text>&amp;Settings</text>
      <Merge name="StandardToolBarMenuHandler"/>
      <Action name="options_configure_toolbars"/>
      <Action name="options_configure_shortcuts"/>
    </Menu>
  </MenuBar>

  <ToolBar name="mainToolBar">
    <text>Main Toolbar</text>
    <Action name="collection_new"/>
    <Action name="collection_save"/>
  </ToolBar>

</gui>
```

**Bump the `version` attribute** whenever you change the `.rc` file, or user
customizations will override your changes.

### setupGUI()

Call `setupGUI()` at the end of your action setup:

```cpp
void MainWindow::setupActions()
{
    KActionCollection *ac = actionCollection();
    // ... create all actions ...
    setupGUI();  // Loads RC file, creates menus/toolbars
}
```

`setupGUI()` provides:
- Toolbar configuration dialog (`Settings > Configure Toolbars`)
- Shortcut configuration dialog (`Settings > Configure Shortcuts`)
- Menu bar construction from the `.rc` file
- Help menu auto-generation

### Menu Mnemonics

Every menu item should have an `&` accelerator in its text:

```cpp
action->setText(i18n("&New Collection..."));  // Alt+N activates
action->setText(i18n("&Delete"));             // Alt+D activates
action->setText(i18n("Go to &Today"));        // Alt+T activates
```

Ensure there are no duplicate mnemonics within the same menu.

### Configure Shortcuts Dialog

Including `options_configure_shortcuts` in the Settings menu (as shown in the
`.rc` file above) enables the standard **Settings > Configure Shortcuts**
dialog. This is provided automatically by `setupGUI()` and requires no
additional code — it reads all actions from `KActionCollection`.

---

## 7. Event Filter Patterns for Custom Keyboard Handling

### When to Use `eventFilter()` vs `keyPressEvent()` Override

| Approach | Use When |
|----------|----------|
| `keyPressEvent()` override | You own the widget class and want to handle keys directly |
| `eventFilter()` | You need to intercept keys in a widget you don't own (e.g., a QTreeView's viewport, a delegate's editor widget) |

### Event Filter Ordering (LIFO)

Event filters installed later run **first** (LIFO order). When you need to
intercept events before another filter (e.g., a delegate's filter on an editor
widget), you must install your filter **after** the other one.

**PlanStanLite's deferred installation pattern** (`ganttsubwindow.cpp:695-716`):

```cpp
bool GanttSubWindow::handleEditorChildTracking(QObject *watched, QEvent *event,
                                                QWidget *treeViewport)
{
    // When the view opens an inline editor, the delegate installs its filter
    // AFTER the widget is parented (ChildAdded fires first). We defer with
    // singleShot(0) so our filter installs after the delegate's, meaning
    // ours runs first (LIFO).
    if (watched == treeViewport && event->type() == QEvent::ChildAdded) {
        auto *ce = static_cast<QChildEvent*>(event);
        if (ce->child()->isWidgetType()) {
            QPointer<QObject> child = ce->child();
            QTimer::singleShot(0, this, [this, child]() {
                if (child)
                    child->installEventFilter(this);
            });
        }
        return true;  // Don't consume ChildAdded
    }
    return false;
}
```

### Return Value: Consume vs Propagate

- **`return true`** — event is consumed; no further processing
- **`return false`** — event propagates to the next filter or the widget itself

Be deliberate about which events you consume:

```cpp
bool GanttSubWindow::eventFilter(QObject *watched, QEvent *event)
{
    // ...

    if (handleEditorChildTracking(watched, event, treeViewport))
        return false;  // Don't consume ChildAdded — just installed filter

    if (handleTabNavigation(watched, event, treeView, treeViewport))
        return true;   // Consume Tab — we handled column cycling

    if (handleRapidTaskEntry(watched, event, treeView, treeViewport))
        return true;   // Consume Enter/Insert/Delete

    return BaseCalendarSubWindow::eventFilter(watched, event);
}
```

### Multi-Handler Event Filter Chain

For complex widgets with multiple keyboard behaviors, decompose the
`eventFilter()` into focused helper methods:

**PlanStanLite's GanttSubWindow** (`ganttsubwindow.cpp:874-909`) uses this
pattern:

| Handler | Responsibility | Lines |
|---------|---------------|-------|
| `handleEditorChildTracking()` | Install filter on new editor widgets | 695-716 |
| `handleTabNavigation()` | Tab/Shift+Tab column cycling in editors | 718-805 |
| `handleRapidTaskEntry()` | Enter/Insert/Delete for task creation | 807-847 |
| `handleChartContextMenu()` | Right-click context menu on Gantt chart | 849-872 |

The main `eventFilter()` delegates to each handler in priority order. Each
handler returns `true` if it handled the event or `false` to pass through.
This keeps each handler small and testable.
