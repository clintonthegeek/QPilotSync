#include "mainwindow.h"
#include "logwidget.h"
#include "exporthandler.h"
#include "importhandler.h"

#include "../qpilotsync_version.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/devicesession.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../settings.h"
#include "../settingsdialog.h"
#include "../profile.h"

#include "../sync/syncengine.h"
#include "../sync/synctypes.h"
#include "../sync/localfilebackend.h"
#include "../sync/conduits/memoconduit.h"
#include "../sync/conduits/contactconduit.h"
#include "../sync/conduits/calendarconduit.h"
#include "../sync/conduits/todoconduit.h"
#include "../sync/conduits/installconduit.h"
#include "../sync/conduits/webcalendarconduit.h"

#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
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
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QStyle>
#include <QCloseEvent>
#include <cstring>

#include <pi-dlp.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_session(nullptr)
    , m_deviceLink(nullptr)
    , m_syncEngine(nullptr)
    , m_installConduit(nullptr)
    , m_exportHandler(nullptr)
    , m_importHandler(nullptr)
    , m_currentProfile(nullptr)
{
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

    // Create export/import handlers
    m_exportHandler = new ExportHandler(this);
    m_exportHandler->setLogWidget(m_logWidget);

    m_importHandler = new ImportHandler(this);
    m_importHandler->setLogWidget(m_logWidget);

    // Initialize sync engine
    initializeSyncEngine();

    // Create menu bar and toolbar
    createMenus();
    createToolBar();

    // Status bar
    statusBar()->showMessage("Ready - No device connected");

    // Log initial message
    m_logWidget->logInfo("QPilotSync " + QString(QPILOTSYNC_VERSION_STRING) + " initialized");

    // Load default profile if set
    QString defaultProfile = Settings::instance().defaultProfilePath();
    if (!defaultProfile.isEmpty() && QDir(defaultProfile).exists()) {
        loadProfile(defaultProfile);
    } else {
        m_logWidget->logInfo("No default profile set. Use File → New Profile to create one.");
    }

    // Initialize menu state
    updateMenuState(false);
    updateProfileMenuState();
}

MainWindow::~MainWindow()
{
    delete m_session;  // DeviceSession handles disconnection
    m_deviceLink = nullptr;  // Owned by DeviceSession
    delete m_syncEngine;
    delete m_currentProfile;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save window geometry
    Settings::instance().setWindowGeometry(saveGeometry());
    Settings::instance().sync();

    // Close profile
    if (m_currentProfile) {
        m_currentProfile->save();
    }

    event->accept();
}

// ========== Device Connection ==========

void MainWindow::onConnectDevice()
{
    // If already listening, stop listening (toggle behavior)
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    // Create dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Connect to Palm Device");
    dialog.setMinimumWidth(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // Info label
    QLabel *infoLabel = new QLabel(
        "Enter the device path and press Connect.\n"
        "Press the HotSync button on your Palm - QPilotSync will wait for it.");
    layout->addWidget(infoLabel);

    // Form layout for settings
    QFormLayout *formLayout = new QFormLayout();

    // Device path with common options
    QComboBox *deviceCombo = new QComboBox();
    deviceCombo->setEditable(true);
    deviceCombo->addItem("/dev/ttyUSB0");
    deviceCombo->addItem("/dev/ttyUSB1");
    deviceCombo->addItem("/dev/pilot");
    deviceCombo->addItem("usb:");

    // Use profile's device path if available, otherwise last used
    if (m_currentProfile && !m_currentProfile->devicePath().isEmpty()) {
        deviceCombo->setCurrentText(m_currentProfile->devicePath());
    } else if (!m_lastUsedDevicePath.isEmpty()) {
        deviceCombo->setCurrentText(m_lastUsedDevicePath);
    }

    formLayout->addRow("Device:", deviceCombo);

    // Baud rate
    QComboBox *baudCombo = new QComboBox();
    baudCombo->addItem("115200");
    baudCombo->addItem("57600");
    baudCombo->addItem("38400");
    baudCombo->addItem("19200");
    baudCombo->addItem("9600");

    if (m_currentProfile && !m_currentProfile->baudRate().isEmpty()) {
        baudCombo->setCurrentText(m_currentProfile->baudRate());
    } else if (!m_lastUsedBaudRate.isEmpty()) {
        baudCombo->setCurrentText(m_lastUsedBaudRate);
    }

    formLayout->addRow("Baud Rate:", baudCombo);

    layout->addLayout(formLayout);

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setText("Connect");
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QString devicePath = deviceCombo->currentText();
    QString baudRate = baudCombo->currentText();

    // Store for next time
    m_lastUsedDevicePath = devicePath;
    m_lastUsedBaudRate = baudRate;

    // Update profile if loaded
    if (m_currentProfile) {
        m_currentProfile->setDevicePath(devicePath);
        m_currentProfile->setBaudRate(baudRate);
        m_currentProfile->save();
    }

    // Check if device exists (skip check for "usb:" protocol)
    bool deviceExists = devicePath.startsWith("usb:") || QFile::exists(devicePath);

    if (deviceExists) {
        // Device exists - connect immediately
        startConnection(devicePath);
    } else {
        // Device doesn't exist - enter listening mode
        startListening(devicePath);
    }
}

void MainWindow::startListening(const QString &devicePath)
{
    m_listeningForDevice = true;
    m_listeningDevicePath = devicePath;

    m_logWidget->logInfo(QString("Waiting for HotSync on %1...").arg(devicePath));
    m_logWidget->logInfo("Press the HotSync button on your Palm device.");
    statusBar()->showMessage(QString("Waiting for HotSync on %1...").arg(devicePath));

    // Create poll timer if needed
    if (!m_devicePollTimer) {
        m_devicePollTimer = new QTimer(this);
        connect(m_devicePollTimer, &QTimer::timeout, this, &MainWindow::onDevicePoll);
    }

    // Poll every 500ms for device appearance
    m_devicePollTimer->start(500);

    // Update UI
    m_cancelConnectionAction->setEnabled(true);
    updateMenuState(false);
}

void MainWindow::stopListening()
{
    if (!m_listeningForDevice) {
        return;
    }

    m_listeningForDevice = false;
    m_listeningDevicePath.clear();

    if (m_devicePollTimer) {
        m_devicePollTimer->stop();
    }

    m_logWidget->logInfo("Stopped listening for HotSync");
    statusBar()->showMessage("Ready");

    m_cancelConnectionAction->setEnabled(false);
    updateMenuState(false);
}

void MainWindow::onDevicePoll()
{
    if (!m_listeningForDevice || m_listeningDevicePath.isEmpty()) {
        return;
    }

    // Check if device appeared
    if (QFile::exists(m_listeningDevicePath)) {
        m_logWidget->logInfo(QString("Device %1 detected!").arg(m_listeningDevicePath));

        // Stop polling
        m_devicePollTimer->stop();
        m_listeningForDevice = false;

        QString devicePath = m_listeningDevicePath;
        m_listeningDevicePath.clear();

        // Connect to the device
        startConnection(devicePath);
    }
}

void MainWindow::startConnection(const QString &devicePath)
{
    // Clean up old session
    if (m_session) {
        delete m_session;
        m_session = nullptr;
        m_deviceLink = nullptr;
    }

    // Create new device session
    m_session = new DeviceSession(this);

    // Apply profile settings to session
    if (m_currentProfile) {
        m_session->setConnectionMode(m_currentProfile->connectionMode());
    }

    // Connect DeviceSession signals
    connect(m_session, &DeviceSession::connectionComplete,
            this, &MainWindow::onConnectionComplete);
    connect(m_session, &DeviceSession::deviceReady,
            this, &MainWindow::onDeviceReady);
    connect(m_session, &DeviceSession::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_session, &DeviceSession::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_session, &DeviceSession::progressUpdated,
            this, &MainWindow::onSyncProgress);
    connect(m_session, &DeviceSession::palmScreenMessage,
            this, &MainWindow::onSessionPalmScreen);
    connect(m_session, &DeviceSession::installFinished,
            this, &MainWindow::onInstallFinished);
    connect(m_session, &DeviceSession::syncFinished,
            this, [this](bool success, const QString &summary) {
                Q_UNUSED(summary);
                statusBar()->showMessage(success ? "Sync complete" : "Sync failed");
            });
    connect(m_session, &DeviceSession::syncResultReady,
            this, &MainWindow::onAsyncSyncResult);
    connect(m_session, &DeviceSession::disconnected,
            this, [this]() {
                m_deviceLink = nullptr;
                m_exportHandler->setDeviceLink(nullptr);
                m_importHandler->setDeviceLink(nullptr);
                updateMenuState(false);
                statusBar()->showMessage("Disconnected");
            });
    connect(m_session, &DeviceSession::readyForSync,
            this, &MainWindow::onReadyForSync);

    m_logWidget->logInfo(QString("Connecting to %1...").arg(devicePath));
    statusBar()->showMessage(QString("Connecting to %1...").arg(devicePath));

    // Start async connection
    m_session->connectDevice(devicePath);

    // Connection attempt started - update menu state
    updateMenuState(false);
    m_cancelConnectionAction->setEnabled(true);
}

void MainWindow::onConnectionComplete(bool success)
{
    m_cancelConnectionAction->setEnabled(false);

    if (!success) {
        m_logWidget->logError("Connection failed or was cancelled");
        statusBar()->showMessage("Connection failed");
        updateMenuState(false);
        return;
    }

    m_logWidget->logInfo("Connection established!");
    statusBar()->showMessage("Connected");

    // Get device link from session for handlers
    m_deviceLink = m_session->deviceLink();

    // Update handlers with device link
    m_exportHandler->setDeviceLink(m_deviceLink);
    m_importHandler->setDeviceLink(m_deviceLink);

    // Read user info to identify device
    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (m_deviceLink->readUserInfo(user)) {
        QString userName = QString::fromLatin1(user.username);
        quint32 userId = user.userID;

        m_logWidget->logInfo(QString("User: %1 (ID: %2)").arg(userName).arg(userId));

        // Create device fingerprint
        DeviceFingerprint connectedDevice;
        connectedDevice.userId = userId;
        connectedDevice.userName = userName;

        // Check if this device matches expected profile
        if (m_currentProfile) {
            if (!handleDeviceFingerprint(connectedDevice)) {
                // User chose to abort
                m_deviceLink->closeConnection();
                updateMenuState(false);
                return;
            }
        } else {
            // No profile - check if we know this device
            QString knownProfile = Settings::instance().findProfileForDevice(connectedDevice);
            if (!knownProfile.isEmpty()) {
                int ret = QMessageBox::question(this, "Known Device",
                    QString("This device (%1) is registered with profile:\n%2\n\nLoad that profile?")
                        .arg(connectedDevice.displayString())
                        .arg(QFileInfo(knownProfile).fileName()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

                if (ret == QMessageBox::Yes) {
                    loadProfile(knownProfile);
                }
            } else {
                m_logWidget->logWarning("No profile loaded. Device data won't be saved. Use File → New Profile.");
            }
        }

        // Handle first sync (no username set on device)
        if (userName.isEmpty() && userId == 0) {
            m_logWidget->logWarning("This appears to be an uninitialized device (no user info)");

            QString newUserName = QInputDialog::getText(this, "Initialize Device",
                "This Palm has no user info. Enter a username:",
                QLineEdit::Normal, "PalmUser");

            if (!newUserName.isEmpty()) {
                strncpy(user.username, newUserName.toLatin1().constData(), sizeof(user.username) - 1);
                user.userID = static_cast<unsigned long>(QDateTime::currentSecsSinceEpoch());

                if (m_deviceLink->writeUserInfo(user)) {
                    m_logWidget->logInfo(QString("Device initialized: %1 (ID: %2)")
                        .arg(newUserName).arg(user.userID));

                    // Register this new device with current profile
                    if (m_currentProfile) {
                        DeviceFingerprint newFp;
                        newFp.userId = user.userID;
                        newFp.userName = newUserName;
                        registerDeviceWithCurrentProfile(newFp);
                    }
                } else {
                    m_logWidget->logError("Failed to write user info to device");
                }
            }
        }
    }

    // Read system info
    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));

    if (m_deviceLink->readSysInfo(sysInfo)) {
        m_logWidget->logInfo(QString("Palm OS: %1.%2, Product ID: %3")
            .arg(sysInfo.romVersion >> 16)
            .arg((sysInfo.romVersion >> 8) & 0xFF)
            .arg(QString::fromLatin1(sysInfo.prodID)));
    }

    // Configure sync engine with device
    m_syncEngine->setDeviceLink(m_deviceLink);

    updateMenuState(true);
}

bool MainWindow::handleDeviceFingerprint(const DeviceFingerprint &connectedDevice)
{
    if (!m_currentProfile) return true;

    DeviceFingerprint expectedDevice = m_currentProfile->deviceFingerprint();

    // If profile has no registered device yet, register this one
    if (!expectedDevice.isValid()) {
        registerDeviceWithCurrentProfile(connectedDevice);
        return true;
    }

    // Check if connected device matches expected
    if (expectedDevice.matches(connectedDevice)) {
        m_logWidget->logInfo("Device fingerprint verified");
        return true;
    }

    // Mismatch! Warn user
    QString message = QString(
        "Device Mismatch!\n\n"
        "Expected: %1\n"
        "Connected: %2\n\n"
        "This profile is configured for a different Palm device.\n"
        "What would you like to do?")
        .arg(expectedDevice.displayString())
        .arg(connectedDevice.displayString());

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Wrong Device");
    msgBox.setText(message);
    msgBox.setIcon(QMessageBox::Warning);

    QPushButton *continueBtn = msgBox.addButton("Continue Anyway", QMessageBox::AcceptRole);
    QPushButton *switchBtn = msgBox.addButton("Switch Profile", QMessageBox::ActionRole);
    QPushButton *abortBtn = msgBox.addButton("Disconnect", QMessageBox::RejectRole);

    msgBox.exec();

    if (msgBox.clickedButton() == continueBtn) {
        m_logWidget->logWarning("User chose to continue with mismatched device");
        return true;
    } else if (msgBox.clickedButton() == switchBtn) {
        // Look up profile for this device
        QString profilePath = Settings::instance().findProfileForDevice(connectedDevice);
        if (!profilePath.isEmpty()) {
            m_logWidget->logInfo(QString("Switching to profile: %1").arg(profilePath));
            loadProfile(profilePath);
            return true;
        } else {
            QMessageBox::information(this, "No Profile Found",
                "No profile is registered for this device.\n"
                "You can create a new profile using File → New Profile.");
            return false;
        }
    } else {
        m_logWidget->logInfo("User chose to disconnect");
        return false;
    }
}

void MainWindow::registerDeviceWithCurrentProfile(const DeviceFingerprint &fingerprint)
{
    if (!m_currentProfile) return;

    m_currentProfile->setDeviceFingerprint(fingerprint);
    m_currentProfile->save();

    Settings::instance().registerDevice(fingerprint, m_currentProfile->syncFolderPath());
    Settings::instance().sync();

    m_logWidget->logInfo(QString("Device registered: %1").arg(fingerprint.displayString()));
}

void MainWindow::onDisconnectDevice()
{
    if (m_session && m_session->isConnected()) {
        m_logWidget->logInfo("Disconnecting...");

        // End sync if device link available
        if (m_deviceLink && m_deviceLink->isConnected()) {
            m_logWidget->logInfo("Ending sync session...");
            m_deviceLink->endSync();
        }

        m_session->disconnectDevice();
        m_deviceLink = nullptr;

        m_exportHandler->setDeviceLink(nullptr);
        m_importHandler->setDeviceLink(nullptr);

        statusBar()->showMessage("Disconnected");
        m_logWidget->logInfo("Disconnected from device");
    }
    updateMenuState(false);
}

void MainWindow::onCancelConnection()
{
    // Stop listening mode if active
    if (m_listeningForDevice) {
        stopListening();
        return;
    }

    // Cancel active connection
    if (m_session) {
        m_session->requestCancel();
        m_logWidget->logInfo("Connection cancelled by user");
    }
    m_cancelConnectionAction->setEnabled(false);
}

void MainWindow::onDeviceStatusChanged(int status)
{
    KPilotLink::LinkStatus linkStatus = static_cast<KPilotLink::LinkStatus>(status);
    switch (linkStatus) {
        case KPilotLink::LinkStatus::Init:
            statusBar()->showMessage("Initializing...");
            break;
        case KPilotLink::LinkStatus::WaitingForDevice:
            statusBar()->showMessage("Waiting for device...");
            break;
        case KPilotLink::LinkStatus::FoundDevice:
            statusBar()->showMessage("Device found");
            break;
        case KPilotLink::LinkStatus::CreatedSocket:
            statusBar()->showMessage("Creating connection...");
            break;
        case KPilotLink::LinkStatus::DeviceOpen:
            statusBar()->showMessage("Device open");
            break;
        case KPilotLink::LinkStatus::AcceptedDevice:
            statusBar()->showMessage("Connected");
            break;
        case KPilotLink::LinkStatus::SyncDone:
            statusBar()->showMessage("Sync complete");
            break;
        case KPilotLink::LinkStatus::PilotLinkError:
            statusBar()->showMessage("Error");
            break;
    }
}

void MainWindow::onDeviceReady(const QString &userName, const QString &deviceName)
{
    m_logWidget->logInfo(QString("Device ready: %1 (%2)").arg(userName).arg(deviceName));
}

void MainWindow::onReadyForSync()
{
    // Check if auto-sync is enabled
    if (!m_currentProfile || !m_currentProfile->autoSyncOnConnect()) {
        return;
    }

    m_logWidget->logInfo("Auto-sync enabled - starting sync...");

    // Determine sync type
    QString syncType = m_currentProfile->defaultSyncType();
    if (syncType == "fullsync") {
        onFullSync();
    } else {
        onHotSync();
    }
}

void MainWindow::onListDatabases()
{
    if (!m_deviceLink) {
        m_logWidget->logError("No device connected");
        return;
    }

    m_logWidget->logInfo("=== Database List ===");

    QStringList databases = m_deviceLink->listDatabases();

    if (databases.isEmpty()) {
        m_logWidget->logWarning("No databases found (or device disconnected)");
        return;
    }

    // Categorize and count
    QSet<QString> pimDatabases = {"MemoDB", "AddressDB", "DatebookDB", "ToDoDB"};
    QStringList pimList, otherList;

    for (const QString &db : databases) {
        if (pimDatabases.contains(db)) {
            int count = countDatabaseRecords(db);
            pimList << QString("%1 (%2 records)").arg(db).arg(count);
        } else {
            otherList << db;
        }
    }

    m_logWidget->logInfo("PIM Databases:");
    for (const QString &db : pimList) {
        m_logWidget->logInfo(QString("  • %1").arg(db));
    }

    m_logWidget->logInfo(QString("Other Databases: %1 total").arg(otherList.size()));
    for (const QString &db : otherList) {
        m_logWidget->logInfo(QString("  • %1").arg(db));
    }

    m_logWidget->logInfo(QString("=== Total: %1 databases ===").arg(databases.size()));
}

int MainWindow::countDatabaseRecords(const QString &dbName)
{
    if (!m_deviceLink) return 0;

    int dbHandle = m_deviceLink->openDatabase(dbName);
    if (dbHandle < 0) return 0;

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    int count = records.size();

    for (PilotRecord *r : records) {
        delete r;
    }

    m_deviceLink->closeDatabase(dbHandle);
    return count;
}

void MainWindow::onSetUserInfo()
{
    if (!m_deviceLink) {
        m_logWidget->logError("No device connected");
        return;
    }

    // Read current info
    struct PilotUser user;
    memset(&user, 0, sizeof(user));

    if (!m_deviceLink->readUserInfo(user)) {
        m_logWidget->logError("Failed to read current user info");
        return;
    }

    QString currentName = QString::fromLatin1(user.username);

    // Create dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Set User Info");
    dialog.setMinimumWidth(300);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QFormLayout *formLayout = new QFormLayout();

    QLineEdit *nameEdit = new QLineEdit(currentName);
    formLayout->addRow("Username:", nameEdit);

    QLabel *idLabel = new QLabel(QString::number(user.userID));
    formLayout->addRow("User ID:", idLabel);

    layout->addLayout(formLayout);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) return;

    QString newName = nameEdit->text().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(this, "Error", "Username cannot be empty");
        return;
    }

    // Update and write back
    strncpy(user.username, newName.toLatin1().constData(), sizeof(user.username) - 1);

    if (m_deviceLink->writeUserInfo(user)) {
        m_logWidget->logInfo(QString("User info updated: %1").arg(newName));
        QMessageBox::information(this, "Success", "User info updated successfully!");

        // Update device fingerprint if profile loaded
        if (m_currentProfile) {
            DeviceFingerprint fp;
            fp.userId = user.userID;
            fp.userName = newName;
            registerDeviceWithCurrentProfile(fp);
        }
    } else {
        m_logWidget->logError("Failed to write user info");
        QMessageBox::warning(this, "Error", "Failed to update user info on device");
    }
}

void MainWindow::onDeviceInfo()
{
    if (!m_deviceLink) {
        m_logWidget->logError("No device connected");
        return;
    }

    // Read user info
    struct PilotUser user;
    memset(&user, 0, sizeof(user));
    m_deviceLink->readUserInfo(user);

    // Read system info
    struct SysInfo sysInfo;
    memset(&sysInfo, 0, sizeof(sysInfo));
    m_deviceLink->readSysInfo(sysInfo);

    // Format version
    QString osVersion = QString("%1.%2.%3")
        .arg(sysInfo.romVersion >> 16)
        .arg((sysInfo.romVersion >> 8) & 0xFF)
        .arg(sysInfo.romVersion & 0xFF);

    // Count databases
    QStringList databases = m_deviceLink->listDatabases();

    // Build info dialog
    QString info = QString(
        "<h3>Device Information</h3>"
        "<table>"
        "<tr><td><b>Username:</b></td><td>%1</td></tr>"
        "<tr><td><b>User ID:</b></td><td>%2</td></tr>"
        "<tr><td><b>Palm OS:</b></td><td>%3</td></tr>"
        "<tr><td><b>Product ID:</b></td><td>0x%4</td></tr>"
        "<tr><td><b>Databases:</b></td><td>%5</td></tr>"
        "</table>")
        .arg(QString::fromLatin1(user.username))
        .arg(user.userID)
        .arg(osVersion)
        .arg(QString::fromLatin1(sysInfo.prodID))
        .arg(databases.size());

    // Add PIM counts
    info += "<h4>PIM Databases</h4><ul>";
    QStringList pimDbs = {"MemoDB", "AddressDB", "DatebookDB", "ToDoDB"};
    for (const QString &db : pimDbs) {
        int count = countDatabaseRecords(db);
        info += QString("<li>%1: %2 records</li>").arg(db).arg(count);
    }
    info += "</ul>";

    QMessageBox::information(this, "Device Information", info);
}

// ========== Sync Engine ==========

void MainWindow::initializeSyncEngine()
{
    m_syncEngine = new Sync::SyncEngine(this);

    // Register conduits
    m_syncEngine->registerConduit(new Sync::MemoConduit());
    m_syncEngine->registerConduit(new Sync::ContactConduit());
    m_syncEngine->registerConduit(new Sync::CalendarConduit());
    m_syncEngine->registerConduit(new Sync::TodoConduit());
    m_syncEngine->registerConduit(new Sync::WebCalendarConduit());

    // Create install conduit (handled separately)
    m_installConduit = new Sync::InstallConduit(this);
    connect(m_installConduit, &Sync::InstallConduit::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_installConduit, &Sync::InstallConduit::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_installConduit, &Sync::InstallConduit::progressUpdated,
            this, [this](int current, int total, const QString &fileName) {
                statusBar()->showMessage(QString("Installing %1 (%2/%3)")
                    .arg(fileName).arg(current).arg(total));
            });

    // Connect sync engine signals
    connect(m_syncEngine, &Sync::SyncEngine::logMessage,
            m_logWidget, &LogWidget::logInfo);
    connect(m_syncEngine, &Sync::SyncEngine::errorOccurred,
            m_logWidget, &LogWidget::logError);
    connect(m_syncEngine, &Sync::SyncEngine::syncStarted,
            this, &MainWindow::onSyncStarted);
    connect(m_syncEngine, &Sync::SyncEngine::syncFinished,
            this, &MainWindow::onSyncFinished);
    connect(m_syncEngine, &Sync::SyncEngine::progressUpdated,
            this, &MainWindow::onSyncProgress);
}

void MainWindow::runInstallConduit()
{
    if (!m_installConduit || !m_session || !m_session->isConnected()) {
        return;
    }

    if (!m_installConduit->hasPendingFiles()) {
        return;
    }

    m_logWidget->logInfo("--- Installing pending files ---");

    int socket = m_deviceLink->socketDescriptor();
    QList<Sync::InstallResult> results = m_installConduit->installAll(socket);

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

void MainWindow::showWebCalendarSettings(QWidget *parent)
{
    // Get the WebCalendarConduit from the sync engine
    Sync::Conduit *conduit = m_syncEngine->conduit("webcalendar");
    Sync::WebCalendarConduit *webCal = dynamic_cast<Sync::WebCalendarConduit*>(conduit);
    if (!webCal) {
        QMessageBox::warning(parent, "Error", "WebCalendarConduit not found");
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle("Web Calendar Settings");
    dialog.setMinimumWidth(500);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // Feeds list
    QGroupBox *feedsGroup = new QGroupBox("Calendar Feeds");
    QVBoxLayout *feedsLayout = new QVBoxLayout(feedsGroup);

    QListWidget *feedsList = new QListWidget();
    feedsList->setMinimumHeight(120);

    // Load existing feeds
    QList<Sync::WebCalendarFeed> feeds = webCal->feeds();
    for (const Sync::WebCalendarFeed &feed : feeds) {
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1 - %2").arg(feed.name).arg(feed.url.toString()));
        item->setData(Qt::UserRole, feed.url.toString());
        item->setData(Qt::UserRole + 1, feed.name);
        item->setData(Qt::UserRole + 2, feed.category);
        item->setCheckState(feed.enabled ? Qt::Checked : Qt::Unchecked);
        feedsList->addItem(item);
    }

    feedsLayout->addWidget(feedsList);

    // Add/Remove buttons
    QHBoxLayout *feedButtonsLayout = new QHBoxLayout();
    QPushButton *addBtn = new QPushButton("Add...");
    QPushButton *removeBtn = new QPushButton("Remove");
    feedButtonsLayout->addWidget(addBtn);
    feedButtonsLayout->addWidget(removeBtn);
    feedButtonsLayout->addStretch();
    feedsLayout->addLayout(feedButtonsLayout);

    layout->addWidget(feedsGroup);

    // Fetch settings
    QGroupBox *fetchGroup = new QGroupBox("Fetch Schedule");
    QFormLayout *fetchLayout = new QFormLayout(fetchGroup);

    QComboBox *intervalCombo = new QComboBox();
    intervalCombo->addItem("Every HotSync", "every_sync");
    intervalCombo->addItem("Daily", "daily");
    intervalCombo->addItem("Weekly", "weekly");
    intervalCombo->addItem("Monthly", "monthly");

    // Set current interval
    int intervalIdx = static_cast<int>(webCal->fetchInterval());
    intervalCombo->setCurrentIndex(intervalIdx);
    fetchLayout->addRow("Fetch Frequency:", intervalCombo);

    layout->addWidget(fetchGroup);

    // Import options
    QGroupBox *importGroup = new QGroupBox("Import Options");
    QFormLayout *importLayout = new QFormLayout(importGroup);

    QComboBox *dateFilterCombo = new QComboBox();
    dateFilterCombo->addItem("All events", "all");
    dateFilterCombo->addItem("Recurring + future events (Recommended)", "recurring_and_future");
    dateFilterCombo->addItem("Future events only", "future");

    // Set current date filter (read from saved settings)
    QJsonObject currentSettings = webCal->saveSettings();
    QString currentFilter = currentSettings["date_filter"].toString("recurring_and_future");
    int filterIdx = dateFilterCombo->findData(currentFilter);
    if (filterIdx >= 0) {
        dateFilterCombo->setCurrentIndex(filterIdx);
    } else {
        dateFilterCombo->setCurrentIndex(1);  // Default to recurring_and_future
    }
    importLayout->addRow("Date Filter:", dateFilterCombo);

    layout->addWidget(importGroup);

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    // Add feed button handler
    connect(addBtn, &QPushButton::clicked, [&]() {
        QDialog addDialog(&dialog);
        addDialog.setWindowTitle("Add Calendar Feed");
        QFormLayout *addLayout = new QFormLayout(&addDialog);

        QLineEdit *nameEdit = new QLineEdit();
        QLineEdit *urlEdit = new QLineEdit();
        urlEdit->setPlaceholderText("https://example.com/calendar.ics");

        addLayout->addRow("Name:", nameEdit);
        addLayout->addRow("URL:", urlEdit);

        QDialogButtonBox *addButtons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(addButtons, &QDialogButtonBox::accepted, &addDialog, &QDialog::accept);
        connect(addButtons, &QDialogButtonBox::rejected, &addDialog, &QDialog::reject);
        addLayout->addWidget(addButtons);

        if (addDialog.exec() == QDialog::Accepted && !urlEdit->text().isEmpty()) {
            QString name = nameEdit->text();
            if (name.isEmpty()) {
                name = "Calendar";
            }
            QListWidgetItem *item = new QListWidgetItem(
                QString("%1 - %2").arg(name).arg(urlEdit->text()));
            item->setData(Qt::UserRole, urlEdit->text());
            item->setData(Qt::UserRole + 1, name);
            item->setData(Qt::UserRole + 2, QString());  // category
            item->setCheckState(Qt::Checked);
            feedsList->addItem(item);
        }
    });

    // Remove feed button handler
    connect(removeBtn, &QPushButton::clicked, [&]() {
        QListWidgetItem *item = feedsList->currentItem();
        if (item) {
            delete feedsList->takeItem(feedsList->row(item));
        }
    });

    if (dialog.exec() == QDialog::Accepted) {
        // Save feeds
        QList<Sync::WebCalendarFeed> newFeeds;
        for (int i = 0; i < feedsList->count(); ++i) {
            QListWidgetItem *item = feedsList->item(i);
            Sync::WebCalendarFeed feed;
            feed.url = QUrl(item->data(Qt::UserRole).toString());
            feed.name = item->data(Qt::UserRole + 1).toString();
            feed.category = item->data(Qt::UserRole + 2).toString();
            feed.enabled = item->checkState() == Qt::Checked;
            newFeeds.append(feed);
        }
        webCal->setFeeds(newFeeds);

        // Save fetch interval
        QString intervalStr = intervalCombo->currentData().toString();
        if (intervalStr == "every_sync") {
            webCal->setFetchInterval(Sync::FetchInterval::EverySync);
        } else if (intervalStr == "daily") {
            webCal->setFetchInterval(Sync::FetchInterval::Daily);
        } else if (intervalStr == "weekly") {
            webCal->setFetchInterval(Sync::FetchInterval::Weekly);
        } else if (intervalStr == "monthly") {
            webCal->setFetchInterval(Sync::FetchInterval::Monthly);
        }

        // Save date filter
        QString filterStr = dateFilterCombo->currentData().toString();
        if (filterStr == "all") {
            webCal->setDateFilter(Sync::WebCalendarConduit::DateFilter::All);
        } else if (filterStr == "recurring_and_future") {
            webCal->setDateFilter(Sync::WebCalendarConduit::DateFilter::RecurringAndFuture);
        } else if (filterStr == "future") {
            webCal->setDateFilter(Sync::WebCalendarConduit::DateFilter::FutureOnly);
        }

        // Save to profile
        if (m_currentProfile) {
            m_currentProfile->setConduitSettings("webcalendar", webCal->saveSettings());
            m_currentProfile->save();
        }

        m_logWidget->logInfo("Web calendar settings saved");
    }
}

// ========== Profile Management ==========

void MainWindow::loadProfile(const QString &path)
{
    // Clean up old profile
    if (m_currentProfile) {
        m_currentProfile->save();
        delete m_currentProfile;
        m_currentProfile = nullptr;
    }

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

    if (isNewProfile) {
        m_currentProfile->initialize();
        m_logWidget->logInfo(QString("Created new profile at: %1").arg(path));

        if (!m_lastUsedDevicePath.isEmpty()) {
            m_currentProfile->setDevicePath(m_lastUsedDevicePath);
            m_currentProfile->setBaudRate(m_lastUsedBaudRate);
            m_currentProfile->save();
            m_logWidget->logInfo(QString("Using device settings: %1 at %2 bps")
                .arg(m_lastUsedDevicePath).arg(m_lastUsedBaudRate));
        }
    }

    m_syncPath = path;

    // Configure sync engine
    m_syncEngine->setStateDirectory(m_currentProfile->stateDirectoryPath());

    Sync::LocalFileBackend *backend = new Sync::LocalFileBackend(m_syncPath);
    m_syncEngine->setBackend(backend);

    // Apply profile's conduit enabled settings to sync engine
    for (const QString &conduitId : m_syncEngine->registeredConduits()) {
        m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));

        // Load conduit-specific settings
        QJsonObject conduitSettings = m_currentProfile->conduitSettings(conduitId);
        if (!conduitSettings.isEmpty()) {
            Sync::Conduit *conduit = m_syncEngine->conduit(conduitId);
            if (conduit) {
                conduit->loadSettings(conduitSettings);
            }
        }
    }

    // Apply connection mode to session
    if (m_session) {
        m_session->setConnectionMode(m_currentProfile->connectionMode());
    }

    // Configure install conduit
    m_installConduit->setInstallFolder(m_currentProfile->installFolderPath());

    // Add to recent profiles
    Settings::instance().addRecentProfile(path);

    // Auto-set as default if none exists
    if (Settings::instance().defaultProfilePath().isEmpty()) {
        Settings::instance().setDefaultProfilePath(path);
        m_logWidget->logInfo("Set as default profile");
    }

    Settings::instance().sync();

    updateWindowTitle();
    updateProfileMenuState();
    updateRecentProfilesMenu();

    m_logWidget->logInfo(QString("Loaded profile: %1").arg(m_currentProfile->name()));
    m_logWidget->logInfo(QString("Sync folder: %1").arg(m_syncPath));
}

void MainWindow::closeProfile()
{
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

void MainWindow::updateWindowTitle()
{
    QString title = "QPilotSync";
    if (m_currentProfile) {
        title += " - " + m_currentProfile->name();
    }
    setWindowTitle(title);
}

void MainWindow::updateProfileMenuState()
{
    bool hasProfile = m_currentProfile != nullptr;
    m_closeProfileAction->setEnabled(hasProfile);
    m_profileSettingsAction->setEnabled(hasProfile);
}

void MainWindow::updateRecentProfilesMenu()
{
    m_recentProfilesMenu->clear();

    QStringList recent = Settings::instance().recentProfiles();

    if (recent.isEmpty()) {
        QAction *emptyAction = m_recentProfilesMenu->addAction("(No recent profiles)");
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString &path : recent) {
        QFileInfo info(path);
        QAction *action = m_recentProfilesMenu->addAction(info.fileName());
        action->setData(path);
        action->setToolTip(path);

        connect(action, &QAction::triggered, this, [this, path]() {
            loadProfile(path);
        });
    }

    m_recentProfilesMenu->addSeparator();
    QAction *clearAction = m_recentProfilesMenu->addAction("Clear Recent");
    connect(clearAction, &QAction::triggered, this, [this]() {
        Settings::instance().clearRecentProfiles();
        updateRecentProfilesMenu();
    });
}

void MainWindow::onNewProfile()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Folder for New Profile",
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);

    if (path.isEmpty()) return;

    loadProfile(path);
}

void MainWindow::onOpenProfile()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Open Profile Folder",
        QDir::homePath(),
        QFileDialog::ShowDirsOnly);

    if (path.isEmpty()) return;

    loadProfile(path);
}

void MainWindow::onCloseProfile()
{
    closeProfile();
}

void MainWindow::onProfileSettings()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning("No profile loaded");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Profile Settings - " + m_currentProfile->name());
    dialog.setMinimumWidth(450);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    // Device group
    QGroupBox *deviceGroup = new QGroupBox("Device");
    QFormLayout *deviceLayout = new QFormLayout(deviceGroup);

    QComboBox *deviceCombo = new QComboBox();
    deviceCombo->setEditable(true);
    deviceCombo->addItem("/dev/ttyUSB0");
    deviceCombo->addItem("/dev/ttyUSB1");
    deviceCombo->addItem("/dev/pilot");
    deviceCombo->addItem("usb:");
    deviceCombo->setCurrentText(m_currentProfile->devicePath());
    deviceLayout->addRow("Device Path:", deviceCombo);

    QComboBox *baudCombo = new QComboBox();
    baudCombo->addItem("115200");
    baudCombo->addItem("57600");
    baudCombo->addItem("38400");
    baudCombo->addItem("19200");
    baudCombo->addItem("9600");
    baudCombo->setCurrentText(m_currentProfile->baudRate());
    deviceLayout->addRow("Baud Rate:", baudCombo);

    DeviceFingerprint fp = m_currentProfile->deviceFingerprint();
    QLabel *deviceLabel = new QLabel(fp.isValid() ? fp.displayString() : "(No device registered)");
    deviceLayout->addRow("Registered Device:", deviceLabel);

    layout->addWidget(deviceGroup);

    // Connection group
    QGroupBox *connectionGroup = new QGroupBox("Connection");
    QFormLayout *connectionLayout = new QFormLayout(connectionGroup);

    QCheckBox *autoSyncCheck = new QCheckBox("Automatically sync after connecting");
    autoSyncCheck->setChecked(m_currentProfile->autoSyncOnConnect());
    connectionLayout->addRow("", autoSyncCheck);

    QComboBox *syncTypeCombo = new QComboBox();
    syncTypeCombo->addItem("HotSync (sync changes)", "hotsync");
    syncTypeCombo->addItem("Full Sync (copy all)", "fullsync");
    syncTypeCombo->setCurrentIndex(m_currentProfile->defaultSyncType() == "fullsync" ? 1 : 0);
    connectionLayout->addRow("Sync type:", syncTypeCombo);

    QComboBox *connectionModeCombo = new QComboBox();
    connectionModeCombo->addItem("Keep connection alive", static_cast<int>(ConnectionMode::KeepAlive));
    connectionModeCombo->addItem("Disconnect after sync", static_cast<int>(ConnectionMode::DisconnectAfterSync));
    connectionModeCombo->setCurrentIndex(
        m_currentProfile->connectionMode() == ConnectionMode::KeepAlive ? 0 : 1);
    connectionLayout->addRow("After sync:", connectionModeCombo);

    QLabel *connectionHint = new QLabel(
        "<small>For traditional HotSync experience: enable auto-sync and disconnect after sync.</small>");
    connectionHint->setWordWrap(true);
    connectionLayout->addRow("", connectionHint);

    layout->addWidget(connectionGroup);

    // Conduits group
    QGroupBox *conduitsGroup = new QGroupBox("Conduits");
    QVBoxLayout *conduitsLayout = new QVBoxLayout(conduitsGroup);

    QCheckBox *memosCheck = new QCheckBox("Memos");
    memosCheck->setChecked(m_currentProfile->conduitEnabled("memos"));
    conduitsLayout->addWidget(memosCheck);

    QCheckBox *contactsCheck = new QCheckBox("Contacts");
    contactsCheck->setChecked(m_currentProfile->conduitEnabled("contacts"));
    conduitsLayout->addWidget(contactsCheck);

    QCheckBox *calendarCheck = new QCheckBox("Calendar");
    calendarCheck->setChecked(m_currentProfile->conduitEnabled("calendar"));
    conduitsLayout->addWidget(calendarCheck);

    QCheckBox *todosCheck = new QCheckBox("Todos");
    todosCheck->setChecked(m_currentProfile->conduitEnabled("todos"));
    conduitsLayout->addWidget(todosCheck);

    // Web Calendar conduit with settings button
    QHBoxLayout *webCalLayout = new QHBoxLayout();
    QCheckBox *webCalCheck = new QCheckBox("Web Calendar Subscriptions");
    webCalCheck->setChecked(m_currentProfile->conduitEnabled("webcalendar"));
    webCalLayout->addWidget(webCalCheck);
    QPushButton *webCalSettingsBtn = new QPushButton("Settings...");
    webCalSettingsBtn->setMaximumWidth(80);
    webCalLayout->addWidget(webCalSettingsBtn);
    webCalLayout->addStretch();
    conduitsLayout->addLayout(webCalLayout);

    // Connect settings button
    connect(webCalSettingsBtn, &QPushButton::clicked, this, [this, &dialog]() {
        showWebCalendarSettings(&dialog);
    });

    layout->addWidget(conduitsGroup);

    // Sync folder info
    QGroupBox *infoGroup = new QGroupBox("Sync Folder");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);

    QLabel *pathLabel = new QLabel(QString("<code>%1</code>").arg(m_currentProfile->syncFolderPath()));
    pathLabel->setTextFormat(Qt::RichText);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(pathLabel);

    layout->addWidget(infoGroup);

    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        m_currentProfile->setDevicePath(deviceCombo->currentText());
        m_currentProfile->setBaudRate(baudCombo->currentText());
        m_currentProfile->setAutoSyncOnConnect(autoSyncCheck->isChecked());
        m_currentProfile->setDefaultSyncType(syncTypeCombo->currentData().toString());
        m_currentProfile->setConnectionMode(
            static_cast<ConnectionMode>(connectionModeCombo->currentData().toInt()));
        m_currentProfile->setConduitEnabled("memos", memosCheck->isChecked());
        m_currentProfile->setConduitEnabled("contacts", contactsCheck->isChecked());
        m_currentProfile->setConduitEnabled("calendar", calendarCheck->isChecked());
        m_currentProfile->setConduitEnabled("todos", todosCheck->isChecked());
        m_currentProfile->setConduitEnabled("webcalendar", webCalCheck->isChecked());
        m_currentProfile->save();

        // Update sync engine with new conduit settings
        for (const QString &conduitId : m_syncEngine->registeredConduits()) {
            m_syncEngine->setConduitEnabled(conduitId, m_currentProfile->conduitEnabled(conduitId));
        }

        // Update session connection mode
        if (m_session) {
            m_session->setConnectionMode(m_currentProfile->connectionMode());
        }

        updateWindowTitle();
        m_logWidget->logInfo("Profile settings saved");
    }
}

// ========== Sync Operations ==========

void MainWindow::onHotSync()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError("No device connected");
        return;
    }

    m_logWidget->logInfo("=== Starting HotSync ===");
    runInstallConduit();

    m_pendingSyncOperationName = "HotSync";
    m_session->requestSync(Sync::SyncMode::HotSync, m_syncEngine);
}

void MainWindow::onFullSync()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError("No device connected");
        return;
    }

    int ret = QMessageBox::question(this, "Full Sync",
        "Full Sync will compare all records on both sides.\n"
        "This may take longer than HotSync.\n\nProceed?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo("=== Starting Full Sync ===");
    runInstallConduit();

    m_pendingSyncOperationName = "Full Sync";
    m_session->requestSync(Sync::SyncMode::FullSync, m_syncEngine);
}

void MainWindow::onCopyPalmToPC()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError("No device connected");
        return;
    }

    int ret = QMessageBox::warning(this, "Copy Palm → PC",
        "This will overwrite PC data with Palm data.\n"
        "Any changes on the PC will be lost.\n\nProceed?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo("=== Copying Palm → PC ===");
    m_pendingSyncOperationName = "Copy Palm → PC";
    m_session->requestSync(Sync::SyncMode::CopyPalmToPC, m_syncEngine);
}

void MainWindow::onCopyPCToPalm()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError("No device connected");
        return;
    }

    int ret = QMessageBox::warning(this, "Copy PC → Palm",
        "This will overwrite Palm data with PC data.\n"
        "Any changes on the Palm will be lost.\n\nProceed?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo("=== Copying PC → Palm ===");
    runInstallConduit();

    m_pendingSyncOperationName = "Copy PC → Palm";
    m_session->requestSync(Sync::SyncMode::CopyPCToPalm, m_syncEngine);
}

void MainWindow::onBackup()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
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
    m_pendingSyncOperationName = "Backup";
    m_session->requestSync(Sync::SyncMode::Backup, m_syncEngine);
}

void MainWindow::onRestore()
{
    if (!m_currentProfile) {
        m_logWidget->logError("No profile loaded. Use File → Open Profile first.");
        return;
    }
    if (!m_session || !m_session->isConnected()) {
        m_logWidget->logError("No device connected");
        return;
    }

    int ret = QMessageBox::warning(this, "Restore PC → Palm",
        "FULL RESTORE\n\n"
        "This will completely overwrite your Palm with PC backup data.\n"
        "Palm records not in the backup WILL BE DELETED.\n\n"
        "Are you sure you want to restore?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    m_logWidget->logInfo("=== Restoring PC → Palm ===");
    runInstallConduit();

    m_pendingSyncOperationName = "Restore";
    m_session->requestSync(Sync::SyncMode::Restore, m_syncEngine);
}

void MainWindow::onChangeSyncFolder()
{
    onOpenProfile();
}

void MainWindow::onOpenSyncFolder()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning("No profile loaded");
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_syncPath));
}

void MainWindow::onInstallFiles()
{
    if (!m_currentProfile) {
        m_logWidget->logWarning("No profile loaded");
        return;
    }

    // Open file dialog for .prc/.pdb files
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select Palm Files to Install",
        QString(),
        "Palm Files (*.prc *.pdb *.PRC *.PDB);;All Files (*)");

    if (files.isEmpty()) {
        return;
    }

    bool connected = m_session && m_session->isConnected();

    if (connected) {
        // Install asynchronously via DeviceSession
        m_logWidget->logInfo("--- Installing files to Palm ---");
        statusBar()->showMessage("Installing files...");
        m_session->requestInstall(files);
        // Results will come via onInstallFinished() slot
    } else {
        // Copy to install folder for next sync
        QString installFolder = m_currentProfile->installFolderPath();
        QDir installDir(installFolder);
        if (!installDir.exists()) {
            installDir.mkpath(".");
        }

        int copiedCount = 0;
        int failCount = 0;

        for (const QString &filePath : files) {
            QFileInfo fileInfo(filePath);
            QString destPath = installDir.filePath(fileInfo.fileName());

            if (QFile::exists(destPath)) {
                // Remove existing file first
                QFile::remove(destPath);
            }

            if (QFile::copy(filePath, destPath)) {
                m_logWidget->logInfo(QString("Queued for install: %1").arg(fileInfo.fileName()));
                copiedCount++;
            } else {
                m_logWidget->logError(QString("Failed to copy %1 to install folder")
                    .arg(fileInfo.fileName()));
                failCount++;
            }
        }

        if (failCount == 0) {
            QMessageBox::information(this, "Files Queued",
                QString("%1 file(s) queued for installation.\n\n"
                        "They will be installed on the next HotSync.")
                    .arg(copiedCount));
        } else {
            QMessageBox::warning(this, "Files Queued",
                QString("%1 file(s) queued, %2 failed to copy.\nCheck the log for details.")
                    .arg(copiedCount).arg(failCount));
        }
    }
}

void MainWindow::onSyncStarted()
{
    statusBar()->showMessage("Syncing...");
}

void MainWindow::onSyncFinished(const Sync::SyncResult &result)
{
    Q_UNUSED(result)
    statusBar()->showMessage("Sync complete");
}

void MainWindow::onSyncProgress(int current, int total, const QString &message)
{
    statusBar()->showMessage(QString("[%1/%2] %3").arg(current).arg(total).arg(message));
}

void MainWindow::showSyncResult(const Sync::SyncResult &result, const QString &operationName)
{
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
                summary += "\n - " + warning.message;
            }
        }
        QMessageBox::warning(this, operationName + " Complete", summary);
    }

    m_logWidget->logInfo(QString("=== %1 Complete: %2 records, %3 errors ===")
        .arg(operationName).arg(totalRecords).arg(errorCount));
}

// ========== DeviceSession Callbacks ==========

void MainWindow::onSessionPalmScreen(const QString &message)
{
    // The Palm screen message has changed (e.g., "Syncing...", "Installing...")
    m_logWidget->logInfo(QString("[Palm Screen] %1").arg(message));
}

void MainWindow::onInstallFinished(bool success, int successCount, int failCount)
{
    statusBar()->showMessage("Install complete");

    m_logWidget->logInfo(QString("Installation complete: %1 succeeded, %2 failed")
        .arg(successCount).arg(failCount));

    if (success && failCount == 0) {
        QMessageBox::information(this, "Install Complete",
            QString("Successfully installed %1 file(s) to Palm.").arg(successCount));
    } else {
        QMessageBox::warning(this, "Install Complete",
            QString("Installed %1 file(s), %2 failed.\nCheck the log for details.")
                .arg(successCount).arg(failCount));
    }
}

void MainWindow::onAsyncSyncResult(const Sync::SyncResult &result)
{
    // Get the operation name that was pending
    QString operationName = m_pendingSyncOperationName;
    m_pendingSyncOperationName.clear();

    if (operationName.isEmpty()) {
        operationName = "Sync";
    }

    // Show the result dialog
    showSyncResult(result, operationName);
}

// ========== Misc ==========

void MainWindow::onQuit()
{
    QApplication::quit();
}

void MainWindow::onAbout()
{
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

void MainWindow::onSettings()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::settingsChanged, this, [this]() {
        m_logWidget->logInfo("Settings updated");
    });
    dialog.exec();
}

void MainWindow::onClearLog()
{
    if (m_logWidget) {
        m_logWidget->clear();
        m_logWidget->logInfo("Log cleared");
    }
}

// ========== UI Setup ==========

void MainWindow::createLogWindow()
{
    m_logWidget = new LogWidget();
    m_logSubWindow = m_mdiArea->addSubWindow(m_logWidget);
    m_logSubWindow->setWindowTitle("Log");
    m_logSubWindow->resize(800, 300);

    // Prevent the log window from being deleted when closed - just hide it
    m_logSubWindow->setAttribute(Qt::WA_DeleteOnClose, false);

    m_logSubWindow->show();
}

void MainWindow::showLogWindow()
{
    if (m_logSubWindow) {
        m_logSubWindow->show();
        m_logSubWindow->raise();
        m_logSubWindow->activateWindow();
    }
}

void MainWindow::updateMenuState(bool connected)
{
    m_disconnectAction->setEnabled(connected);
    m_toolbarDisconnectAction->setEnabled(connected);
    m_listDatabasesAction->setEnabled(connected);
    m_setUserInfoAction->setEnabled(connected);
    m_deviceInfoAction->setEnabled(connected);

    // Sync actions
    bool canSync = connected && m_currentProfile != nullptr;
    m_hotSyncAction->setEnabled(canSync);
    m_fullSyncAction->setEnabled(canSync);
    m_copyPalmToPCAction->setEnabled(canSync);
    m_copyPCToPalmAction->setEnabled(canSync);
    m_backupAction->setEnabled(canSync);
    m_restoreAction->setEnabled(canSync);

    m_toolbarHotSyncAction->setEnabled(canSync);
    m_toolbarFullSyncAction->setEnabled(canSync);
    m_toolbarBackupAction->setEnabled(canSync);
    m_toolbarRestoreAction->setEnabled(canSync);

    // Install files - enabled when profile is loaded, label changes based on connection
    bool hasProfile = m_currentProfile != nullptr;
    m_installFilesAction->setEnabled(hasProfile);
    if (connected) {
        m_installFilesAction->setText("Install Files Now...");
    } else {
        m_installFilesAction->setText("Install Files on Next Sync...");
    }

    // Export/Import
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

void MainWindow::createMenus()
{
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

    m_recentProfilesMenu = fileMenu->addMenu("Recent Profiles");
    m_recentProfilesMenu->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    updateRecentProfilesMenu();

    m_closeProfileAction = fileMenu->addAction("Close Profile");
    connect(m_closeProfileAction, &QAction::triggered, this, &MainWindow::onCloseProfile);

    fileMenu->addSeparator();

    m_profileSettingsAction = fileMenu->addAction("Profile Settings...");
    connect(m_profileSettingsAction, &QAction::triggered, this, &MainWindow::onProfileSettings);

    fileMenu->addSeparator();

    QAction *settingsAction = fileMenu->addAction("&Settings...");
    settingsAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onSettings);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);

    // Device menu
    QMenu *deviceMenu = menuBar()->addMenu("&Device");

    QAction *connectAction = deviceMenu->addAction("&Connect...");
    connectAction->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    connect(connectAction, &QAction::triggered, this, &MainWindow::onConnectDevice);

    m_disconnectAction = deviceMenu->addAction("&Disconnect");
    m_disconnectAction->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    connect(m_disconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectDevice);

    m_cancelConnectionAction = deviceMenu->addAction("Cancel Connection");
    m_cancelConnectionAction->setEnabled(false);
    connect(m_cancelConnectionAction, &QAction::triggered, this, &MainWindow::onCancelConnection);

    deviceMenu->addSeparator();

    m_listDatabasesAction = deviceMenu->addAction("List &Databases");
    connect(m_listDatabasesAction, &QAction::triggered, this, &MainWindow::onListDatabases);

    m_setUserInfoAction = deviceMenu->addAction("Set &User Info...");
    connect(m_setUserInfoAction, &QAction::triggered, this, &MainWindow::onSetUserInfo);

    m_deviceInfoAction = deviceMenu->addAction("Device &Info");
    connect(m_deviceInfoAction, &QAction::triggered, this, &MainWindow::onDeviceInfo);

    // Sync menu
    QMenu *syncMenu = menuBar()->addMenu("&Sync");

    m_hotSyncAction = syncMenu->addAction("&HotSync");
    m_hotSyncAction->setShortcut(QKeySequence("Ctrl+H"));
    m_hotSyncAction->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    connect(m_hotSyncAction, &QAction::triggered, this, &MainWindow::onHotSync);

    m_fullSyncAction = syncMenu->addAction("&Full Sync");
    m_fullSyncAction->setShortcut(QKeySequence("Ctrl+Shift+H"));
    connect(m_fullSyncAction, &QAction::triggered, this, &MainWindow::onFullSync);

    syncMenu->addSeparator();

    m_copyPalmToPCAction = syncMenu->addAction("Copy Palm → PC");
    connect(m_copyPalmToPCAction, &QAction::triggered, this, &MainWindow::onCopyPalmToPC);

    m_copyPCToPalmAction = syncMenu->addAction("Copy PC → Palm");
    connect(m_copyPCToPalmAction, &QAction::triggered, this, &MainWindow::onCopyPCToPalm);

    syncMenu->addSeparator();

    m_backupAction = syncMenu->addAction("&Backup (Palm → PC)");
    m_backupAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(m_backupAction, &QAction::triggered, this, &MainWindow::onBackup);

    m_restoreAction = syncMenu->addAction("&Restore (PC → Palm)");
    m_restoreAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(m_restoreAction, &QAction::triggered, this, &MainWindow::onRestore);

    syncMenu->addSeparator();

    m_changeSyncFolderAction = syncMenu->addAction("Change Sync &Folder...");
    connect(m_changeSyncFolderAction, &QAction::triggered, this, &MainWindow::onChangeSyncFolder);

    m_openSyncFolderAction = syncMenu->addAction("Open Sync Folder");
    m_openSyncFolderAction->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    connect(m_openSyncFolderAction, &QAction::triggered, this, &MainWindow::onOpenSyncFolder);

    syncMenu->addSeparator();

    m_installFilesAction = syncMenu->addAction("Install Files...");
    m_installFilesAction->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    connect(m_installFilesAction, &QAction::triggered, this, &MainWindow::onInstallFiles);

    // Data menu
    QMenu *dataMenu = menuBar()->addMenu("D&ata");

    QMenu *exportMenu = dataMenu->addMenu("&Export");
    m_exportMemosAction = exportMenu->addAction("Memos to Markdown...");
    connect(m_exportMemosAction, &QAction::triggered, m_exportHandler, &ExportHandler::exportMemos);

    m_exportContactsAction = exportMenu->addAction("Contacts to vCard...");
    connect(m_exportContactsAction, &QAction::triggered, m_exportHandler, &ExportHandler::exportContacts);

    m_exportCalendarAction = exportMenu->addAction("Calendar to iCalendar...");
    connect(m_exportCalendarAction, &QAction::triggered, m_exportHandler, &ExportHandler::exportCalendar);

    m_exportTodosAction = exportMenu->addAction("Todos to iCalendar...");
    connect(m_exportTodosAction, &QAction::triggered, m_exportHandler, &ExportHandler::exportTodos);

    exportMenu->addSeparator();
    m_exportAllAction = exportMenu->addAction("Export All...");
    connect(m_exportAllAction, &QAction::triggered, m_exportHandler, &ExportHandler::exportAll);

    QMenu *importMenu = dataMenu->addMenu("&Import");
    m_importMemoAction = importMenu->addAction("Memo from Markdown...");
    connect(m_importMemoAction, &QAction::triggered, m_importHandler, &ImportHandler::importMemo);

    m_importContactAction = importMenu->addAction("Contact from vCard...");
    connect(m_importContactAction, &QAction::triggered, m_importHandler, &ImportHandler::importContact);

    m_importEventAction = importMenu->addAction("Event from iCalendar...");
    connect(m_importEventAction, &QAction::triggered, m_importHandler, &ImportHandler::importEvent);

    m_importTodoAction = importMenu->addAction("Todo from iCalendar...");
    connect(m_importTodoAction, &QAction::triggered, m_importHandler, &ImportHandler::importTodo);

    // View menu
    QMenu *viewMenu = menuBar()->addMenu("&View");

    m_tabbedViewAction = viewMenu->addAction("&Tabbed View");
    m_tabbedViewAction->setCheckable(true);
    m_tabbedViewAction->setChecked(false);
    connect(m_tabbedViewAction, &QAction::toggled, this, [this](bool checked) {
        if (checked) {
            m_mdiArea->setViewMode(QMdiArea::TabbedView);
            m_mdiArea->setTabPosition(QTabWidget::West);
            m_mdiArea->setTabsClosable(true);
            m_mdiArea->setTabsMovable(true);
        } else {
            m_mdiArea->setViewMode(QMdiArea::SubWindowView);
        }
    });

    viewMenu->addSeparator();

    QAction *showLogAction = viewMenu->addAction("Show &Log");
    connect(showLogAction, &QAction::triggered, this, &MainWindow::showLogWindow);

    QAction *clearLogAction = viewMenu->addAction("&Clear Log");
    connect(clearLogAction, &QAction::triggered, this, &MainWindow::onClearLog);

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu("&Help");

    QAction *aboutAction = helpMenu->addAction("&About QPilotSync");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    QAction *aboutQtAction = helpMenu->addAction("About &Qt");
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::createToolBar()
{
    QToolBar *toolbar = addToolBar("Main");
    toolbar->setObjectName("MainToolBar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // Profile actions
    QAction *newProfileAction = toolbar->addAction("New Profile");
    newProfileAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    newProfileAction->setToolTip("Create a new sync profile");
    connect(newProfileAction, &QAction::triggered, this, &MainWindow::onNewProfile);

    QAction *openProfileAction = toolbar->addAction("Open Profile");
    openProfileAction->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    openProfileAction->setToolTip("Open an existing sync profile");
    connect(openProfileAction, &QAction::triggered, this, &MainWindow::onOpenProfile);

    toolbar->addSeparator();

    // Connection actions
    QAction *connectAction = toolbar->addAction("Connect");
    connectAction->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
    connectAction->setToolTip("Connect to Palm device");
    connect(connectAction, &QAction::triggered, this, &MainWindow::onConnectDevice);

    m_toolbarDisconnectAction = toolbar->addAction("Disconnect");
    m_toolbarDisconnectAction->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_toolbarDisconnectAction->setToolTip("Disconnect from Palm device");
    m_toolbarDisconnectAction->setEnabled(false);
    connect(m_toolbarDisconnectAction, &QAction::triggered, this, &MainWindow::onDisconnectDevice);

    toolbar->addSeparator();

    // Sync actions
    m_toolbarHotSyncAction = toolbar->addAction("HotSync");
    m_toolbarHotSyncAction->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_toolbarHotSyncAction->setToolTip("Perform HotSync (sync modified records)");
    m_toolbarHotSyncAction->setEnabled(false);
    connect(m_toolbarHotSyncAction, &QAction::triggered, this, &MainWindow::onHotSync);

    m_toolbarFullSyncAction = toolbar->addAction("Full Sync");
    m_toolbarFullSyncAction->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    m_toolbarFullSyncAction->setToolTip("Full sync (compare all records)");
    m_toolbarFullSyncAction->setEnabled(false);
    connect(m_toolbarFullSyncAction, &QAction::triggered, this, &MainWindow::onFullSync);

    toolbar->addSeparator();

    m_toolbarBackupAction = toolbar->addAction("Backup");
    m_toolbarBackupAction->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    m_toolbarBackupAction->setToolTip("Backup Palm to PC");
    m_toolbarBackupAction->setEnabled(false);
    connect(m_toolbarBackupAction, &QAction::triggered, this, &MainWindow::onBackup);

    m_toolbarRestoreAction = toolbar->addAction("Restore");
    m_toolbarRestoreAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_toolbarRestoreAction->setToolTip("Restore PC backup to Palm");
    m_toolbarRestoreAction->setEnabled(false);
    connect(m_toolbarRestoreAction, &QAction::triggered, this, &MainWindow::onRestore);

    toolbar->addSeparator();

    // Utility actions
    QAction *installAction = toolbar->addAction("Install Files");
    installAction->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    installAction->setToolTip("Install .prc/.pdb files to Palm");
    connect(installAction, &QAction::triggered, this, &MainWindow::onInstallFiles);

    QAction *openFolderAction = toolbar->addAction("Sync Folder");
    openFolderAction->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    openFolderAction->setToolTip("Open sync folder in file manager");
    connect(openFolderAction, &QAction::triggered, this, &MainWindow::onOpenSyncFolder);
}
