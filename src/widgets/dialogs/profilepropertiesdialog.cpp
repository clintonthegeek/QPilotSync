#include "profilepropertiesdialog.h"

#include "../../profile.h"
#include "../../kf6/conduitmanager.h"

#include <KLocalizedString>
#include <KPluginMetaData>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QVBoxLayout>

// ========== Constructor ==========

ProfilePropertiesDialog::ProfilePropertiesDialog(Profile *profile,
                                                  ConduitManager *conduitManager,
                                                  QWidget *parent)
    : KPageDialog(parent)
    , m_profile(profile)
    , m_conduitManager(conduitManager)
{
    setWindowTitle(m_profile->name().isEmpty()
                       ? i18n("Profile Properties")
                       : i18n("Profile Properties — %1", m_profile->name()));
    setFaceType(KPageDialog::List);
    resize(500, 400);

    // --- Device page ---
    QWidget *deviceWidget = createDevicePage();
    KPageWidgetItem *devicePage = addPage(deviceWidget, i18n("Device"));
    devicePage->setIcon(QIcon::fromTheme(QStringLiteral("drive-removable-media")));

    // --- Conduits page ---
    QWidget *conduitsWidget = createConduitsPage();
    KPageWidgetItem *conduitsPage = addPage(conduitsWidget, i18n("Conduits"));
    conduitsPage->setIcon(QIcon::fromTheme(QStringLiteral("preferences-plugin")));

    // --- Conflict page ---
    QWidget *conflictWidget = createConflictPage();
    KPageWidgetItem *conflictPage = addPage(conflictWidget, i18n("Conflict Resolution"));
    conflictPage->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));

    // Wire OK / Apply
    connect(this, &QDialog::accepted, this, &ProfilePropertiesDialog::onApply);

    // Populate widgets from profile
    loadSettings();
}

// ========== Page Builders ==========

QWidget* ProfilePropertiesDialog::createDevicePage()
{
    auto *page = new QWidget;
    auto *layout = new QFormLayout(page);

    // Device path
    m_devicePathEdit = new QLineEdit(page);
    m_devicePathEdit->setPlaceholderText(QStringLiteral("/dev/pilot"));
    layout->addRow(i18n("Device path:"), m_devicePathEdit);

    // Baud rate
    m_baudRateCombo = new QComboBox(page);
    m_baudRateCombo->addItems({
        QStringLiteral("9600"),
        QStringLiteral("19200"),
        QStringLiteral("38400"),
        QStringLiteral("57600"),
        QStringLiteral("115200"),
    });
    layout->addRow(i18n("Baud rate:"), m_baudRateCombo);

    // Connection mode
    m_connectionModeCombo = new QComboBox(page);
    m_connectionModeCombo->addItem(i18n("Keep Alive"),
                                    static_cast<int>(ConnectionMode::KeepAlive));
    m_connectionModeCombo->addItem(i18n("Disconnect After Sync"),
                                    static_cast<int>(ConnectionMode::DisconnectAfterSync));
    layout->addRow(i18n("Connection mode:"), m_connectionModeCombo);

    // Auto-sync on connect
    m_autoSyncCheck = new QCheckBox(i18n("Automatically sync when device connects"), page);
    layout->addRow(QString(), m_autoSyncCheck);

    // Default sync type
    m_defaultSyncTypeCombo = new QComboBox(page);
    m_defaultSyncTypeCombo->addItem(i18n("HotSync"), QStringLiteral("hotsync"));
    m_defaultSyncTypeCombo->addItem(i18n("Full Sync"), QStringLiteral("fullsync"));
    layout->addRow(i18n("Default sync type:"), m_defaultSyncTypeCombo);

    return page;
}

QWidget* ProfilePropertiesDialog::createConduitsPage()
{
    auto *page = new QWidget;
    auto *outerLayout = new QVBoxLayout(page);

    if (!m_conduitManager) {
        outerLayout->addWidget(new QLabel(i18n("No conduit plugins found"), page));
        outerLayout->addStretch();
        return page;
    }

    const QList<ConduitManager::PluginInfo> plugins = m_conduitManager->conduitList();
    if (plugins.isEmpty()) {
        outerLayout->addWidget(new QLabel(i18n("No conduit plugins found"), page));
        outerLayout->addStretch();
        return page;
    }

    // Scroll area to handle many conduits
    auto *scrollArea = new QScrollArea(page);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *inner = new QWidget;
    auto *innerLayout = new QVBoxLayout(inner);

    for (const ConduitManager::PluginInfo &info : plugins) {
        // Determine conduit ID: prefer X-WildPalms-ConduitId, fall back to pluginId
        QString conduitId = info.metaData.value(QStringLiteral("X-WildPalms-ConduitId"));
        if (conduitId.isEmpty()) {
            conduitId = info.metaData.pluginId();
        }
        if (conduitId.isEmpty()) {
            continue;
        }

        auto *cb = new QCheckBox(info.metaData.name(), inner);
        innerLayout->addWidget(cb);
        m_conduitChecks.insert(conduitId, cb);

        // Mutual-exclusion: when a conduit is checked, uncheck any other conduit
        // that shares the same Palm creator ID (only one conduit per Palm DB).
        connect(cb, &QCheckBox::toggled, this, [this, conduitId](bool checked) {
            if (!checked) return;

            const QString myCreatorId = m_conduitManager->palmCreatorId(conduitId);
            if (myCreatorId.isEmpty()) return;

            for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
                if (it.key() == conduitId) continue;

                const QString otherCreatorId = m_conduitManager->palmCreatorId(it.key());
                if (otherCreatorId == myCreatorId && it.value()->isChecked()) {
                    it.value()->setChecked(false);
                }
            }
        });
    }

    innerLayout->addStretch();
    scrollArea->setWidget(inner);
    outerLayout->addWidget(scrollArea);

    return page;
}

QWidget* ProfilePropertiesDialog::createConflictPage()
{
    auto *page = new QWidget;
    auto *layout = new QFormLayout(page);

    // Auto-resolve strategy
    m_autoResolveCombo = new QComboBox(page);
    m_autoResolveCombo->addItem(i18n("None"),        QStringLiteral("none"));
    m_autoResolveCombo->addItem(i18n("Palm Wins"),   QStringLiteral("palm_wins"));
    m_autoResolveCombo->addItem(i18n("PC Wins"),     QStringLiteral("pc_wins"));
    m_autoResolveCombo->addItem(i18n("Newer Wins"),  QStringLiteral("newer_wins"));
    m_autoResolveCombo->addItem(i18n("Older Wins"),  QStringLiteral("older_wins"));
    m_autoResolveCombo->addItem(i18n("Duplicate"),   QStringLiteral("duplicate"));
    layout->addRow(i18n("Auto-resolve strategy:"), m_autoResolveCombo);

    // Fallback behavior
    m_fallbackCombo = new QComboBox(page);
    m_fallbackCombo->addItem(i18n("Defer"),        QStringLiteral("defer"));
    m_fallbackCombo->addItem(i18n("Skip"),         QStringLiteral("skip"));
    m_fallbackCombo->addItem(i18n("Use Default"),  QStringLiteral("use_default"));
    layout->addRow(i18n("Fallback behavior:"), m_fallbackCombo);

    return page;
}

// ========== Load / Save ==========

void ProfilePropertiesDialog::loadSettings()
{
    // Device page
    m_devicePathEdit->setText(m_profile->devicePath());

    int baudIdx = m_baudRateCombo->findText(m_profile->baudRate());
    m_baudRateCombo->setCurrentIndex(baudIdx >= 0 ? baudIdx : 4); // default 115200

    int modeIdx = m_connectionModeCombo->findData(
        static_cast<int>(m_profile->connectionMode()));
    if (modeIdx >= 0) m_connectionModeCombo->setCurrentIndex(modeIdx);

    m_autoSyncCheck->setChecked(m_profile->autoSyncOnConnect());

    int syncTypeIdx = m_defaultSyncTypeCombo->findData(m_profile->defaultSyncType());
    if (syncTypeIdx >= 0) m_defaultSyncTypeCombo->setCurrentIndex(syncTypeIdx);

    // Conduits page
    for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
        it.value()->setChecked(m_profile->conduitEnabled(it.key()));
    }

    // Conflict page
    int resolveIdx = m_autoResolveCombo->findData(m_profile->conflictAutoResolve());
    if (resolveIdx >= 0) m_autoResolveCombo->setCurrentIndex(resolveIdx);

    int fallbackIdx = m_fallbackCombo->findData(m_profile->conflictFallback());
    if (fallbackIdx >= 0) m_fallbackCombo->setCurrentIndex(fallbackIdx);
}

void ProfilePropertiesDialog::saveSettings()
{
    // Device page
    m_profile->setDevicePath(m_devicePathEdit->text().trimmed());
    m_profile->setBaudRate(m_baudRateCombo->currentText());

    int modeData = m_connectionModeCombo->currentData().toInt();
    m_profile->setConnectionMode(static_cast<ConnectionMode>(modeData));

    m_profile->setAutoSyncOnConnect(m_autoSyncCheck->isChecked());
    m_profile->setDefaultSyncType(
        m_defaultSyncTypeCombo->currentData().toString());

    // Conduits page
    for (auto it = m_conduitChecks.constBegin(); it != m_conduitChecks.constEnd(); ++it) {
        m_profile->setConduitEnabled(it.key(), it.value()->isChecked());
    }

    // Conflict page
    m_profile->setConflictAutoResolve(
        m_autoResolveCombo->currentData().toString());
    m_profile->setConflictFallback(
        m_fallbackCombo->currentData().toString());

    m_profile->save();
}

// ========== Slots ==========

void ProfilePropertiesDialog::onApply()
{
    saveSettings();
    emit settingsChanged();
}
