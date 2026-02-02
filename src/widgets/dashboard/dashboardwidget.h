#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class Profile;

/**
 * @brief Dashboard widget showing device and sync status
 *
 * Displays:
 * - Device connection status
 * - Current profile info
 * - Last sync info
 * - Quick action buttons
 */
class DashboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override = default;

    void updateStatus(Profile *profile, bool connected);

Q_SIGNALS:
    void hotSyncRequested();
    void connectRequested();

private:
    void setupUI();
    void createDeviceCard();
    void createProfileCard();
    void createActionsCard();

    // Device status card
    QLabel *m_deviceIconLabel;
    QLabel *m_deviceNameLabel;
    QLabel *m_deviceStatusLabel;
    QLabel *m_deviceUserLabel;

    // Profile card
    QLabel *m_profileNameLabel;
    QLabel *m_profilePathLabel;
    QLabel *m_lastSyncLabel;

    // Actions
    QPushButton *m_connectButton;
    QPushButton *m_hotSyncButton;

    bool m_connected;
};

#endif // DASHBOARDWIDGET_H
