# UI Components

This document covers common UI components used in KDE power applications.

---

## Dock Widgets

### Basic QDockWidget Setup

```cpp
void MainWindow::setupDockWidgets()
{
    // Create dock widget
    QDockWidget *infoDock = new QDockWidget(i18n("Information"), this);
    infoDock->setObjectName(QStringLiteral("infoDock"));  // For state saving

    // Set allowed areas
    infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    // Create content widget
    QWidget *infoWidget = new InformationPanel(infoDock);
    infoDock->setWidget(infoWidget);

    // Add to main window
    addDockWidget(Qt::RightDockWidgetArea, infoDock);

    // Add toggle action to View menu
    QAction *toggleAction = infoDock->toggleViewAction();
    actionCollection()->addAction(QStringLiteral("show_info_dock"), toggleAction);
}
```

### Custom Dock Widget (Dolphin Style)

```cpp
// Based on dolphin/src/dolphindockwidget.h

class CustomDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit CustomDockWidget(const QString &title, QWidget *parent = nullptr)
        : QDockWidget(title, parent)
        , m_locked(false)
        , m_dockTitleBar(nullptr)
    {
        // Store original title bar
        m_dockTitleBar = titleBarWidget();
    }

    bool isLocked() const { return m_locked; }

    void setLocked(bool lock)
    {
        if (lock != m_locked) {
            m_locked = lock;

            if (lock) {
                // Hide title bar when locked
                setTitleBarWidget(new QWidget(this));
                setFeatures(features() & ~(DockWidgetMovable | DockWidgetFloatable));
            } else {
                // Restore title bar
                setTitleBarWidget(m_dockTitleBar);
                setFeatures(DockWidgetMovable | DockWidgetClosable | DockWidgetFloatable);
            }
        }
    }

protected:
    bool event(QEvent *event) override
    {
        // Prevent spurious visibilityChanged signals during minimize/restore
        switch (event->type()) {
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
            // Don't emit visibilityChanged for these events
            return QWidget::event(event);
        default:
            return QDockWidget::event(event);
        }
    }

private:
    bool m_locked;
    QWidget *m_dockTitleBar;
};
```

### Lock All Docks Action

```cpp
void MainWindow::setupDockLocking()
{
    QAction *lockAction = new QAction(i18n("Lock Panels"), this);
    lockAction->setCheckable(true);
    actionCollection()->addAction(QStringLiteral("lock_panels"), lockAction);

    connect(lockAction, &QAction::toggled, this, [this](bool locked) {
        for (QDockWidget *dock : findChildren<QDockWidget *>()) {
            if (auto *customDock = qobject_cast<CustomDockWidget *>(dock)) {
                customDock->setLocked(locked);
            }
        }
    });
}
```

---

## Sidebar Panels

### Panel Base Class

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
            if (urlChanged()) {
                Q_EMIT urlChanged(m_url);
            }
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
    void urlChanged(const QUrl &url);

protected:
    virtual bool urlChanged() = 0;

private:
    QUrl m_url;
    QList<QAction *> m_customContextMenuActions;
};
```

### Information Panel

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
        layout->setSpacing(0);

        // Preview widget
        m_previewWidget = new PreviewWidget(this);
        layout->addWidget(m_previewWidget);

        // Metadata display
        m_metadataWidget = new MetadataWidget(this);
        layout->addWidget(m_metadataWidget);

        // Stretch at bottom
        layout->addStretch();
    }

    void readSettings() override
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("InformationPanel"));
        m_showPreview = config.readEntry("ShowPreview", true);
        m_previewSize = config.readEntry("PreviewSize", 256);

        m_previewWidget->setVisible(m_showPreview);
        m_previewWidget->setPreviewSize(m_previewSize);
    }

protected:
    bool urlChanged() override
    {
        if (url().isValid()) {
            m_previewWidget->loadPreview(url());
            m_metadataWidget->loadMetadata(url());
            return true;
        }
        return false;
    }

private:
    PreviewWidget *m_previewWidget;
    MetadataWidget *m_metadataWidget;
    bool m_showPreview = true;
    int m_previewSize = 256;
};
```

### Folder Tree Panel

```cpp
class FoldersPanel : public Panel
{
    Q_OBJECT

public:
    explicit FoldersPanel(QWidget *parent = nullptr)
        : Panel(parent)
    {
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);

        m_treeView = new QTreeView(this);
        m_treeView->setHeaderHidden(true);
        m_treeView->setUniformRowHeights(true);
        m_treeView->setAnimated(true);

        // Use KDirModel for folder browsing
        m_model = new KDirModel(this);
        m_model->setDirLister(new KDirLister(this));
        m_model->dirLister()->setDirOnlyMode(true);  // Only show folders

        m_treeView->setModel(m_model);

        // Hide all columns except name
        for (int i = 1; i < m_model->columnCount(); ++i) {
            m_treeView->hideColumn(i);
        }

        layout->addWidget(m_treeView);

        // Connect selection to URL activation
        connect(m_treeView->selectionModel(),
                &QItemSelectionModel::currentChanged,
                this, &FoldersPanel::onCurrentChanged);
    }

    void readSettings() override
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("FoldersPanel"));
        m_showHidden = config.readEntry("ShowHidden", false);
        m_model->dirLister()->setShowHiddenFiles(m_showHidden);
    }

protected:
    bool urlChanged() override
    {
        // Expand to and select the current URL
        QModelIndex index = m_model->indexForUrl(url());
        if (index.isValid()) {
            m_treeView->setCurrentIndex(index);
            m_treeView->scrollTo(index);
        }
        return true;
    }

private Q_SLOTS:
    void onCurrentChanged(const QModelIndex &current)
    {
        QUrl newUrl = m_model->itemForIndex(current).url();
        if (newUrl.isValid()) {
            Q_EMIT urlActivated(newUrl);
        }
    }

private:
    QTreeView *m_treeView;
    KDirModel *m_model;
    bool m_showHidden = false;
};
```

---

## Status Bars

### Basic Status Bar

```cpp
void MainWindow::setupStatusBar()
{
    // Status message label
    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel, 1);  // Stretch factor 1

    // Permanent widgets (right side)
    m_lineColumnLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_lineColumnLabel);

    m_encodingLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_encodingLabel);
}

void MainWindow::showStatusMessage(const QString &message, int timeout)
{
    statusBar()->showMessage(message, timeout);
}
```

### Animated Status Bar (Dolphin Style)

```cpp
// Based on dolphin/src/statusbar/dolphinstatusbar.cpp

class AnimatedStatusBar : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int height READ height WRITE setHeight)

public:
    explicit AnimatedStatusBar(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_height(0)
        , m_targetHeight(defaultHeight())
    {
        m_contentsWidget = new QWidget(this);

        QHBoxLayout *layout = new QHBoxLayout(m_contentsWidget);
        layout->setContentsMargins(4, 0, 4, 0);

        // Message label
        m_label = new KSqueezedTextLabel(m_contentsWidget);
        m_label->setTextFormat(Qt::PlainText);
        layout->addWidget(m_label, 1);

        // Progress bar (hidden by default)
        m_progressBar = new QProgressBar(m_contentsWidget);
        m_progressBar->hide();
        layout->addWidget(m_progressBar);

        // Stop button for canceling operations
        m_stopButton = new QToolButton(m_contentsWidget);
        m_stopButton->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
        m_stopButton->setAutoRaise(true);
        m_stopButton->hide();
        layout->addWidget(m_stopButton);

        // Zoom slider
        m_zoomSlider = new QSlider(Qt::Horizontal, m_contentsWidget);
        m_zoomSlider->setRange(0, 100);
        m_zoomSlider->setMaximumWidth(150);
        layout->addWidget(m_zoomSlider);

        // Animation
        m_animation = new QPropertyAnimation(this, "height", this);
        m_animation->setDuration(150);

        // Timer to delay showing progress bar
        m_progressTimer = new QTimer(this);
        m_progressTimer->setSingleShot(true);
        m_progressTimer->setInterval(500);  // Show after 500ms
        connect(m_progressTimer, &QTimer::timeout, this, [this]() {
            m_progressBar->show();
            m_stopButton->show();
        });

        setFixedHeight(0);  // Start hidden
    }

    void setMessage(const QString &message)
    {
        m_label->setText(message);
    }

    void showProgress(const QString &message, int percent)
    {
        m_label->setText(message);
        m_progressBar->setValue(percent);

        if (!m_progressBar->isVisible()) {
            m_progressTimer->start();
        }

        show();
    }

    void hideProgress()
    {
        m_progressTimer->stop();
        m_progressBar->hide();
        m_stopButton->hide();
        m_progressBar->setValue(0);
    }

    void show()
    {
        m_animation->stop();
        m_animation->setStartValue(height());
        m_animation->setEndValue(m_targetHeight);
        m_animation->start();
    }

    void hide()
    {
        m_animation->stop();
        m_animation->setStartValue(height());
        m_animation->setEndValue(0);
        m_animation->start();
    }

Q_SIGNALS:
    void stopPressed();
    void zoomLevelChanged(int level);

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_contentsWidget->setGeometry(0, 0, width(), m_targetHeight);
    }

private:
    int defaultHeight() const { return fontMetrics().height() + 8; }

    QWidget *m_contentsWidget;
    KSqueezedTextLabel *m_label;
    QProgressBar *m_progressBar;
    QToolButton *m_stopButton;
    QSlider *m_zoomSlider;
    QPropertyAnimation *m_animation;
    QTimer *m_progressTimer;
    int m_height;
    int m_targetHeight;
};
```

### Progress Widget with IStatus Interface

```cpp
// Based on kdevelop/kdevplatform/interfaces/istatus.h

class IStatus
{
public:
    virtual ~IStatus() = default;

    virtual QString statusName() const = 0;

    // Signals (pure virtual for interface)
    virtual void clearMessage(IStatus *) = 0;
    virtual void showMessage(IStatus *, const QString &message, int timeout = 0) = 0;
    virtual void showErrorMessage(const QString &message, int timeout = 5) = 0;
    virtual void hideProgress(IStatus *) = 0;
    virtual void showProgress(IStatus *, int minimum, int maximum, int value) = 0;
};

// Status bar that aggregates multiple IStatus providers
class AggregateStatusBar : public QStatusBar
{
    Q_OBJECT

public:
    void registerStatus(IStatus *status)
    {
        connect(qobject_cast<QObject *>(status),
                SIGNAL(showMessage(IStatus *, const QString &, int)),
                this, SLOT(onShowMessage(IStatus *, const QString &, int)));

        connect(qobject_cast<QObject *>(status),
                SIGNAL(showProgress(IStatus *, int, int, int)),
                this, SLOT(onShowProgress(IStatus *, int, int, int)));

        connect(qobject_cast<QObject *>(status),
                SIGNAL(hideProgress(IStatus *)),
                this, SLOT(onHideProgress(IStatus *)));
    }

private Q_SLOTS:
    void onShowMessage(IStatus *source, const QString &message, int timeout)
    {
        // Show message in status bar
        showMessage(QString("[%1] %2").arg(source->statusName(), message),
                    timeout * 1000);
    }

    void onShowProgress(IStatus *source, int min, int max, int value)
    {
        // Show or update progress
        if (!m_progressWidgets.contains(source)) {
            auto *progress = new QProgressBar(this);
            progress->setMaximumWidth(200);
            addPermanentWidget(progress);
            m_progressWidgets[source] = progress;
        }

        QProgressBar *progress = m_progressWidgets[source];
        progress->setRange(min, max);
        progress->setValue(value);
        progress->show();
    }

    void onHideProgress(IStatus *source)
    {
        if (m_progressWidgets.contains(source)) {
            m_progressWidgets[source]->hide();
        }
    }

private:
    QHash<IStatus *, QProgressBar *> m_progressWidgets;
};
```

---

## Tab Widgets

### Document Tab Widget

```cpp
class DocumentTabWidget : public QTabWidget
{
    Q_OBJECT

public:
    explicit DocumentTabWidget(QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabsClosable(true);
        setMovable(true);
        setDocumentMode(true);
        setUsesScrollButtons(true);

        // Context menu on tabs
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested,
                this, &DocumentTabWidget::onTabContextMenu);

        // Close tab on middle click
        tabBar()->installEventFilter(this);

        // Close button
        connect(this, &QTabWidget::tabCloseRequested,
                this, &DocumentTabWidget::onCloseTab);
    }

    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (obj == tabBar() && event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                int index = tabBar()->tabAt(mouseEvent->pos());
                if (index >= 0) {
                    Q_EMIT tabCloseRequested(index);
                    return true;
                }
            }
        }
        return QTabWidget::eventFilter(obj, event);
    }

private Q_SLOTS:
    void onTabContextMenu(const QPoint &pos)
    {
        int index = tabBar()->tabAt(pos);
        if (index < 0) {
            return;
        }

        QMenu menu(this);
        menu.addAction(i18n("Close"), this, [this, index]() {
            Q_EMIT tabCloseRequested(index);
        });
        menu.addAction(i18n("Close Others"), this, [this, index]() {
            closeOtherTabs(index);
        });
        menu.addAction(i18n("Close All"), this, [this]() {
            closeAllTabs();
        });
        menu.addSeparator();
        menu.addAction(i18n("Duplicate"), this, [this, index]() {
            Q_EMIT duplicateRequested(index);
        });

        menu.exec(tabBar()->mapToGlobal(pos));
    }

    void onCloseTab(int index)
    {
        QWidget *w = widget(index);
        removeTab(index);
        delete w;
    }

    void closeOtherTabs(int keepIndex)
    {
        for (int i = count() - 1; i >= 0; --i) {
            if (i != keepIndex) {
                onCloseTab(i);
            }
        }
    }

    void closeAllTabs()
    {
        while (count() > 0) {
            onCloseTab(0);
        }
    }

Q_SIGNALS:
    void duplicateRequested(int index);
};
```

### Tab Bar with New Tab Button

```cpp
class TabBarWithNewButton : public QTabBar
{
    Q_OBJECT

public:
    explicit TabBarWithNewButton(QWidget *parent = nullptr)
        : QTabBar(parent)
    {
        m_newTabButton = new QToolButton(this);
        m_newTabButton->setIcon(QIcon::fromTheme(QStringLiteral("tab-new")));
        m_newTabButton->setAutoRaise(true);
        m_newTabButton->setToolTip(i18n("New Tab"));

        connect(m_newTabButton, &QToolButton::clicked,
                this, &TabBarWithNewButton::newTabRequested);
    }

Q_SIGNALS:
    void newTabRequested();

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTabBar::resizeEvent(event);
        positionNewTabButton();
    }

    void tabLayoutChange() override
    {
        QTabBar::tabLayoutChange();
        positionNewTabButton();
    }

private:
    void positionNewTabButton()
    {
        int lastTabRight = 0;
        if (count() > 0) {
            lastTabRight = tabRect(count() - 1).right();
        }

        int x = lastTabRight + 4;
        int y = (height() - m_newTabButton->height()) / 2;

        m_newTabButton->move(x, y);
    }

    QToolButton *m_newTabButton;
};
```

---

## Toolviews (KateMDI Pattern)

### Creating Toolviews

```cpp
// Using Kate's toolview API
void MyMainWindow::setupToolViews()
{
    // Create left sidebar toolview
    m_fileTreeToolView = createToolView(
        nullptr,  // plugin (nullptr for built-in)
        QStringLiteral("file_tree_view"),
        KMultiTabBar::Left,
        QIcon::fromTheme(QStringLiteral("folder")),
        i18n("Files"));

    // Add content
    m_fileTree = new FileTreeWidget(m_fileTreeToolView);
    m_fileTreeToolView->layout()->addWidget(m_fileTree);

    // Create bottom sidebar toolview
    m_outputToolView = createToolView(
        nullptr,
        QStringLiteral("output_view"),
        KMultiTabBar::Bottom,
        QIcon::fromTheme(QStringLiteral("dialog-messages")),
        i18n("Output"));

    m_outputWidget = new OutputWidget(m_outputToolView);
    m_outputToolView->layout()->addWidget(m_outputWidget);
}

// Show/hide toolview
void MyMainWindow::toggleFileTree()
{
    if (m_fileTreeToolView->toolVisible()) {
        hideToolView(m_fileTreeToolView);
    } else {
        showToolView(m_fileTreeToolView);
    }
}
```

---

## Splitters

### Persistent Splitter State

```cpp
class PersistentSplitter : public QSplitter
{
    Q_OBJECT

public:
    explicit PersistentSplitter(const QString &configKey,
                                Qt::Orientation orientation,
                                QWidget *parent = nullptr)
        : QSplitter(orientation, parent)
        , m_configKey(configKey)
    {
        // Restore state
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("Splitters"));
        QByteArray state = config.readEntry(m_configKey, QByteArray());
        if (!state.isEmpty()) {
            restoreState(state);
        }

        // Save on changes (debounced)
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(500);
        connect(m_saveTimer, &QTimer::timeout, this, &PersistentSplitter::saveState);

        connect(this, &QSplitter::splitterMoved, this, [this]() {
            m_saveTimer->start();
        });
    }

    ~PersistentSplitter() override
    {
        saveState();
    }

private:
    void saveState()
    {
        KConfigGroup config(KSharedConfig::openConfig(),
                            QStringLiteral("Splitters"));
        config.writeEntry(m_configKey, QSplitter::saveState());
    }

    QString m_configKey;
    QTimer *m_saveTimer;
};
```

### Three-Panel Layout

```cpp
void MainWindow::setupLayout()
{
    // Main horizontal splitter
    m_mainSplitter = new PersistentSplitter(
        QStringLiteral("MainSplitter"),
        Qt::Horizontal, this);

    // Left panel (sidebar)
    m_leftPanel = new QWidget(m_mainSplitter);
    m_mainSplitter->addWidget(m_leftPanel);

    // Center area with vertical splitter
    m_centerSplitter = new PersistentSplitter(
        QStringLiteral("CenterSplitter"),
        Qt::Vertical, m_mainSplitter);
    m_mainSplitter->addWidget(m_centerSplitter);

    // Main content area
    m_contentArea = new QWidget(m_centerSplitter);
    m_centerSplitter->addWidget(m_contentArea);

    // Bottom panel
    m_bottomPanel = new QWidget(m_centerSplitter);
    m_centerSplitter->addWidget(m_bottomPanel);

    // Right panel
    m_rightPanel = new QWidget(m_mainSplitter);
    m_mainSplitter->addWidget(m_rightPanel);

    // Set initial sizes (20% / 60% / 20%)
    m_mainSplitter->setSizes({200, 600, 200});
    m_centerSplitter->setSizes({400, 100});

    // Allow collapsing
    m_mainSplitter->setCollapsible(0, true);  // Left
    m_mainSplitter->setCollapsible(2, true);  // Right
    m_centerSplitter->setCollapsible(1, true);  // Bottom

    setCentralWidget(m_mainSplitter);
}
```

---

## Busy/Loading Indicators

### Overlay Loading Indicator

```cpp
class LoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setAutoFillBackground(true);

        QPalette pal = palette();
        QColor bg = pal.window().color();
        bg.setAlphaF(0.8);
        pal.setColor(QPalette::Window, bg);
        setPalette(pal);

        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setAlignment(Qt::AlignCenter);

        // Spinner (using KBusyIndicatorWidget or custom)
        m_spinner = new QProgressBar(this);
        m_spinner->setRange(0, 0);  // Indeterminate
        m_spinner->setMaximumWidth(200);
        layout->addWidget(m_spinner);

        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_label);

        hide();
    }

    void start(const QString &message = QString())
    {
        m_label->setText(message);
        show();
        raise();
    }

    void stop()
    {
        hide();
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        // Ensure we cover the parent
        if (parentWidget()) {
            setGeometry(parentWidget()->rect());
        }
    }

private:
    QProgressBar *m_spinner;
    QLabel *m_label;
};

// Usage
void MyWidget::startLoading()
{
    if (!m_loadingOverlay) {
        m_loadingOverlay = new LoadingOverlay(this);
    }
    m_loadingOverlay->start(i18n("Loading..."));
}

void MyWidget::stopLoading()
{
    if (m_loadingOverlay) {
        m_loadingOverlay->stop();
    }
}
```
