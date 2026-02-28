#include "dashboardwidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QLocale>

#include <KLocalizedString>

#include "../../profile.h"

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void DashboardWidget::setupUI()
{
    // Fixed-height strip
    setFixedHeight(120);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(12, 8, 12, 8);
    outer->setSpacing(16);

    // --- Left: device icon + name/status ---
    m_deviceIconLabel = new QLabel;
    m_deviceIconLabel->setPixmap(
        QIcon::fromTheme(QStringLiteral("phone")).pixmap(48, 48));
    m_deviceIconLabel->setFixedSize(48, 48);
    outer->addWidget(m_deviceIconLabel);

    auto *deviceCol = new QVBoxLayout;
    deviceCol->setSpacing(2);

    m_deviceNameLabel = new QLabel(i18n("No device"));
    m_deviceNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    deviceCol->addWidget(m_deviceNameLabel);

    m_deviceStatusLabel = new QLabel(i18n("Disconnected"));
    m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    deviceCol->addWidget(m_deviceStatusLabel);

    m_deviceDetailsLabel = new QLabel;
    m_deviceDetailsLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    m_deviceDetailsLabel->hide();
    deviceCol->addWidget(m_deviceDetailsLabel);

    deviceCol->addStretch();
    outer->addLayout(deviceCol);

    // --- Separator ---
    auto *sep1 = new QFrame;
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);
    outer->addWidget(sep1);

    // --- Center: profile + last sync ---
    auto *profileCol = new QVBoxLayout;
    profileCol->setSpacing(2);

    m_profileNameLabel = new QLabel(i18n("No profile"));
    m_profileNameLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
    profileCol->addWidget(m_profileNameLabel);

    m_lastSyncLabel = new QLabel(i18n("Last sync: Never"));
    m_lastSyncLabel->setStyleSheet(QStringLiteral("color: gray;"));
    profileCol->addWidget(m_lastSyncLabel);

    profileCol->addStretch();
    outer->addLayout(profileCol);

    // --- Separator ---
    auto *sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    outer->addWidget(sep2);

    // --- Right: status headline (stretches) ---
    m_statusLabel = new QLabel;
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("font-size: 13px;"));
    m_statusLabel->setWordWrap(true);
    outer->addWidget(m_statusLabel, 1);

    setListening(true);
}

void DashboardWidget::updateStatus(Profile *profile, bool connected)
{
    m_connected = connected;

    if (profile) {
        m_profileNameLabel->setText(profile->name());

        // Show last sync time from profile
        QDateTime lastSync = profile->lastSyncTime();
        if (lastSync.isValid()) {
            m_lastSyncLabel->setText(i18n("Last sync: %1",
                QLocale().toString(lastSync, QLocale::ShortFormat)));
        } else {
            m_lastSyncLabel->setText(i18n("Last sync: Never"));
        }

        // Show device info from profile fingerprint (even when disconnected)
        DeviceFingerprint fp = profile->deviceFingerprint();
        if (fp.isValid()) {
            m_deviceNameLabel->setText(fp.displayString());

            // Show extended device details if available
            if (fp.hasExtendedInfo()) {
                QStringList details;
                QString osVer = fp.palmOSVersionString();
                if (!osVer.isEmpty()) {
                    details << i18n("Palm OS %1", osVer);
                }
                if (fp.ramSize != 0) {
                    QString ramTotal = DeviceFingerprint::formatMemorySize(fp.ramSize);
                    if (fp.ramFree != 0) {
                        QString ramFree = DeviceFingerprint::formatMemorySize(fp.ramFree);
                        details << i18n("%1 (%2 free)", ramTotal, ramFree);
                    } else {
                        details << ramTotal;
                    }
                }
                if (!details.isEmpty()) {
                    m_deviceDetailsLabel->setText(details.join(QStringLiteral(" | ")));
                    m_deviceDetailsLabel->show();
                } else {
                    m_deviceDetailsLabel->hide();
                }
            } else {
                m_deviceDetailsLabel->hide();
            }
        } else {
            m_deviceNameLabel->setText(i18n("No device registered"));
            m_deviceDetailsLabel->hide();
        }
    } else {
        m_profileNameLabel->setText(i18n("No profile"));
        m_lastSyncLabel->setText(i18n("Last sync: Never"));
        m_deviceNameLabel->setText(i18n("No device"));
        m_deviceDetailsLabel->hide();
    }

    if (connected) {
        m_deviceStatusLabel->setText(i18n("Connected"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: green; font-weight: bold;"));
        m_deviceIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("network-connect")).pixmap(48, 48));
        m_statusLabel->setText(i18n("Ready to sync"));
    } else {
        m_deviceStatusLabel->setText(i18n("Disconnected"));
        m_deviceStatusLabel->setStyleSheet(QStringLiteral("color: gray;"));
        m_deviceIconLabel->setPixmap(
            QIcon::fromTheme(QStringLiteral("phone")).pixmap(48, 48));
        setListening(true);
    }
}

void DashboardWidget::setListening(bool listening)
{
    if (listening) {
        m_statusLabel->setText(i18n("Listening for Palm devices...\nPress HotSync on your Palm."));
    } else {
        m_statusLabel->setText(i18n("Idle"));
    }
}

void DashboardWidget::setSyncing(bool syncing, const QString &deviceName)
{
    if (syncing) {
        if (deviceName.isEmpty()) {
            m_statusLabel->setText(i18n("Syncing..."));
        } else {
            m_statusLabel->setText(i18n("Syncing with %1...", deviceName));
        }
    } else {
        setListening(true);
    }
}

void DashboardWidget::setLastSyncSummary(const QString &summary)
{
    m_lastSyncLabel->setText(summary);
}
