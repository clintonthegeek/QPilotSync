#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>

class QLabel;
class QVBoxLayout;
class Profile;

/**
 * @brief Compact status header showing device, profile, and sync state
 *
 * Sits between the toolbar and the conduit page area.
 * Height is fixed to ~120-160 px.
 */
class DashboardWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override = default;

    void updateStatus(Profile *profile, bool connected);

    void setListening(bool listening);
    void setSyncing(bool syncing, const QString &deviceName = QString());
    void setLastSyncSummary(const QString &summary);

private:
    void setupUI();

    // Left: device info
    QLabel *m_deviceIconLabel;
    QLabel *m_deviceNameLabel;
    QLabel *m_deviceStatusLabel;
    QLabel *m_deviceDetailsLabel;

    // Center: profile info
    QLabel *m_profileNameLabel;
    QLabel *m_lastSyncLabel;

    // Right: status headline
    QLabel *m_statusLabel;

    bool m_connected = false;
};

#endif // DASHBOARDWIDGET_H
