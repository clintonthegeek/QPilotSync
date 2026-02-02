# UI Design Principles

## The Breeze Design Language

KDE applications follow the Breeze design language, which emphasizes:

- **Clarity**: Clean lines, appropriate whitespace, readable typography
- **Consistency**: Uniform styling across all applications
- **Subtlety**: Understated visual elements that don't distract from content
- **Depth**: Subtle shadows and layering to indicate hierarchy

### Color Palette Integration

Use KDE's color scheme system rather than hardcoding colors:

```cpp
#include <KColorScheme>

// Get colors from the current scheme
KColorScheme scheme(QPalette::Active);
QColor viewBackground = scheme.background(KColorScheme::NormalBackground).color();
QColor viewForeground = scheme.foreground(KColorScheme::NormalText).color();
QColor selection = scheme.background(KColorScheme::ActiveBackground).color();

// Decoration colors (for highlights, accents)
KColorScheme decorationScheme(QPalette::Active, KColorScheme::Selection);
QColor accent = decorationScheme.background().color();
```

**Color Roles:**
| Role | Use Case |
|------|----------|
| `NormalBackground` | Standard widget background |
| `AlternateBackground` | Alternating row backgrounds |
| `ActiveBackground` | Selected items, focus indicators |
| `LinkBackground` | Hyperlink hover states |
| `VisitedBackground` | Visited link indicators |
| `NegativeBackground` | Error states, destructive actions |
| `NeutralBackground` | Warnings, caution states |
| `PositiveBackground` | Success states, confirmations |

### Theme-Aware Custom Painting

When implementing custom widgets, respect the theme:

```cpp
void MyWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    // Use palette colors, not hardcoded values
    painter.fillRect(rect(), palette().window());
    painter.setPen(palette().windowText().color());

    // For more specific colors, use KColorScheme
    KColorScheme scheme(QPalette::Active, KColorScheme::View);
    painter.setBrush(scheme.background(KColorScheme::NormalBackground));
}
```

---

## Layout Patterns

### Standard Spacing Values

KDE applications use consistent spacing:

```cpp
// Standard margins and spacing
const int StandardMargin = 11;   // Dialog/window margins
const int StandardSpacing = 6;   // Between related widgets
const int GroupSpacing = 11;     // Between widget groups
const int SectionSpacing = 17;   // Between major sections
```

Use `QStyle` for dynamic spacing:

```cpp
int spacing = style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
int margin = style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
```

### Splitter-Based Layouts

Multi-panel applications use `QSplitter` for resizable sections:

```cpp
// Dolphin-style three-panel layout
QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);

// Left panel (places/folders)
QWidget *leftPanel = createLeftPanel();
mainSplitter->addWidget(leftPanel);

// Center (main content)
QWidget *centerPanel = createCenterPanel();
mainSplitter->addWidget(centerPanel);

// Right panel (information/preview)
QWidget *rightPanel = createRightPanel();
mainSplitter->addWidget(rightPanel);

// Set initial proportions (20% / 60% / 20%)
mainSplitter->setSizes({200, 600, 200});

// Allow collapsing side panels
mainSplitter->setCollapsible(0, true);
mainSplitter->setCollapsible(2, true);
```

### Stacked Widget Patterns

For view switching (common in Kate's bottom bar):

```cpp
// From Kate's bottom view bar container pattern
class ContainerStackedLayout : public QStackedLayout
{
public:
    explicit ContainerStackedLayout(QWidget *parent)
        : QStackedLayout(parent)
    {
        setStackingMode(QStackedLayout::StackAll);
    }

    QSize sizeHint() const override
    {
        // Return size of current widget, not largest
        if (QWidget *w = currentWidget()) {
            return w->sizeHint();
        }
        return QStackedLayout::sizeHint();
    }

    QSize minimumSize() const override
    {
        if (QWidget *w = currentWidget()) {
            return w->minimumSize();
        }
        return QStackedLayout::minimumSize();
    }
};
```

---

## Icon Integration

### Using Freedesktop Icons

Always use the icon theme system:

```cpp
#include <QIcon>

// Standard icons from theme
QIcon saveIcon = QIcon::fromTheme(QStringLiteral("document-save"));
QIcon openIcon = QIcon::fromTheme(QStringLiteral("document-open"));

// With fallback
QIcon myIcon = QIcon::fromTheme(QStringLiteral("my-custom-icon"),
                                 QIcon::fromTheme(QStringLiteral("application-x-generic")));

// For actions (with fallback to built-in)
QAction *action = new QAction(this);
action->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
```

### Standard Icon Names

Use Freedesktop icon naming conventions:

| Category | Examples |
|----------|----------|
| Actions | `document-new`, `document-open`, `document-save`, `edit-undo`, `edit-redo` |
| Applications | `system-file-manager`, `utilities-terminal` |
| Categories | `applications-development`, `preferences-system` |
| Devices | `drive-harddisk`, `media-optical` |
| Emblems | `emblem-favorite`, `emblem-important` |
| Mimetypes | `text-plain`, `image-png`, `application-pdf` |
| Places | `folder`, `user-home`, `user-trash` |
| Status | `dialog-information`, `dialog-warning`, `dialog-error` |

### Icon Sizes

Standard icon sizes for different contexts:

```cpp
// Toolbar icons
const int ToolbarSmall = 16;
const int ToolbarMedium = 22;
const int ToolbarLarge = 32;

// Other contexts
const int MenuIcon = 16;
const int DialogIcon = 32;
const int MessageBoxIcon = 64;
const int SplashIcon = 128;

// Using style hints
int toolbarSize = style()->pixelMetric(QStyle::PM_ToolBarIconSize);
int smallIconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
int largeIconSize = style()->pixelMetric(QStyle::PM_LargeIconSize);
```

---

## Panel Design Patterns

### Panel Base Class

Dolphin's panel pattern provides a clean abstraction:

```cpp
// Based on dolphin/src/panels/panel.h
class Panel : public QWidget
{
    Q_OBJECT

public:
    explicit Panel(QWidget *parent = nullptr)
        : QWidget(parent)
    {}

    QUrl url() const { return m_url; }

    void setUrl(const QUrl &url)
    {
        if (m_url != url) {
            m_url = url;
            urlChanged();
        }
    }

    void setCustomContextMenuActions(const QList<QAction *> &actions)
    {
        m_customContextMenuActions = actions;
    }

    QList<QAction *> customContextMenuActions() const
    {
        return m_customContextMenuActions;
    }

    virtual void readSettings() = 0;

Q_SIGNALS:
    void urlActivated(const QUrl &url);

protected:
    virtual bool urlChanged() = 0;

private:
    QUrl m_url;
    QList<QAction *> m_customContextMenuActions;
};
```

### Information Panels

For displaying metadata/properties:

```cpp
class InformationPanel : public Panel
{
    Q_OBJECT

public:
    explicit InformationPanel(QWidget *parent = nullptr)
        : Panel(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        // Preview area
        m_preview = new QLabel(this);
        m_preview->setAlignment(Qt::AlignCenter);
        m_preview->setMinimumSize(128, 128);
        layout->addWidget(m_preview);

        // Metadata display
        m_metadataWidget = new MetadataWidget(this);
        layout->addWidget(m_metadataWidget);

        layout->addStretch();
    }

protected:
    bool urlChanged() override
    {
        // Update preview and metadata for new URL
        updatePreview();
        updateMetadata();
        return true;
    }
};
```

---

## Accessibility

### Keyboard Navigation

Ensure all functionality is keyboard accessible:

```cpp
// Set focus policy for interactive widgets
widget->setFocusPolicy(Qt::StrongFocus);

// Tab order
QWidget::setTabOrder(widget1, widget2);
QWidget::setTabOrder(widget2, widget3);

// Keyboard shortcuts
QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_F2), widget);
connect(shortcut, &QShortcut::activated, this, &MyClass::onRename);
```

### Accessible Names and Descriptions

```cpp
// Set accessible properties
widget->setAccessibleName(i18n("File list"));
widget->setAccessibleDescription(i18n("List of files in the current directory"));

// For custom widgets
slider->setAccessibleName(i18n("Zoom"));
slider->setAccessibleDescription(i18n("Adjust zoom level from %1 to %2 percent",
                                       minZoom, maxZoom));
```

### Focus Indicators

Ensure visible focus indicators:

```cpp
void MyWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);

    // Draw content...

    // Draw focus indicator if focused
    if (hasFocus()) {
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.backgroundColor = palette().window().color();
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
    }
}
```

---

## Responsive Design

### Handling Window Resize

```cpp
void MainWindow::resizeEvent(QResizeEvent *event)
{
    KXmlGuiWindow::resizeEvent(event);

    // Adjust layout based on window size
    if (event->size().width() < 800) {
        // Compact mode: hide optional panels
        m_infoPanel->hide();
        m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    } else {
        m_infoPanel->show();
        m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
}
```

### Size Policies

Use appropriate size policies:

```cpp
// Fixed size (buttons, icons)
button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

// Expanding (main content areas)
view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

// Preferred (panels, toolbars)
panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

// Minimum expanding (sidebars that should shrink)
sidebar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
sidebar->setMinimumWidth(200);
```

---

## Visual Hierarchy

### Grouping Related Controls

```cpp
// Use QGroupBox for labeled groups
QGroupBox *appearanceGroup = new QGroupBox(i18n("Appearance"), this);
QVBoxLayout *groupLayout = new QVBoxLayout(appearanceGroup);

// Use frames for visual separation without labels
QFrame *separator = new QFrame(this);
separator->setFrameShape(QFrame::HLine);
separator->setFrameShadow(QFrame::Sunken);

// Use spacing to create visual groups
layout->addWidget(relatedWidget1);
layout->addWidget(relatedWidget2);
layout->addSpacing(StandardSpacing);  // Group separator
layout->addWidget(unrelatedWidget);
```

### Typography Hierarchy

```cpp
// Headers - larger, possibly bold
QLabel *header = new QLabel(i18n("Section Title"), this);
QFont headerFont = header->font();
headerFont.setPointSize(headerFont.pointSize() + 2);
headerFont.setBold(true);
header->setFont(headerFont);

// Subheaders
QLabel *subheader = new QLabel(i18n("Subsection"), this);
QFont subFont = subheader->font();
subFont.setBold(true);
subheader->setFont(subFont);

// Body text - default font

// Captions/hints - smaller, possibly gray
QLabel *hint = new QLabel(i18n("Optional description"), this);
QFont hintFont = hint->font();
hintFont.setPointSize(hintFont.pointSize() - 1);
hint->setFont(hintFont);
hint->setForegroundRole(QPalette::PlaceholderText);
```

---

## Loading and Progress States

### Busy Indicators

```cpp
// For indeterminate progress
QProgressBar *progressBar = new QProgressBar(this);
progressBar->setRange(0, 0);  // Indeterminate mode

// Cursor feedback
QGuiApplication::setOverrideCursor(Qt::WaitCursor);
// ... long operation ...
QGuiApplication::restoreOverrideCursor();

// For widgets
class BusyWidget : public QWidget
{
    void setBusy(bool busy)
    {
        if (busy) {
            // Show spinner overlay
            m_spinner->start();
            m_spinner->show();
        } else {
            m_spinner->stop();
            m_spinner->hide();
        }
    }
};
```

### Progress Feedback

See [07 - UI Components](07-ui-components.md) for detailed status bar and progress patterns.

---

## Animation Guidelines

Animations should be:
- **Subtle**: 150-300ms duration for most transitions
- **Purposeful**: Convey meaning, not just decoration
- **Interruptible**: User actions should cancel animations
- **Respectful**: Honor system animation preferences

```cpp
// Check if animations are enabled
bool animationsEnabled = !QApplication::styleHints()->prefersReducedMotion();

if (animationsEnabled) {
    QPropertyAnimation *animation = new QPropertyAnimation(widget, "geometry");
    animation->setDuration(200);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(startRect);
    animation->setEndValue(endRect);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
} else {
    widget->setGeometry(endRect);
}
```
