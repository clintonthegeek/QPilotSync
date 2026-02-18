#include "settingsdialog.h"
#include "kf6/kf6settings.h"
#include "kf6/conduitmanager.h"
#include "core/iconduit.h"
#include "profile.h"

#include <KLocalizedString>
#include <KPageWidgetItem>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QStandardPaths>

// Human-readable names for well-known Palm OS creator IDs
static QString palmAppName(const QString &creatorId)
{
    static const QMap<QString, QString> names = {
        { QStringLiteral("memo"), QStringLiteral("Memo Pad") },
        { QStringLiteral("addr"), QStringLiteral("Address Book") },
        { QStringLiteral("date"), QStringLiteral("Date Book") },
        { QStringLiteral("todo"), QStringLiteral("To Do List") },
        { QStringLiteral("mail"), QStringLiteral("Mail") },
        { QStringLiteral("lnch"), QStringLiteral("Launcher") },
        { QStringLiteral("Plkr"), QStringLiteral("Plucker") },
        { QStringLiteral("Mcal"), QStringLiteral("DateBk") },
        { QStringLiteral("psys"), QStringLiteral("System Preferences") },
        { QStringLiteral("secr"), QStringLiteral("Security") },
    };
    return names.value(creatorId);
}

SettingsDialog::SettingsDialog(ConduitManager *conduitManager,
                               QWidget *parent)
    : KPageDialog(parent)
    , m_conduitManager(conduitManager)
{
    setWindowTitle(i18n("Configure QPilotSync"));
    setFaceType(KPageDialog::List);
    setMinimumSize(650, 550);

    // Standard OK / Cancel / Apply buttons
    setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                       | QDialogButtonBox::Apply);
    button(QDialogButtonBox::Apply)->setEnabled(true);

    // Add pages with icons — Conduits first
    auto *conduitsPage = new KPageWidgetItem(createConduitsPage(), i18n("Conduits"));
    conduitsPage->setIcon(QIcon::fromTheme(QStringLiteral("application-x-addon")));
    addPage(conduitsPage);

    auto *profilesPage = new KPageWidgetItem(createProfilesPage(), i18n("Profiles"));
    profilesPage->setIcon(QIcon::fromTheme(QStringLiteral("user-identity")));
    addPage(profilesPage);

    auto *devicesPage = new KPageWidgetItem(createDevicesPage(), i18n("Devices"));
    devicesPage->setIcon(QIcon::fromTheme(QStringLiteral("phone")));
    addPage(devicesPage);

    auto *advancedPage = new KPageWidgetItem(createAdvancedPage(), i18n("Advanced"));
    advancedPage->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    addPage(advancedPage);

    // Add config pages for already-enabled conduits
    if (m_conduitManager) {
        const auto plugins = m_conduitManager->conduitList();
        for (const auto &plugin : plugins) {
            QString id = plugin.metaData.value(QStringLiteral("X-QPilotSync-ConduitId"));
            if (id.isEmpty()) id = plugin.metaData.pluginId();
            if (plugin.enabled) {
                addConduitConfigPages(id);
            }
        }
    }

    // Wire buttons
    connect(this, &QDialog::accepted, this, [this]() {
        saveSettings();
    });
    connect(button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SettingsDialog::onApply);

    loadSettings();
}

// ========== Conduits Page ==========

QWidget* SettingsDialog::createConduitsPage()
{
    auto *page = new QWidget();
    auto *layout = new QVBoxLayout(page);

    auto *info = new QLabel(
        i18n("Each conduit handles synchronization for a specific Palm application. "
             "Only one conduit may be active per Palm app at a time. "
             "Enabling a competing conduit will automatically disable the incumbent."));
    info->setWordWrap(true);
    layout->addWidget(info);

    // Tree widget: top-level = creator ID group, children = conduits
    m_conduitTree = new QTreeWidget();
    m_conduitTree->setHeaderLabels({i18n("Conduit"), i18n("Version"), i18n("Creator ID")});
    m_conduitTree->setAlternatingRowColors(true);
    m_conduitTree->setRootIsDecorated(true);
    m_conduitTree->header()->setStretchLastSection(false);
    m_conduitTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_conduitTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_conduitTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    if (m_conduitManager) {
        // Group conduits by creator ID
        QMap<QString, QList<ConduitManager::PluginInfo>> groups;
        const auto plugins = m_conduitManager->conduitList();
        for (const auto &info : plugins) {
            QString creatorId = info.palmCreatorId;
            if (creatorId.isEmpty()) {
                creatorId = QStringLiteral("_utilities");
            }
            groups[creatorId].append(info);
        }

        for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
            const QString &creatorId = it.key();
            const auto &conduits = it.value();

            // Group header
            QString groupLabel;
            if (creatorId == QStringLiteral("_utilities")) {
                groupLabel = i18n("Utilities");
            } else {
                QString appName = palmAppName(creatorId);
                if (appName.isEmpty()) {
                    groupLabel = i18n("Palm App \"%1\"", creatorId);
                } else {
                    groupLabel = QStringLiteral("%1").arg(appName);
                }
            }

            auto *groupItem = new QTreeWidgetItem(m_conduitTree);
            groupItem->setText(0, groupLabel);
            if (creatorId != QStringLiteral("_utilities")) {
                groupItem->setText(2, creatorId);
            }
            groupItem->setFlags(groupItem->flags() & ~Qt::ItemIsSelectable);
            QFont groupFont = groupItem->font(0);
            groupFont.setBold(true);
            groupItem->setFont(0, groupFont);
            groupItem->setExpanded(true);

            for (const auto &plugin : conduits) {
                QString conduitId = plugin.metaData.value(
                    QStringLiteral("X-QPilotSync-ConduitId"));
                if (conduitId.isEmpty()) conduitId = plugin.metaData.pluginId();

                auto *conduitItem = new QTreeWidgetItem(groupItem);
                conduitItem->setText(0, plugin.metaData.name());
                conduitItem->setText(1, plugin.metaData.version());
                conduitItem->setToolTip(0, plugin.metaData.description());
                conduitItem->setData(0, Qt::UserRole, conduitId);
                conduitItem->setData(0, Qt::UserRole + 1, plugin.palmCreatorId);
                conduitItem->setFlags(conduitItem->flags() | Qt::ItemIsUserCheckable);
                conduitItem->setCheckState(0, plugin.enabled ? Qt::Checked : Qt::Unchecked);

                // Show the conduit's icon if available
                QString iconName = plugin.metaData.iconName();
                if (!iconName.isEmpty()) {
                    conduitItem->setIcon(0, QIcon::fromTheme(iconName));
                }
            }
        }
    }

    connect(m_conduitTree, &QTreeWidget::itemChanged,
            this, &SettingsDialog::onConduitToggled);

    layout->addWidget(m_conduitTree);

    // Detail label below the tree
    m_conduitDetailLabel = new QLabel();
    m_conduitDetailLabel->setWordWrap(true);
    m_conduitDetailLabel->setTextFormat(Qt::RichText);
    layout->addWidget(m_conduitDetailLabel);

    connect(m_conduitTree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current || current->data(0, Qt::UserRole).toString().isEmpty()) {
            m_conduitDetailLabel->clear();
            return;
        }
        QString conduitId = current->data(0, Qt::UserRole).toString();
        KPluginMetaData md = m_conduitManager->conduitMetaData(conduitId);
        QString palmDb = md.value(QStringLiteral("X-QPilotSync-PalmDatabase"));
        QString desc = md.description();
        QString text = QStringLiteral("<b>%1</b>").arg(md.name());
        if (!desc.isEmpty()) {
            text += QStringLiteral("<br>%1").arg(desc);
        }
        if (!palmDb.isEmpty()) {
            text += QStringLiteral("<br>") + i18n("Palm database: <code>%1</code>", palmDb);
        }
        m_conduitDetailLabel->setText(text);
    });

    return page;
}

void SettingsDialog::onConduitToggled(QTreeWidgetItem *item, int column)
{
    if (column != 0) return;
    QString conduitId = item->data(0, Qt::UserRole).toString();
    if (conduitId.isEmpty()) return;  // group header, ignore

    bool enabled = (item->checkState(0) == Qt::Checked);
    m_conduitManager->setConduitEnabled(conduitId, enabled);

    if (enabled) {
        // setConduitEnabled may have auto-disabled another conduit sharing
        // the same creator ID — update any sibling checkboxes that changed
        QString creatorId = item->data(0, Qt::UserRole + 1).toString();
        if (!creatorId.isEmpty()) {
            QTreeWidgetItem *parent = item->parent();
            if (parent) {
                // Block signals to avoid recursive onConduitToggled calls
                m_conduitTree->blockSignals(true);
                for (int i = 0; i < parent->childCount(); ++i) {
                    QTreeWidgetItem *sibling = parent->child(i);
                    if (sibling == item) continue;
                    QString siblingId = sibling->data(0, Qt::UserRole).toString();
                    if (!m_conduitManager->isConduitEnabled(siblingId)) {
                        sibling->setCheckState(0, Qt::Unchecked);
                        removeConduitConfigPages(siblingId);
                    }
                }
                m_conduitTree->blockSignals(false);
            }
        }
        addConduitConfigPages(conduitId);
    } else {
        removeConduitConfigPages(conduitId);
    }
}

void SettingsDialog::addConduitConfigPages(const QString &conduitId)
{
    if (m_conduitConfigPages.contains(conduitId)) return;  // already added

    IConduit *conduit = m_conduitManager->conduit(conduitId);
    if (!conduit) return;

    int numPages = conduit->configPages();
    if (numPages <= 0) return;

    QList<KPageWidgetItem*> pages;
    for (int i = 0; i < numPages; ++i) {
        QWidget *configWidget = conduit->createConfigPage(i, nullptr);
        if (!configWidget) continue;

        QString pageName = (numPages == 1)
            ? conduit->displayName()
            : QStringLiteral("%1 (%2)").arg(conduit->displayName()).arg(i + 1);

        auto *pageItem = new KPageWidgetItem(configWidget, pageName);
        pageItem->setIcon(conduit->icon());
        addPage(pageItem);
        pages.append(pageItem);
    }

    if (!pages.isEmpty()) {
        m_conduitConfigPages.insert(conduitId, pages);
    }
}

void SettingsDialog::removeConduitConfigPages(const QString &conduitId)
{
    if (!m_conduitConfigPages.contains(conduitId)) return;

    const auto pages = m_conduitConfigPages.take(conduitId);
    for (KPageWidgetItem *page : pages) {
        removePage(page);  // KPageDialog takes ownership and deletes
    }
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
        QStandardPaths::ConfigLocation) + QStringLiteral("/qpilotsyncrc");
    m_configFileLabel = new QLabel(
        i18n("Settings file: <code>%1</code>", configPath));
    m_configFileLabel->setTextFormat(Qt::RichText);
    m_configFileLabel->setWordWrap(true);
    m_configFileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addWidget(m_configFileLabel);

    auto *profileInfo = new QLabel(
        i18n("Profile settings are stored as <code>.qpilotsync.conf</code> "
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

    // Persist conduit enabled/disabled state
    if (m_conduitManager) {
        m_conduitManager->saveConfig();
    }

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
