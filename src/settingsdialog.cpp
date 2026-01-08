#include "settingsdialog.h"
#include "settings.h"
#include "profile.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QStandardPaths>
#include <QStyle>
#include <QFileInfo>
#include <QFont>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumSize(550, 450);
    setupUi();
    loadSettings();
}

void SettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Tab widget
    m_tabWidget = new QTabWidget();
    m_tabWidget->addTab(createProfilesPage(), "Profiles");
    m_tabWidget->addTab(createDevicesPage(), "Devices");
    m_tabWidget->addTab(createAdvancedPage(), "Advanced");
    mainLayout->addWidget(m_tabWidget);

    // Button box
    QDialogButtonBox *buttonBox = new QDialogButtonBox();
    m_okButton = buttonBox->addButton(QDialogButtonBox::Ok);
    m_cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    m_applyButton = buttonBox->addButton(QDialogButtonBox::Apply);

    connect(m_okButton, &QPushButton::clicked, this, &SettingsDialog::onAccept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::onApply);

    mainLayout->addWidget(buttonBox);
}

QWidget* SettingsDialog::createProfilesPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);

    // Profiles list group
    QGroupBox *profilesGroup = new QGroupBox("Known Profiles");
    QVBoxLayout *profilesLayout = new QVBoxLayout(profilesGroup);

    QLabel *profilesInfo = new QLabel(
        "Double-click a profile to set it as the default. "
        "The default profile (shown in <b>bold</b>) is loaded automatically on startup.");
    profilesInfo->setWordWrap(true);
    profilesInfo->setTextFormat(Qt::RichText);
    profilesLayout->addWidget(profilesInfo);

    m_recentProfilesList = new QListWidget();
    m_recentProfilesList->setAlternatingRowColors(true);
    m_recentProfilesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_recentProfilesList, &QListWidget::itemDoubleClicked, this, &SettingsDialog::onSetDefaultProfile);
    profilesLayout->addWidget(m_recentProfilesList);

    QHBoxLayout *profileButtonLayout = new QHBoxLayout();

    m_setDefaultBtn = new QPushButton("Set as Default");
    m_setDefaultBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    connect(m_setDefaultBtn, &QPushButton::clicked, this, &SettingsDialog::onSetDefaultProfile);

    m_removeRecentBtn = new QPushButton("Remove");
    m_removeRecentBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_removeRecentBtn, &QPushButton::clicked, this, &SettingsDialog::onRemoveRecentProfile);

    m_clearRecentBtn = new QPushButton("Clear All");
    connect(m_clearRecentBtn, &QPushButton::clicked, this, &SettingsDialog::onClearRecentProfiles);

    m_browseProfileBtn = new QPushButton("Add...");
    m_browseProfileBtn->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(m_browseProfileBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseDefaultProfile);

    profileButtonLayout->addWidget(m_setDefaultBtn);
    profileButtonLayout->addWidget(m_removeRecentBtn);
    profileButtonLayout->addWidget(m_clearRecentBtn);
    profileButtonLayout->addStretch();
    profileButtonLayout->addWidget(m_browseProfileBtn);
    profilesLayout->addLayout(profileButtonLayout);

    layout->addWidget(profilesGroup);

    // Current default info
    QGroupBox *defaultGroup = new QGroupBox("Current Default");
    QHBoxLayout *defaultLayout = new QHBoxLayout(defaultGroup);

    m_defaultProfileEdit = new QLineEdit();
    m_defaultProfileEdit->setReadOnly(true);
    m_defaultProfileEdit->setPlaceholderText("(No default set)");
    defaultLayout->addWidget(m_defaultProfileEdit);

    m_clearProfileBtn = new QPushButton("Clear");
    m_clearProfileBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_clearProfileBtn->setToolTip("Clear the default profile (app will start without loading a profile)");
    connect(m_clearProfileBtn, &QPushButton::clicked, this, &SettingsDialog::onClearDefaultProfile);
    defaultLayout->addWidget(m_clearProfileBtn);

    layout->addWidget(defaultGroup);

    return page;
}

QWidget* SettingsDialog::createDevicesPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);

    // Device registry group
    QGroupBox *registryGroup = new QGroupBox("Registered Devices");
    QVBoxLayout *registryLayout = new QVBoxLayout(registryGroup);

    QLabel *registryInfo = new QLabel(
        "When you sync a Palm device with a profile, the device is registered here.\n"
        "This allows the application to detect if the wrong device is connected.");
    registryInfo->setWordWrap(true);
    registryLayout->addWidget(registryInfo);

    m_deviceRegistryList = new QListWidget();
    m_deviceRegistryList->setAlternatingRowColors(true);
    registryLayout->addWidget(m_deviceRegistryList);

    QHBoxLayout *registryButtonLayout = new QHBoxLayout();
    m_clearRegistryBtn = new QPushButton("Clear All Registrations");
    m_clearRegistryBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(m_clearRegistryBtn, &QPushButton::clicked, this, &SettingsDialog::onClearDeviceRegistry);

    registryButtonLayout->addWidget(m_clearRegistryBtn);
    registryButtonLayout->addStretch();
    registryLayout->addLayout(registryButtonLayout);

    layout->addWidget(registryGroup);

    // Info
    QLabel *info = new QLabel(
        "<i>To change which device is associated with a profile, use "
        "File → Profile Settings.</i>");
    info->setWordWrap(true);
    layout->addWidget(info);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createAdvancedPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout(page);

    // Debug group
    QGroupBox *debugGroup = new QGroupBox("Debugging");
    QVBoxLayout *debugLayout = new QVBoxLayout(debugGroup);

    m_debugLoggingCheck = new QCheckBox("Enable verbose debug logging");
    debugLayout->addWidget(m_debugLoggingCheck);

    layout->addWidget(debugGroup);

    // Config file info
    QGroupBox *infoGroup = new QGroupBox("Configuration");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);

    QString configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + "/QPilotSync/QPilotSync.conf";
    m_configFileLabel = new QLabel(QString("Global settings file: <code>%1</code>").arg(configPath));
    m_configFileLabel->setTextFormat(Qt::RichText);
    m_configFileLabel->setWordWrap(true);
    m_configFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_configFileLabel);

    QLabel *profileConfigInfo = new QLabel(
        "Profile settings are stored as <code>.qpilotsync.conf</code> in each sync folder.");
    profileConfigInfo->setTextFormat(Qt::RichText);
    profileConfigInfo->setWordWrap(true);
    infoLayout->addWidget(profileConfigInfo);

    layout->addWidget(infoGroup);

    layout->addStretch();
    return page;
}

void SettingsDialog::loadSettings()
{
    Settings &s = Settings::instance();

    // Get default profile path
    QString defaultProfile = s.defaultProfilePath();
    m_defaultProfileEdit->setText(defaultProfile.isEmpty() ? "" : QFileInfo(defaultProfile).fileName());
    m_defaultProfileEdit->setToolTip(defaultProfile);
    m_clearProfileBtn->setEnabled(!defaultProfile.isEmpty());

    // Load profiles list
    m_recentProfilesList->clear();
    QStringList recent = s.recentProfiles();

    for (const QString &path : recent) {
        QFileInfo info(path);
        QListWidgetItem *item = new QListWidgetItem();

        // Show folder name prominently
        QString displayText = info.fileName();

        // Mark default with bold
        bool isDefault = (path == defaultProfile);
        if (isDefault) {
            displayText += " (Default)";
        }

        item->setText(displayText);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);

        // Bold for default
        if (isDefault) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }

        m_recentProfilesList->addItem(item);
    }

    if (recent.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem("(No profiles yet - create one via File → New Profile)");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(Qt::gray);
        m_recentProfilesList->addItem(item);
    }

    // Device registry
    m_deviceRegistryList->clear();
    QMap<QString, QString> registry = s.deviceRegistry();
    for (auto it = registry.begin(); it != registry.end(); ++it) {
        DeviceFingerprint fp = DeviceFingerprint::fromRegistryKey(it.key());
        QFileInfo profileInfo(it.value());

        QString displayText = QString("%1 → %2")
            .arg(fp.displayString())
            .arg(profileInfo.fileName());

        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setToolTip(QString("Device: %1\nProfile: %2").arg(fp.displayString()).arg(it.value()));
        m_deviceRegistryList->addItem(item);
    }

    if (registry.isEmpty()) {
        QListWidgetItem *item = new QListWidgetItem("(No devices registered yet)");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(Qt::gray);
        m_deviceRegistryList->addItem(item);
    }

    // Advanced
    m_debugLoggingCheck->setChecked(s.debugLogging());
}

void SettingsDialog::saveSettings()
{
    Settings &s = Settings::instance();

    // Advanced
    s.setDebugLogging(m_debugLoggingCheck->isChecked());

    s.sync();
    emit settingsChanged();
}

void SettingsDialog::onSetDefaultProfile()
{
    QListWidgetItem *item = m_recentProfilesList->currentItem();
    if (!item) return;

    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    Settings::instance().setDefaultProfilePath(path);
    Settings::instance().sync();

    loadSettings();  // Refresh to show bold
}

void SettingsDialog::onBrowseDefaultProfile()
{
    QString currentPath = m_defaultProfileEdit->toolTip();
    if (currentPath.isEmpty()) {
        currentPath = QDir::homePath();
    }

    QString path = QFileDialog::getExistingDirectory(
        this,
        "Select Profile Folder to Add",
        currentPath,
        QFileDialog::ShowDirsOnly
    );

    if (!path.isEmpty()) {
        // Add to recent profiles and set as default
        Settings::instance().addRecentProfile(path);
        Settings::instance().setDefaultProfilePath(path);
        Settings::instance().sync();

        loadSettings();  // Refresh list
    }
}

void SettingsDialog::onClearDefaultProfile()
{
    Settings::instance().setDefaultProfilePath(QString());
    Settings::instance().sync();

    loadSettings();  // Refresh
}

void SettingsDialog::onRemoveRecentProfile()
{
    QListWidgetItem *item = m_recentProfilesList->currentItem();
    if (!item) return;

    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    Settings::instance().removeRecentProfile(path);

    // If this was the default, clear it
    if (path == Settings::instance().defaultProfilePath()) {
        Settings::instance().setDefaultProfilePath(QString());
    }

    Settings::instance().sync();

    loadSettings();  // Refresh
}

void SettingsDialog::onClearRecentProfiles()
{
    Settings::instance().clearRecentProfiles();
    Settings::instance().setDefaultProfilePath(QString());
    Settings::instance().sync();

    loadSettings();  // Refresh
}

void SettingsDialog::onClearDeviceRegistry()
{
    Settings::instance().clearDeviceRegistry();
    Settings::instance().sync();
    loadSettings();  // Refresh the list
}

void SettingsDialog::onAccept()
{
    saveSettings();
    accept();
}

void SettingsDialog::onApply()
{
    saveSettings();
}
