#include "dashboardwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFrame>

#include <KLocalizedString>

#include "../../profile.h"

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
    , m_deviceIconLabel(nullptr)
    , m_deviceNameLabel(nullptr)
    , m_deviceStatusLabel(nullptr)
    , m_deviceUserLabel(nullptr)
    , m_profileNameLabel(nullptr)
    , m_profilePathLabel(nullptr)
    , m_lastSyncLabel(nullptr)
    , m_connectButton(nullptr)
    , m_hotSyncButton(nullptr)
    , m_connected(false)
{
    setupUI();
}

void DashboardWidget::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(20);

    // Welcome message
    QLabel *welcomeLabel = new QLabel(i18n("<h2>Welcome to QPilotSync</h2>"));
    welcomeLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(welcomeLabel);

    // Cards layout
    QHBoxLayout *cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(20);

    createDeviceCard();
    createProfileCard();
    createActionsCard();

    // Device card
    QGroupBox *deviceCard = new QGroupBox(i18n("Device"));
    QVBoxLayout *deviceLayout = new QVBoxLayout(deviceCard);

    QHBoxLayout *deviceIconLayout = new QHBoxLayout();
    m_deviceIconLabel = new QLabel();
    m_deviceIconLabel->setPixmap(QIcon::fromTheme(QStringLiteral("phone")).pixmap(64, 64));
    deviceIconLayout->addWidget(m_deviceIconLabel);
    deviceIconLayout->addStretch();
    deviceLayout->addLayout(deviceIconLayout);

    m_deviceNameLabel = new QLabel(i18n("No device connected"));
    m_deviceNameLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    deviceLayout->addWidget(m_deviceNameLabel);

    m_deviceUserLabel = new QLabel();
    deviceLayout->addWidget(m_deviceUserLabel);

    m_deviceStatusLabel = new QLabel(i18n("Status: Disconnected"));
    deviceLayout->addWidget(m_deviceStatusLabel);

    deviceLayout->addStretch();
    cardsLayout->addWidget(deviceCard);

    // Profile card
    QGroupBox *profileCard = new QGroupBox(i18n("Profile"));
    QVBoxLayout *profileLayout = new QVBoxLayout(profileCard);

    m_profileNameLabel = new QLabel(i18n("No profile loaded"));
    m_profileNameLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 14px;"));
    profileLayout->addWidget(m_profileNameLabel);

    m_profilePathLabel = new QLabel();
    m_profilePathLabel->setWordWrap(true);
    profileLayout->addWidget(m_profilePathLabel);

    m_lastSyncLabel = new QLabel(i18n("Last sync: Never"));
    profileLayout->addWidget(m_lastSyncLabel);

    profileLayout->addStretch();
    cardsLayout->addWidget(profileCard);

    // Actions card
    QGroupBox *actionsCard = new QGroupBox(i18n("Quick Actions"));
    QVBoxLayout *actionsLayout = new QVBoxLayout(actionsCard);

    m_connectButton = new QPushButton(QIcon::fromTheme(QStringLiteral("network-connect")),
                                       i18n("Connect to Device"));
    m_connectButton->setMinimumHeight(40);
    connect(m_connectButton, &QPushButton::clicked, this, &DashboardWidget::connectRequested);
    actionsLayout->addWidget(m_connectButton);

    m_hotSyncButton = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                       i18n("Start HotSync"));
    m_hotSyncButton->setMinimumHeight(40);
    m_hotSyncButton->setEnabled(false);
    connect(m_hotSyncButton, &QPushButton::clicked, this, &DashboardWidget::hotSyncRequested);
    actionsLayout->addWidget(m_hotSyncButton);

    actionsLayout->addStretch();
    cardsLayout->addWidget(actionsCard);

    mainLayout->addLayout(cardsLayout);
    mainLayout->addStretch();
}

void DashboardWidget::createDeviceCard()
{
    // Already created in setupUI
}

void DashboardWidget::createProfileCard()
{
    // Already created in setupUI
}

void DashboardWidget::createActionsCard()
{
    // Already created in setupUI
}

void DashboardWidget::updateStatus(Profile *profile, bool connected)
{
    m_connected = connected;

    // Update device status
    if (connected) {
        m_deviceStatusLabel->setText(i18n("Status: Connected"));
        m_deviceIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("network-connect")).pixmap(64, 64));
        m_connectButton->setText(i18n("Disconnect"));
        m_hotSyncButton->setEnabled(true);
    } else {
        m_deviceNameLabel->setText(i18n("No device connected"));
        m_deviceUserLabel->clear();
        m_deviceStatusLabel->setText(i18n("Status: Disconnected"));
        m_deviceIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("phone")).pixmap(64, 64));
        m_connectButton->setText(i18n("Connect to Device"));
        m_hotSyncButton->setEnabled(false);
    }

    // Update profile info
    if (profile) {
        m_profileNameLabel->setText(profile->name());
        m_profilePathLabel->setText(profile->syncFolderPath());

        DeviceFingerprint fp = profile->deviceFingerprint();
        if (fp.isValid()) {
            m_deviceNameLabel->setText(fp.displayString());
            m_deviceUserLabel->setText(i18n("User ID: %1", fp.userId));
        }

        // TODO: Get last sync time from sync state
        m_lastSyncLabel->setText(i18n("Last sync: Unknown"));
    } else {
        m_profileNameLabel->setText(i18n("No profile loaded"));
        m_profilePathLabel->clear();
        m_lastSyncLabel->setText(i18n("Last sync: Never"));
    }
}
