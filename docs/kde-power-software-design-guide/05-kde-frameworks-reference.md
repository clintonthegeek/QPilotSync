# KDE Frameworks Reference

This document covers the essential KDE Frameworks used by power applications.

---

## Framework Dependencies by Application Type

### Minimal Application

```cmake
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons     # Core utilities
    I18n           # Internationalization
    WidgetsAddons  # Extra widgets
    XmlGui         # Menu/toolbar system
    Config         # Configuration
)
```

### Standard Desktop Application

```cmake
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    WidgetsAddons
    XmlGui
    Config
    ConfigWidgets  # Config dialogs
    KIO            # File operations
    Crash          # Crash handling
    DBusAddons     # D-Bus utilities
    IconThemes     # Icon loading
    WindowSystem   # Window management
)
```

### Full-Featured Application (Dolphin-style)

```cmake
find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    KCMUtils       # KCM plugin support
    NewStuff       # Get Hot New Stuff
    CoreAddons
    I18n
    DBusAddons
    Bookmarks      # Bookmark management
    Config
    KIO
    Parts          # KParts framework
    Solid          # Hardware detection
    IconThemes
    Completion     # Text completion
    TextWidgets    # Text editing widgets
    Notifications  # System notifications
    Crash
    WindowSystem
    WidgetsAddons
    Codecs         # Text encoding
    GuiAddons      # GUI utilities
    ColorScheme    # Color scheme access
)
```

---

## KConfig - Configuration Management

KConfig provides persistent application settings with automatic file management.

### Basic Usage

```cpp
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

// Open application config (creates if doesn't exist)
KSharedConfig::Ptr config = KSharedConfig::openConfig();

// Access a group
KConfigGroup generalGroup = config->group(QStringLiteral("General"));

// Read values with defaults
QString name = generalGroup.readEntry("Username", QString());
int width = generalGroup.readEntry("Width", 800);
bool enabled = generalGroup.readEntry("FeatureEnabled", true);
QColor color = generalGroup.readEntry("HighlightColor", QColor(255, 255, 0));
QStringList recentFiles = generalGroup.readEntry("RecentFiles", QStringList());

// Write values
generalGroup.writeEntry("Username", m_username);
generalGroup.writeEntry("Width", width());
generalGroup.writeEntry("FeatureEnabled", m_featureEnabled);
generalGroup.writeEntry("HighlightColor", m_highlightColor);
generalGroup.writeEntry("RecentFiles", m_recentFiles);

// Save to disk
config->sync();
```

### Nested Groups

```cpp
KConfigGroup windowGroup = config->group(QStringLiteral("MainWindow"));
KConfigGroup geometryGroup = windowGroup.group(QStringLiteral("Geometry"));

geometryGroup.writeEntry("Width", 1024);
geometryGroup.writeEntry("Height", 768);
```

### State vs Config

Use state config for non-essential data (window positions, UI state):

```cpp
// State config (separate file, less important)
KSharedConfig::Ptr stateConfig = KSharedConfig::openStateConfig();
KConfigGroup stateGroup = stateConfig->group(QStringLiteral("UIState"));
stateGroup.writeEntry("SidebarVisible", m_sidebar->isVisible());
```

### Configuration Cascading

KConfig supports reading from multiple sources:

```cpp
// Normal: user config only
KConfig userConfig(QStringLiteral("myapprc"));

// Full cascade: system defaults + user overrides
KConfig cascadeConfig(QStringLiteral("myapprc"),
                      KConfig::FullConfig);

// Include kdeglobals (color scheme, fonts, etc.)
KConfig withGlobals(QStringLiteral("myapprc"),
                    KConfig::IncludeGlobals);
```

### Watching for Changes

```cpp
#include <KConfigWatcher>

// Watch for external changes
KConfigWatcher::Ptr watcher = KConfigWatcher::create(config);
connect(watcher.data(), &KConfigWatcher::configChanged,
        this, [this](const KConfigGroup &group, const QByteArrayList &names) {
    if (group.name() == QLatin1String("General")) {
        reloadSettings();
    }
});
```

---

## KIO - File Operations

KIO provides transparent access to local and remote files.

### Opening Files

```cpp
#include <KIO/OpenUrlJob>
#include <KIO/JobUiDelegateFactory>

void MainWindow::openFile(const QUrl &url)
{
    auto *job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(
        KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}
```

### File Operations

```cpp
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/MkdirJob>

// Copy files
KIO::CopyJob *copyJob = KIO::copy(sourceUrls, destUrl);
connect(copyJob, &KJob::result, this, &MainWindow::onCopyFinished);

// Move files
KIO::CopyJob *moveJob = KIO::move(sourceUrls, destUrl);

// Delete files (to trash)
KIO::DeleteJob *trashJob = KIO::trash(urls);

// Delete permanently
KIO::DeleteJob *deleteJob = KIO::del(urls);

// Create directory
KIO::MkdirJob *mkdirJob = KIO::mkdir(dirUrl);
```

### File Undo Manager

```cpp
#include <KIO/FileUndoManager>

// Setup undo manager
KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
undoManager->setUiInterface(new KIO::FileUndoManager::UiInterface());

// Connect to actions
connect(undoManager, &KIO::FileUndoManager::undoAvailable,
        m_undoAction, &QAction::setEnabled);
connect(undoManager, &KIO::FileUndoManager::undoTextChanged,
        this, [this](const QString &text) {
    m_undoAction->setText(text);
});

// Perform undo
undoManager->undo();
```

### Job Progress

```cpp
#include <KIO/Job>
#include <KJobWidgets>

void MainWindow::copyFiles(const QList<QUrl> &sources, const QUrl &dest)
{
    KIO::CopyJob *job = KIO::copy(sources, dest);

    // Show progress dialog
    KJobWidgets::setWindow(job, this);

    // Or track progress manually
    connect(job, &KIO::CopyJob::processedAmount,
            this, [](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Files) {
            qDebug() << "Processed" << amount << "files";
        }
    });

    connect(job, &KIO::CopyJob::percent,
            this, [this](KJob *, unsigned long percent) {
        m_progressBar->setValue(percent);
    });

    connect(job, &KJob::result, this, [this](KJob *job) {
        if (job->error()) {
            KMessageBox::error(this, job->errorString());
        } else {
            statusBar()->showMessage(i18n("Copy complete"));
        }
    });
}
```

### Listing Directories

```cpp
#include <KIO/ListJob>

void MainWindow::listDirectory(const QUrl &url)
{
    KIO::ListJob *job = KIO::listDir(url);

    connect(job, &KIO::ListJob::entries,
            this, [this](KIO::Job *, const KIO::UDSEntryList &entries) {
        for (const KIO::UDSEntry &entry : entries) {
            QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            bool isDir = entry.isDir();
            qint64 size = entry.numberValue(KIO::UDSEntry::UDS_SIZE);

            addItem(name, isDir, size);
        }
    });

    connect(job, &KJob::result, this, [](KJob *job) {
        if (job->error()) {
            qWarning() << "List failed:" << job->errorString();
        }
    });
}
```

---

## KParts - Document Embedding

KParts enables embedding document viewers/editors in applications.

### Loading a Part

```cpp
#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>

KParts::ReadOnlyPart *MainWindow::loadPart(const QString &partId)
{
    // Find the part plugin
    const KPluginMetaData metaData(QStringLiteral("kf6/parts/") + partId);

    if (!metaData.isValid()) {
        qWarning() << "Part not found:" << partId;
        return nullptr;
    }

    // Load the factory
    auto factoryResult = KPluginFactory::loadFactory(metaData);
    if (!factoryResult.plugin) {
        qWarning() << "Failed to load part:" << factoryResult.errorString;
        return nullptr;
    }

    // Create the part
    return factoryResult.plugin->create<KParts::ReadOnlyPart>(this);
}
```

### Using the Part

```cpp
void MainWindow::openDocument(const QUrl &url)
{
    KParts::ReadOnlyPart *part = loadPart(QStringLiteral("okularpart"));

    if (!part) {
        return;
    }

    // Add part widget to UI
    setCentralWidget(part->widget());

    // Merge part's GUI
    createGUI(part);

    // Connect signals
    connect(part, &KParts::ReadOnlyPart::completed, this, [this]() {
        setWindowTitle(part->url().fileName());
    });

    // Open the document
    part->openUrl(url);
}
```

### Navigation Extension

```cpp
#include <KParts/NavigationExtension>

// Get navigation extension from part
KParts::NavigationExtension *navExt = part->navigationExtension();

if (navExt) {
    connect(navExt, &KParts::NavigationExtension::openUrlRequest,
            this, &MainWindow::openUrl);

    // Control navigation
    m_backAction->setEnabled(navExt->isURLDropHandlingEnabled());
}
```

---

## KI18n - Internationalization

### Basic Translation

```cpp
#include <KLocalizedString>

// Simple translation
QString text = i18n("Hello World");

// With arguments
QString greeting = i18n("Hello, %1!", userName);
QString fileInfo = i18n("%1 files, %2 bytes", fileCount, totalSize);

// Plural forms
QString message = i18np("1 file selected", "%1 files selected", count);

// With context for translators
QString label = i18nc("@label:textbox", "Name:");
QString button = i18nc("@action:button", "Apply");
QString tooltip = i18nc("@info:tooltip", "Click to save changes");
```

### Context Markers

| Marker | Use Case |
|--------|----------|
| `@action:button` | Button labels |
| `@action:inmenu` | Menu items |
| `@action:intoolbar` | Toolbar buttons |
| `@info:tooltip` | Tooltips |
| `@info:whatsthis` | What's This help |
| `@info:status` | Status bar messages |
| `@label` | Form labels |
| `@title:window` | Window titles |
| `@title:menu` | Menu titles |

### Setup in main.cpp

```cpp
#include <KLocalizedString>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set translation domain
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("myapp"));

    // ... rest of initialization
}
```

### CMake Setup

```cmake
# Find KI18n
find_package(KF6I18n REQUIRED)

# Install translations
ki18n_install(po)
```

---

## KCrash - Crash Handling

```cpp
#include <KCrash>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Initialize crash handler
    KCrash::initialize();

    // Optional: set flags
    KCrash::setFlags(KCrash::AutoRestart | KCrash::SaferDialog);

    // ... rest of application
}
```

---

## KNotifications - System Notifications

```cpp
#include <KNotification>

void MainWindow::notifyDownloadComplete(const QString &filename)
{
    KNotification *notification = new KNotification(
        QStringLiteral("downloadComplete"), this);

    notification->setTitle(i18n("Download Complete"));
    notification->setText(i18n("File %1 has been downloaded.", filename));
    notification->setIconName(QStringLiteral("download"));

    // Add actions
    notification->setDefaultAction(i18n("Open"));
    connect(notification, &KNotification::defaultActivated,
            this, [this, filename]() {
        openFile(filename);
    });

    notification->sendEvent();
}
```

### Notification Configuration (myapp.notifyrc)

```ini
[Global]
IconName=myapp
Comment=My Application
Name=My Application

[Event/downloadComplete]
Name=Download Complete
Comment=A download has completed
Action=Popup
```

---

## KWindowSystem - Window Management

```cpp
#include <KWindowSystem>

// Raise and activate window
void MainWindow::activateWindow()
{
    KWindowSystem::activateWindow(windowHandle());
}

// Check if on current desktop
bool MainWindow::isOnCurrentDesktop()
{
    return KWindowSystem::windowInfo(winId(),
        NET::WMDesktop).isOnCurrentDesktop();
}
```

### Platform-Specific Code

```cpp
#if __has_include(<KX11Extras>)
#include <KX11Extras>
#define HAVE_X11 1
#endif

#if __has_include(<KWaylandExtras>)
#include <KWaylandExtras>
#define HAVE_WAYLAND 1
#endif

void MainWindow::setStartupId(const QString &startupId)
{
#if HAVE_X11
    if (KWindowSystem::isPlatformX11()) {
        KStartupInfo::setNewStartupId(windowHandle(), startupId.toUtf8());
    }
#endif
#if HAVE_WAYLAND
    if (KWindowSystem::isPlatformWayland()) {
        KWaylandExtras::self()->setActivationToken(startupId);
    }
#endif
}
```

---

## KJobWidgets - Job UI Integration

```cpp
#include <KJobWidgets>
#include <KIO/Job>

void MainWindow::startLongOperation()
{
    KJob *job = createMyJob();

    // Associate with window for progress dialogs
    KJobWidgets::setWindow(job, this);

    // Start the job
    job->start();
}
```

---

## KCompletion - Text Completion

```cpp
#include <KCompletion>
#include <KLineEdit>

void MainWindow::setupCompletion()
{
    KLineEdit *lineEdit = new KLineEdit(this);

    // Create completion object
    KCompletion *completion = new KCompletion();
    completion->setItems(m_history);  // Previous entries
    completion->setCompletionMode(KCompletion::CompletionPopupAuto);

    // Connect to line edit
    lineEdit->setCompletionObject(completion);
    lineEdit->setAutoDeleteCompletionObject(true);

    // Save new entries
    connect(lineEdit, &KLineEdit::returnPressed,
            this, [completion, lineEdit]() {
        completion->addItem(lineEdit->text());
    });
}
```

---

## Common Include Patterns

### Typical Main Window Includes

```cpp
// KDE Frameworks
#include <KAboutData>
#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>
#include <KSharedConfig>
#include <KStandardAction>
#include <KXmlGuiWindow>

// For file operations
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/FileUndoManager>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>

// For configuration
#include <KConfigDialog>
#include <KConfigSkeleton>

// Optional
#include <KCrash>
#include <KDBusService>
#include <KNotification>
#include <KWindowSystem>
```

### CMakeLists.txt Target Linking

```cmake
target_link_libraries(myapp
    PUBLIC
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::KIOCore
        KF6::KIOWidgets
        KF6::KIOFileWidgets
        KF6::WidgetsAddons
        KF6::WindowSystem
        KF6::Crash
    PRIVATE
        KF6::DBusAddons
        KF6::Notifications
)
```
