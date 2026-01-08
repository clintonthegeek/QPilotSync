#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QTextEdit>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QInputDialog>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QSet>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QStyle>
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
#include "settingsdialog.h"
#include "profile.h"

// Sync engine
#include "sync/syncengine.h"
#include "sync/localfilebackend.h"
#include "sync/conduits/memoconduit.h"
#include "sync/conduits/contactconduit.h"
#include "sync/conduits/calendarconduit.h"
#include "sync/conduits/todoconduit.h"
#include "sync/conduits/installconduit.h"

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
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent), m_deviceLink(nullptr), m_syncEngine(nullptr), m_currentProfile(nullptr) {
        setWindowTitle("QPilotSync - Palm Pilot Synchronization");
        setMinimumSize(900, 600);

        // Restore window geometry if saved
        QByteArray savedGeometry = Settings::instance().windowGeometry();
        if (!savedGeometry.isEmpty()) {
            restoreGeometry(savedGeometry);
        }

        // Create MDI area as central widget
        m_mdiArea = new QMdiArea(this);
        m_mdiArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_mdiArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_mdiArea->setViewMode(QMdiArea::SubWindowView);
        m_mdiArea->setDocumentMode(true);
        setCentralWidget(m_mdiArea);

        // Create log widget as MDI subwindow
        createLogWindow();

        // Initialize sync engine
        initializeSyncEngine();

        // Create menu bar and toolbar
        createMenus();
        createToolBar();

        // Status bar
        statusBar()->showMessage("Ready - No device connected");

        // Log initial message
        m_logWidget->logInfo("QPilotSync " + QString(QPILOTSYNC_VERSION_STRING) + " initialized");
        m_logWidget->logInfo("Ready to connect to Palm device");

        // Load default profile if set
        QString defaultProfile = Settings::instance().defaultProfilePath();
        if (!defaultProfile.isEmpty() && QDir(defaultProfile).exists()) {
            loadProfile(defaultProfile);
        } else {
            m_logWidget->logInfo("No profile loaded. Use File → Open Profile to select a sync folder.");
            updateWindowTitle();
        }
    }

    ~MainWindow() {
        // Save window geometry
        Settings::instance().setWindowGeometry(saveGeometry());
        Settings::instance().sync();

        if (m_deviceLink) {
            m_deviceLink->closeConnection();
            delete m_deviceLink;
        }

        delete m_syncEngine;
        delete m_currentProfile;
    }

private slots:
    void onConnectDevice() {
        // Get default device settings from current profile or use fallbacks
        QString defaultDevice = "/dev/ttyUSB0";
        QString defaultBaud = "115200";

        if (m_currentProfile) {
            defaultDevice = m_currentProfile->devicePath();
            defaultBaud = m_currentProfile->baudRate();
        }

        // Create a dialog for connection settings
        QDialog dialog(this);
        dialog.setWindowTitle("Connect to Palm Device");
        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        // Profile info
        if (m_currentProfile) {
            QLabel *profileLabel = new QLabel(QString("Profile: <b>%1</b>").arg(m_currentProfile->name()), &dialog);
            profileLabel->setTextFormat(Qt::RichText);
            layout->addWidget(profileLabel);
        } else {
            QLabel *noProfileLabel = new QLabel("<i>No profile loaded - device will not be registered</i>", &dialog);
            noProfileLabel->setTextFormat(Qt::RichText);
            noProfileLabel->setStyleSheet("color: orange;");
            layout->addWidget(noProfileLabel);
        }

        // Device path - use profile settings as default
        QLabel *pathLabel = new QLabel("Device:", &dialog);
        QComboBox *pathCombo = new QComboBox(&dialog);
        pathCombo->setEditable(true);
        pathCombo->addItems({"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyS0", "usb:"});
        pathCombo->setCurrentText(defaultDevice);

        // Baud rate - use profile settings as default
        QLabel *baudLabel = new QLabel("Baud Rate:", &dialog);
        QComboBox *baudCombo = new QComboBox(&dialog);
        baudCombo->addItems({"115200", "57600", "38400", "19200", "9600"});
        baudCombo->setCurrentText(defaultBaud);

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

        // Store for later (in case user creates a new profile after connecting)
        m_lastUsedDevicePath = devicePath;
        m_lastUsedBaudRate = baudRate;

        // Save settings to profile if loaded
        if (m_currentProfile) {
            m_currentProfile->setDevicePath(devicePath);
            m_currentProfile->setBaudRate(baudRate);
            m_currentProfile->save();
        }

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

        // Connect signals - use lambdas so log widget can be recreated
        connect(m_deviceLink, &KPilotLink::logMessage,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logInfo(msg); });
        connect(m_deviceLink, &KPilotLink::errorOccurred,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logError(msg); });
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

            // Create device fingerprint from connected Palm
            DeviceFingerprint connectedDevice;
            connectedDevice.userId = user.userID;
            connectedDevice.userName = QString::fromUtf8(user.username);

            // Check device fingerprint against current profile and registry
            if (!handleDeviceFingerprint(connectedDevice)) {
                // User chose to abort or switch profiles - disconnect
                onDisconnectDevice();
                return;
            }

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

            // Set device link on sync engine
            m_syncEngine->setDeviceLink(m_deviceLink);

            // Enable device-dependent menu items
            updateMenuState(true);
            statusBar()->showMessage(QString("Connected: %1").arg(username));
        } else {
            m_logWidget->logError("Failed to read user info from device");
            statusBar()->showMessage("Connection error");
        }
    }

    // Returns true to continue, false to abort connection
    bool handleDeviceFingerprint(const DeviceFingerprint &connectedDevice) {
        if (connectedDevice.isEmpty()) {
            // No fingerprint - can't verify
            return true;
        }

        // If no profile loaded, just inform and continue
        if (!m_currentProfile) {
            QString registeredProfile = Settings::instance().findProfileForDevice(connectedDevice);
            if (!registeredProfile.isEmpty()) {
                // Device is registered with a profile - suggest loading it
                int ret = QMessageBox::question(this, "Known Device Detected",
                    QString("This device (%1) is registered with profile:\n%2\n\n"
                            "Would you like to load that profile?")
                        .arg(connectedDevice.displayString())
                        .arg(QFileInfo(registeredProfile).fileName()),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                    QMessageBox::Yes);

                if (ret == QMessageBox::Yes) {
                    loadProfile(registeredProfile);
                    return true;
                } else if (ret == QMessageBox::Cancel) {
                    return false;
                }
            }
            return true;
        }

        // Profile is loaded - check if device matches
        DeviceFingerprint expectedDevice = m_currentProfile->deviceFingerprint();

        if (expectedDevice.isEmpty()) {
            // Profile has no registered device - offer to register this one
            int ret = QMessageBox::question(this, "Register Device",
                QString("This profile (%1) has no registered device.\n\n"
                        "Register '%2' as this profile's device?")
                    .arg(m_currentProfile->name())
                    .arg(connectedDevice.displayString()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (ret == QMessageBox::Yes) {
                registerDeviceWithCurrentProfile(connectedDevice);
            }
            return true;
        }

        // Check if fingerprints match
        if (expectedDevice.matches(connectedDevice)) {
            m_logWidget->logInfo(QString("Device verified: %1").arg(connectedDevice.displayString()));
            return true;
        }

        // MISMATCH! Wrong device connected to this profile
        m_logWidget->logWarning(QString("Device mismatch! Expected: %1, Got: %2")
            .arg(expectedDevice.displayString())
            .arg(connectedDevice.displayString()));

        // Check if this device belongs to another profile
        QString correctProfile = Settings::instance().findProfileForDevice(connectedDevice);

        if (!correctProfile.isEmpty() && correctProfile != m_currentProfile->syncFolderPath()) {
            // Device belongs to a different profile
            int ret = QMessageBox::warning(this, "Wrong Device for Profile",
                QString("This device (%1) belongs to a different profile:\n\n"
                        "Current profile: %2\n"
                        "Device's profile: %3\n\n"
                        "What would you like to do?")
                    .arg(connectedDevice.displayString())
                    .arg(m_currentProfile->name())
                    .arg(QFileInfo(correctProfile).fileName()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            // Yes = Switch to correct profile, No = Continue anyway, Cancel = Abort

            if (ret == QMessageBox::Yes) {
                // Disconnect and switch profiles
                loadProfile(correctProfile);
                m_logWidget->logInfo(QString("Switched to profile: %1").arg(QFileInfo(correctProfile).fileName()));
                // Return false to disconnect, user should reconnect
                return false;
            } else if (ret == QMessageBox::Cancel) {
                return false;
            }
            // No = continue with warning
            m_logWidget->logWarning("Continuing with mismatched device - sync may cause data issues!");
            return true;
        } else {
            // Unknown device
            int ret = QMessageBox::warning(this, "Unknown Device",
                QString("This device (%1) is not recognized.\n\n"
                        "This profile expects: %2\n\n"
                        "Do you want to:\n"
                        "- Yes: Register this device with current profile (replaces old device)\n"
                        "- No: Continue without registering\n"
                        "- Cancel: Abort connection")
                    .arg(connectedDevice.displayString())
                    .arg(expectedDevice.displayString()),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

            if (ret == QMessageBox::Yes) {
                registerDeviceWithCurrentProfile(connectedDevice);
                return true;
            } else if (ret == QMessageBox::Cancel) {
                return false;
            }
            m_logWidget->logWarning("Continuing with unregistered device");
            return true;
        }
    }

    void registerDeviceWithCurrentProfile(const DeviceFingerprint &fingerprint) {
        if (!m_currentProfile) return;

        m_currentProfile->setDeviceFingerprint(fingerprint);
        m_currentProfile->save();

        // Also register in global registry
        Settings::instance().registerDevice(fingerprint, m_currentProfile->syncFolderPath());
        Settings::instance().sync();

        m_logWidget->logInfo(QString("Registered device '%1' with profile '%2'")
            .arg(fingerprint.displayString())
            .arg(m_currentProfile->name()));
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

    void onSettings() {
        SettingsDialog dialog(this);
        connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
            m_logWidget->logInfo("Settings updated");
        });
        dialog.exec();
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

        QAction *newProfileAction = fileMenu->addAction("&New Profile...");
        newProfileAction->setShortcut(QKeySequence::New);
        newProfileAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
        connect(newProfileAction, &QAction::triggered, this, &MainWindow::onNewProfile);

        QAction *openProfileAction = fileMenu->addAction("&Open Profile...");
        openProfileAction->setShortcut(QKeySequence::Open);
        openProfileAction->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        connect(openProfileAction, &QAction::triggered, this, &MainWindow::onOpenProfile);

        // Recent profiles submenu
        m_recentProfilesMenu = fileMenu->addMenu("Recent Profiles");
        m_recentProfilesMenu->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        updateRecentProfilesMenu();

        m_closeProfileAction = fileMenu->addAction("&Close Profile");
        m_closeProfileAction->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
        connect(m_closeProfileAction, &QAction::triggered, this, &MainWindow::onCloseProfile);
        m_closeProfileAction->setEnabled(false);

        fileMenu->addSeparator();

        m_profileSettingsAction = fileMenu->addAction("Profile &Settings...");
        m_profileSettingsAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
        connect(m_profileSettingsAction, &QAction::triggered, this, &MainWindow::onProfileSettings);
        m_profileSettingsAction->setEnabled(false);

        fileMenu->addSeparator();

        QAction *quitAction = fileMenu->addAction("&Quit");
        quitAction->setShortcut(QKeySequence::Quit);
        connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);

        // Edit menu
        QMenu *editMenu = menuBar()->addMenu("&Edit");

        QAction *settingsAction = editMenu->addAction("&Settings...");
        settingsAction->setShortcut(QKeySequence::Preferences);
        settingsAction->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

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

        // Sync menu - new sync engine operations
        QMenu *syncMenu = menuBar()->addMenu("&Sync");

        m_hotSyncAction = syncMenu->addAction("&HotSync");
        m_hotSyncAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
        connect(m_hotSyncAction, &QAction::triggered, this, &MainWindow::onHotSync);
        m_hotSyncAction->setEnabled(false);

        m_fullSyncAction = syncMenu->addAction("&Full Sync");
        m_fullSyncAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
        connect(m_fullSyncAction, &QAction::triggered, this, &MainWindow::onFullSync);
        m_fullSyncAction->setEnabled(false);

        syncMenu->addSeparator();

        m_copyPalmToPCAction = syncMenu->addAction("Copy &Palm → PC");
        connect(m_copyPalmToPCAction, &QAction::triggered, this, &MainWindow::onCopyPalmToPC);
        m_copyPalmToPCAction->setEnabled(false);

        m_copyPCToPalmAction = syncMenu->addAction("Copy P&C → Palm");
        connect(m_copyPCToPalmAction, &QAction::triggered, this, &MainWindow::onCopyPCToPalm);
        m_copyPCToPalmAction->setEnabled(false);

        syncMenu->addSeparator();

        m_backupAction = syncMenu->addAction("&Backup Palm → PC");
        m_backupAction->setToolTip("Backup Palm data to PC (preserves old files)");
        connect(m_backupAction, &QAction::triggered, this, &MainWindow::onBackup);
        m_backupAction->setEnabled(false);

        m_restoreAction = syncMenu->addAction("&Restore PC → Palm");
        m_restoreAction->setToolTip("Restore Palm from PC backup (full restore)");
        connect(m_restoreAction, &QAction::triggered, this, &MainWindow::onRestore);
        m_restoreAction->setEnabled(false);

        syncMenu->addSeparator();

        m_changeSyncFolderAction = syncMenu->addAction("Change Sync &Folder...");
        connect(m_changeSyncFolderAction, &QAction::triggered, this, &MainWindow::onChangeSyncFolder);

        m_openSyncFolderAction = syncMenu->addAction("&Open Sync Folder");
        connect(m_openSyncFolderAction, &QAction::triggered, this, &MainWindow::onOpenSyncFolder);

        // Legacy Export submenu (kept for compatibility during development)
        syncMenu->addSeparator();
        QMenu *legacyExportMenu = syncMenu->addMenu("Legacy Export");

        m_exportMemosAction = legacyExportMenu->addAction("Export &Memos...");
        connect(m_exportMemosAction, &QAction::triggered, this, &MainWindow::onExportMemos);
        m_exportMemosAction->setEnabled(false);

        m_exportContactsAction = legacyExportMenu->addAction("Export &Contacts...");
        connect(m_exportContactsAction, &QAction::triggered, this, &MainWindow::onExportContacts);
        m_exportContactsAction->setEnabled(false);

        m_exportCalendarAction = legacyExportMenu->addAction("Export C&alendar...");
        connect(m_exportCalendarAction, &QAction::triggered, this, &MainWindow::onExportCalendar);
        m_exportCalendarAction->setEnabled(false);

        m_exportTodosAction = legacyExportMenu->addAction("Export &ToDos...");
        connect(m_exportTodosAction, &QAction::triggered, this, &MainWindow::onExportTodos);
        m_exportTodosAction->setEnabled(false);

        legacyExportMenu->addSeparator();

        m_exportAllAction = legacyExportMenu->addAction("Export &All...");
        m_exportAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
        connect(m_exportAllAction, &QAction::triggered, this, &MainWindow::onExportAll);
        m_exportAllAction->setEnabled(false);

        // Legacy Import submenu
        QMenu *legacyImportMenu = syncMenu->addMenu("Legacy Import");

        m_importMemoAction = legacyImportMenu->addAction("Import Memo (Markdown)...");
        connect(m_importMemoAction, &QAction::triggered, this, &MainWindow::onImportMemo);
        m_importMemoAction->setEnabled(false);

        m_importContactAction = legacyImportMenu->addAction("Import Contact (vCard)...");
        connect(m_importContactAction, &QAction::triggered, this, &MainWindow::onImportContact);
        m_importContactAction->setEnabled(false);

        m_importEventAction = legacyImportMenu->addAction("Import Event (iCal)...");
        connect(m_importEventAction, &QAction::triggered, this, &MainWindow::onImportEvent);
        m_importEventAction->setEnabled(false);

        m_importTodoAction = legacyImportMenu->addAction("Import ToDo (iCal)...");
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

    void createToolBar() {
        // Main toolbar
        QToolBar *mainToolBar = addToolBar("Main");
        mainToolBar->setObjectName("MainToolBar");
        mainToolBar->setMovable(false);
        mainToolBar->setIconSize(QSize(24, 24));
        mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        // === Device Group ===
        QAction *connectAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_ComputerIcon),
            "Connect");
        connectAction->setToolTip("Connect to Palm device (Ctrl+D)");
        connect(connectAction, &QAction::triggered, this, &MainWindow::onConnectDevice);

        m_toolbarDisconnectAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_DialogCloseButton),
            "Disconnect");
        m_toolbarDisconnectAction->setToolTip("Disconnect from device");
        m_toolbarDisconnectAction->setEnabled(false);
        connect(m_toolbarDisconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectDevice);

        mainToolBar->addSeparator();

        // === Sync Group ===
        m_toolbarHotSyncAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_BrowserReload),
            "HotSync");
        m_toolbarHotSyncAction->setToolTip("Sync modified records (Ctrl+H)");
        m_toolbarHotSyncAction->setEnabled(false);
        connect(m_toolbarHotSyncAction, &QAction::triggered, this, &MainWindow::onHotSync);

        m_toolbarFullSyncAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_DialogApplyButton),
            "Full Sync");
        m_toolbarFullSyncAction->setToolTip("Compare all records");
        m_toolbarFullSyncAction->setEnabled(false);
        connect(m_toolbarFullSyncAction, &QAction::triggered, this, &MainWindow::onFullSync);

        mainToolBar->addSeparator();

        // === Backup/Restore Group ===
        m_toolbarBackupAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_DialogSaveButton),
            "Backup");
        m_toolbarBackupAction->setToolTip("Backup Palm → PC (preserves old files)");
        m_toolbarBackupAction->setEnabled(false);
        connect(m_toolbarBackupAction, &QAction::triggered, this, &MainWindow::onBackup);

        m_toolbarRestoreAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_DialogOpenButton),
            "Restore");
        m_toolbarRestoreAction->setToolTip("Restore PC → Palm (full restore)");
        m_toolbarRestoreAction->setEnabled(false);
        connect(m_toolbarRestoreAction, &QAction::triggered, this, &MainWindow::onRestore);

        mainToolBar->addSeparator();

        // === Folder Group ===
        QAction *openFolderAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_DirOpenIcon),
            "Open Folder");
        openFolderAction->setToolTip("Open sync folder in file manager");
        connect(openFolderAction, &QAction::triggered, this, &MainWindow::onOpenSyncFolder);

        // Add stretch to push remaining items to the right (if any)
        QWidget *spacer = new QWidget();
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        mainToolBar->addWidget(spacer);

        // === View Group (right side) ===
        QAction *showLogAction = mainToolBar->addAction(
            style()->standardIcon(QStyle::SP_FileDialogDetailedView),
            "Log");
        showLogAction->setToolTip("Show/maximize sync log window");
        connect(showLogAction, &QAction::triggered, this, &MainWindow::showLogWindow);
    }

    void createLogWindow() {
        m_logWidget = new LogWidget();
        m_logSubWindow = m_mdiArea->addSubWindow(m_logWidget);
        m_logSubWindow->setWindowTitle("Sync Log");
        m_logSubWindow->setWindowIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        m_logSubWindow->showMaximized();

        // When the subwindow is closed/destroyed, null our pointers
        connect(m_logSubWindow, &QObject::destroyed, this, [this]() {
            m_logSubWindow = nullptr;
            m_logWidget = nullptr;
        });
    }

    void showLogWindow() {
        if (!m_logSubWindow) {
            // Recreate the log window if it was closed
            createLogWindow();
            m_logWidget->logInfo("Log window reopened");
        } else {
            m_logSubWindow->show();
            m_logSubWindow->showMaximized();
        }
        m_mdiArea->setActiveSubWindow(m_logSubWindow);
    }

    void updateMenuState(bool connected) {
        m_disconnectAction->setEnabled(connected);
        m_listDatabasesAction->setEnabled(connected);
        m_setUserInfoAction->setEnabled(connected);
        m_deviceInfoAction->setEnabled(connected);

        // New sync operations
        m_hotSyncAction->setEnabled(connected);
        m_fullSyncAction->setEnabled(connected);
        m_copyPalmToPCAction->setEnabled(connected);
        m_copyPCToPalmAction->setEnabled(connected);
        m_backupAction->setEnabled(connected);
        m_restoreAction->setEnabled(connected);

        // Toolbar actions
        m_toolbarDisconnectAction->setEnabled(connected);
        m_toolbarHotSyncAction->setEnabled(connected);
        m_toolbarFullSyncAction->setEnabled(connected);
        m_toolbarBackupAction->setEnabled(connected);
        m_toolbarRestoreAction->setEnabled(connected);

        // Legacy export/import operations
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

    // MDI area and log window
    QMdiArea *m_mdiArea;
    QMdiSubWindow *m_logSubWindow;
    LogWidget *m_logWidget;

    KPilotDeviceLink *m_deviceLink;

    // Sync engine
    Sync::SyncEngine *m_syncEngine;
    Sync::InstallConduit *m_installConduit;
    QString m_syncPath;

    // Last used connection settings (for passing to new profiles)
    QString m_lastUsedDevicePath;
    QString m_lastUsedBaudRate;

    // Profile
    Profile *m_currentProfile;
    QMenu *m_recentProfilesMenu;
    QAction *m_closeProfileAction;
    QAction *m_profileSettingsAction;

    // Menu actions that need to be enabled/disabled
    QAction *m_disconnectAction;
    QAction *m_cancelConnectionAction;
    QAction *m_listDatabasesAction;
    QAction *m_setUserInfoAction;
    QAction *m_deviceInfoAction;

    // Sync menu actions
    QAction *m_hotSyncAction;
    QAction *m_fullSyncAction;
    QAction *m_copyPalmToPCAction;
    QAction *m_copyPCToPalmAction;
    QAction *m_backupAction;
    QAction *m_restoreAction;
    QAction *m_changeSyncFolderAction;
    QAction *m_openSyncFolderAction;

    // Toolbar actions
    QAction *m_toolbarDisconnectAction;
    QAction *m_toolbarHotSyncAction;
    QAction *m_toolbarFullSyncAction;
    QAction *m_toolbarBackupAction;
    QAction *m_toolbarRestoreAction;

    // Legacy export/import actions
    QAction *m_exportMemosAction;
    QAction *m_exportContactsAction;
    QAction *m_exportCalendarAction;
    QAction *m_exportTodosAction;
    QAction *m_exportAllAction;
    QAction *m_importMemoAction;
    QAction *m_importContactAction;
    QAction *m_importEventAction;
    QAction *m_importTodoAction;

    // ========== Private Methods ==========

    void initializeSyncEngine() {
        // Create sync engine (backend and conduits registered when profile is loaded)
        m_syncEngine = new Sync::SyncEngine(this);

        // Register conduits
        m_syncEngine->registerConduit(new Sync::MemoConduit());
        m_syncEngine->registerConduit(new Sync::ContactConduit());
        m_syncEngine->registerConduit(new Sync::CalendarConduit());
        m_syncEngine->registerConduit(new Sync::TodoConduit());

        // Create install conduit (handled separately from sync engine)
        m_installConduit = new Sync::InstallConduit(this);
        connect(m_installConduit, &Sync::InstallConduit::logMessage,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logInfo(msg); });
        connect(m_installConduit, &Sync::InstallConduit::errorOccurred,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logError(msg); });
        connect(m_installConduit, &Sync::InstallConduit::progressUpdated,
                this, [this](int current, int total, const QString &fileName) {
                    statusBar()->showMessage(QString("Installing %1 (%2/%3)")
                        .arg(fileName).arg(current).arg(total));
                });

        // Connect signals - use lambdas so log widget can be recreated
        connect(m_syncEngine, &Sync::SyncEngine::logMessage,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logInfo(msg); });
        connect(m_syncEngine, &Sync::SyncEngine::errorOccurred,
                this, [this](const QString &msg) { if (m_logWidget) m_logWidget->logError(msg); });
        connect(m_syncEngine, &Sync::SyncEngine::syncStarted,
                this, &MainWindow::onSyncStarted);
        connect(m_syncEngine, &Sync::SyncEngine::syncFinished,
                this, &MainWindow::onSyncFinished);
        connect(m_syncEngine, &Sync::SyncEngine::progressUpdated,
                this, &MainWindow::onSyncProgress);
    }

    // ========== Profile Management ==========

    void loadProfile(const QString &path) {
        // Clean up old profile
        if (m_currentProfile) {
            m_currentProfile->save();
            delete m_currentProfile;
            m_currentProfile = nullptr;
        }

        // Create and load new profile
        m_currentProfile = new Profile(path);

        if (!m_currentProfile->isValid()) {
            m_logWidget->logError(QString("Invalid profile path: %1").arg(path));
            delete m_currentProfile;
            m_currentProfile = nullptr;
            updateWindowTitle();
            updateProfileMenuState();
            return;
        }

        bool isNewProfile = !m_currentProfile->exists();

        // Initialize profile if needed (creates directories and default config)
        if (isNewProfile) {
            m_currentProfile->initialize();
            m_logWidget->logInfo(QString("Created new profile at: %1").arg(path));

            // For new profiles, use last used connection settings if available
            if (!m_lastUsedDevicePath.isEmpty()) {
                m_currentProfile->setDevicePath(m_lastUsedDevicePath);
                m_currentProfile->setBaudRate(m_lastUsedBaudRate);
                m_currentProfile->save();
                m_logWidget->logInfo(QString("Using device settings: %1 at %2 bps")
                    .arg(m_lastUsedDevicePath).arg(m_lastUsedBaudRate));
            }
        }

        m_syncPath = path;

        // Update sync engine with profile settings
        m_syncEngine->setStateDirectory(m_currentProfile->stateDirectoryPath());

        Sync::LocalFileBackend *backend = new Sync::LocalFileBackend(m_syncPath);
        m_syncEngine->setBackend(backend);

        // Configure install conduit with profile's install folder
        m_installConduit->setInstallFolder(m_currentProfile->installFolderPath());

        // Add to recent profiles
        Settings::instance().addRecentProfile(path);

        // Auto-set as default if no default profile exists
        if (Settings::instance().defaultProfilePath().isEmpty()) {
            Settings::instance().setDefaultProfilePath(path);
            m_logWidget->logInfo("Set as default profile");
        }

        Settings::instance().sync();

        // Update UI
        updateWindowTitle();
        updateProfileMenuState();
        updateRecentProfilesMenu();

        m_logWidget->logInfo(QString("Loaded profile: %1").arg(m_currentProfile->name()));
        m_logWidget->logInfo(QString("Sync folder: %1").arg(m_syncPath));
    }

    void closeProfile() {
        if (m_currentProfile) {
            m_currentProfile->save();
            delete m_currentProfile;
            m_currentProfile = nullptr;
        }

        m_syncPath.clear();

        updateWindowTitle();
        updateProfileMenuState();

        m_logWidget->logInfo("Profile closed");
    }

    void updateWindowTitle() {
        QString title = "QPilotSync";
        if (m_currentProfile) {
            title += " - " + m_currentProfile->name();
        }
        setWindowTitle(title);
    }

    void updateProfileMenuState() {
        bool hasProfile = (m_currentProfile != nullptr);
        m_closeProfileAction->setEnabled(hasProfile);
        m_profileSettingsAction->setEnabled(hasProfile);

        // Sync operations require a profile
        m_changeSyncFolderAction->setEnabled(hasProfile);
        m_openSyncFolderAction->setEnabled(hasProfile);
    }

    void updateRecentProfilesMenu() {
        m_recentProfilesMenu->clear();

        QStringList recent = Settings::instance().recentProfiles();

        if (recent.isEmpty()) {
            QAction *emptyAction = m_recentProfilesMenu->addAction("(No recent profiles)");
            emptyAction->setEnabled(false);
            return;
        }

        for (const QString &path : recent) {
            QFileInfo info(path);
            QString displayName = info.fileName();
            QAction *action = m_recentProfilesMenu->addAction(displayName);
            action->setToolTip(path);
            action->setData(path);
            connect(action, &QAction::triggered, this, [this, path]() {
                loadProfile(path);
            });
        }

        m_recentProfilesMenu->addSeparator();
        QAction *clearAction = m_recentProfilesMenu->addAction("Clear Recent");
        connect(clearAction, &QAction::triggered, this, [this]() {
            Settings::instance().clearRecentProfiles();
            Settings::instance().sync();
            updateRecentProfilesMenu();
        });
    }

private slots:
    // ========== Profile Slots ==========

    void onNewProfile() {
        QString path = QFileDialog::getExistingDirectory(this,
            "Select Folder for New Profile",
            QDir::homePath(),
            QFileDialog::ShowDirsOnly);

        if (path.isEmpty()) return;

        // Check if it already has a profile
        Profile testProfile(path);
        if (testProfile.exists()) {
            int ret = QMessageBox::question(this, "Profile Exists",
                QString("A profile already exists in:\n%1\n\nDo you want to load it instead?").arg(path),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            if (ret == QMessageBox::Yes) {
                loadProfile(path);
            }
            return;
        }

        // Create new profile
        loadProfile(path);
    }

    void onOpenProfile() {
        QString path = QFileDialog::getExistingDirectory(this,
            "Select Profile Folder",
            QDir::homePath(),
            QFileDialog::ShowDirsOnly);

        if (path.isEmpty()) return;

        loadProfile(path);
    }

    void onCloseProfile() {
        closeProfile();
    }

    void onProfileSettings() {
        if (!m_currentProfile) {
            m_logWidget->logWarning("No profile loaded");
            return;
        }

        // Create a dialog for profile-specific settings
        QDialog dialog(this);
        dialog.setWindowTitle(QString("Profile Settings - %1").arg(m_currentProfile->name()));
        dialog.setMinimumWidth(500);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        // Profile info
        QGroupBox *infoGroup = new QGroupBox("Profile Information");
        QFormLayout *infoLayout = new QFormLayout(infoGroup);

        QLineEdit *nameEdit = new QLineEdit(m_currentProfile->name());
        infoLayout->addRow("Profile Name:", nameEdit);

        QLabel *pathLabel = new QLabel(m_currentProfile->syncFolderPath());
        pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoLayout->addRow("Sync Folder:", pathLabel);

        layout->addWidget(infoGroup);

        // Device settings
        QGroupBox *deviceGroup = new QGroupBox("Device Connection");
        QFormLayout *deviceLayout = new QFormLayout(deviceGroup);

        // Device path
        QComboBox *devicePathCombo = new QComboBox();
        devicePathCombo->setEditable(true);
        devicePathCombo->addItems({"/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyS0", "usb:"});
        devicePathCombo->setCurrentText(m_currentProfile->devicePath());
        deviceLayout->addRow("Device Port:", devicePathCombo);

        // Baud rate
        QComboBox *baudRateCombo = new QComboBox();
        baudRateCombo->addItems({"115200", "57600", "38400", "19200", "9600"});
        baudRateCombo->setCurrentText(m_currentProfile->baudRate());
        deviceLayout->addRow("Baud Rate:", baudRateCombo);

        // Registered device info
        DeviceFingerprint fp = m_currentProfile->deviceFingerprint();
        QString deviceInfo = fp.isEmpty() ? "(No device registered)" : fp.displayString();
        QLabel *registeredDeviceLabel = new QLabel(deviceInfo);
        registeredDeviceLabel->setStyleSheet(fp.isEmpty() ? "color: gray;" : "");
        deviceLayout->addRow("Registered Device:", registeredDeviceLabel);

        // Clear device registration button
        QPushButton *clearDeviceBtn = new QPushButton("Clear Device Registration");
        clearDeviceBtn->setEnabled(!fp.isEmpty());
        connect(clearDeviceBtn, &QPushButton::clicked, this, [this, registeredDeviceLabel, clearDeviceBtn]() {
            DeviceFingerprint oldFp = m_currentProfile->deviceFingerprint();
            if (!oldFp.isEmpty()) {
                Settings::instance().unregisterDevice(oldFp);
                Settings::instance().sync();
            }
            m_currentProfile->setDeviceFingerprint(DeviceFingerprint());
            m_currentProfile->save();
            registeredDeviceLabel->setText("(No device registered)");
            registeredDeviceLabel->setStyleSheet("color: gray;");
            clearDeviceBtn->setEnabled(false);
            m_logWidget->logInfo("Device registration cleared");
        });
        deviceLayout->addRow("", clearDeviceBtn);

        layout->addWidget(deviceGroup);

        // Conflict resolution
        QGroupBox *conflictGroup = new QGroupBox("Conflict Resolution");
        QFormLayout *conflictLayout = new QFormLayout(conflictGroup);

        QComboBox *conflictCombo = new QComboBox();
        conflictCombo->addItem("Ask for each conflict", "ask");
        conflictCombo->addItem("Palm always wins", "palm-wins");
        conflictCombo->addItem("PC always wins", "pc-wins");
        conflictCombo->addItem("Keep both (duplicate)", "duplicate");
        conflictCombo->addItem("Newest wins", "newest");
        conflictCombo->addItem("Skip conflicts", "skip");

        QString currentPolicy = m_currentProfile->conflictPolicy();
        for (int i = 0; i < conflictCombo->count(); i++) {
            if (conflictCombo->itemData(i).toString() == currentPolicy) {
                conflictCombo->setCurrentIndex(i);
                break;
            }
        }
        conflictLayout->addRow("When conflicts occur:", conflictCombo);

        layout->addWidget(conflictGroup);

        // Enabled conduits
        QGroupBox *conduitsGroup = new QGroupBox("Enabled Conduits");
        QVBoxLayout *conduitsLayout = new QVBoxLayout(conduitsGroup);

        QCheckBox *memosCheck = new QCheckBox("Memos (MemoDB → Markdown files)");
        QCheckBox *contactsCheck = new QCheckBox("Contacts (AddressDB → vCard files)");
        QCheckBox *calendarCheck = new QCheckBox("Calendar (DatebookDB → iCalendar files)");
        QCheckBox *todosCheck = new QCheckBox("ToDos (ToDoDB → iCalendar VTODO files)");

        memosCheck->setChecked(m_currentProfile->conduitEnabled("memos"));
        contactsCheck->setChecked(m_currentProfile->conduitEnabled("contacts"));
        calendarCheck->setChecked(m_currentProfile->conduitEnabled("calendar"));
        todosCheck->setChecked(m_currentProfile->conduitEnabled("todos"));

        conduitsLayout->addWidget(memosCheck);
        conduitsLayout->addWidget(contactsCheck);
        conduitsLayout->addWidget(calendarCheck);
        conduitsLayout->addWidget(todosCheck);

        layout->addWidget(conduitsGroup);

        // Button box
        QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttonBox);

        if (dialog.exec() == QDialog::Accepted) {
            // Save profile settings
            m_currentProfile->setName(nameEdit->text());
            m_currentProfile->setDevicePath(devicePathCombo->currentText());
            m_currentProfile->setBaudRate(baudRateCombo->currentText());
            m_currentProfile->setConflictPolicy(conflictCombo->currentData().toString());
            m_currentProfile->setConduitEnabled("memos", memosCheck->isChecked());
            m_currentProfile->setConduitEnabled("contacts", contactsCheck->isChecked());
            m_currentProfile->setConduitEnabled("calendar", calendarCheck->isChecked());
            m_currentProfile->setConduitEnabled("todos", todosCheck->isChecked());
            m_currentProfile->save();

            updateWindowTitle();
            m_logWidget->logInfo("Profile settings saved");
        }
    }

    // ========== Install Conduit ==========

    void runInstallConduit() {
        if (!m_installConduit || !m_deviceLink || !m_deviceLink->isConnected()) {
            return;
        }

        if (!m_installConduit->hasPendingFiles()) {
            return;
        }

        m_logWidget->logInfo("--- Installing pending files ---");

        int socket = m_deviceLink->socketDescriptor();
        QList<Sync::InstallResult> results = m_installConduit->installAll(socket);

        // Count successes/failures
        int successCount = 0;
        int failCount = 0;
        for (const Sync::InstallResult &r : results) {
            if (r.success) {
                successCount++;
            } else {
                failCount++;
            }
        }

        if (successCount > 0 || failCount > 0) {
            m_logWidget->logInfo(QString("Install complete: %1 succeeded, %2 failed")
                .arg(successCount).arg(failCount));
        }
    }

    // ========== Sync Slots ==========

    void onHotSync() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        m_logWidget->logInfo("=== Starting HotSync ===");

        // Install any pending files first
        runInstallConduit();

        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::HotSync);
        showSyncResult(result, "HotSync");
    }

    void onFullSync() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        int ret = QMessageBox::question(this, "Full Sync",
            "Full Sync will compare all records on both sides.\n"
            "This may take longer than HotSync.\n\nProceed?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (ret != QMessageBox::Yes) return;

        m_logWidget->logInfo("=== Starting Full Sync ===");

        // Install any pending files first
        runInstallConduit();

        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::FullSync);
        showSyncResult(result, "Full Sync");
    }

    void onCopyPalmToPC() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        int ret = QMessageBox::warning(this, "Copy Palm → PC",
            "This will overwrite PC data with Palm data.\n"
            "Any changes on the PC will be lost.\n\nProceed?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        m_logWidget->logInfo("=== Copying Palm → PC ===");
        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::CopyPalmToPC);
        showSyncResult(result, "Copy Palm → PC");
    }

    void onCopyPCToPalm() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        int ret = QMessageBox::warning(this, "Copy PC → Palm",
            "This will overwrite Palm data with PC data.\n"
            "Any changes on the Palm will be lost.\n\nProceed?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        m_logWidget->logInfo("=== Copying PC → Palm ===");

        // Install any pending files first
        runInstallConduit();

        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::CopyPCToPalm);
        showSyncResult(result, "Copy PC → Palm");
    }

    void onBackup() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        int ret = QMessageBox::question(this, "Backup Palm → PC",
            "This will backup all Palm data to your PC.\n"
            "Existing backup files will be updated.\n"
            "Old files not on Palm will be preserved.\n\nProceed?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

        if (ret != QMessageBox::Yes) return;

        m_logWidget->logInfo("=== Backing up Palm → PC ===");
        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::Backup);
        showSyncResult(result, "Backup");
    }

    void onRestore() {
        if (!m_currentProfile) {
            m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
            return;
        }
        if (!m_deviceLink || !m_deviceLink->isConnected()) {
            m_logWidget->logError("No device connected");
            return;
        }

        int ret = QMessageBox::warning(this, "Restore PC → Palm",
            "⚠️ FULL RESTORE\n\n"
            "This will completely overwrite your Palm with PC backup data.\n"
            "Palm records not in the backup WILL BE DELETED.\n\n"
            "Are you sure you want to restore?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (ret != QMessageBox::Yes) return;

        m_logWidget->logInfo("=== Restoring PC → Palm ===");

        // Install any pending files first
        runInstallConduit();

        Sync::SyncResult result = m_syncEngine->syncAll(Sync::SyncMode::Restore);
        showSyncResult(result, "Restore");
    }

    void onChangeSyncFolder() {
        // Now handled through profiles - redirect to Open Profile
        onOpenProfile();
    }

    void onOpenSyncFolder() {
        if (!m_currentProfile) {
            m_logWidget->logWarning("No profile loaded");
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_syncPath));
    }

    void onSyncStarted() {
        statusBar()->showMessage("Syncing...");
        // Could disable menu items during sync
    }

    void onSyncFinished(const Sync::SyncResult &result) {
        Q_UNUSED(result)
        statusBar()->showMessage("Sync complete");
    }

    void onSyncProgress(int current, int total, const QString &message) {
        statusBar()->showMessage(QString("[%1/%2] %3").arg(current).arg(total).arg(message));
    }

    void showSyncResult(const Sync::SyncResult &result, const QString &operationName) {
        int totalRecords = result.palmStats.total() + result.pcStats.total();
        int errorCount = result.palmStats.errors + result.pcStats.errors;

        QString summary = QString("%1 Complete!\n\n"
                                  "Palm: %2\n"
                                  "PC: %3\n"
                                  "Errors: %4")
            .arg(operationName)
            .arg(result.palmStats.summary())
            .arg(result.pcStats.summary())
            .arg(errorCount);

        if (result.success && errorCount == 0) {
            QMessageBox::information(this, operationName + " Complete", summary);
        } else {
            if (!result.errorMessage.isEmpty()) {
                summary += "\n\nError: " + result.errorMessage;
            }
            for (const auto &warning : result.warnings) {
                if (warning.severity == Sync::WarningSeverity::Error) {
                    summary += "\n• " + warning.message;
                }
            }
            QMessageBox::warning(this, operationName + " Complete", summary);
        }

        m_logWidget->logInfo(QString("=== %1 Complete: %2 records, %3 errors ===")
            .arg(operationName)
            .arg(totalRecords)
            .arg(errorCount));
    }
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
