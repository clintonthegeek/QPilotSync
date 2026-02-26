# Install Conduit Plugin Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Convert the ad-hoc file installation paths into a proper IConduit plugin with a view tab showing pending and installed files.

**Architecture:** The install conduit implements IConduit directly (not ISyncConduit, not IToolConduit) as a KDE plugin in `src/plugins/install/`. It runs inside the SyncEngine pipeline, scans `<profile>/install/` for `.prc`/`.pdb` files, installs them via `pi_file_install()`, and moves successes to `installed/`. The old hardcoded install paths in KF6MainWindow, DeviceSession, and DeviceWorker are removed.

**Tech Stack:** Qt6, KDE Frameworks 6 (KCoreAddons plugin system, KI18n), pilot-link (pi_file_install), CMake

**Design doc:** `docs/plans/2026-02-25-install-conduit-design.md`

---

### Task 1: Create install conduit plugin skeleton

Create the plugin directory, metadata JSON, CMakeLists.txt, and minimal class that compiles.

**Files:**
- Create: `src/plugins/install/install-conduit.json`
- Create: `src/plugins/install/CMakeLists.txt`
- Create: `src/plugins/install/installconduit.h`
- Create: `src/plugins/install/installconduit.cpp`
- Modify: `src/plugins/CMakeLists.txt` (add `add_subdirectory(install)`)

**Step 1: Create plugin metadata JSON**

Create `src/plugins/install/install-conduit.json`:

```json
{
    "KPlugin": {
        "Name": "Install Files",
        "Description": "Installs .prc and .pdb files to Palm devices",
        "Icon": "document-import",
        "Authors": [{ "Name": "Clinton Ignatov" }],
        "License": "GPL",
        "Version": "1.0.0",
        "Category": "Tool"
    },
    "X-WildPalms-ConduitId": "install",
    "X-WildPalms-ConduitType": "tool",
    "X-WildPalms-RequiresDevice": true,
    "X-WildPalms-DefaultEnabled": true,
    "X-WildPalms-SortOrder": 0
}
```

Note: `SortOrder: 0` ensures it runs before all data conduits (calendar=2, contacts=3, etc.).

**Step 2: Create CMakeLists.txt**

Create `src/plugins/install/CMakeLists.txt`:

```cmake
kcoreaddons_add_plugin(wildpalms_install
    SOURCES
        installconduit.cpp
        installconduit.h
    INSTALL_NAMESPACE "wildpalms/conduits"
)

target_link_libraries(wildpalms_install
    WildPalmsCore
    KF6::CoreAddons
    KF6::I18n
    Qt::Widgets
)
```

Note: `WildPalmsCore` already links pilot-link, so `pi_file_install` is available.

**Step 3: Create header**

Create `src/plugins/install/installconduit.h`:

```cpp
#ifndef INSTALLCONDUIT_PLUGIN_H
#define INSTALLCONDUIT_PLUGIN_H

#include <QObject>
#include <QIcon>
#include "core/iconduit.h"

namespace Sync {
class SyncContext;
struct SyncResult;
}

class InstallConduit : public QObject, public IConduit
{
    Q_OBJECT
    Q_INTERFACES(IConduit)

public:
    explicit InstallConduit(QObject *parent = nullptr);
    ~InstallConduit() override = default;

    // IConduit Identity
    QString conduitId() const override { return QStringLiteral("install"); }
    QString displayName() const override { return QStringLiteral("Install Files"); }
    QIcon icon() const override;
    QString description() const override;
    QString version() const override { return QStringLiteral("1.0.0"); }

    // IConduit Capabilities
    bool requiresDevice() const override { return true; }

    // IConduit Sync
    Sync::SyncResult sync(Sync::SyncContext *context) override;
    bool canSync(const Sync::SyncContext *context) const override;
    bool shouldRun(const Sync::SyncContext *context) const override;

    // IConduit UI
    bool hasView() const override { return true; }
    QWidget *createView(QWidget *parent) override;
    QString viewName() const override { return QStringLiteral("Install"); }
    QIcon viewIcon() const override;

    // Install-specific
    QStringList pendingFiles(const QString &installFolder) const;
    QStringList installedFiles(const QString &installFolder) const;

private:
    bool installFile(const QString &filePath, int socket);
    bool moveToInstalled(const QString &filePath, const QString &installFolder);
    void ensureFoldersExist(const QString &installFolder);
};

#endif // INSTALLCONDUIT_PLUGIN_H
```

**Step 4: Create minimal implementation**

Create `src/plugins/install/installconduit.cpp`:

```cpp
#include "installconduit.h"
#include "sync/synctypes.h"
#include "sync/conduit.h"

#include <KPluginFactory>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

extern "C" {
#include <pi-file.h>
#include <pi-dlp.h>
}

K_PLUGIN_FACTORY_WITH_JSON(InstallConduitFactory, "install-conduit.json",
                           registerPlugin<InstallConduit>();)

InstallConduit::InstallConduit(QObject *parent)
    : QObject(parent)
{
}

QIcon InstallConduit::icon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

QString InstallConduit::description() const
{
    return QStringLiteral("Installs .prc and .pdb files to Palm devices");
}

QIcon InstallConduit::viewIcon() const
{
    return QIcon::fromTheme(QStringLiteral("document-import"));
}

bool InstallConduit::canSync(const Sync::SyncContext *context) const
{
    return context && context->deviceLink;
}

bool InstallConduit::shouldRun(const Sync::SyncContext *context) const
{
    if (!context || context->syncFolderPath.isEmpty()) {
        return false;
    }
    QString installFolder = QDir(context->syncFolderPath).filePath(QStringLiteral("install"));
    return !pendingFiles(installFolder).isEmpty();
}

QStringList InstallConduit::pendingFiles(const QString &installFolder) const
{
    QStringList files;
    QDir dir(installFolder);
    if (!dir.exists()) {
        return files;
    }

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo &info : entries) {
        files << info.absoluteFilePath();
    }
    return files;
}

QStringList InstallConduit::installedFiles(const QString &installFolder) const
{
    QStringList files;
    QDir dir(QDir(installFolder).filePath(QStringLiteral("installed")));
    if (!dir.exists()) {
        return files;
    }

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable);
    for (const QFileInfo &info : entries) {
        files << info.absoluteFilePath();
    }
    return files;
}

Sync::SyncResult InstallConduit::sync(Sync::SyncContext *context)
{
    Sync::SyncResult result;
    result.startTime = QDateTime::currentDateTime();
    result.success = true;

    if (!context || !context->deviceLink || context->syncFolderPath.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("No device or sync folder");
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    QString installFolder = QDir(context->syncFolderPath).filePath(QStringLiteral("install"));
    ensureFoldersExist(installFolder);

    QStringList files = pendingFiles(installFolder);
    if (files.isEmpty()) {
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    int socket = context->deviceLink->socketDescriptor();

    for (const QString &filePath : files) {
        if (context->cancelled) {
            break;
        }

        QFileInfo fi(filePath);
        if (installFile(filePath, socket)) {
            moveToInstalled(filePath, installFolder);
            result.palmStats.created++;
            qDebug() << "[InstallConduit] Installed:" << fi.fileName();
        } else {
            result.palmStats.errors++;
            qDebug() << "[InstallConduit] Failed:" << fi.fileName();
        }
    }

    result.endTime = QDateTime::currentDateTime();
    if (result.palmStats.errors > 0 && result.palmStats.created == 0) {
        result.success = false;
        result.errorMessage = QStringLiteral("All file installations failed");
    }

    return result;
}

bool InstallConduit::installFile(const QString &filePath, int socket)
{
    pi_file_t *pf = pi_file_open(filePath.toLocal8Bit().constData());
    if (!pf) {
        return false;
    }

    int rc = pi_file_install(pf, socket, 0, nullptr);
    pi_file_close(pf);

    return rc >= 0;
}

bool InstallConduit::moveToInstalled(const QString &filePath, const QString &installFolder)
{
    QDir installDir(installFolder);
    QString installedPath = installDir.filePath(QStringLiteral("installed"));

    if (!QDir(installedPath).exists()) {
        installDir.mkpath(QStringLiteral("installed"));
    }

    QFileInfo info(filePath);
    QString destPath = QDir(installedPath).filePath(info.fileName());

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    return QFile::rename(filePath, destPath);
}

void InstallConduit::ensureFoldersExist(const QString &installFolder)
{
    QDir dir(installFolder);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QString installedPath = dir.filePath(QStringLiteral("installed"));
    if (!QDir(installedPath).exists()) {
        dir.mkpath(QStringLiteral("installed"));
    }
}

QWidget *InstallConduit::createView(QWidget *parent)
{
    // Placeholder — Task 2 adds the real view
    Q_UNUSED(parent)
    return nullptr;
}

#include "installconduit.moc"
```

**Step 5: Add to parent CMakeLists.txt**

In `src/plugins/CMakeLists.txt`, add `add_subdirectory(install)` alongside the other plugins.

**Step 6: Build and verify**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -C build -j$(($(nproc)-1))`

Expected: Build succeeds. Plugin `.so` appears in `build/bin/wildpalms/conduits/`.

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`

Expected: All existing tests pass.

**Step 7: Commit**

```bash
git add src/plugins/install/
git add src/plugins/CMakeLists.txt
git commit -m "feat(install): add install conduit plugin skeleton"
```

---

### Task 2: Create InstallView widget

Add the view tab that shows pending files and installed history.

**Files:**
- Create: `src/plugins/install/installview.h`
- Create: `src/plugins/install/installview.cpp`
- Modify: `src/plugins/install/installconduit.cpp` (wire up createView)
- Modify: `src/plugins/install/installconduit.h` (add installFolderPath setter)
- Modify: `src/plugins/install/CMakeLists.txt` (add new source files)

**Step 1: Create InstallView header**

Create `src/plugins/install/installview.h`:

```cpp
#ifndef INSTALLVIEW_H
#define INSTALLVIEW_H

#include <QWidget>

class QListWidget;
class QPushButton;

class InstallView : public QWidget
{
    Q_OBJECT

public:
    explicit InstallView(QWidget *parent = nullptr);

    void setInstallFolder(const QString &path);
    void refresh();

private slots:
    void onAddFiles();
    void onRemoveSelected();
    void onClearInstalled();

private:
    void populatePendingList();
    void populateInstalledList();

    QString m_installFolder;

    QListWidget *m_pendingList;
    QListWidget *m_installedList;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_clearInstalledButton;
    QPushButton *m_refreshButton;
};

#endif // INSTALLVIEW_H
```

**Step 2: Create InstallView implementation**

Create `src/plugins/install/installview.cpp`:

```cpp
#include "installview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QDropEvent>
#include <QMimeData>
#include <QDragEnterEvent>

#include <KLocalizedString>

InstallView::InstallView(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);

    // --- Pending files section ---
    auto *pendingGroup = new QGroupBox(i18n("Pending Installation"), this);
    auto *pendingLayout = new QVBoxLayout(pendingGroup);

    m_pendingList = new QListWidget(pendingGroup);
    m_pendingList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pendingLayout->addWidget(m_pendingList);

    auto *pendingButtons = new QHBoxLayout;
    m_addButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")),
                                  i18n("Add Files..."), pendingGroup);
    m_removeButton = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")),
                                     i18n("Remove"), pendingGroup);
    pendingButtons->addWidget(m_addButton);
    pendingButtons->addWidget(m_removeButton);
    pendingButtons->addStretch();
    pendingLayout->addLayout(pendingButtons);

    mainLayout->addWidget(pendingGroup);

    // --- Installed history section ---
    auto *installedGroup = new QGroupBox(i18n("Installed History"), this);
    auto *installedLayout = new QVBoxLayout(installedGroup);

    m_installedList = new QListWidget(installedGroup);
    m_installedList->setSelectionMode(QAbstractItemView::NoSelection);
    installedLayout->addWidget(m_installedList);

    auto *installedButtons = new QHBoxLayout;
    m_clearInstalledButton = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")),
                                             i18n("Clear History"), installedGroup);
    installedButtons->addWidget(m_clearInstalledButton);
    installedButtons->addStretch();
    installedLayout->addLayout(installedButtons);

    mainLayout->addWidget(installedGroup);

    // --- Refresh button ---
    auto *bottomBar = new QHBoxLayout;
    m_refreshButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                      i18n("Refresh"), this);
    bottomBar->addStretch();
    bottomBar->addWidget(m_refreshButton);
    mainLayout->addLayout(bottomBar);

    // Connections
    connect(m_addButton, &QPushButton::clicked, this, &InstallView::onAddFiles);
    connect(m_removeButton, &QPushButton::clicked, this, &InstallView::onRemoveSelected);
    connect(m_clearInstalledButton, &QPushButton::clicked, this, &InstallView::onClearInstalled);
    connect(m_refreshButton, &QPushButton::clicked, this, &InstallView::refresh);

    // Enable drag-and-drop
    setAcceptDrops(true);
}

void InstallView::setInstallFolder(const QString &path)
{
    m_installFolder = path;
    refresh();
}

void InstallView::refresh()
{
    populatePendingList();
    populateInstalledList();
}

void InstallView::populatePendingList()
{
    m_pendingList->clear();
    if (m_installFolder.isEmpty()) return;

    QDir dir(m_installFolder);
    if (!dir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                     QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &info : entries) {
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("application-x-palm-database")),
            info.fileName());
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(i18n("Size: %1 bytes", info.size()));
        m_pendingList->addItem(item);
    }
}

void InstallView::populateInstalledList()
{
    m_installedList->clear();
    if (m_installFolder.isEmpty()) return;

    QDir dir(QDir(m_installFolder).filePath(QStringLiteral("installed")));
    if (!dir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                     QDir::Time | QDir::Reversed);
    for (const QFileInfo &info : entries) {
        auto *item = new QListWidgetItem(
            QIcon::fromTheme(QStringLiteral("dialog-ok-apply")),
            info.fileName());
        item->setToolTip(i18n("Installed: %1", info.lastModified().toString(Qt::DefaultLocaleShortDate)));
        m_installedList->addItem(item);
    }
}

void InstallView::onAddFiles()
{
    if (m_installFolder.isEmpty()) return;

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        i18n("Select Palm Files to Install"),
        QString(),
        i18n("Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)"));

    if (files.isEmpty()) return;

    QDir installDir(m_installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        QFile::copy(filePath, destPath);
    }

    refresh();
}

void InstallView::onRemoveSelected()
{
    const QList<QListWidgetItem *> selected = m_pendingList->selectedItems();
    if (selected.isEmpty()) return;

    for (QListWidgetItem *item : selected) {
        QString filePath = item->data(Qt::UserRole).toString();
        QFile::remove(filePath);
    }

    refresh();
}

void InstallView::onClearInstalled()
{
    if (m_installFolder.isEmpty()) return;

    QDir installedDir(QDir(m_installFolder).filePath(QStringLiteral("installed")));
    if (!installedDir.exists()) return;

    const QStringList filters{
        QStringLiteral("*.prc"), QStringLiteral("*.pdb"),
        QStringLiteral("*.PRC"), QStringLiteral("*.PDB")
    };

    const QFileInfoList entries = installedDir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo &info : entries) {
        QFile::remove(info.absoluteFilePath());
    }

    refresh();
}
```

**Step 3: Update CMakeLists.txt to include new files**

In `src/plugins/install/CMakeLists.txt`, add `installview.cpp` and `installview.h` to the SOURCES list.

**Step 4: Wire up createView in installconduit.cpp**

Add `#include "installview.h"` to the includes in `installconduit.cpp`.

Replace the placeholder `createView`:

```cpp
QWidget *InstallConduit::createView(QWidget *parent)
{
    auto *view = new InstallView(parent);
    return view;
}
```

Note: The view's install folder path gets set by KF6MainWindow when it creates the view tab — that's the existing pattern from other conduits. We'll handle that wiring in Task 4.

**Step 5: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`

Expected: Build succeeds.

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`

Expected: All tests pass.

**Step 6: Commit**

```bash
git add src/plugins/install/installview.h src/plugins/install/installview.cpp
git add src/plugins/install/CMakeLists.txt
git add src/plugins/install/installconduit.cpp
git commit -m "feat(install): add InstallView widget with pending/installed file lists"
```

---

### Task 3: Remove old InstallConduit and DeviceWorker/DeviceSession install path

Remove the old non-plugin install conduit class and the threaded immediate-install code path.

**Files:**
- Delete: `src/sync/conduits/installconduit.h`
- Delete: `src/sync/conduits/installconduit.cpp`
- Modify: `src/CMakeLists.txt` (remove installconduit.cpp/h from WildPalmsCore)
- Modify: `src/palm/deviceworker.h` (remove doInstall slot, installFinished signal)
- Modify: `src/palm/deviceworker.cpp` (remove doInstall method)
- Modify: `src/palm/devicesession.h` (remove requestInstall, installFinished, onWorkerInstallFinished)
- Modify: `src/palm/devicesession.cpp` (remove requestInstall, onWorkerInstallFinished, installFinished connection in ensureWorkerThread)

**Step 1: Delete old install conduit files**

Delete `src/sync/conduits/installconduit.h` and `src/sync/conduits/installconduit.cpp`.

**Step 2: Remove from CMakeLists.txt**

In `src/CMakeLists.txt`, remove these lines from the WildPalmsCore SHARED library source list:

```cmake
    sync/conduits/installconduit.cpp
    sync/conduits/installconduit.h
```

**Step 3: Remove DeviceWorker::doInstall**

In `src/palm/deviceworker.h`:
- Remove the `void doInstall(const QStringList &filePaths);` slot declaration
- Remove the `void installFinished(bool success, int successCount, int failCount);` signal declaration

In `src/palm/deviceworker.cpp`:
- Remove the entire `DeviceWorker::doInstall()` method (lines ~80-149)
- Remove the `#include <pi-file.h>` include if no other method uses it (check — `doOpenConduit` uses `dlp_OpenConduit` from `pi-dlp.h`, not `pi-file.h`)

**Step 4: Remove DeviceSession install path**

In `src/palm/devicesession.h`:
- Remove `void requestInstall(const QStringList &filePaths);` from public methods
- Remove `void installFinished(bool success, int successCount, int failCount);` from signals
- Remove `void onWorkerInstallFinished(bool success, int successCount, int failCount);` from private slots
- Remove `QStringList m_pendingInstallFiles;` from private members

In `src/palm/devicesession.cpp`:
- Remove the entire `DeviceSession::requestInstall()` method
- Remove the entire `DeviceSession::onWorkerInstallFinished()` method
- In `ensureWorkerThread()`, remove the connect for `DeviceWorker::installFinished` → `DeviceSession::onWorkerInstallFinished`

**Step 5: Fix any remaining references**

Search for `requestInstall`, `doInstall`, `installFinished` (the DeviceWorker/DeviceSession versions), `InstallConduit` (the old `Sync::InstallConduit`), and `installconduit.h` across the codebase. Remove or update any remaining includes or references.

Key places to check:
- `src/kf6/kf6mainwindow.cpp` — `m_session->requestInstall(files)` in `onInstallFiles()` (will be handled in Task 4)
- `src/kf6/kf6mainwindow.h` — `Sync::InstallConduit *m_installConduit` member (will be handled in Task 4)

For now, the build may have errors in KF6MainWindow referencing removed APIs. That's expected — Task 4 cleans those up.

**Step 6: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`

Expected: Build may fail in `kf6mainwindow.cpp` due to references to removed APIs. That's OK — we fix those in Task 4. If it does fail, verify the errors are ONLY in kf6mainwindow.cpp. No other file should reference the removed APIs.

If you want a clean build at this point, you can temporarily comment out the problematic lines in `kf6mainwindow.cpp` (the `m_installConduit` usage and `requestInstall` call) to verify the rest compiles.

**Step 7: Commit**

```bash
git add -u  # stages deletions and modifications
git commit -m "refactor(install): remove old InstallConduit and DeviceWorker/DeviceSession install path"
```

---

### Task 4: Update KF6MainWindow — remove old install wiring, simplify onInstallFiles

Remove `m_installConduit`, `runInstallConduit()`, and the pre-sync install calls. Simplify `onInstallFiles()` to always queue to the install folder.

**Files:**
- Modify: `src/kf6/kf6mainwindow.h`
- Modify: `src/kf6/kf6mainwindow.cpp`

**Step 1: Remove m_installConduit member and includes**

In `src/kf6/kf6mainwindow.h`:
- Remove the forward declaration `namespace Sync { class InstallConduit; }` (if present)
- Remove `Sync::InstallConduit *m_installConduit;` member variable
- Remove `void runInstallConduit();` method declaration

In `src/kf6/kf6mainwindow.cpp`:
- Remove `#include "sync/conduits/installconduit.h"` (or `#include "../sync/conduits/installconduit.h"`)

**Step 2: Remove runInstallConduit() method**

In `src/kf6/kf6mainwindow.cpp`, delete the entire `KF6MainWindow::runInstallConduit()` method.

**Step 3: Remove runInstallConduit() calls from sync methods**

In `onHotSync()`, remove the `runInstallConduit();` call (line before `m_session->requestSync`).
In `onFullSync()`, remove the `runInstallConduit();` call.
In `onCopyPCToPalm()`, remove the `runInstallConduit();` call.
In `onRestore()`, remove the `runInstallConduit();` call.

The install conduit now runs as part of the SyncEngine pipeline — it doesn't need manual pre-sync invocation.

**Step 4: Remove m_installConduit initialization**

Search `kf6mainwindow.cpp` for any place `m_installConduit` is created, configured, or used:
- Remove `m_installConduit = new Sync::InstallConduit(this);` from initialization
- Remove `m_installConduit->setInstallFolder(...)` calls
- Remove any signal connections involving `m_installConduit`

**Step 5: Simplify onInstallFiles()**

Replace the existing `onInstallFiles()` method with a version that always queues to the install folder (no more immediate install path):

```cpp
void KF6MainWindow::onInstallFiles()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning(i18n("No profile loaded"));
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(
        this,
        i18n("Select Palm Files to Install"),
        QString(),
        i18n("Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)"));

    if (files.isEmpty()) {
        return;
    }

    QString installFolder = m_currentProfile->installFolderPath();
    QDir installDir(installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    int copiedCount = 0;
    int failCount = 0;

    for (const QString &filePath : files) {
        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }

        if (QFile::copy(filePath, destPath)) {
            m_logWidget->logInfo(i18n("Queued for install: %1", fileInfo.fileName()));
            copiedCount++;
        } else {
            m_logWidget->logError(i18n("Failed to copy %1 to install folder", fileInfo.fileName()));
            failCount++;
        }
    }

    if (failCount == 0) {
        QMessageBox::information(this, i18n("Files Queued"),
            i18n("%1 file(s) queued for installation.\n\n"
                 "They will be installed on the next sync.", copiedCount));
    } else {
        QMessageBox::warning(this, i18n("Files Queued"),
            i18n("%1 file(s) queued, %2 failed to copy.\nCheck the log for details.",
                 copiedCount, failCount));
    }

    // Refresh the install conduit view if visible
    // (The view will pick up new files on next refresh)
}
```

**Step 6: Remove onWorkerInstallFinished slot connection if present**

Search `kf6mainwindow.cpp` for any connect to `installFinished` from `m_session`. Remove it.

**Step 7: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`

Expected: Build succeeds with zero errors.

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`

Expected: All tests pass.

**Step 8: Commit**

```bash
git add src/kf6/kf6mainwindow.h src/kf6/kf6mainwindow.cpp
git commit -m "refactor(install): remove old install wiring from KF6MainWindow, simplify onInstallFiles"
```

---

### Task 5: Wire InstallView to profile's install folder

When the install conduit's view tab is created by KF6MainWindow (during conduit loading), set the install folder path so the view can show files. This follows the existing pattern where KF6MainWindow creates conduit views and adds them as tabs.

**Files:**
- Modify: `src/kf6/kf6mainwindow.cpp` (conduit view tab creation)
- Modify: `src/plugins/install/installconduit.h` (add setInstallFolder)
- Modify: `src/plugins/install/installconduit.cpp` (implement setInstallFolder, pass to view)

**Step 1: Check how conduit views are created**

Find where KF6MainWindow creates conduit view tabs. Search for `createView` and `hasView` in `kf6mainwindow.cpp`. The pattern is: iterate loaded conduits, call `hasView()`, call `createView(parent)`, add to tab widget.

**Step 2: Add install folder path to InstallConduit**

In `src/plugins/install/installconduit.h`, add:

```cpp
    void setInstallFolder(const QString &path);
    QString installFolder() const { return m_installFolder; }

private:
    // ... existing private members ...
    QString m_installFolder;
    InstallView *m_view = nullptr;
```

In `src/plugins/install/installconduit.cpp`:

```cpp
void InstallConduit::setInstallFolder(const QString &path)
{
    m_installFolder = path;
    if (m_view) {
        m_view->setInstallFolder(path);
    }
}
```

Update `createView`:

```cpp
QWidget *InstallConduit::createView(QWidget *parent)
{
    m_view = new InstallView(parent);
    if (!m_installFolder.isEmpty()) {
        m_view->setInstallFolder(m_installFolder);
    }
    return m_view;
}
```

**Step 3: Wire install folder in KF6MainWindow**

In the section of `kf6mainwindow.cpp` where conduit views are created (or where conduits are loaded/initialized after profile load), add logic: if the conduit's `conduitId() == "install"`, cast to get the `setInstallFolder` method and call it with `m_currentProfile->installFolderPath()`.

Since IConduit doesn't have `setInstallFolder`, use `QObject` property or dynamic cast. The cleanest approach: after creating the view, check if the conduit is the install conduit and call `setInstallFolder` on it.

Find the profile-load conduit initialization code. After loading conduits, add:

```cpp
// Set install folder for install conduit
IConduit *installCond = m_conduitManager->conduit(QStringLiteral("install"));
if (installCond) {
    auto *ic = qobject_cast<QObject*>(dynamic_cast<QObject*>(installCond));
    if (ic) {
        QMetaObject::invokeMethod(ic, "setInstallFolder",
                                  Q_ARG(QString, m_currentProfile->installFolderPath()));
    }
}
```

Or more directly, since we know the type:

```cpp
// Configure install conduit with profile's install folder
if (auto *installObj = dynamic_cast<QObject*>(m_conduitManager->conduit(QStringLiteral("install")))) {
    installObj->setProperty("installFolder", m_currentProfile->installFolderPath());
}
```

Actually, the cleanest way is to make `setInstallFolder` a Q_INVOKABLE or expose it via a property. Add to `installconduit.h`:

```cpp
    Q_PROPERTY(QString installFolder READ installFolder WRITE setInstallFolder)
```

Then in KF6MainWindow, after profile load:

```cpp
if (auto *installObj = dynamic_cast<QObject*>(m_conduitManager->conduit(QStringLiteral("install")))) {
    installObj->setProperty("installFolder", m_currentProfile->installFolderPath());
}
```

**Step 4: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`

Expected: Build succeeds.

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`

Expected: All tests pass.

**Step 5: Manual verification**

Launch the app, load a profile, verify the Install view tab appears alongside Calendar/Contacts/etc. Add some `.prc`/`.pdb` files to the profile's install folder and verify they appear in the pending list. Click "Add Files..." and verify new files appear.

**Step 6: Commit**

```bash
git add src/plugins/install/ src/kf6/kf6mainwindow.cpp
git commit -m "feat(install): wire InstallView to profile install folder"
```

---

### Task 6: Add drag-and-drop support to InstallView

**Files:**
- Modify: `src/plugins/install/installview.cpp`

**Step 1: Add drag-and-drop event handlers**

The constructor already has `setAcceptDrops(true)`. Add the event handler overrides to the header:

In `src/plugins/install/installview.h`, add to the class:

```cpp
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
```

**Step 2: Implement handlers**

In `src/plugins/install/installview.cpp`:

```cpp
void InstallView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        // Accept if any URLs end in .prc or .pdb
        for (const QUrl &url : event->mimeData()->urls()) {
            QString path = url.toLocalFile();
            if (path.endsWith(QLatin1String(".prc"), Qt::CaseInsensitive) ||
                path.endsWith(QLatin1String(".pdb"), Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void InstallView::dropEvent(QDropEvent *event)
{
    if (m_installFolder.isEmpty()) return;

    QDir installDir(m_installFolder);
    if (!installDir.exists()) {
        installDir.mkpath(QStringLiteral("."));
    }

    for (const QUrl &url : event->mimeData()->urls()) {
        QString filePath = url.toLocalFile();
        if (filePath.isEmpty()) continue;

        if (!filePath.endsWith(QLatin1String(".prc"), Qt::CaseInsensitive) &&
            !filePath.endsWith(QLatin1String(".pdb"), Qt::CaseInsensitive)) {
            continue;
        }

        QFileInfo fileInfo(filePath);
        QString destPath = installDir.filePath(fileInfo.fileName());

        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }
        QFile::copy(filePath, destPath);
    }

    refresh();
}
```

**Step 3: Build and verify**

Run: `make -C build -j$(($(nproc)-1))`

Expected: Build succeeds.

**Step 4: Commit**

```bash
git add src/plugins/install/installview.h src/plugins/install/installview.cpp
git commit -m "feat(install): add drag-and-drop support to InstallView"
```

---

### Task 7: Final verification and cleanup

**Step 1: Full build from clean**

Run: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -C build -j$(($(nproc)-1))`

Expected: Zero errors, zero warnings related to install code.

**Step 2: Run all tests**

Run: `ctest --test-dir build --output-on-failure -j$(($(nproc)-1))`

Expected: All tests pass.

**Step 3: Grep for orphaned references**

Search for any remaining references to the old install paths:

```bash
# Old InstallConduit class (Sync:: namespace version)
grep -rn "Sync::InstallConduit" src/

# Old runInstallConduit
grep -rn "runInstallConduit" src/

# Old requestInstall (DeviceSession)
grep -rn "requestInstall" src/

# Old doInstall (DeviceWorker)
grep -rn "doInstall" src/

# Old installconduit.h include
grep -rn "installconduit.h" src/
```

Expected: No hits in `src/` (docs/ may still reference them — that's fine).

**Step 4: Verify plugin is discovered**

If the app has a way to list discovered plugins (check log output), verify "Install Files" appears alongside Calendar, Contacts, Memo, Todo, Plucker.

**Step 5: Commit if any cleanup was needed**

```bash
git add -u
git commit -m "chore(install): final cleanup of orphaned references"
```
