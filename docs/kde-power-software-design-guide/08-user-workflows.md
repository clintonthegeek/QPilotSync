# User Workflows

This document covers common user workflow patterns: session management, document handling, navigation, and undo/redo.

---

## Session Management

### Session Concept

A session captures the application state for later restoration:
- Open documents/files
- Window geometry and layout
- UI state (visible panels, splitter positions)
- Application-specific data (cursor positions, scroll states)

### File-Based Sessions (Kate Pattern)

```cpp
// Based on kate/apps/lib/session/katesession.h

class Session : public QSharedData
{
public:
    using Ptr = QExplicitlySharedDataPointer<Session>;

    static Ptr create(const QString &file, const QString &name);

    QString name() const { return m_name; }
    QString file() const { return m_file; }

    KConfig *config() const { return m_config.data(); }
    unsigned int documents() const { return m_documents; }

    bool isAnonymous() const { return m_anonymous; }

private:
    Session(const QString &file, const QString &name);

    QString m_name;
    QString m_file;
    QScopedPointer<KConfig> m_config;
    unsigned int m_documents = 0;
    bool m_anonymous = false;
};

Session::Ptr Session::create(const QString &file, const QString &name)
{
    return Ptr(new Session(file, name));
}

Session::Session(const QString &file, const QString &name)
    : m_name(name)
    , m_file(file)
    , m_config(new KConfig(file, KConfig::SimpleConfig))
{
    // Count documents in session
    KConfigGroup general = m_config->group(QStringLiteral("Open Documents"));
    m_documents = general.readEntry("Count", 0);
}
```

### Session Manager

```cpp
// Based on kate/apps/lib/session/katesessionmanager.cpp

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(QObject *parent, const QString &sessionsDir);

    // Session access
    Session::Ptr activeSession() const { return m_activeSession; }
    QList<Session::Ptr> sessions() const { return m_sessions.values(); }
    Session::Ptr sessionForName(const QString &name) const;

    // Session operations
    bool activateSession(const QString &name);
    Session::Ptr createSession(const QString &name);
    bool deleteSession(const QString &name);
    bool renameSession(const QString &oldName, const QString &newName);

    // Persistence
    void saveActiveSession();

Q_SIGNALS:
    void sessionActivated(Session::Ptr session);
    void sessionCreated(const QString &name);
    void sessionDeleted(const QString &name);
    void sessionListChanged();

private:
    void updateSessionList();
    QString sessionFileForName(const QString &name) const;
    QStringList getAllSessionsInDir(const QString &dir) const;

    QString m_sessionsDir;
    Session::Ptr m_activeSession;
    QHash<QString, Session::Ptr> m_sessions;
    QFileSystemWatcher m_dirWatch;
    QTimer m_sessionSaveTimer;
};

SessionManager::SessionManager(QObject *parent, const QString &sessionsDir)
    : QObject(parent)
    , m_sessionsDir(sessionsDir)
{
    // Ensure directory exists
    QDir().mkpath(m_sessionsDir);

    // Watch for external changes
    m_dirWatch.addPath(m_sessionsDir);
    connect(&m_dirWatch, &QFileSystemWatcher::directoryChanged,
            this, &SessionManager::updateSessionList);

    // Auto-save timer (5 second debounce)
    m_sessionSaveTimer.setInterval(5000);
    m_sessionSaveTimer.setSingleShot(true);
    connect(&m_sessionSaveTimer, &QTimer::timeout,
            this, &SessionManager::saveActiveSession);

    // Initial session list
    updateSessionList();
}

void SessionManager::updateSessionList()
{
    const QStringList list = getAllSessionsInDir(m_sessionsDir);

    // Add new sessions
    for (const QString &name : list) {
        if (!m_sessions.contains(name)) {
            QString file = sessionFileForName(name);
            m_sessions.insert(name, Session::create(file, name));
        }
    }

    // Remove deleted sessions
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        if (!list.contains(it.key()) && it.value() != m_activeSession) {
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }

    Q_EMIT sessionListChanged();
}

bool SessionManager::activateSession(const QString &name)
{
    // Save current session first
    saveActiveSession();

    Session::Ptr session = sessionForName(name);
    if (!session) {
        return false;
    }

    m_activeSession = session;

    // Load session data
    KConfig *config = session->config();
    KConfigGroup general = config->group(QStringLiteral("Open Documents"));

    int count = general.readEntry("Count", 0);
    for (int i = 0; i < count; ++i) {
        KConfigGroup docGroup = general.group(QString::number(i));
        QString url = docGroup.readEntry("URL", QString());
        if (!url.isEmpty()) {
            // Open document at saved cursor position
            int line = docGroup.readEntry("Line", 0);
            int column = docGroup.readEntry("Column", 0);
            openDocument(QUrl::fromUserInput(url), line, column);
        }
    }

    Q_EMIT sessionActivated(session);
    return true;
}

void SessionManager::saveActiveSession()
{
    if (!m_activeSession) {
        return;
    }

    KConfig *config = m_activeSession->config();
    KConfigGroup general = config->group(QStringLiteral("Open Documents"));

    // Clear old document list
    general.deleteGroup();

    // Save open documents
    const auto documents = documentManager()->documents();
    general.writeEntry("Count", documents.count());

    for (int i = 0; i < documents.count(); ++i) {
        KConfigGroup docGroup = general.group(QString::number(i));
        Document *doc = documents.at(i);

        docGroup.writeEntry("URL", doc->url().toString());

        // Save view state if available
        if (View *view = doc->activeView()) {
            docGroup.writeEntry("Line", view->cursorPosition().line());
            docGroup.writeEntry("Column", view->cursorPosition().column());
        }
    }

    config->sync();
}
```

### Session Scheduling (Auto-Save)

```cpp
void SessionManager::scheduleSessionSave()
{
    // Debounce saves: wait for activity to settle
    m_sessionSaveTimer.start();
}

// Connect to document events
void SessionManager::setupAutoSave()
{
    DocumentManager *dm = documentManager();

    connect(dm, &DocumentManager::documentCreated,
            this, &SessionManager::scheduleSessionSave);
    connect(dm, &DocumentManager::documentClosed,
            this, &SessionManager::scheduleSessionSave);
    connect(dm, &DocumentManager::documentSaved,
            this, &SessionManager::scheduleSessionSave);
}
```

---

## Document Management

### Document Manager

```cpp
class DocumentManager : public QObject
{
    Q_OBJECT

public:
    explicit DocumentManager(QObject *parent = nullptr);

    // Document access
    QList<Document *> documents() const { return m_documents; }
    Document *findDocument(const QUrl &url) const;
    Document *activeDocument() const;

    // Document operations
    Document *createDocument();
    Document *openDocument(const QUrl &url);
    bool closeDocument(Document *doc);
    bool closeAllDocuments();

    // Batch operations
    void saveAllDocuments();
    QList<Document *> modifiedDocuments() const;

Q_SIGNALS:
    void documentCreated(Document *doc);
    void documentOpened(Document *doc);
    void documentClosed(Document *doc);
    void documentModifiedChanged(Document *doc, bool modified);

private:
    QList<Document *> m_documents;
};

Document *DocumentManager::openDocument(const QUrl &url)
{
    // Check if already open
    if (Document *existing = findDocument(url)) {
        Q_EMIT documentOpened(existing);
        return existing;
    }

    // Create new document
    Document *doc = createDocument();
    if (!doc->openUrl(url)) {
        delete doc;
        return nullptr;
    }

    connect(doc, &Document::modifiedChanged, this, [this, doc]() {
        Q_EMIT documentModifiedChanged(doc, doc->isModified());
    });

    Q_EMIT documentOpened(doc);
    return doc;
}

bool DocumentManager::closeDocument(Document *doc)
{
    // Check for unsaved changes
    if (doc->isModified()) {
        int result = KMessageBox::warningTwoActionsCancel(
            nullptr,
            i18n("The document '%1' has unsaved changes.\n"
                 "Do you want to save it before closing?", doc->title()),
            i18n("Close Document"),
            KStandardGuiItem::save(),
            KStandardGuiItem::discard());

        switch (result) {
        case KMessageBox::PrimaryAction:  // Save
            if (!doc->save()) {
                return false;  // Save failed
            }
            break;
        case KMessageBox::SecondaryAction:  // Discard
            break;
        case KMessageBox::Cancel:
            return false;
        }
    }

    m_documents.removeOne(doc);
    Q_EMIT documentClosed(doc);
    doc->deleteLater();
    return true;
}
```

### Recent Files

```cpp
#include <KRecentFilesAction>

void MainWindow::setupRecentFiles()
{
    m_recentFilesAction = KStandardAction::openRecent(
        this, &MainWindow::openRecentFile, actionCollection());

    m_recentFilesAction->setMaxItems(20);

    // Load recent files from config
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("RecentFiles"));
    m_recentFilesAction->loadEntries(config);
}

void MainWindow::openRecentFile(const QUrl &url)
{
    openDocument(url);
}

void MainWindow::addRecentFile(const QUrl &url)
{
    m_recentFilesAction->addUrl(url);

    // Save to config
    KConfigGroup config(KSharedConfig::openConfig(),
                        QStringLiteral("RecentFiles"));
    m_recentFilesAction->saveEntries(config);
}
```

---

## Navigation

### URL Navigation (Dolphin Pattern)

```cpp
class NavigationController : public QObject
{
    Q_OBJECT

public:
    explicit NavigationController(QObject *parent = nullptr);

    QUrl currentUrl() const { return m_currentUrl; }

    // Navigation
    void setUrl(const QUrl &url);
    void goBack();
    void goForward();
    void goUp();
    void goHome();

    // History access
    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex < m_history.size() - 1; }
    QList<QUrl> backHistory(int maxItems = 10) const;
    QList<QUrl> forwardHistory(int maxItems = 10) const;

Q_SIGNALS:
    void urlChanged(const QUrl &url);
    void historyChanged();

private:
    void addToHistory(const QUrl &url);

    QUrl m_currentUrl;
    QList<QUrl> m_history;
    int m_historyIndex = -1;
    bool m_navigatingHistory = false;
};

void NavigationController::setUrl(const QUrl &url)
{
    if (url == m_currentUrl) {
        return;
    }

    if (!m_navigatingHistory) {
        // Truncate forward history
        if (m_historyIndex < m_history.size() - 1) {
            m_history = m_history.mid(0, m_historyIndex + 1);
        }
        addToHistory(url);
    }

    m_currentUrl = url;
    Q_EMIT urlChanged(url);
    Q_EMIT historyChanged();
}

void NavigationController::goBack()
{
    if (!canGoBack()) {
        return;
    }

    m_navigatingHistory = true;
    m_historyIndex--;
    setUrl(m_history.at(m_historyIndex));
    m_navigatingHistory = false;
}

void NavigationController::goForward()
{
    if (!canGoForward()) {
        return;
    }

    m_navigatingHistory = true;
    m_historyIndex++;
    setUrl(m_history.at(m_historyIndex));
    m_navigatingHistory = false;
}

void NavigationController::goUp()
{
    QUrl parentUrl = m_currentUrl.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    if (parentUrl != m_currentUrl) {
        setUrl(parentUrl);
    }
}

void NavigationController::goHome()
{
    setUrl(QUrl::fromLocalFile(QDir::homePath()));
}
```

### Breadcrumb Navigation

```cpp
class BreadcrumbBar : public QWidget
{
    Q_OBJECT

public:
    explicit BreadcrumbBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_layout = new QHBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);
    }

    void setUrl(const QUrl &url)
    {
        // Clear existing buttons
        while (m_layout->count() > 0) {
            delete m_layout->takeAt(0)->widget();
        }

        // Build path components
        QUrl current = url;
        QList<QUrl> components;

        while (current.isValid() && !current.path().isEmpty()) {
            components.prepend(current);
            QUrl parent = current.adjusted(
                QUrl::RemoveFilename | QUrl::StripTrailingSlash);
            if (parent == current) {
                break;
            }
            current = parent;
        }

        // Create buttons
        for (const QUrl &component : components) {
            QString name = component.fileName();
            if (name.isEmpty()) {
                name = component.toDisplayString();
            }

            QToolButton *button = new QToolButton(this);
            button->setText(name);
            button->setAutoRaise(true);
            button->setProperty("url", component);

            connect(button, &QToolButton::clicked, this, [this, component]() {
                Q_EMIT urlActivated(component);
            });

            m_layout->addWidget(button);

            // Add separator (except after last)
            if (&component != &components.last()) {
                QLabel *sep = new QLabel(QStringLiteral("/"), this);
                m_layout->addWidget(sep);
            }
        }

        m_layout->addStretch();
    }

Q_SIGNALS:
    void urlActivated(const QUrl &url);

private:
    QHBoxLayout *m_layout;
};
```

---

## Undo/Redo

### Using KIO::FileUndoManager (File Operations)

```cpp
#include <KIO/FileUndoManager>

void MainWindow::setupUndo()
{
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();

    // Set UI interface for dialogs
    undoManager->setUiInterface(
        new KIO::FileUndoManager::UiInterface(this));

    // Create undo/redo actions
    m_undoAction = KStandardAction::undo(
        undoManager, &KIO::FileUndoManager::undo, actionCollection());
    m_redoAction = KStandardAction::redo(
        undoManager, &KIO::FileUndoManager::redo, actionCollection());

    // Update action state and text
    connect(undoManager, &KIO::FileUndoManager::undoAvailable,
            m_undoAction, &QAction::setEnabled);
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged,
            m_undoAction, &QAction::setText);

    connect(undoManager, &KIO::FileUndoManager::redoAvailable,
            m_redoAction, &QAction::setEnabled);
    connect(undoManager, &KIO::FileUndoManager::redoTextChanged,
            m_redoAction, &QAction::setText);
}
```

### Custom Undo Stack (QUndoStack)

```cpp
#include <QUndoStack>
#include <QUndoCommand>

// Custom undo command
class RenameCommand : public QUndoCommand
{
public:
    RenameCommand(const QString &oldName, const QString &newName,
                  ItemModel *model, QUndoCommand *parent = nullptr)
        : QUndoCommand(parent)
        , m_oldName(oldName)
        , m_newName(newName)
        , m_model(model)
    {
        setText(QObject::tr("Rename '%1' to '%2'").arg(oldName, newName));
    }

    void undo() override
    {
        m_model->rename(m_newName, m_oldName);
    }

    void redo() override
    {
        m_model->rename(m_oldName, m_newName);
    }

private:
    QString m_oldName;
    QString m_newName;
    ItemModel *m_model;
};

// Undo stack setup
class Editor : public QWidget
{
public:
    Editor(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        m_undoStack = new QUndoStack(this);

        // Create actions from undo stack
        m_undoAction = m_undoStack->createUndoAction(this);
        m_undoAction->setShortcut(QKeySequence::Undo);

        m_redoAction = m_undoStack->createRedoAction(this);
        m_redoAction->setShortcut(QKeySequence::Redo);
    }

    void renameItem(const QString &oldName, const QString &newName)
    {
        m_undoStack->push(new RenameCommand(oldName, newName, m_model));
    }

private:
    QUndoStack *m_undoStack;
    QAction *m_undoAction;
    QAction *m_redoAction;
    ItemModel *m_model;
};
```

### Macro Commands (Grouped Undo)

```cpp
void Editor::performBatchOperation()
{
    // Begin macro (all commands grouped as one undo)
    m_undoStack->beginMacro(i18n("Batch Operation"));

    for (Item *item : selectedItems()) {
        m_undoStack->push(new ModifyCommand(item));
    }

    m_undoStack->endMacro();
}
```

### Undo View (Visual History)

```cpp
void MainWindow::setupUndoView()
{
    QUndoView *undoView = new QUndoView(m_undoStack, this);
    undoView->setCleanIcon(QIcon::fromTheme(QStringLiteral("document-save")));

    QDockWidget *undoDock = new QDockWidget(i18n("Undo History"), this);
    undoDock->setWidget(undoView);
    addDockWidget(Qt::RightDockWidgetArea, undoDock);
}
```

---

## Drag and Drop

### Handling Drops

```cpp
class MyView : public QTreeView
{
    Q_OBJECT

public:
    MyView(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
        setAcceptDrops(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent *event) override
    {
        const QMimeData *mimeData = event->mimeData();

        if (mimeData->hasUrls()) {
            QList<QUrl> urls = mimeData->urls();

            // Determine drop target
            QModelIndex index = indexAt(event->position().toPoint());
            QUrl destUrl = model()->data(index, UrlRole).toUrl();

            // Use KIO for the operation
            Qt::DropAction action = event->dropAction();

            if (action == Qt::CopyAction) {
                KIO::copy(urls, destUrl);
            } else if (action == Qt::MoveAction) {
                KIO::move(urls, destUrl);
            } else if (action == Qt::LinkAction) {
                KIO::link(urls, destUrl);
            }

            event->acceptProposedAction();
        }
    }
};
```

### Using KIO::DropJob

```cpp
#include <KIO/DropJob>

void MyView::handleDrop(QDropEvent *event, const QUrl &destUrl)
{
    KIO::DropJob *job = KIO::drop(event, destUrl);
    KJobWidgets::setWindow(job, this);

    connect(job, &KJob::result, this, [](KJob *job) {
        if (job->error()) {
            qWarning() << "Drop failed:" << job->errorString();
        }
    });
}
```

---

## Multi-Window/Multi-Tab Workflows

### Window Management

```cpp
class Application : public QObject
{
    Q_OBJECT

public:
    MainWindow *createWindow()
    {
        MainWindow *window = new MainWindow();
        m_windows.append(window);

        connect(window, &QObject::destroyed, this, [this, window]() {
            m_windows.removeOne(window);

            // Quit when last window closes
            if (m_windows.isEmpty()) {
                qApp->quit();
            }
        });

        window->show();
        return window;
    }

    MainWindow *findWindowForUrl(const QUrl &url) const
    {
        for (MainWindow *window : m_windows) {
            if (window->hasDocument(url)) {
                return window;
            }
        }
        return nullptr;
    }

    QList<MainWindow *> windows() const { return m_windows; }

private:
    QList<MainWindow *> m_windows;
};
```

### Tab Management

```cpp
void MainWindow::newTab()
{
    int index = m_tabWidget->addTab(createView(), i18n("New Tab"));
    m_tabWidget->setCurrentIndex(index);
}

void MainWindow::duplicateTab()
{
    View *current = currentView();
    if (!current) {
        return;
    }

    View *newView = createView();
    newView->setUrl(current->url());

    int index = m_tabWidget->addTab(newView, current->title());
    m_tabWidget->setCurrentIndex(index);
}

void MainWindow::detachTab(int index)
{
    QWidget *widget = m_tabWidget->widget(index);
    if (!widget) {
        return;
    }

    View *view = qobject_cast<View *>(widget);
    QUrl url = view->url();

    // Remove from current window
    m_tabWidget->removeTab(index);

    // Create new window with this content
    MainWindow *newWindow = m_application->createWindow();
    newWindow->openUrl(url);

    delete widget;
}
```
