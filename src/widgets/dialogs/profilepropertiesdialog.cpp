#include "profilepropertiesdialog.h"

#include "../../profile.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

// ========== Constructor ==========

ProfilePropertiesDialog::ProfilePropertiesDialog(Profile *profile,
                                                  QWidget *parent)
    : KPageDialog(parent)
    , m_profile(profile)
{
    setWindowTitle(m_profile->name().isEmpty()
                       ? i18n("Profile Properties")
                       : i18n("Profile Properties — %1", m_profile->name()));
    setFaceType(KPageDialog::List);
    resize(500, 400);

    // --- General page ---
    QWidget *generalWidget = createGeneralPage();
    KPageWidgetItem *generalPage = addPage(generalWidget, i18n("General"));
    generalPage->setIcon(QIcon::fromTheme(QStringLiteral("user-identity")));

    // --- Device page ---
    QWidget *deviceWidget = createDevicePage();
    KPageWidgetItem *devicePage = addPage(deviceWidget, i18n("Device"));
    devicePage->setIcon(QIcon::fromTheme(QStringLiteral("drive-removable-media")));

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

QWidget *ProfilePropertiesDialog::createGeneralPage()
{
    auto *page = new QWidget;
    auto *layout = new QFormLayout(page);

    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setObjectName(QStringLiteral("profileName"));
    m_nameEdit->setText(m_profile->name());
    layout->addRow(i18n("Profile name:"), m_nameEdit);

    return page;
}

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

QWidget* ProfilePropertiesDialog::createConflictPage()
{
    auto *page = new QWidget;
    auto *mainLayout = new QVBoxLayout(page);

    // Auto-resolve settings
    auto *autoLayout = new QFormLayout;

    m_autoResolveCombo = new QComboBox(page);
    m_autoResolveCombo->addItem(i18n("None"),        QStringLiteral("none"));
    m_autoResolveCombo->addItem(i18n("Palm Wins"),   QStringLiteral("palm_wins"));
    m_autoResolveCombo->addItem(i18n("PC Wins"),     QStringLiteral("pc_wins"));
    m_autoResolveCombo->addItem(i18n("Newer Wins"),  QStringLiteral("newer_wins"));
    m_autoResolveCombo->addItem(i18n("Older Wins"),  QStringLiteral("older_wins"));
    m_autoResolveCombo->addItem(i18n("Duplicate"),   QStringLiteral("duplicate"));
    autoLayout->addRow(i18n("Auto-resolve strategy:"), m_autoResolveCombo);

    m_fallbackCombo = new QComboBox(page);
    m_fallbackCombo->addItem(i18n("Defer"),        QStringLiteral("defer"));
    m_fallbackCombo->addItem(i18n("Skip"),         QStringLiteral("skip"));
    m_fallbackCombo->addItem(i18n("Use Default"),  QStringLiteral("use_default"));
    autoLayout->addRow(i18n("Fallback behavior:"), m_fallbackCombo);

    mainLayout->addLayout(autoLayout);

    // Interactive Conflict Resolution group
    auto *interactiveGroup = new QGroupBox(i18n("Interactive Conflict Resolution"), page);
    auto *interactiveLayout = new QFormLayout(interactiveGroup);

    auto *interactiveNote = new QLabel(
        i18n("These settings apply when auto-resolve is \"None\" or cannot resolve a conflict."),
        interactiveGroup);
    interactiveNote->setWordWrap(true);
    interactiveNote->setStyleSheet(QStringLiteral("color: #666; font-size: 11px; margin-bottom: 6px;"));
    interactiveLayout->addRow(interactiveNote);

    m_promptStrategyCombo = new QComboBox(interactiveGroup);
    m_promptStrategyCombo->addItem(i18n("Always Ask"),    QStringLiteral("always_ask"));
    m_promptStrategyCombo->addItem(i18n("First Only"),    QStringLiteral("first_only"));
    m_promptStrategyCombo->addItem(i18n("Batch at End"),  QStringLiteral("batch_at_end"));
    interactiveLayout->addRow(i18n("Prompt strategy:"), m_promptStrategyCombo);

    m_connectionBehaviorCombo = new QComboBox(interactiveGroup);
    m_connectionBehaviorCombo->addItem(i18n("Keep Alive"),           QStringLiteral("keep_alive"));
    m_connectionBehaviorCombo->addItem(i18n("Disconnect && Defer"),   QStringLiteral("disconnect_and_defer"));
    m_connectionBehaviorCombo->addItem(i18n("Timeout && Defer"),      QStringLiteral("timeout_and_defer"));
    interactiveLayout->addRow(i18n("Connection behavior:"), m_connectionBehaviorCombo);

    m_timeoutSpinBox = new QSpinBox(interactiveGroup);
    m_timeoutSpinBox->setRange(15, 300);
    m_timeoutSpinBox->setValue(60);
    m_timeoutSpinBox->setSuffix(i18n(" seconds"));
    interactiveLayout->addRow(i18n("Dialog timeout:"), m_timeoutSpinBox);

    mainLayout->addWidget(interactiveGroup);
    mainLayout->addStretch();

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

    // Conflict page
    int resolveIdx = m_autoResolveCombo->findData(m_profile->conflictAutoResolve());
    if (resolveIdx >= 0) m_autoResolveCombo->setCurrentIndex(resolveIdx);

    int fallbackIdx = m_fallbackCombo->findData(m_profile->conflictFallback());
    if (fallbackIdx >= 0) m_fallbackCombo->setCurrentIndex(fallbackIdx);

    int promptIdx = m_promptStrategyCombo->findData(m_profile->conflictPromptStrategy());
    if (promptIdx >= 0) m_promptStrategyCombo->setCurrentIndex(promptIdx);

    int connIdx = m_connectionBehaviorCombo->findData(m_profile->conflictConnectionBehavior());
    if (connIdx >= 0) m_connectionBehaviorCombo->setCurrentIndex(connIdx);

    m_timeoutSpinBox->setValue(m_profile->conflictTimeoutSeconds());
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

    // Conflict page
    m_profile->setConflictAutoResolve(
        m_autoResolveCombo->currentData().toString());
    m_profile->setConflictFallback(
        m_fallbackCombo->currentData().toString());
    m_profile->setConflictPromptStrategy(
        m_promptStrategyCombo->currentData().toString());
    m_profile->setConflictConnectionBehavior(
        m_connectionBehaviorCombo->currentData().toString());
    m_profile->setConflictTimeoutSeconds(
        m_timeoutSpinBox->value());

    m_profile->save();
}

// ========== Slots ==========

void ProfilePropertiesDialog::onApply()
{
    const QString trimmedName = m_nameEdit->text().trimmed();
    if (!trimmedName.isEmpty() && trimmedName != m_profile->name()) {
        emit renameRequested(m_profile->id(), trimmedName);
    }

    saveSettings();
    emit settingsChanged();
}
