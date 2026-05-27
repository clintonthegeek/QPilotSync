#include "settingsdialog.h"
#include "kf6/kf6settings.h"
#include "profile.h"
#include "runtime/profileregistry.h"
#include "app/accounts/accountspage.h"
#include "app/mapping/syncmappingspage.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <KPageWidgetItem>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QDateTime>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent, Profile *profile)
    : KPageDialog(parent)
    , m_profile(profile)
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

    // Sync page is only meaningful when a profile is supplied
    if (m_profile) {
        auto *syncPage = new KPageWidgetItem(createSyncPage(), i18n("Sync"));
        syncPage->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        addPage(syncPage);
    }

    auto *advancedPage = new KPageWidgetItem(createAdvancedPage(), i18n("Advanced"));
    advancedPage->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    addPage(advancedPage);

    // Wire buttons
    connect(this, &QDialog::accepted, this, [this]() {
        saveSettings();
        // OK must persist the per-profile Accounts + Sync Mappings too —
        // previously only the Apply button did, so wiring an edge and clicking
        // OK silently discarded it (empty mappings.conf, sync fell back to
        // rawfiles defaults).
        onApplyAccountsAndMappings();
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

    m_profilesTree = new QTreeWidget();
    m_profilesTree->setColumnCount(3);
    m_profilesTree->setHeaderLabels(
        {i18n("Name"), i18n("Last used"), i18n("Path")});
    m_profilesTree->setRootIsDecorated(false);
    m_profilesTree->setAlternatingRowColors(true);
    m_profilesTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_profilesTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_profilesTree->header()->setStretchLastSection(true);
    connect(m_profilesTree, &QTreeWidget::itemDoubleClicked,
            this, &SettingsDialog::onSetDefaultProfile);
    profilesLayout->addWidget(m_profilesTree);

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

// ========== Sync Page ==========

namespace {

// Helper: populate combo with display/value pairs and select by data value
struct ComboEntry { QString display; QString value; };

void populateCombo(QComboBox *combo, const QList<ComboEntry> &entries)
{
    for (const auto &e : entries) {
        combo->addItem(e.display, e.value);
    }
}

void selectComboValue(QComboBox *combo, const QString &value)
{
    int idx = combo->findData(value);
    if (idx < 0) {
        // Unknown value: append it so the user can see what's stored
        combo->addItem(value, value);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx);
}

QString comboValue(QComboBox *combo)
{
    return combo->currentData().toString();
}

} // namespace

QWidget* SettingsDialog::createSyncPage()
{
    auto *page = new QWidget();
    auto *outer = new QVBoxLayout(page);

    auto *defaultsBox = new QGroupBox(i18n("Default conflict policy"), page);
    auto *form = new QFormLayout(defaultsBox);

    m_syncAutoResolveCombo = new QComboBox(defaultsBox);
    populateCombo(m_syncAutoResolveCombo, {
        { i18n("None (always prompt)"), QStringLiteral("none") },
        { i18n("Palm wins"),             QStringLiteral("palm_wins") },
        { i18n("PC wins"),               QStringLiteral("pc_wins") },
        { i18n("Newer wins"),            QStringLiteral("newer_wins") },
        { i18n("Older wins"),            QStringLiteral("older_wins") },
        { i18n("Duplicate"),             QStringLiteral("duplicate") },
    });
    form->addRow(i18n("Auto-resolve:"), m_syncAutoResolveCombo);

    m_syncFallbackCombo = new QComboBox(defaultsBox);
    populateCombo(m_syncFallbackCombo, {
        { i18n("Defer"),       QStringLiteral("defer") },
        { i18n("Use default"), QStringLiteral("use_default") },
        { i18n("Skip"),        QStringLiteral("skip") },
    });
    form->addRow(i18n("Fallback:"), m_syncFallbackCombo);

    m_syncPromptCombo = new QComboBox(defaultsBox);
    populateCombo(m_syncPromptCombo, {
        { i18n("Always ask"),       QStringLiteral("always_ask") },
        { i18n("First conflict only"), QStringLiteral("first_only") },
        { i18n("Batch at end"),     QStringLiteral("batch_at_end") },
    });
    form->addRow(i18n("Prompt strategy:"), m_syncPromptCombo);

    m_syncConnectionCombo = new QComboBox(defaultsBox);
    populateCombo(m_syncConnectionCombo, {
        { i18n("Keep alive"),            QStringLiteral("keep_alive") },
        { i18n("Disconnect and defer"),  QStringLiteral("disconnect_and_defer") },
        { i18n("Timeout and defer"),     QStringLiteral("timeout_and_defer") },
    });
    form->addRow(i18n("Connection behavior:"), m_syncConnectionCombo);

    m_syncTimeoutSpin = new QSpinBox(defaultsBox);
    m_syncTimeoutSpin->setRange(15, 300);
    m_syncTimeoutSpin->setSuffix(i18n(" seconds"));
    form->addRow(i18n("Conflict timeout:"), m_syncTimeoutSpin);

    outer->addWidget(defaultsBox);

    outer->addStretch();
    return page;
}

void SettingsDialog::loadSyncSettings()
{
    if (!m_profile || !m_syncAutoResolveCombo) return;

    selectComboValue(m_syncAutoResolveCombo, m_profile->conflictAutoResolve());
    selectComboValue(m_syncFallbackCombo,    m_profile->conflictFallback());
    selectComboValue(m_syncPromptCombo,      m_profile->conflictPromptStrategy());
    selectComboValue(m_syncConnectionCombo,  m_profile->conflictConnectionBehavior());
    m_syncTimeoutSpin->setValue(m_profile->conflictTimeoutSeconds());
}

void SettingsDialog::saveSyncSettings()
{
    if (!m_profile || !m_syncAutoResolveCombo) return;

    m_profile->setConflictAutoResolve(comboValue(m_syncAutoResolveCombo));
    m_profile->setConflictFallback(comboValue(m_syncFallbackCombo));
    m_profile->setConflictPromptStrategy(comboValue(m_syncPromptCombo));
    m_profile->setConflictConnectionBehavior(comboValue(m_syncConnectionCombo));
    m_profile->setConflictTimeoutSeconds(m_syncTimeoutSpin->value());

    m_profile->save();
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

    // Known profiles. Source from the app ProfileRegistry, which holds the
    // real display name, stable id and last-used time \u2014 so distinctly-named
    // profiles are recognizable and don't all collapse to their folder
    // basename ("profileN"). Falls back to the legacy KF6Settings recent-paths
    // list (basenames only) when no registry was supplied.
    m_profilesTree->clear();
    int profileRows = 0;
    if (m_profileRegistry) {
        for (const auto &e : m_profileRegistry->entries()) {
            const bool isDefault = (e.path == defaultProfile);
            const QString lastUsed = e.lastOpened.isValid()
                ? e.lastOpened.toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                : i18n("Never");
            QString name = e.name.isEmpty() ? e.id : e.name;
            if (isDefault) name += i18n(" (Default)");
            auto *item = new QTreeWidgetItem(
                m_profilesTree, QStringList{ name, lastUsed, e.path });
            item->setData(0, Qt::UserRole, e.path);
            item->setData(0, Qt::UserRole + 1, e.id);
            item->setToolTip(0, i18n("id: %1\npath: %2", e.id, e.path));
            if (isDefault) {
                QFont f = item->font(0);
                f.setBold(true);
                for (int c = 0; c < 3; ++c) item->setFont(c, f);
            }
            ++profileRows;
        }
    } else {
        for (const QString &path : s.recentProfiles()) {
            const bool isDefault = (path == defaultProfile);
            QString name = QFileInfo(path).fileName();
            if (isDefault) name += i18n(" (Default)");
            auto *item = new QTreeWidgetItem(
                m_profilesTree, QStringList{ name, QString(), path });
            item->setData(0, Qt::UserRole, path);
            item->setToolTip(0, path);
            ++profileRows;
        }
    }

    if (profileRows == 0) {
        auto *item = new QTreeWidgetItem(
            m_profilesTree,
            QStringList{ i18n("(No profiles yet \u2014 create one via File \u2192 New Profile)") });
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    m_profilesTree->resizeColumnToContents(0);
    m_profilesTree->resizeColumnToContents(1);

    // Registered devices (by USB serial \u2014 Phase L Task 0.B consolidated
    // the previous fingerprint-keyed DeviceRegistry into DeviceSerials).
    m_deviceRegistryList->clear();
    KConfigGroup serials(KSharedConfig::openConfig(), QStringLiteral("DeviceSerials"));
    QStringList serialKeys = serials.keyList();
    for (const QString &serial : serialKeys) {
        QString profilePath = serials.readEntry(serial, QString());
        if (profilePath.isEmpty()) continue;
        QFileInfo profileInfo(profilePath);

        auto *item = new QListWidgetItem(
            QStringLiteral("%1 \u2192 %2").arg(serial, profileInfo.fileName()));
        item->setToolTip(i18n("USB Serial: %1\nProfile: %2", serial, profilePath));
        m_deviceRegistryList->addItem(item);
    }

    if (serialKeys.isEmpty()) {
        auto *item = new QListWidgetItem(i18n("(No devices registered yet)"));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        item->setForeground(Qt::gray);
        m_deviceRegistryList->addItem(item);
    }

    // Advanced
    m_minimizeToTrayCheck->setChecked(s.minimizeToTray());
    m_debugLoggingCheck->setChecked(s.debugLogging());

    // Sync (per-profile; only present when m_profile is set)
    loadSyncSettings();
}

void SettingsDialog::saveSettings()
{
    KF6Settings &s = KF6Settings::instance();
    s.setMinimizeToTray(m_minimizeToTrayCheck->isChecked());
    s.setDebugLogging(m_debugLoggingCheck->isChecked());
    s.sync();

    // Sync (per-profile; only present when m_profile is set)
    saveSyncSettings();

    Q_EMIT settingsChanged();
}

// ========== Profile Slots ==========

void SettingsDialog::onSetDefaultProfile()
{
    QTreeWidgetItem *item = m_profilesTree->currentItem();
    if (!item) return;
    QString path = item->data(0, Qt::UserRole).toString();
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
    QTreeWidgetItem *item = m_profilesTree->currentItem();
    if (!item) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    const QString id   = item->data(0, Qt::UserRole + 1).toString();
    if (path.isEmpty() && id.isEmpty()) return;

    KF6Settings &s = KF6Settings::instance();
    if (!path.isEmpty()) {
        s.removeRecentProfile(path);
        if (path == s.defaultProfilePath())
            s.setDefaultProfilePath(QString());
        s.sync();
    }
    // Forget it from the app registry too, so it doesn't reappear in the
    // list. This drops the registry entry only; the on-disk profile folder
    // is left intact.
    if (m_profileRegistry && !id.isEmpty())
        m_profileRegistry->unregister(id);
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
    // Phase L Task 0.B: DeviceRegistry removed; clear DeviceSerials instead.
    KConfigGroup serials(KSharedConfig::openConfig(), QStringLiteral("DeviceSerials"));
    const QStringList keys = serials.keyList();
    for (const QString &key : keys) {
        serials.deleteEntry(key);
    }
    KSharedConfig::openConfig()->sync();
    loadSettings();
}

void SettingsDialog::onApply()
{
    saveSettings();
    onApplyAccountsAndMappings();    // F.3
}

// ========== F.3: Accounts + Sync Mappings ==========

void SettingsDialog::setAccountController(WildPalms::Runtime::AccountController *ac)
{
    m_accountController = ac;
    buildAccountsAndMappingsPagesIfReady();
}

void SettingsDialog::setPalmRuntime(WildPalms::Runtime::PalmRuntime *palmRuntime)
{
    m_palmRuntime = palmRuntime;
    buildAccountsAndMappingsPagesIfReady();
}

void SettingsDialog::setProfileRegistry(WildPalms::Runtime::ProfileRegistry *registry)
{
    m_profileRegistry = registry;
    // Repaint the Profiles page from the richer source if it's already built.
    if (m_profilesTree)
        loadSettings();
}

void SettingsDialog::buildAccountsAndMappingsPagesIfReady()
{
    // Accounts page needs BOTH the controller and the runtime (it connects to
    // PalmRuntime signals in buildUi). Requiring both avoids constructing it
    // with a null runtime (which produced connect(nullptr,...) warnings and a
    // page that never received run signals).
    if (m_accountController && m_palmRuntime && !m_accountsPage) {
        m_accountsPage = new WildPalms::App::Accounts::AccountsPage(
            m_accountController, m_palmRuntime, this);
        auto *item = new KPageWidgetItem(m_accountsPage, i18n("Accounts"));
        item->setIcon(QIcon::fromTheme(QStringLiteral("network-server")));
        addPage(item);
    }

    // Sync Mappings page needs Profile + AccountController + PalmRuntime.
    if (m_profile && m_accountController && m_palmRuntime
        && !m_syncMappingsPage) {
        m_syncMappingsPage = new WildPalms::AppMapping::SyncMappingsPage(
            m_profile, m_accountController, m_palmRuntime, this);
        m_syncMappingsPageItem = new KPageWidgetItem(
            m_syncMappingsPage, i18n("Sync Mappings"));
        m_syncMappingsPageItem->setIcon(
            QIcon::fromTheme(QStringLiteral("view-list-tree")));
        addPage(m_syncMappingsPageItem);
    }
}

void SettingsDialog::navigateToSyncMappings()
{
    if (m_syncMappingsPageItem)
        setCurrentPage(m_syncMappingsPageItem);
}

void SettingsDialog::onApplyAccountsAndMappings()
{
    if (m_syncMappingsPage && m_profile)
        m_syncMappingsPage->applyTo(m_profile);
}
