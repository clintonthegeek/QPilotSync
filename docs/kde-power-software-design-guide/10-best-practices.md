# Best Practices

This document covers code organization, CMake patterns, testing, and common pitfalls.

---

## Code Organization

### Directory Structure

```
myapp/
├── CMakeLists.txt
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── mainwindow.h
│   ├── core/                    # Core application logic
│   │   ├── document.cpp
│   │   ├── document.h
│   │   └── documentmanager.cpp
│   ├── ui/                      # UI components
│   │   ├── views/
│   │   ├── panels/
│   │   └── dialogs/
│   ├── settings/                # Settings pages
│   │   ├── settingsdialog.cpp
│   │   └── pages/
│   └── plugins/                 # Built-in plugins (optional)
├── data/
│   ├── myappui.rc              # XMLGUI definition
│   ├── myapp.desktop           # Desktop entry
│   └── icons/                  # Application icons
├── doc/                        # Documentation
├── po/                         # Translations
└── autotests/                  # Unit tests
```

### Header File Organization

```cpp
// mainwindow.h
#pragma once

// Standard library (alphabetical)
#include <memory>
#include <vector>

// Qt headers (alphabetical)
#include <QHash>
#include <QUrl>

// KDE headers (alphabetical)
#include <KXmlGuiWindow>

// Forward declarations (avoid includes in headers)
class QAction;
class QDockWidget;
class KActionCollection;

namespace MyApp {
class Document;
class DocumentManager;
}

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Public API grouped logically
    void openDocument(const QUrl &url);
    void closeDocument(MyApp::Document *doc);

public Q_SLOTS:
    void newDocument();

Q_SIGNALS:
    void documentOpened(MyApp::Document *doc);

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onDocumentModified();

private:
    void setupActions();
    void setupDockWidgets();

    // Use d-pointer for ABI stability in libraries
    // For applications, direct members are fine
    std::unique_ptr<MyApp::DocumentManager> m_documentManager;
    QHash<QUrl, MyApp::Document *> m_documents;
};
```

### D-Pointer Pattern (for Libraries)

```cpp
// myclass.h
#pragma once

#include <mylib_export.h>
#include <memory>

class MyClassPrivate;

class MYLIB_EXPORT MyClass
{
public:
    MyClass();
    ~MyClass();

    void doSomething();

private:
    std::unique_ptr<MyClassPrivate> d;
};

// myclass.cpp
#include "myclass.h"
#include "myclass_p.h"  // Private header

class MyClassPrivate
{
public:
    QString m_data;
    int m_count = 0;
};

MyClass::MyClass()
    : d(std::make_unique<MyClassPrivate>())
{
}

MyClass::~MyClass() = default;

void MyClass::doSomething()
{
    d->m_count++;
}
```

---

## CMake Patterns

### Main CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)

project(myapp VERSION 1.0.0)

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

# KDE CMake settings
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# Feature summary
include(FeatureSummary)

# ECM modules
include(ECMSetupVersion)
include(ECMQtDeclareLoggingCategory)
include(ECMAddAppIcon)
include(ECMInstallIcons)

# Setup version
ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX MYAPP
    VERSION_HEADER myapp_version.h
)

# Find Qt
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS
    Core
    Widgets
)

# Find KDE Frameworks
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    XmlGui
    Config
    ConfigWidgets
    KIO
    Crash
)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(doc)
add_subdirectory(autotests)

# Install translations
ki18n_install(po)

# Feature summary
feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
```

### Source CMakeLists.txt

```cmake
# src/CMakeLists.txt

# Declare logging categories
ecm_qt_declare_logging_category(myapp_SRCS
    HEADER myapp_debug.h
    IDENTIFIER MYAPP_LOG
    CATEGORY_NAME org.kde.myapp
    DEFAULT_SEVERITY Warning
    DESCRIPTION "MyApp"
    EXPORT MYAPP
)

# KConfig files
kconfig_add_kcfg_files(myapp_SRCS
    settings/myappsettings.kcfgc
)

# Source files
target_sources(myapp PRIVATE
    main.cpp
    mainwindow.cpp
    core/document.cpp
    core/documentmanager.cpp
    ui/views/mainview.cpp
    settings/settingsdialog.cpp
    ${myapp_SRCS}
)

# Create executable
add_executable(myapp)

target_link_libraries(myapp
    Qt6::Core
    Qt6::Widgets
    KF6::CoreAddons
    KF6::I18n
    KF6::XmlGui
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::KIOCore
    KF6::KIOWidgets
    KF6::Crash
)

# Install
install(TARGETS myapp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
```

### Data CMakeLists.txt

```cmake
# data/CMakeLists.txt

# Install XMLGUI file
install(FILES myappui.rc
        DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/myapp)

# Install desktop file
install(PROGRAMS org.kde.myapp.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})

# Install AppStream metadata
install(FILES org.kde.myapp.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})

# Install icons
ecm_install_icons(ICONS
    16-apps-myapp.png
    22-apps-myapp.png
    32-apps-myapp.png
    48-apps-myapp.png
    64-apps-myapp.png
    128-apps-myapp.png
    sc-apps-myapp.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)
```

---

## Application Entry Point

### main.cpp Pattern

```cpp
#include "mainwindow.h"
#include "myapp_version.h"

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Set application metadata
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("myapp"));

    KAboutData aboutData(
        QStringLiteral("myapp"),
        i18n("My Application"),
        QStringLiteral(MYAPP_VERSION_STRING),
        i18n("A KDE application"),
        KAboutLicense::GPL_V3,
        i18n("Copyright 2024 Author Name"));

    aboutData.addAuthor(
        i18n("Author Name"),
        i18n("Developer"),
        QStringLiteral("author@example.com"));

    aboutData.setOrganizationDomain(QByteArrayLiteral("kde.org"));
    aboutData.setDesktopFileName(QStringLiteral("org.kde.myapp"));

    KAboutData::setApplicationData(aboutData);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("myapp")));

    // Initialize crash handler
    KCrash::initialize();

    // Command line parsing
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    parser.addPositionalArgument(
        QStringLiteral("file"),
        i18n("File to open"),
        QStringLiteral("[file...]"));

    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Single instance handling
    KDBusService service(KDBusService::Unique);

    // Create main window
    MainWindow *mainWindow = new MainWindow();

    // Connect activation signal for single instance
    QObject::connect(&service, &KDBusService::activateRequested,
                     mainWindow, [mainWindow](const QStringList &args, const QString &) {
        mainWindow->raise();
        mainWindow->activateWindow();
        // Handle additional arguments if needed
    });

    // Open files from command line
    const QStringList args = parser.positionalArguments();
    for (const QString &file : args) {
        mainWindow->openDocument(QUrl::fromUserInput(file, QDir::currentPath()));
    }

    mainWindow->show();

    return app.exec();
}
```

---

## Memory Management

### Qt Parent-Child Ownership

```cpp
// Children are automatically deleted when parent is deleted
MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    // These are owned by this (parent)
    auto *toolbar = new QToolBar(this);
    auto *dockWidget = new QDockWidget(this);

    // Child of dock widget
    auto *panel = new Panel(dockWidget);
    dockWidget->setWidget(panel);
}
// No manual deletion needed - Qt handles it
```

### Smart Pointers for Non-QObject

```cpp
class MyClass
{
private:
    // Use smart pointers for non-QObject members
    std::unique_ptr<DataParser> m_parser;
    std::shared_ptr<Cache> m_cache;

    // Raw pointers for QObject children (Qt manages lifetime)
    QTimer *m_timer;  // Created with this as parent
};
```

### Signal-Slot Connection Lifetime

```cpp
// Connection automatically disconnected when sender or receiver is destroyed
connect(sender, &Sender::signal, receiver, &Receiver::slot);

// For lambdas capturing 'this', ensure proper lifetime
connect(timer, &QTimer::timeout, this, [this]() {
    // Safe: connection broken when 'this' is destroyed
    doSomething();
});

// Explicit connection management when needed
QMetaObject::Connection conn = connect(obj, &Obj::signal, this, &This::slot);
// Later...
disconnect(conn);
```

---

## Common Pitfalls

### 1. Blocking the Event Loop

```cpp
// BAD: Blocks UI
void MainWindow::loadData()
{
    QFile file("largefile.dat");
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();  // Blocks!
    processData(data);
}

// GOOD: Use async operations
void MainWindow::loadData()
{
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished,
            this, [this, watcher]() {
        processData(watcher->result());
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([]() {
        QFile file("largefile.dat");
        file.open(QIODevice::ReadOnly);
        return file.readAll();
    }));
}
```

### 2. Missing Signal-Slot Connections

```cpp
// BAD: Typo in signal/slot name not caught at compile time (old syntax)
connect(button, SIGNAL(clickd()), this, SLOT(onClicked()));  // Typo!

// GOOD: Compile-time checked (new syntax)
connect(button, &QPushButton::clicked, this, &MyClass::onClicked);
```

### 3. Incorrect i18n Usage

```cpp
// BAD: Not translatable
label->setText("Hello, " + name);

// BAD: Broken sentence for translators
label->setText(i18n("Hello, ") + name + i18n("!"));

// GOOD: Complete sentence with placeholder
label->setText(i18n("Hello, %1!", name));

// GOOD: Context for ambiguous strings
button->setText(i18nc("@action:button verb", "Open"));
menu->addAction(i18nc("@action:inmenu adjective", "Open"));
```

### 4. Resource Leaks with Jobs

```cpp
// BAD: Job never started or result not handled
KIO::CopyJob *job = KIO::copy(source, dest);
// Job is created but never started or connected!

// GOOD: Proper job handling
KIO::CopyJob *job = KIO::copy(source, dest);
connect(job, &KJob::result, this, [](KJob *job) {
    if (job->error()) {
        qWarning() << "Copy failed:" << job->errorString();
    }
});
// Job auto-starts and auto-deletes
```

### 5. State Modification During Iteration

```cpp
// BAD: Modifying container during iteration
for (auto *item : m_items) {
    if (item->shouldRemove()) {
        m_items.removeOne(item);  // Invalidates iterator!
        delete item;
    }
}

// GOOD: Collect items first, then modify
QList<Item *> toRemove;
for (auto *item : m_items) {
    if (item->shouldRemove()) {
        toRemove.append(item);
    }
}
for (auto *item : toRemove) {
    m_items.removeOne(item);
    delete item;
}

// Or use iterators properly
for (auto it = m_items.begin(); it != m_items.end(); ) {
    if ((*it)->shouldRemove()) {
        delete *it;
        it = m_items.erase(it);
    } else {
        ++it;
    }
}
```

### 6. Forgetting to Sync KConfig

```cpp
// BAD: Changes not saved
void saveSettings()
{
    KConfigGroup config(KSharedConfig::openConfig(), "Settings");
    config.writeEntry("Value", m_value);
    // Missing sync()!
}

// GOOD: Explicitly sync
void saveSettings()
{
    KConfigGroup config(KSharedConfig::openConfig(), "Settings");
    config.writeEntry("Value", m_value);
    config.sync();  // Write to disk
}
```

---

## Testing

### Unit Test Setup

```cmake
# autotests/CMakeLists.txt
include(ECMAddTests)

ecm_add_test(documenttest.cpp
    TEST_NAME documenttest
    LINK_LIBRARIES
        myapp_core  # Library with tested code
        Qt6::Test
)
```

### Test Class Pattern

```cpp
#include <QTest>
#include "document.h"

class DocumentTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Once before all tests
    }

    void init()
    {
        // Before each test
        m_doc = new Document();
    }

    void cleanup()
    {
        // After each test
        delete m_doc;
    }

    void testOpen()
    {
        QVERIFY(m_doc->open(QUrl::fromLocalFile("/tmp/test.txt")));
        QCOMPARE(m_doc->title(), QStringLiteral("test.txt"));
    }

    void testModified()
    {
        QSignalSpy spy(m_doc, &Document::modifiedChanged);

        m_doc->setModified(true);

        QCOMPARE(spy.count(), 1);
        QVERIFY(m_doc->isModified());
    }

private:
    Document *m_doc = nullptr;
};

QTEST_MAIN(DocumentTest)
#include "documenttest.moc"
```

---

## Debugging

### Logging Categories

```cpp
// myapp_debug.h (generated by ECM)
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(MYAPP_LOG)

// Usage
#include "myapp_debug.h"

void MyClass::doSomething()
{
    qCDebug(MYAPP_LOG) << "Doing something";
    qCWarning(MYAPP_LOG) << "Something might be wrong";
    qCCritical(MYAPP_LOG) << "Something is definitely wrong";
}
```

### Environment Variables

```bash
# Enable all debug output
export QT_LOGGING_RULES="*.debug=true"

# Enable specific category
export QT_LOGGING_RULES="org.kde.myapp.debug=true"

# Debug XMLGUI loading
export KDE_DEBUG=1

# Debug KIO
export KIO_WORKER_DEBUG=1
```

### XMLGUI Debugging

```bash
# Show XMLGUI parsing errors
export KDE_DEBUG=1
myapp

# Check for .rc file issues
xmllint --noout --dtdvalid /usr/share/kf6/xmlgui/kpartgui.dtd myappui.rc
```
