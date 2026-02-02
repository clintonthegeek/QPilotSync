# Plugin Architecture

KDE applications use a standardized plugin system based on KPluginFactory, enabling extensibility while maintaining API stability.

---

## KPluginFactory Overview

The plugin system consists of:

1. **Plugin Metadata** - JSON file describing the plugin
2. **Plugin Factory** - Macro-generated factory class
3. **Plugin Class** - Your actual plugin implementation
4. **Host Application** - Discovers and loads plugins

---

## Creating a Plugin

### 1. Plugin Metadata (JSON)

```json
{
    "KPlugin": {
        "Name": "My Plugin",
        "Name[de]": "Mein Plugin",
        "Description": "Does something useful",
        "Description[de]": "Macht etwas Nützliches",
        "Icon": "preferences-plugin",
        "Authors": [
            {
                "Name": "Author Name",
                "Email": "author@example.com"
            }
        ],
        "License": "GPL",
        "Version": "1.0",
        "Website": "https://example.com/myplugin"
    }
}
```

For KTextEditor plugins (Kate):

```json
{
    "KPlugin": {
        "Name": "File Browser",
        "Description": "Browse the filesystem in a sidebar",
        "Icon": "document-open-folder",
        "Authors": [
            {
                "Name": "KDE Developers"
            }
        ],
        "License": "LGPL",
        "Category": "Utilities"
    }
}
```

For KDevelop plugins (with dependencies):

```json
{
    "KPlugin": {
        "Name": "C++ Support",
        "Description": "C/C++ language support",
        "Icon": "text-x-c++src",
        "ServiceTypes": ["KDevelop/Plugin"]
    },
    "X-KDevelop-Version": "60",
    "X-KDevelop-Category": "Language Support",
    "X-KDevelop-Mode": "NoGUI",
    "X-KDevelop-LoadMode": "AlwaysOn",
    "X-KDevelop-Interfaces": ["ILanguageSupport"],
    "X-KDevelop-IRequired": ["IProject"],
    "X-KDevelop-IOptional": ["IDebugger"]
}
```

### 2. Plugin Implementation

```cpp
// myplugin.h
#pragma once

#include <KTextEditor/Plugin>
#include <KTextEditor/MainWindow>

class MyPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit MyPlugin(QObject *parent, const QVariantList &args);
    ~MyPlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    // Optional: config page
    int configPages() const override { return 1; }
    KTextEditor::ConfigPage *configPage(int number,
                                        QWidget *parent) override;
};

// mypluginview.h
#pragma once

#include <KXMLGUIClient>
#include <QObject>

class MyPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    explicit MyPluginView(MyPlugin *plugin,
                          KTextEditor::MainWindow *mainWindow);
    ~MyPluginView() override;

private Q_SLOTS:
    void onAction();

private:
    KTextEditor::MainWindow *m_mainWindow;
    QWidget *m_toolView;
};
```

```cpp
// myplugin.cpp
#include "myplugin.h"
#include "mypluginview.h"

#include <KPluginFactory>
#include <KLocalizedString>

// Register the plugin with its JSON metadata
K_PLUGIN_FACTORY_WITH_JSON(MyPluginFactory, "myplugin.json",
                           registerPlugin<MyPlugin>();)

MyPlugin::MyPlugin(QObject *parent, const QVariantList &args)
    : KTextEditor::Plugin(parent)
{
    Q_UNUSED(args)
}

MyPlugin::~MyPlugin() = default;

QObject *MyPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new MyPluginView(this, mainWindow);
}

KTextEditor::ConfigPage *MyPlugin::configPage(int number, QWidget *parent)
{
    if (number == 0) {
        return new MyPluginConfigPage(parent, this);
    }
    return nullptr;
}

#include "myplugin.moc"
```

```cpp
// mypluginview.cpp
#include "mypluginview.h"
#include "myplugin.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KXMLGUIFactory>

MyPluginView::MyPluginView(MyPlugin *plugin,
                           KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , KXMLGUIClient()
    , m_mainWindow(mainWindow)
{
    // Set component name for XMLGUI
    KXMLGUIClient::setComponentName(QStringLiteral("myplugin"),
                                    i18n("My Plugin"));
    setXMLFile(QStringLiteral("mypluginui.rc"));

    // Create action
    QAction *action = actionCollection()->addAction(
        QStringLiteral("my_plugin_action"));
    action->setText(i18n("Do Something"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
    actionCollection()->setDefaultShortcut(action,
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    connect(action, &QAction::triggered, this, &MyPluginView::onAction);

    // Create tool view
    m_toolView = m_mainWindow->createToolView(
        plugin,
        QStringLiteral("my_plugin_toolview"),
        KTextEditor::MainWindow::Left,
        QIcon::fromTheme(QStringLiteral("preferences-plugin")),
        i18n("My Plugin"));

    // Add content to tool view
    QWidget *content = new MyToolViewWidget(m_toolView);
    m_toolView->layout()->addWidget(content);

    // Register GUI with main window
    m_mainWindow->guiFactory()->addClient(this);
}

MyPluginView::~MyPluginView()
{
    // Unregister GUI
    m_mainWindow->guiFactory()->removeClient(this);

    // Tool view is automatically destroyed by main window
}

void MyPluginView::onAction()
{
    // Perform action using current view
    if (auto *view = m_mainWindow->activeView()) {
        // Do something with the view
    }
}
```

### 3. Plugin XMLGUI File

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE gui SYSTEM "kpartgui.dtd">
<gui name="myplugin" version="1" translationDomain="myplugin">

  <MenuBar>
    <Menu name="tools">
      <Action name="my_plugin_action"/>
    </Menu>
  </MenuBar>

</gui>
```

### 4. CMakeLists.txt

```cmake
add_library(myplugin MODULE)

target_sources(myplugin PRIVATE
    myplugin.cpp
    mypluginview.cpp
    myplugin.qrc
)

target_link_libraries(myplugin
    KF6::I18n
    KF6::TextEditor
    KF6::XmlGui
)

# Install plugin
install(TARGETS myplugin
        DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/ktexteditor)

# Install metadata (embedded in plugin via K_PLUGIN_FACTORY_WITH_JSON)
# No separate installation needed for KF6
```

---

## Plugin Discovery

### Finding Available Plugins

```cpp
#include <KPluginMetaData>

void PluginManager::discoverPlugins()
{
    // Find all plugins in the search path
    const QList<KPluginMetaData> plugins =
        KPluginMetaData::findPlugins(QStringLiteral("kf6/ktexteditor"));

    for (const KPluginMetaData &metaData : plugins) {
        qDebug() << "Found plugin:" << metaData.name();
        qDebug() << "  Description:" << metaData.description();
        qDebug() << "  Icon:" << metaData.iconName();
        qDebug() << "  File:" << metaData.fileName();

        // Store for later loading
        m_availablePlugins.append(metaData);
    }
}
```

### Loading a Plugin

```cpp
#include <KPluginFactory>

bool PluginManager::loadPlugin(const KPluginMetaData &metaData)
{
    // Load the factory
    auto factoryResult = KPluginFactory::loadFactory(metaData);

    if (!factoryResult.plugin) {
        qWarning() << "Failed to load plugin:"
                   << factoryResult.errorString;
        return false;
    }

    // Create the plugin instance
    auto *plugin = factoryResult.plugin->create<KTextEditor::Plugin>(
        this,  // parent
        QVariantList()  // construction arguments
    );

    if (!plugin) {
        qWarning() << "Failed to create plugin instance";
        return false;
    }

    m_loadedPlugins[metaData.pluginId()] = plugin;
    return true;
}
```

---

## Kate Plugin Manager Pattern

Kate's plugin manager demonstrates comprehensive plugin lifecycle management:

```cpp
// Based on kate/apps/lib/katepluginmanager.cpp

class PluginManager : public QObject
{
    Q_OBJECT

public:
    struct PluginInfo {
        KPluginMetaData metaData;
        KTextEditor::Plugin *plugin = nullptr;
        bool defaultLoad = false;
        int sortOrder = 0;  // Load order priority
        bool load = false;
    };

    explicit PluginManager(QObject *parent);

    void loadConfig();
    void saveConfig();

    bool loadPlugin(PluginInfo &info);
    void unloadPlugin(PluginInfo &info);

    const QList<PluginInfo> &pluginList() const { return m_pluginList; }

private:
    void setupPluginList();

    QList<PluginInfo> m_pluginList;
};

void PluginManager::setupPluginList()
{
    // Define default plugins with load order
    struct DefaultPlugin {
        const char *name;
        int sortOrder;
    };

    constexpr DefaultPlugin defaultPlugins[] = {
        {"katefiletreeplugin", -1000},   // Load first
        {"katesearchplugin", -900},
        {"kateprojectplugin", -800},
        {"tabswitcherplugin", -100},
        {"lspclientplugin", -100},
        {"katekonsoleplugin", -100},
    };

    // Discover all available plugins
    const auto plugins = KPluginMetaData::findPlugins(
        QStringLiteral("kf6/ktexteditor"));

    for (const auto &metaData : plugins) {
        PluginInfo info;
        info.metaData = metaData;

        // Check if it's a default plugin
        QString pluginId = metaData.pluginId();
        for (const auto &def : defaultPlugins) {
            if (pluginId == QLatin1String(def.name)) {
                info.defaultLoad = true;
                info.sortOrder = def.sortOrder;
                break;
            }
        }

        m_pluginList.append(info);
    }

    // Sort by load order
    std::sort(m_pluginList.begin(), m_pluginList.end(),
              [](const PluginInfo &a, const PluginInfo &b) {
        return a.sortOrder < b.sortOrder;
    });
}

void PluginManager::loadConfig()
{
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("Plugins"));

    for (PluginInfo &info : m_pluginList) {
        QString key = info.metaData.pluginId() + QLatin1String("Enabled");
        info.load = config.readEntry(key, info.defaultLoad);

        if (info.load) {
            loadPlugin(info);
        }
    }
}

void PluginManager::saveConfig()
{
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("Plugins"));

    for (const PluginInfo &info : m_pluginList) {
        QString key = info.metaData.pluginId() + QLatin1String("Enabled");
        config.writeEntry(key, info.load);
    }
}

bool PluginManager::loadPlugin(PluginInfo &info)
{
    if (info.plugin) {
        return true;  // Already loaded
    }

    auto result = KPluginFactory::loadFactory(info.metaData);
    if (!result.plugin) {
        qWarning() << "Failed to load" << info.metaData.name()
                   << ":" << result.errorString;
        return false;
    }

    info.plugin = result.plugin->create<KTextEditor::Plugin>(this);
    if (!info.plugin) {
        qWarning() << "Failed to create" << info.metaData.name();
        return false;
    }

    // Create views for all main windows
    for (MainWindow *window : m_mainWindows) {
        QObject *view = info.plugin->createView(window->wrapper());
        if (view) {
            m_pluginViews[window][info.plugin] = view;
        }
    }

    Q_EMIT pluginLoaded(info.plugin);
    return true;
}

void PluginManager::unloadPlugin(PluginInfo &info)
{
    if (!info.plugin) {
        return;
    }

    Q_EMIT pluginUnloading(info.plugin);

    // Destroy views
    for (MainWindow *window : m_mainWindows) {
        delete m_pluginViews[window].take(info.plugin);
    }

    delete info.plugin;
    info.plugin = nullptr;
}
```

---

## KDevelop Plugin Controller

KDevelop's plugin system adds dependency tracking:

```cpp
// Based on kdevelop/kdevplatform/shell/plugincontroller.cpp

class PluginController : public IPluginController
{
    Q_OBJECT

public:
    // Custom metadata keys
    static inline QString KEY_LoadMode() {
        return QStringLiteral("X-KDevelop-LoadMode");
    }
    static inline QString KEY_Category() {
        return QStringLiteral("X-KDevelop-Category");
    }
    static inline QString KEY_Interfaces() {
        return QStringLiteral("X-KDevelop-Interfaces");
    }
    static inline QString KEY_Required() {
        return QStringLiteral("X-KDevelop-IRequired");
    }
    static inline QString KEY_Optional() {
        return QStringLiteral("X-KDevelop-IOptional");
    }

    enum LoadMode {
        AlwaysOn,    // Load at startup, can't disable
        UserSelectable,  // User can enable/disable
        NoGUI        // Background plugin, no UI
    };

    bool canUnload(const KPluginMetaData &metaData) const
    {
        // Check if other plugins depend on this one
        QString interfaces = metaData.value(KEY_Interfaces());

        for (const auto &other : m_loadedPlugins) {
            QString required = other.metaData.value(KEY_Required());
            if (required.contains(interfaces)) {
                return false;  // Something depends on us
            }
        }
        return true;
    }

    QList<KPluginMetaData> dependenciesForPlugin(
        const KPluginMetaData &metaData) const
    {
        QList<KPluginMetaData> deps;
        QString required = metaData.value(KEY_Required());

        for (const QString &iface : required.split(QLatin1Char(','))) {
            // Find plugin providing this interface
            for (const auto &plugin : m_availablePlugins) {
                if (plugin.value(KEY_Interfaces()).contains(iface)) {
                    deps.append(plugin);
                    break;
                }
            }
        }
        return deps;
    }
};
```

---

## Plugin Configuration Pages

### Creating a Config Page

```cpp
// mypluginconfig.h
#pragma once

#include <KTextEditor/ConfigPage>

class MyPluginConfigPage : public KTextEditor::ConfigPage
{
    Q_OBJECT

public:
    explicit MyPluginConfigPage(QWidget *parent, MyPlugin *plugin);

    QString name() const override;
    QString fullName() const override;
    QIcon icon() const override;

    void apply() override;
    void reset() override;
    void defaults() override;

private:
    MyPlugin *m_plugin;
    QCheckBox *m_enableFeature;
    QSpinBox *m_valueSpinBox;
};
```

```cpp
// mypluginconfig.cpp
#include "mypluginconfig.h"

MyPluginConfigPage::MyPluginConfigPage(QWidget *parent, MyPlugin *plugin)
    : KTextEditor::ConfigPage(parent)
    , m_plugin(plugin)
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    m_enableFeature = new QCheckBox(i18n("Enable feature"), this);
    layout->addWidget(m_enableFeature);

    QHBoxLayout *valueLayout = new QHBoxLayout();
    valueLayout->addWidget(new QLabel(i18n("Value:"), this));
    m_valueSpinBox = new QSpinBox(this);
    m_valueSpinBox->setRange(1, 100);
    valueLayout->addWidget(m_valueSpinBox);
    valueLayout->addStretch();
    layout->addLayout(valueLayout);

    layout->addStretch();

    // Connect change signals
    connect(m_enableFeature, &QCheckBox::toggled,
            this, &MyPluginConfigPage::changed);
    connect(m_valueSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MyPluginConfigPage::changed);

    reset();  // Load current values
}

QString MyPluginConfigPage::name() const
{
    return i18n("My Plugin");
}

QString MyPluginConfigPage::fullName() const
{
    return i18n("My Plugin Settings");
}

QIcon MyPluginConfigPage::icon() const
{
    return QIcon::fromTheme(QStringLiteral("preferences-plugin"));
}

void MyPluginConfigPage::apply()
{
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("MyPlugin"));
    config.writeEntry("EnableFeature", m_enableFeature->isChecked());
    config.writeEntry("Value", m_valueSpinBox->value());
    config.sync();

    // Notify plugin of changes
    m_plugin->settingsChanged();
}

void MyPluginConfigPage::reset()
{
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("MyPlugin"));
    m_enableFeature->setChecked(config.readEntry("EnableFeature", true));
    m_valueSpinBox->setValue(config.readEntry("Value", 10));
}

void MyPluginConfigPage::defaults()
{
    m_enableFeature->setChecked(true);
    m_valueSpinBox->setValue(10);
}
```

---

## Best Practices

### 1. Plugin Isolation

```cpp
// Don't access host internals directly
// Use provided interfaces

// Bad:
auto *mainWindow = qobject_cast<KateMainWindow *>(parent());
mainWindow->m_privateData;  // Don't do this!

// Good:
auto *mainWindow = m_mainWindow;  // Use provided interface
mainWindow->activeView();  // Use public API
```

### 2. Clean Unloading

```cpp
MyPluginView::~MyPluginView()
{
    // Always unregister from GUI factory
    m_mainWindow->guiFactory()->removeClient(this);

    // Disconnect all signals
    disconnect();

    // Clean up tool views (usually automatic)
    // delete m_toolView;  // Usually managed by main window
}
```

### 3. Lazy Loading

```cpp
// Don't do heavy initialization in constructor
MyPluginView::MyPluginView(...)
{
    // Light setup only
    setupActions();

    // Defer heavy work
    QTimer::singleShot(0, this, &MyPluginView::initialize);
}

void MyPluginView::initialize()
{
    // Heavy initialization here
    m_heavyWidget = new HeavyWidget(m_toolView);
}
```

### 4. Configuration Watching

```cpp
void MyPlugin::settingsChanged()
{
    // Re-read config
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("MyPlugin"));

    bool enabled = config.readEntry("EnableFeature", true);

    // Update all views
    for (MyPluginView *view : m_views) {
        view->setFeatureEnabled(enabled);
    }
}
```

### 5. Proper Versioning

```json
{
    "KPlugin": {
        "Version": "1.2.3"
    },
    "X-MyApp-MinVersion": "5.0"
}
```

```cpp
bool PluginManager::isCompatible(const KPluginMetaData &metaData) const
{
    QString minVersion = metaData.value(
        QStringLiteral("X-MyApp-MinVersion"));

    if (!minVersion.isEmpty()) {
        QVersionNumber required = QVersionNumber::fromString(minVersion);
        QVersionNumber current = QVersionNumber::fromString(
            QStringLiteral(APP_VERSION));

        if (current < required) {
            qWarning() << metaData.name() << "requires version"
                       << minVersion;
            return false;
        }
    }
    return true;
}
```
