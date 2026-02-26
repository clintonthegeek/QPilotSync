#include "settingsdialog.h"
#include "kf6/kf6settings.h"
#include "profile.h"

#include <KLocalizedString>
#include <KPageWidgetItem>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent)
    : KPageDialog(parent)
{
    setWindowTitle(i18n("Configure Wild Palms"));
    setFaceType(KPageDialog::List);
    setMinimumSize(650, 550);

    // Standard OK / Cancel / Apply buttons
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                       | QDialogButtonBox::Apply);
    button(QDialogButtonBox::Apply)->setEnabled(true);

    // Add pages with icons
    auto *profilesPage = new KPageWidgetItem(createProfilesPage(), i18n("Profiles"));
    profilesPage->setIcon(QIcon::fromTheme(QStringLiteral("user-identity")));
    addPage(profilesPage);

    auto *devicesPage = new KPageWidgetItem(createDevicesPage(), i18n("Devices"));
    devicesPage->setIcon(QIcon::fromTheme(QStringLiteral("phone")));
    addPage(devicesPage);

    auto *advancedPage = new KPageWidgetItem(createAdvancedPage(), i18n("Advanced"));
    advancedPage->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    addPage(advancedPage);

    // Wire buttons
    connect(this, &QDialog::accepted, this, [this]() {
        saveSettings();
    });
    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SettingsDialog::onApply);

    loadSettings();
}

// ========== Profiles Page ==========

QWidget* SettingsDialog::createProfilesPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *profilesGroup = new QGroupBox(i18n("Known Profiles"));
    auto *profilesLayout = new QVBoxLayout(profilesGroup);

    auto *info = new QLabel(
        i18n("Double-click a profile to set it as the default. "
             "The default profile (shown in <b>bold</b>) is loaded automatically on startup."));
    info->setWordWrap(true);
    info->setTextFormat(Qt::RichText);
    profilesLayout->addWidget(info);

    m_recentProfilesList = new QListWidget();
    m_recentProfilesList->setAlternatingRowColors(true);
    m_recentProfilesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_recentProfilesList, &QListWidget::itemDoubleClicked,
            this, &SettingsDialog::onSetDefaultProfile);
    profilesLayout->addWidget(m_recentProfilesList);

    auto *btnLayout = new QHBoxLayout();
    m_setDefaultBtn = new QPushButton(i18n("Set as Default"));
    m_setDefaultBtn->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")));
    connect(m_setDefaultBtn, &QPushButton::clicked,
            this, &SettingsDialog::onSetDefaultProfile);

    m_removeRecentBtn = new QPushButton(i18n("Remove"));
    m_removeRecentBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    connect(m_removeRecentBtn, &QPushButton::clicked,
            this, &SettingsDialog::onRemoveRecentProfile);

    m_clearRecentBtn = new QPushButton(i18n("Clear All"));
    connect(m_clearRecentBtn, &QPushButton::clicked,
            this, &SettingsDialog::onClearRecentProfiles);

    m_browseProfileBtn = new QPushButton(i18n("Add..."));
    m_browseProfileBtn->setIcon(QIcon::fromTheme(QStringLiteral("document-open-folder")));
    connect(m_browseProfileBtn, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseDefaultProfile);

    btnLayout->addWidget(m_setDefaultBtn);
    btnLayout->addWidget(m_removeRecentBtn);
    btnLayout->addWidget(m_clearRecentBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_browseProfileBtn);
    profilesLayout->addLayout(btnLayout);

    layout->addWidget(profilesGroup);

    // Current default
    auto *defaultGroup = new QGroupBox(i18n("Current Default"));
    auto *defaultLayout = new QHBoxLayout(defaultGroup);

    m_defaultProfileEdit = new QLineEdit();
    m_defaultProfileEdit->setReadOnly(true);
    m_defaultProfileEdit->setPlaceholderText(i18n("(No default set)"));
    defaultLayout->addWidget(m_defaultProfileEdit);

    m_clearProfileBtn = new QPushButton(i18n("Clear"));
    m_clearProfileBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
    m_clearProfileBtn->setToolTip(
        i18n("Clear the default profile (app will start without loading a profile)"));
    connect(m_clearProfileBtn, &QPushButton::clicked,
            this, &SettingsDialog::onClearDefaultProfile);
    defaultLayout->addWidget(m_clearProfileBtn);

    layout->addWidget(defaultGroup);
    return page;
}

// ========== Devices Page ==========

QWidget* SettingsDialog::createDevicesPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *registryGroup = new QGroupBox(i18n("Registered Devices"));
    auto *registryLayout = new QVBoxLayout(registryGroup);

    auto *info = new QLabel(
        i18n("When you sync a Palm device with a profile, the device is registered here. "
             "This allows the application to automatically load the correct profile."));
    info->setWordWrap(true);
    registryLayout->addWidget(info);

    m_deviceRegistryList = new QListWidget();
    m_deviceRegistryList->setAlternatingRowColors(true);
    registryLayout->addWidget(m_deviceRegistryList);

    auto *btnLayout = new QHBoxLayout();
    m_clearRegistryBtn = new QPushButton(i18n("Clear All Registrations"));
    m_clearRegistryBtn->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete")));
    connect(m_clearRegistryBtn, &QPushButton::clicked,
            this, &SettingsDialog::onClearDeviceRegistry);
    btnLayout->addWidget(m_clearRegistryBtn);
    btnLayout->addStretch();
    registryLayout->addLayout(btnLayout);

    layout->addWidget(registryGroup);
    layout->addStretch();
    return page;
}

// ========== Advanced Page ==========

QWidget* SettingsDialog::createAdvancedPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    // System tray
    auto *trayGroup = new QGroupBox(i18n("System Tray"));
    auto *trayLayout = new QVBoxLayout(trayGroup);
    m_minimizeToTrayCheck = new QCheckBox(
        i18n("Minimize to system tray instead of closing"));
    trayLayout->addWidget(m_minimizeToTrayCheck);
    layout->addWidget(trayGroup);

    // Debug
    auto *debugGroup = new QGroupBox(i18n("Debugging"));
    auto *debugLayout = new QVBoxLayout(debugGroup);
    m_debugLoggingCheck = new QCheckBox(i18n("Enable verbose debug logging"));
    debugLayout->addWidget(m_debugLoggingCheck);
    layout->addWidget(debugGroup);

    // Config info
    auto *infoGroup = new QGroupBox(i18n("Configuration"));
    auto *infoLayout = new QVBoxLayout(infoGroup);

    QString configPath = QStandardPaths::writableLocation(
        QStandardPaths::ConfigLocation) + QStringLiteral("/wildpalmsrc");
    m_configFileLabel = new QLabel(
        i18n("Settings file: <code>%1</code>", configPath));
    m_configFileLabel->setTextFormat(Qt::RichText);
    m_configFileLabel->setWordWrap(true);
    m_configFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_configFileLabel);

    auto *profileInfo = new QLabel(
        i18n("Profile settings are stored as <code>.wildpalms.conf</code> "
             "in each sync folder."));
    profileInfo->setTextFormat(Qt::RichText);
    profileInfo->setWordWrap(true);
    infoLayout->addWidget(profileInfo);

    layout->addWidget(infoGroup);
    layout->addStretch();
    return page;
}

// ========== Settings Load / Save ==========

void SettingsDialog::loadSettings()
{
    KF6Settings &s = KF6Settings::instance();

    // Default profile
    QString defaultProfile = s.defaultProfilePath();
    m_defaultProfileEdit->setText(
        defaultProfile.isEmpty() ? QString() : QFileInfo(defaultProfile).fileName());
    m_defaultProfileEdit->setToolTip(defaultProfile);
    m_clearProfileBtn->setEnabled(!defaultProfile.isEmpty());

    // Recent profiles list
    m_recentProfilesList->clear();
    QStringList recent = s.recentProfiles();

    for (const QString &path : recent) {
        QFileInfo fi(path);
        QString displayText = fi.fileName();
        bool isDefault = (path == defaultProfile);
        if (isDefault) {
            displayText += i18n(" (Default)");
        }

        auto *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        if (isDefault) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        m_recentProfilesList->addItem(item);
    }

    if (recent.isEmpty()) {
        auto *item = new QListWidgetItem(
            i18n("(No profiles yet \u2014 create one via File \u2192 New Profile)"));
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

        auto *item = new QListWidgetItem(
            QStringLiteral("%1 \u2192 %2")
                .arg(fp.displayString(), profileInfo.fileName()));
        item->setToolTip(
            i18n("Device: %1\nProfile: %2", fp.displayString(), it.value()));
        m_deviceRegistryList->addItem(item);
    }

    if (registry.isEmpty()) {
        auto *item = new QListWidgetItem(i18n("(No devices registered yet)"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(Qt::gray);
        m_deviceRegistryList->addItem(item);
    }

    // Advanced
    m_minimizeToTrayCheck->setChecked(s.minimizeToTray());
    m_debugLoggingCheck->setChecked(s.debugLogging());
}

void SettingsDialog::saveSettings()
{
    KF6Settings &s = KF6Settings::instance();
    s.setMinimizeToTray(m_minimizeToTrayCheck->isChecked());
    s.setDebugLogging(m_debugLoggingCheck->isChecked());
    s.sync();

    Q_EMIT settingsChanged();
}

// ========== Profile Slots ==========

void SettingsDialog::onSetDefaultProfile()
{
    QListWidgetItem *item = m_recentProfilesList->currentItem();
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    KF6Settings::instance().setDefaultProfilePath(path);
    KF6Settings::instance().sync();
    loadSettings();
}

void SettingsDialog::onBrowseDefaultProfile()
{
    QString currentPath = m_defaultProfileEdit->toolTip();
    if (currentPath.isEmpty()) currentPath = QDir::homePath();

    QString path = QFileDialog::getExistingDirectory(
        this, i18n("Select Profile Folder to Add"), currentPath,
        QFileDialog::ShowDirsOnly);

    if (!path.isEmpty()) {
        KF6Settings::instance().addRecentProfile(path);
        KF6Settings::instance().setDefaultProfilePath(path);
        KF6Settings::instance().sync();
        loadSettings();
    }
}

void SettingsDialog::onClearDefaultProfile()
{
    KF6Settings::instance().setDefaultProfilePath(QString());
    KF6Settings::instance().sync();
    loadSettings();
}

void SettingsDialog::onRemoveRecentProfile()
{
    QListWidgetItem *item = m_recentProfilesList->currentItem();
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    KF6Settings &s = KF6Settings::instance();
    s.removeRecentProfile(path);
    if (path == s.defaultProfilePath()) {
        s.setDefaultProfilePath(QString());
    }
    s.sync();
    loadSettings();
}

void SettingsDialog::onClearRecentProfiles()
{
    KF6Settings &s = KF6Settings::instance();
    s.clearRecentProfiles();
    s.setDefaultProfilePath(QString());
    s.sync();
    loadSettings();
}

void SettingsDialog::onClearDeviceRegistry()
{
    KF6Settings::instance().clearDeviceRegistry();
    KF6Settings::instance().sync();
    loadSettings();
}

void SettingsDialog::onApply()
{
    saveSettings();
}
