#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSet>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <cstring>

// pilot-link headers for PilotUser and SysInfo
#include <pi-dlp.h>

#include "qpilotsync_version.h"
#include "palm/kpilotdevicelink.h"
#include "palm/pilotrecord.h"
#include "mappers/memomapper.h"
#include "mappers/contactmapper.h"
#include "mappers/calendarmapper.h"
#include "mappers/todomapper.h"
#include "palm/categoryinfo.h"
#include "settings.h"

// Simple log widget for now
class LogWidget : public QTextEdit {
    Q_OBJECT

public:
    explicit LogWidget(QWidget *parent = nullptr) : QTextEdit(parent) {
        setReadOnly(true);
        document()->setMaximumBlockCount(1000); // Limit log size
    }

public slots:
    void logInfo(const QString &message) {
        append(QString("[INFO] %1").arg(message));
    }

    void logWarning(const QString &message) {
        append(QString("[WARNING] %1").arg(message));
    }

    void logError(const QString &message) {
        append(QString("[ERROR] %1").arg(message));
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent), m_deviceLink(nullptr) {
        setWindowTitle("QPilotSync - Palm Pilot Synchronization");
        setMinimumSize(900, 600);

        // Restore window geometry if saved
        QByteArray savedGeometry = Settings::instance().windowGeometry();
        if (!savedGeometry.isEmpty()) {
            restoreGeometry(savedGeometry);
        }

        // Create central widget with log viewer
        m_logWidget = new LogWidget(this);
        setCentralWidget(m_logWidget);

        // Create menu bar
        createMenus();

        // Status bar
        statusBar()->showMessage("Ready - No device connected");

        // Log initial message
        m_logWidget->logInfo("QPilotSync " + QString(QPILOTSYNC_VERSION_STRING) + " initialized");
        m_logWidget->logInfo("Ready to connect to Palm device");
        m_logWidget->logInfo("Go to Device → Connect to Device to start");
    }

    ~MainWindow() {
        // Save window geometry
        Settings::instance().setWindowGeometry(saveGeometry());
        Settings::instance().sync();

        if (m_deviceLink) {
            m_deviceLink->closeConnection();
            delete m_deviceLink;
        }
    }

private slots:
    void onConnectDevice() {
        // Create a dialog for connection settings
        QDialog dialog(this);
        dialog.setWindowTitle("Connect to Palm Device");
        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        // Device path - use saved settings as default
        QLabel *pathLabel = new QLabel("Device:", &dialog);
        QComboBox *pathCombo = new QComboBox(&dialog);
        pathCombo->setEditable(true);
        pathCombo->addItems({"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyS0", "usb:"});
        pathCombo->setCurrentText(Settings::instance().devicePath());

        // Baud rate - use saved settings as default
        QLabel *baudLabel = new QLabel("Baud Rate:", &dialog);
        QComboBox *baudCombo = new QComboBox(&dialog);
        baudCombo->addItems({"115200", "57600", "38400", "19200", "9600"});
        baudCombo->setCurrentText(Settings::instance().baudRate());

        // Flow control info
        QLabel *flowLabel = new QLabel("Flow Control: Automatic (hardware/software)", &dialog);
        flowLabel->setStyleSheet("color: gray; font-style: italic;");

        // Buttons
        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        layout->addWidget(pathLabel);
        layout->addWidget(pathCombo);
        layout->addWidget(baudLabel);
        layout->addWidget(baudCombo);
        layout->addWidget(flowLabel);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        // Get device path (baud rate is handled by pilot-link automatically)
        QString devicePath = pathCombo->currentText();
        QString baudRate = baudCombo->currentText();

        // Save settings for next time
        Settings::instance().setDevicePath(devicePath);
        Settings::instance().setBaudRate(baudRate);
        Settings::instance().sync();

        // Log the settings being used
        m_logWidget->logInfo(QString("Using device: %1 at %2 bps").arg(devicePath, baudRate));

        // Clean up existing connection
        if (m_deviceLink) {
            m_deviceLink->closeConnection();
            delete m_deviceLink;
            m_deviceLink = nullptr;
        }

        // Create device link
        m_deviceLink = new KPilotDeviceLink(devicePath, this);

        // Connect signals
        connect(m_deviceLink, &KPilotLink::logMessage,
                m_logWidget, &LogWidget::logInfo);
        connect(m_deviceLink, &KPilotLink::errorOccurred,
                m_logWidget, &LogWidget::logError);
        connect(m_deviceLink, &KPilotLink::statusChanged,
                this, &MainWindow::onDeviceStatusChanged);
        connect(m_deviceLink, &KPilotLink::deviceReady,
                this, &MainWindow::onDeviceReady);

        // Connect to async connection complete signal
        connect(m_deviceLink, &KPilotDeviceLink::connectionComplete,
                this, &MainWindow::onConnectionComplete);

        // Start async connection (returns immediately)
        statusBar()->showMessage("Waiting for Palm device - press HotSync button...");
        m_logWidget->logInfo(QString("Connecting to device: %1").arg(devicePath));
        m_logWidget->logInfo("Press the HotSync button on your Palm device now!");

        if (m_deviceLink->openConnection()) {
            // Enable cancel button while waiting for connection
            m_cancelConnectionAction->setEnabled(true);
        } else {
            m_logWidget->logError("Failed to start connection");
            statusBar()->showMessage("Connection failed");
        }
        // Connection will complete asynchronously - see onConnectionComplete()
    }

    void onConnectionComplete(bool success) {
        qDebug() << "[MainWindow] onConnectionComplete() success:" << success;

        // Disable cancel since connection attempt is over
        m_cancelConnectionAction->setEnabled(false);

        if (!success) {
            m_logWidget->logError("Connection failed");
            statusBar()->showMessage("Connection failed");
            return;
        }

        // Connection established - now read user info
        PilotUser user;
        if (m_deviceLink->readUserInfo(user)) {
            QString username = QString::fromUtf8(user.username);
            if (username.isEmpty()) {
                m_logWidget->logWarning("No username set on Palm device - first sync detected");

                // Prompt user to initialize their Palm
                bool ok;
                QString newUsername = QInputDialog::getText(this,
                    "First Sync - Set Palm User Name",
                    "This appears to be the first sync for this Palm device.\n"
                    "Please enter a user name to identify this device:",
                    QLineEdit::Normal,
                    QDir::home().dirName(),  // Default to system username
                    &ok);

                if (ok && !newUsername.isEmpty()) {
                    // Set the username on the Palm
                    strncpy(user.username, newUsername.toUtf8().constData(), sizeof(user.username) - 1);
                    user.username[sizeof(user.username) - 1] = '\0';

                    // Generate a user ID if not set
                    if (user.userID == 0) {
                        user.userID = QDateTime::currentSecsSinceEpoch() & 0xFFFFFFFF;
                    }

                    if (m_deviceLink->writeUserInfo(user)) {
                        m_logWidget->logInfo(QString("Palm initialized with username: %1").arg(newUsername));
                        username = newUsername;
                    } else {
                        m_logWidget->logError("Failed to write user info to Palm");
                        username = "<Initialization Failed>";
                    }
                } else {
                    username = "<No Name>";
                }
            }
            m_logWidget->logInfo(QString("Connected to: %1").arg(username));

            // Read system info
            SysInfo sysInfo;
            if (m_deviceLink->readSysInfo(sysInfo)) {
                // ROM version is a 32-bit value with BCD encoding
                // High byte = major, next byte = minor
                int major = (sysInfo.romVersion >> 12) & 0x0F;
                int minor = (sysInfo.romVersion >> 8) & 0x0F;
                int bugfix = (sysInfo.romVersion >> 4) & 0x0F;

                m_logWidget->logInfo(QString("Palm OS Version: %1.%2.%3")
                    .arg(major).arg(minor).arg(bugfix));
                m_logWidget->logInfo(QString("Product ID: %1")
                    .arg(QString::fromUtf8(sysInfo.prodID)));
            }

            // Enable device-dependent menu items
            updateMenuState(true);
            statusBar()->showMessage(QString("Connected: %1").arg(username));
        } else {
            m_logWidget->logError("Failed to read user info from device");
            statusBar()->showMessage("Connection error");
        }
    }

    void onDisconnectDevice() {
        if (m_deviceLink) {
            m_deviceLink->closeConnection();
            delete m_deviceLink;
            m_deviceLink = nullptr;
        }
        updateMenuState(false);
        m_cancelConnectionAction->setEnabled(false);
        m_logWidget->logInfo("Device disconnected");
        statusBar()->showMessage("Ready - No device connected");
    }

    void onCancelConnection() {
        qDebug() << "[MainWindow] onCancelConnection() called";
        if (m_deviceLink && m_deviceLink->isConnecting()) {
            m_deviceLink->cancelConnection();
            m_cancelConnectionAction->setEnabled(false);
            statusBar()->showMessage("Connection cancelled");
        }
    }

    void onDeviceStatusChanged(KPilotLink::LinkStatus status) {
        QString statusMsg;
        switch (status) {
            case KPilotLink::Init:
                statusMsg = "Initializing...";
                break;
            case KPilotLink::WaitingForDevice:
                statusMsg = "Waiting for device (press HotSync button)...";
                break;
            case KPilotLink::FoundDevice:
                statusMsg = "Device found!";
                break;
            case KPilotLink::DeviceOpen:
                statusMsg = "Device opened";
                break;
            case KPilotLink::AcceptedDevice:
                statusMsg = "Connection accepted";
                break;
            case KPilotLink::SyncDone:
                statusMsg = "Sync complete";
                break;
            case KPilotLink::PilotLinkError:
                statusMsg = "Error occurred";
                break;
        }
        statusBar()->showMessage(statusMsg);
    }

    void onDeviceReady(const QString &userName, const QString &deviceName) {
        m_logWidget->logInfo(QString("Device ready: %1 on %2").arg(userName, deviceName));
        statusBar()->showMessage(QString("Connected: %1").arg(userName));
    }

    void onListDatabases() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        m_logWidget->logInfo("Enumerating databases...");
        QStringList databases = m_deviceLink->listDatabases();

        if (databases.isEmpty()) {
            m_logWidget->logWarning("No databases found on device");
            return;
        }

        // Remove duplicates and sort
        QSet<QString> uniqueDbs(databases.begin(), databases.end());
        QStringList sortedDbs = uniqueDbs.values();
        sortedDbs.sort();

        // Create a scrollable dialog
        QDialog dialog(this);
        dialog.setWindowTitle("Palm Databases");
        dialog.resize(400, 500);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        QLabel *label = new QLabel(QString("Found %1 unique databases:").arg(sortedDbs.size()));
        layout->addWidget(label);

        QListWidget *listWidget = new QListWidget(&dialog);
        listWidget->addItems(sortedDbs);
        layout->addWidget(listWidget);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(buttons);

        dialog.exec();
    }

    void onSetUserInfo() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Read current user info
        PilotUser currentUser;
        if (!m_deviceLink->readUserInfo(currentUser)) {
            m_logWidget->logError("Failed to read current user info");
            return;
        }

        // Prompt for new username
        bool ok;
        QString newUsername = QInputDialog::getText(this,
            "Set Palm User Name",
            "Enter user name for this Palm device:",
            QLineEdit::Normal,
            QString::fromUtf8(currentUser.username),
            &ok);

        if (!ok || newUsername.isEmpty()) {
            return;
        }

        // Update user info
        PilotUser newUser = currentUser;
        strncpy(newUser.username, newUsername.toUtf8().constData(), sizeof(newUser.username) - 1);
        newUser.username[sizeof(newUser.username) - 1] = '\0';

        if (m_deviceLink->writeUserInfo(newUser)) {
            m_logWidget->logInfo(QString("User name set to: %1").arg(newUsername));
            QMessageBox::information(this, "Success",
                QString("Palm user name set to:\n%1").arg(newUsername));
        } else {
            m_logWidget->logError("Failed to set user name");
            QMessageBox::warning(this, "Error", "Failed to set user name on Palm device");
        }
    }

    void onExportMemos() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Ask user for export directory
        QString exportDir = QFileDialog::getExistingDirectory(this,
            "Select Memos Export Directory",
            Settings::instance().lastExportPath(),
            QFileDialog::ShowDirsOnly);

        if (exportDir.isEmpty()) {
            return;
        }

        // Save for next time
        Settings::instance().setLastExportPath(exportDir);

        m_logWidget->logInfo(QString("Exporting memos to: %1").arg(exportDir));

        // Open MemoDB
        int dbHandle = m_deviceLink->openDatabase("MemoDB");
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open MemoDB");
            QMessageBox::warning(this, "Error", "Failed to open MemoDB on Palm device");
            return;
        }

        // Read category names from AppInfo block
        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
            m_logWidget->logInfo("Loaded category names from database");
        }

        // Read all memo records
        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

        if (records.isEmpty()) {
            m_deviceLink->closeDatabase(dbHandle);
            m_logWidget->logWarning("No memo records found or device disconnected");
            QMessageBox::warning(this, "Warning",
                "No memo records found or device was disconnected during read");
            return;
        }

        m_logWidget->logInfo(QString("Found %1 memo records").arg(records.size()));

        int exportedCount = 0;
        int skippedCount = 0;

        for (PilotRecord *record : records) {
            // Skip deleted records
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Unpack memo
            MemoMapper::Memo memo = MemoMapper::unpackMemo(record);

            // Skip empty memos
            if (memo.text.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Convert to Markdown with category name
            QString categoryName = categories.categoryName(memo.category);
            QString markdown = MemoMapper::memoToMarkdown(memo, categoryName);

            // Generate filename
            QString filename = MemoMapper::generateFilename(memo);
            QString filepath = QDir(exportDir).filePath(filename);

            // Check for duplicate filenames and add number suffix if needed
            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 3) + QString("_%1.md").arg(suffix);
                suffix++;
            }

            // Write to file
            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << markdown;
                file.close();
                exportedCount++;
                m_logWidget->logInfo(QString("Exported: %1").arg(filename));
            } else {
                m_logWidget->logError(QString("Failed to write: %1").arg(filename));
            }

            delete record;
        }

        m_deviceLink->closeDatabase(dbHandle);

        QString summary = QString("Export complete!\n\nExported: %1 memos\nSkipped: %2 (deleted or empty)")
            .arg(exportedCount).arg(skippedCount);

        m_logWidget->logInfo(summary);
        QMessageBox::information(this, "Memo Export Complete", summary);
    }

    void onExportContacts() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Ask user for export directory
        QString exportDir = QFileDialog::getExistingDirectory(this,
            "Select Contacts Export Directory",
            Settings::instance().lastExportPath(),
            QFileDialog::ShowDirsOnly);

        if (exportDir.isEmpty()) {
            return;
        }

        // Save for next time
        Settings::instance().setLastExportPath(exportDir);

        m_logWidget->logInfo(QString("Exporting contacts to: %1").arg(exportDir));

        // Open AddressDB
        int dbHandle = m_deviceLink->openDatabase("AddressDB");
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open AddressDB");
            QMessageBox::warning(this, "Error", "Failed to open AddressDB on Palm device");
            return;
        }

        // Read category names from AppInfo block
        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
            m_logWidget->logInfo("Loaded category names from database");
        }

        // Read all contact records
        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

        if (records.isEmpty()) {
            m_deviceLink->closeDatabase(dbHandle);
            m_logWidget->logWarning("No contact records found or device disconnected");
            QMessageBox::warning(this, "Warning",
                "No contact records found or device was disconnected during read");
            return;
        }

        m_logWidget->logInfo(QString("Found %1 contact records").arg(records.size()));

        int exportedCount = 0;
        int skippedCount = 0;
        bool errorOccurred = false;

        for (PilotRecord *record : records) {
            // Skip deleted records
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Unpack contact
            ContactMapper::Contact contact = ContactMapper::unpackContact(record);

            // Skip contacts with no useful data
            if (contact.firstName.isEmpty() && contact.lastName.isEmpty() &&
                contact.company.isEmpty() && contact.phone1.isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Convert to vCard with category name
            QString categoryName = categories.categoryName(contact.category);
            QString vcard = ContactMapper::contactToVCard(contact, categoryName);

            // Generate filename
            QString filename = ContactMapper::generateFilename(contact);
            QString filepath = QDir(exportDir).filePath(filename);

            // Check for duplicate filenames and add number suffix if needed
            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.vcf").arg(suffix);
                suffix++;
            }

            // Write to file
            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << vcard;
                file.close();
                exportedCount++;
                m_logWidget->logInfo(QString("Exported: %1").arg(filename));
            } else {
                m_logWidget->logError(QString("Failed to write: %1").arg(filename));
            }

            delete record;
        }

        m_deviceLink->closeDatabase(dbHandle);

        QString summary = QString("Export complete!\n\nExported: %1 contacts\nSkipped: %2 (deleted or empty)")
            .arg(exportedCount).arg(skippedCount);

        m_logWidget->logInfo(summary);
        QMessageBox::information(this, "Contact Export Complete", summary);
    }

    void onExportCalendar() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Ask user for export directory
        QString exportDir = QFileDialog::getExistingDirectory(this,
            "Select Calendar Export Directory",
            Settings::instance().lastExportPath(),
            QFileDialog::ShowDirsOnly);

        if (exportDir.isEmpty()) {
            return;
        }

        // Save for next time
        Settings::instance().setLastExportPath(exportDir);

        m_logWidget->logInfo(QString("Exporting calendar events to: %1").arg(exportDir));

        // Open DatebookDB
        int dbHandle = m_deviceLink->openDatabase("DatebookDB");
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open DatebookDB");
            QMessageBox::warning(this, "Error", "Failed to open DatebookDB on Palm device");
            return;
        }

        // Read category names from AppInfo block
        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
            m_logWidget->logInfo("Loaded category names from database");
        }

        // Read all calendar records
        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

        if (records.isEmpty()) {
            m_deviceLink->closeDatabase(dbHandle);
            m_logWidget->logWarning("No calendar records found or device disconnected");
            QMessageBox::warning(this, "Warning",
                "No calendar records found or device was disconnected during read");
            return;
        }

        m_logWidget->logInfo(QString("Found %1 calendar records").arg(records.size()));

        int exportedCount = 0;
        int skippedCount = 0;

        for (PilotRecord *record : records) {
            // Skip deleted records
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Unpack event
            CalendarMapper::Event event = CalendarMapper::unpackEvent(record);

            // Skip events with no description
            if (event.description.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Convert to iCalendar with category name
            QString categoryName = categories.categoryName(event.category);
            QString ical = CalendarMapper::eventToICal(event, categoryName);

            // Generate filename
            QString filename = CalendarMapper::generateFilename(event);
            QString filepath = QDir(exportDir).filePath(filename);

            // Check for duplicate filenames and add number suffix if needed
            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix);
                suffix++;
            }

            // Write to file
            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << ical;
                file.close();
                exportedCount++;
                m_logWidget->logInfo(QString("Exported: %1").arg(filename));
            } else {
                m_logWidget->logError(QString("Failed to write: %1").arg(filename));
            }

            delete record;
        }

        m_deviceLink->closeDatabase(dbHandle);

        QString summary = QString("Export complete!\n\nExported: %1 events\nSkipped: %2 (deleted or empty)")
            .arg(exportedCount).arg(skippedCount);

        m_logWidget->logInfo(summary);
        QMessageBox::information(this, "Calendar Export Complete", summary);
    }

    void onExportTodos() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Ask user for export directory
        QString exportDir = QFileDialog::getExistingDirectory(this,
            "Select ToDo Export Directory",
            Settings::instance().lastExportPath(),
            QFileDialog::ShowDirsOnly);

        if (exportDir.isEmpty()) {
            return;
        }

        // Save for next time
        Settings::instance().setLastExportPath(exportDir);

        m_logWidget->logInfo(QString("Exporting todos to: %1").arg(exportDir));

        // Open ToDoDB
        int dbHandle = m_deviceLink->openDatabase("ToDoDB");
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open ToDoDB");
            QMessageBox::warning(this, "Error", "Failed to open ToDoDB on Palm device");
            return;
        }

        // Read category names from AppInfo block
        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
            m_logWidget->logInfo("Loaded category names from database");
        }

        // Read all todo records
        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

        if (records.isEmpty()) {
            m_deviceLink->closeDatabase(dbHandle);
            m_logWidget->logWarning("No todo records found or device disconnected");
            QMessageBox::warning(this, "Warning",
                "No todo records found or device was disconnected during read");
            return;
        }

        m_logWidget->logInfo(QString("Found %1 todo records").arg(records.size()));

        int exportedCount = 0;
        int skippedCount = 0;

        for (PilotRecord *record : records) {
            // Skip deleted records
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Unpack todo
            TodoMapper::Todo todo = TodoMapper::unpackTodo(record);

            // Skip todos with no description
            if (todo.description.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            // Convert to iCalendar VTODO with category name
            QString categoryName = categories.categoryName(todo.category);
            QString ical = TodoMapper::todoToICal(todo, categoryName);

            // Generate filename
            QString filename = TodoMapper::generateFilename(todo);
            QString filepath = QDir(exportDir).filePath(filename);

            // Check for duplicate filenames and add number suffix if needed
            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix);
                suffix++;
            }

            // Write to file
            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << ical;
                file.close();
                exportedCount++;
                m_logWidget->logInfo(QString("Exported: %1").arg(filename));
            } else {
                m_logWidget->logError(QString("Failed to write: %1").arg(filename));
            }

            delete record;
        }

        m_deviceLink->closeDatabase(dbHandle);

        QString summary = QString("Export complete!\n\nExported: %1 todos\nSkipped: %2 (deleted or empty)")
            .arg(exportedCount).arg(skippedCount);

        m_logWidget->logInfo(summary);
        QMessageBox::information(this, "ToDo Export Complete", summary);
    }

    void onSyncFromPalm() {
        QMessageBox::information(this, "Sync from Palm",
            "Sync functionality coming in Phase 2.\n\n"
            "This will export:\n"
            "- Calendar events to .ics files\n"
            "- Contacts to .vcf files\n"
            "- Memos to Markdown files");
    }

    void onQuit() {
        QApplication::quit();
    }

    void onAbout() {
        QMessageBox::about(this, "About QPilotSync",
            QString("<h3>QPilotSync %1</h3>"
                    "<p>Modern Palm Pilot synchronization for Linux</p>"
                    "<p>Built with:</p>"
                    "<ul>"
                    "<li>Qt %2</li>"
                    "<li>KDE Frameworks 6</li>"
                    "<li>pilot-link</li>"
                    "</ul>"
                    "<p>Bringing classic Palm Pilots into the modern era!</p>")
                .arg(QPILOTSYNC_VERSION_STRING)
                .arg(QT_VERSION_STR));
    }

    void onClearLog() {
        m_logWidget->clear();
        m_logWidget->logInfo("Log cleared");
    }

private:
    void createMenus() {
        // File menu
        QMenu *fileMenu = menuBar()->addMenu("&File");

        QAction *quitAction = fileMenu->addAction("&Quit");
        quitAction->setShortcut(QKeySequence::Quit);
        connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);

        // Device menu
        QMenu *deviceMenu = menuBar()->addMenu("&Device");

        QAction *connectAction = deviceMenu->addAction("&Connect to Device");
        connectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        connect(connectAction, &QAction::triggered, this, &MainWindow::onConnectDevice);

        m_disconnectAction = deviceMenu->addAction("&Disconnect");
        connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectDevice);
        m_disconnectAction->setEnabled(false);

        m_cancelConnectionAction = deviceMenu->addAction("Ca&ncel Connection");
        m_cancelConnectionAction->setShortcut(QKeySequence(Qt::Key_Escape));
        connect(m_cancelConnectionAction, &QAction::triggered, this, &MainWindow::onCancelConnection);
        m_cancelConnectionAction->setEnabled(false);

        deviceMenu->addSeparator();

        m_listDatabasesAction = deviceMenu->addAction("&List Databases");
        connect(m_listDatabasesAction, &QAction::triggered, this, &MainWindow::onListDatabases);
        m_listDatabasesAction->setEnabled(false);

        m_setUserInfoAction = deviceMenu->addAction("Set &User Info...");
        connect(m_setUserInfoAction, &QAction::triggered, this, &MainWindow::onSetUserInfo);
        m_setUserInfoAction->setEnabled(false);

        m_deviceInfoAction = deviceMenu->addAction("Device &Info");
        m_deviceInfoAction->setEnabled(false); // Will enable when device connected

        // Sync menu
        QMenu *syncMenu = menuBar()->addMenu("&Sync");

        m_exportMemosAction = syncMenu->addAction("Export &Memos...");
        connect(m_exportMemosAction, &QAction::triggered, this, &MainWindow::onExportMemos);
        m_exportMemosAction->setEnabled(false);

        m_exportContactsAction = syncMenu->addAction("Export &Contacts...");
        connect(m_exportContactsAction, &QAction::triggered, this, &MainWindow::onExportContacts);
        m_exportContactsAction->setEnabled(false);

        m_exportCalendarAction = syncMenu->addAction("Export C&alendar...");
        connect(m_exportCalendarAction, &QAction::triggered, this, &MainWindow::onExportCalendar);
        m_exportCalendarAction->setEnabled(false);

        m_exportTodosAction = syncMenu->addAction("Export &ToDos...");
        connect(m_exportTodosAction, &QAction::triggered, this, &MainWindow::onExportTodos);
        m_exportTodosAction->setEnabled(false);

        syncMenu->addSeparator();

        m_syncFromPalmAction = syncMenu->addAction("Sync &from Palm");
        connect(m_syncFromPalmAction, &QAction::triggered, this, &MainWindow::onSyncFromPalm);
        m_syncFromPalmAction->setEnabled(false);

        syncMenu->addSeparator();

        QAction *settingsAction = syncMenu->addAction("&Settings...");
        settingsAction->setEnabled(false); // Phase 5

        // View menu
        QMenu *viewMenu = menuBar()->addMenu("&View");

        QAction *clearLogAction = viewMenu->addAction("&Clear Log");
        connect(clearLogAction, &QAction::triggered, this, &MainWindow::onClearLog);

        // Help menu
        QMenu *helpMenu = menuBar()->addMenu("&Help");

        QAction *aboutAction = helpMenu->addAction("&About");
        connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

        QAction *aboutQtAction = helpMenu->addAction("About &Qt");
        connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
    }

    void updateMenuState(bool connected) {
        m_disconnectAction->setEnabled(connected);
        m_listDatabasesAction->setEnabled(connected);
        m_setUserInfoAction->setEnabled(connected);
        m_deviceInfoAction->setEnabled(connected);
        m_exportMemosAction->setEnabled(connected);
        m_exportContactsAction->setEnabled(connected);
        m_exportCalendarAction->setEnabled(connected);
        m_exportTodosAction->setEnabled(connected);
        m_syncFromPalmAction->setEnabled(connected);
    }

    LogWidget *m_logWidget;
    KPilotDeviceLink *m_deviceLink;

    // Menu actions that need to be enabled/disabled
    QAction *m_disconnectAction;
    QAction *m_cancelConnectionAction;
    QAction *m_listDatabasesAction;
    QAction *m_setUserInfoAction;
    QAction *m_deviceInfoAction;
    QAction *m_exportMemosAction;
    QAction *m_exportContactsAction;
    QAction *m_exportCalendarAction;
    QAction *m_exportTodosAction;
    QAction *m_syncFromPalmAction;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set application metadata
    app.setApplicationName("QPilotSync");
    app.setApplicationVersion(QPILOTSYNC_VERSION_STRING);
    app.setOrganizationName("QPilotSync");
    app.setOrganizationDomain("qpilotsync.org");

    MainWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
