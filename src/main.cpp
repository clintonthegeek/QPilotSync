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
            // Properly end sync session before closing
            if (m_deviceLink->isConnected()) {
                m_logWidget->logInfo("Ending sync session...");
                m_deviceLink->endSync();
            }
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

    void onDeviceInfo() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Gather device information
        PilotUser user;
        SysInfo sysInfo;

        QString userName = "<Unknown>";
        QString userId = "<Unknown>";
        QString lastSyncDate = "<Never>";
        QString palmOS = "<Unknown>";
        QString productId = "<Unknown>";

        if (m_deviceLink->readUserInfo(user)) {
            userName = QString::fromUtf8(user.username);
            if (userName.isEmpty()) userName = "<Not Set>";
            userId = QString::number(user.userID);

            // Convert last sync time
            if (user.lastSyncDate > 0) {
                // Palm epoch is Jan 1, 1904. Unix epoch is Jan 1, 1970.
                // Difference is 2082844800 seconds
                qint64 unixTime = user.lastSyncDate - 2082844800;
                QDateTime lastSync = QDateTime::fromSecsSinceEpoch(unixTime);
                lastSyncDate = lastSync.toString("yyyy-MM-dd hh:mm:ss");
            }
        }

        if (m_deviceLink->readSysInfo(sysInfo)) {
            int major = (sysInfo.romVersion >> 12) & 0x0F;
            int minor = (sysInfo.romVersion >> 8) & 0x0F;
            int bugfix = (sysInfo.romVersion >> 4) & 0x0F;
            palmOS = QString("%1.%2.%3").arg(major).arg(minor).arg(bugfix);
            productId = QString::fromUtf8(sysInfo.prodID);
        }

        // Count records in main databases
        int memoCount = countDatabaseRecords("MemoDB");
        int contactCount = countDatabaseRecords("AddressDB");
        int calendarCount = countDatabaseRecords("DatebookDB");
        int todoCount = countDatabaseRecords("ToDoDB");

        // Build info dialog
        QDialog dialog(this);
        dialog.setWindowTitle("Palm Device Information");
        dialog.setMinimumWidth(350);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        QString infoHtml = QString(
            "<h3>Device Information</h3>"
            "<table>"
            "<tr><td><b>User Name:</b></td><td>%1</td></tr>"
            "<tr><td><b>User ID:</b></td><td>%2</td></tr>"
            "<tr><td><b>Last Sync:</b></td><td>%3</td></tr>"
            "<tr><td><b>Palm OS:</b></td><td>%4</td></tr>"
            "<tr><td><b>Product ID:</b></td><td>%5</td></tr>"
            "</table>"
            "<h3>Database Record Counts</h3>"
            "<table>"
            "<tr><td><b>Memos:</b></td><td>%6</td></tr>"
            "<tr><td><b>Contacts:</b></td><td>%7</td></tr>"
            "<tr><td><b>Calendar Events:</b></td><td>%8</td></tr>"
            "<tr><td><b>ToDos:</b></td><td>%9</td></tr>"
            "</table>"
        ).arg(userName, userId, lastSyncDate, palmOS, productId)
         .arg(memoCount).arg(contactCount).arg(calendarCount).arg(todoCount);

        QLabel *infoLabel = new QLabel(infoHtml, &dialog);
        infoLabel->setTextFormat(Qt::RichText);
        layout->addWidget(infoLabel);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        layout->addWidget(buttons);

        dialog.exec();
    }

    int countDatabaseRecords(const QString &dbName) {
        int dbHandle = m_deviceLink->openDatabase(dbName);
        if (dbHandle < 0) {
            return -1;  // Could not open
        }

        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
        int count = 0;
        for (PilotRecord *record : records) {
            if (!record->isDeleted()) {
                count++;
            }
            delete record;
        }
        m_deviceLink->closeDatabase(dbHandle);
        return count;
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

    void onExportAll() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        // Ask user for base export directory
        QString baseDir = QFileDialog::getExistingDirectory(this,
            "Select Export Directory (subdirectories will be created)",
            Settings::instance().lastExportPath(),
            QFileDialog::ShowDirsOnly);

        if (baseDir.isEmpty()) {
            return;
        }

        Settings::instance().setLastExportPath(baseDir);

        m_logWidget->logInfo("=== Starting Full Export ===");
        m_logWidget->logInfo(QString("Base directory: %1").arg(baseDir));

        // Create subdirectories
        QDir base(baseDir);
        QString memosDir = base.filePath("Memos");
        QString contactsDir = base.filePath("Contacts");
        QString calendarDir = base.filePath("Calendar");
        QString todosDir = base.filePath("ToDos");

        QDir().mkpath(memosDir);
        QDir().mkpath(contactsDir);
        QDir().mkpath(calendarDir);
        QDir().mkpath(todosDir);

        int totalExported = 0;
        int totalSkipped = 0;
        QStringList results;

        // Export Memos
        m_logWidget->logInfo("--- Exporting Memos ---");
        int memoCount = 0, memoSkipped = 0;
        exportMemosToDir(memosDir, memoCount, memoSkipped);
        totalExported += memoCount;
        totalSkipped += memoSkipped;
        results << QString("Memos: %1 exported, %2 skipped").arg(memoCount).arg(memoSkipped);

        // Export Contacts
        m_logWidget->logInfo("--- Exporting Contacts ---");
        int contactCount = 0, contactSkipped = 0;
        exportContactsToDir(contactsDir, contactCount, contactSkipped);
        totalExported += contactCount;
        totalSkipped += contactSkipped;
        results << QString("Contacts: %1 exported, %2 skipped").arg(contactCount).arg(contactSkipped);

        // Export Calendar
        m_logWidget->logInfo("--- Exporting Calendar ---");
        int calendarCount = 0, calendarSkipped = 0;
        exportCalendarToDir(calendarDir, calendarCount, calendarSkipped);
        totalExported += calendarCount;
        totalSkipped += calendarSkipped;
        results << QString("Calendar: %1 exported, %2 skipped").arg(calendarCount).arg(calendarSkipped);

        // Export ToDos
        m_logWidget->logInfo("--- Exporting ToDos ---");
        int todoCount = 0, todoSkipped = 0;
        exportTodosToDir(todosDir, todoCount, todoSkipped);
        totalExported += todoCount;
        totalSkipped += todoSkipped;
        results << QString("ToDos: %1 exported, %2 skipped").arg(todoCount).arg(todoSkipped);

        m_logWidget->logInfo("=== Export Complete ===");

        QString summary = QString("Full Export Complete!\n\n%1\n\nTotal: %2 items exported, %3 skipped\n\nExported to:\n%4")
            .arg(results.join("\n"))
            .arg(totalExported)
            .arg(totalSkipped)
            .arg(baseDir);

        QMessageBox::information(this, "Export Complete", summary);
    }

    // Helper functions for batch export
    void exportMemosToDir(const QString &exportDir, int &exportedCount, int &skippedCount) {
        exportedCount = 0;
        skippedCount = 0;

        int dbHandle = m_deviceLink->openDatabase("MemoDB");
        if (dbHandle < 0) {
            m_logWidget->logWarning("Could not open MemoDB");
            return;
        }

        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
        }

        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
        for (PilotRecord *record : records) {
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            MemoMapper::Memo memo = MemoMapper::unpackMemo(record);
            if (memo.text.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            QString categoryName = categories.categoryName(memo.category);
            QString markdown = MemoMapper::memoToMarkdown(memo, categoryName);
            QString filename = MemoMapper::generateFilename(memo);
            QString filepath = QDir(exportDir).filePath(filename);

            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 3) + QString("_%1.md").arg(suffix++);
            }

            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << markdown;
                file.close();
                exportedCount++;
            }
            delete record;
        }
        m_deviceLink->closeDatabase(dbHandle);
    }

    void exportContactsToDir(const QString &exportDir, int &exportedCount, int &skippedCount) {
        exportedCount = 0;
        skippedCount = 0;

        int dbHandle = m_deviceLink->openDatabase("AddressDB");
        if (dbHandle < 0) {
            m_logWidget->logWarning("Could not open AddressDB");
            return;
        }

        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
        }

        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
        for (PilotRecord *record : records) {
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            ContactMapper::Contact contact = ContactMapper::unpackContact(record);
            if (contact.firstName.isEmpty() && contact.lastName.isEmpty() &&
                contact.company.isEmpty() && contact.phone1.isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            QString categoryName = categories.categoryName(contact.category);
            QString vcard = ContactMapper::contactToVCard(contact, categoryName);
            QString filename = ContactMapper::generateFilename(contact);
            QString filepath = QDir(exportDir).filePath(filename);

            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.vcf").arg(suffix++);
            }

            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << vcard;
                file.close();
                exportedCount++;
            }
            delete record;
        }
        m_deviceLink->closeDatabase(dbHandle);
    }

    void exportCalendarToDir(const QString &exportDir, int &exportedCount, int &skippedCount) {
        exportedCount = 0;
        skippedCount = 0;

        int dbHandle = m_deviceLink->openDatabase("DatebookDB");
        if (dbHandle < 0) {
            m_logWidget->logWarning("Could not open DatebookDB");
            return;
        }

        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
        }

        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
        for (PilotRecord *record : records) {
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            CalendarMapper::Event event = CalendarMapper::unpackEvent(record);
            if (event.description.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            QString categoryName = categories.categoryName(event.category);
            QString ical = CalendarMapper::eventToICal(event, categoryName);
            QString filename = CalendarMapper::generateFilename(event);
            QString filepath = QDir(exportDir).filePath(filename);

            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix++);
            }

            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << ical;
                file.close();
                exportedCount++;
            }
            delete record;
        }
        m_deviceLink->closeDatabase(dbHandle);
    }

    void exportTodosToDir(const QString &exportDir, int &exportedCount, int &skippedCount) {
        exportedCount = 0;
        skippedCount = 0;

        int dbHandle = m_deviceLink->openDatabase("ToDoDB");
        if (dbHandle < 0) {
            m_logWidget->logWarning("Could not open ToDoDB");
            return;
        }

        CategoryInfo categories;
        unsigned char appInfoBuf[4096];
        size_t appInfoSize = sizeof(appInfoBuf);
        if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
            categories.parse(appInfoBuf, appInfoSize);
        }

        QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
        for (PilotRecord *record : records) {
            if (record->isDeleted()) {
                skippedCount++;
                delete record;
                continue;
            }

            TodoMapper::Todo todo = TodoMapper::unpackTodo(record);
            if (todo.description.trimmed().isEmpty()) {
                skippedCount++;
                delete record;
                continue;
            }

            QString categoryName = categories.categoryName(todo.category);
            QString ical = TodoMapper::todoToICal(todo, categoryName);
            QString filename = TodoMapper::generateFilename(todo);
            QString filepath = QDir(exportDir).filePath(filename);

            int suffix = 1;
            QString basePath = filepath;
            while (QFile::exists(filepath)) {
                filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix++);
            }

            QFile file(filepath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << ical;
                file.close();
                exportedCount++;
            }
            delete record;
        }
        m_deviceLink->closeDatabase(dbHandle);
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

    // ========== Import functions for testing write operations ==========

    void onImportMemo() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        QString filePath = QFileDialog::getOpenFileName(this,
            "Select Markdown Memo File",
            Settings::instance().lastExportPath(),
            "Markdown Files (*.md);;All Files (*)");

        if (filePath.isEmpty()) return;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
            return;
        }

        QString content = QString::fromUtf8(file.readAll());
        file.close();

        m_logWidget->logInfo(QString("Importing memo from: %1").arg(filePath));

        // Parse markdown to memo
        MemoMapper::Memo memo = MemoMapper::markdownToMemo(content);
        memo.recordId = 0;  // Create new record (ID 0 = new)

        // Pack to Palm record
        PilotRecord *record = MemoMapper::packMemo(memo);
        if (!record) {
            m_logWidget->logError("Failed to pack memo");
            return;
        }

        // Open database for write
        int dbHandle = m_deviceLink->openDatabase("MemoDB", true);  // read-write
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open MemoDB for writing");
            delete record;
            return;
        }

        // Write record
        if (m_deviceLink->writeRecord(dbHandle, record)) {
            m_logWidget->logInfo(QString("Memo imported successfully! New ID: %1").arg(record->id()));
            QMessageBox::information(this, "Import Complete",
                QString("Memo imported successfully!\nNew record ID: %1").arg(record->id()));
        } else {
            m_logWidget->logError("Failed to write memo record");
            QMessageBox::warning(this, "Import Failed", "Failed to write memo to Palm device.");
        }

        m_deviceLink->closeDatabase(dbHandle);
        delete record;
    }

    void onImportContact() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        QString filePath = QFileDialog::getOpenFileName(this,
            "Select vCard File",
            Settings::instance().lastExportPath(),
            "vCard Files (*.vcf);;All Files (*)");

        if (filePath.isEmpty()) return;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
            return;
        }

        QString content = QString::fromUtf8(file.readAll());
        file.close();

        m_logWidget->logInfo(QString("Importing contact from: %1").arg(filePath));

        // Parse vCard to contact
        ContactMapper::Contact contact = ContactMapper::vCardToContact(content);
        contact.recordId = 0;  // Create new record

        // Pack to Palm record
        PilotRecord *record = ContactMapper::packContact(contact);
        if (!record) {
            m_logWidget->logError("Failed to pack contact");
            return;
        }

        // Open database for write
        int dbHandle = m_deviceLink->openDatabase("AddressDB", true);
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open AddressDB for writing");
            delete record;
            return;
        }

        // Write record
        if (m_deviceLink->writeRecord(dbHandle, record)) {
            m_logWidget->logInfo(QString("Contact imported successfully! New ID: %1").arg(record->id()));
            QMessageBox::information(this, "Import Complete",
                QString("Contact imported successfully!\nNew record ID: %1").arg(record->id()));
        } else {
            m_logWidget->logError("Failed to write contact record");
            QMessageBox::warning(this, "Import Failed", "Failed to write contact to Palm device.");
        }

        m_deviceLink->closeDatabase(dbHandle);
        delete record;
    }

    void onImportEvent() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        QString filePath = QFileDialog::getOpenFileName(this,
            "Select iCalendar Event File",
            Settings::instance().lastExportPath(),
            "iCalendar Files (*.ics);;All Files (*)");

        if (filePath.isEmpty()) return;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
            return;
        }

        QString content = QString::fromUtf8(file.readAll());
        file.close();

        m_logWidget->logInfo(QString("Importing event from: %1").arg(filePath));

        // Parse iCal to event
        CalendarMapper::Event event = CalendarMapper::iCalToEvent(content);
        event.recordId = 0;  // Create new record

        // Pack to Palm record
        PilotRecord *record = CalendarMapper::packEvent(event);
        if (!record) {
            m_logWidget->logError("Failed to pack event");
            return;
        }

        // Open database for write
        int dbHandle = m_deviceLink->openDatabase("DatebookDB", true);
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open DatebookDB for writing");
            delete record;
            return;
        }

        // Write record
        if (m_deviceLink->writeRecord(dbHandle, record)) {
            m_logWidget->logInfo(QString("Event imported successfully! New ID: %1").arg(record->id()));
            QMessageBox::information(this, "Import Complete",
                QString("Event imported successfully!\nNew record ID: %1").arg(record->id()));
        } else {
            m_logWidget->logError("Failed to write event record");
            QMessageBox::warning(this, "Import Failed", "Failed to write event to Palm device.");
        }

        m_deviceLink->closeDatabase(dbHandle);
        delete record;
    }

    void onImportTodo() {
        if (!m_deviceLink) {
            m_logWidget->logError("No device connected");
            return;
        }

        QString filePath = QFileDialog::getOpenFileName(this,
            "Select iCalendar ToDo File",
            Settings::instance().lastExportPath(),
            "iCalendar Files (*.ics);;All Files (*)");

        if (filePath.isEmpty()) return;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
            return;
        }

        QString content = QString::fromUtf8(file.readAll());
        file.close();

        m_logWidget->logInfo(QString("Importing todo from: %1").arg(filePath));

        // Parse iCal to todo
        TodoMapper::Todo todo = TodoMapper::iCalToTodo(content);
        todo.recordId = 0;  // Create new record

        // Pack to Palm record
        PilotRecord *record = TodoMapper::packTodo(todo);
        if (!record) {
            m_logWidget->logError("Failed to pack todo");
            return;
        }

        // Open database for write
        int dbHandle = m_deviceLink->openDatabase("ToDoDB", true);
        if (dbHandle < 0) {
            m_logWidget->logError("Failed to open ToDoDB for writing");
            delete record;
            return;
        }

        // Write record
        if (m_deviceLink->writeRecord(dbHandle, record)) {
            m_logWidget->logInfo(QString("ToDo imported successfully! New ID: %1").arg(record->id()));
            QMessageBox::information(this, "Import Complete",
                QString("ToDo imported successfully!\nNew record ID: %1").arg(record->id()));
        } else {
            m_logWidget->logError("Failed to write todo record");
            QMessageBox::warning(this, "Import Failed", "Failed to write ToDo to Palm device.");
        }

        m_deviceLink->closeDatabase(dbHandle);
        delete record;
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

        m_deviceInfoAction = deviceMenu->addAction("Device &Info...");
        connect(m_deviceInfoAction, &QAction::triggered, this, &MainWindow::onDeviceInfo);
        m_deviceInfoAction->setEnabled(false);

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

        m_exportAllAction = syncMenu->addAction("Export &All...");
        m_exportAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
        connect(m_exportAllAction, &QAction::triggered, this, &MainWindow::onExportAll);
        m_exportAllAction->setEnabled(false);

        syncMenu->addSeparator();

        // Import actions for testing write operations
        m_importMemoAction = syncMenu->addAction("Import Memo (Markdown)...");
        connect(m_importMemoAction, &QAction::triggered, this, &MainWindow::onImportMemo);
        m_importMemoAction->setEnabled(false);

        m_importContactAction = syncMenu->addAction("Import Contact (vCard)...");
        connect(m_importContactAction, &QAction::triggered, this, &MainWindow::onImportContact);
        m_importContactAction->setEnabled(false);

        m_importEventAction = syncMenu->addAction("Import Event (iCal)...");
        connect(m_importEventAction, &QAction::triggered, this, &MainWindow::onImportEvent);
        m_importEventAction->setEnabled(false);

        m_importTodoAction = syncMenu->addAction("Import ToDo (iCal)...");
        connect(m_importTodoAction, &QAction::triggered, this, &MainWindow::onImportTodo);
        m_importTodoAction->setEnabled(false);

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
        m_exportAllAction->setEnabled(connected);
        m_importMemoAction->setEnabled(connected);
        m_importContactAction->setEnabled(connected);
        m_importEventAction->setEnabled(connected);
        m_importTodoAction->setEnabled(connected);
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
    QAction *m_exportAllAction;
    QAction *m_importMemoAction;
    QAction *m_importContactAction;
    QAction *m_importEventAction;
    QAction *m_importTodoAction;
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
