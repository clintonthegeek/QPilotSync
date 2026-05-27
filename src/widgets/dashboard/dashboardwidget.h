#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QWidget>

class QLabel;
class QPushButton;
class QProgressBar;
class QHBoxLayout;
class QTimer;
class SyncStatusModel;

/**
 * Two-tier status strip rendered entirely from a SyncStatusModel.
 * Top row: device | profile | "now" zone (headline/progress) + primary button.
 * Bottom row: per-conduit chips.
 */
class DashboardWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DashboardWidget(QWidget *parent = nullptr);
    ~DashboardWidget() override = default;

    /// Bind to the model (borrowed). Re-bindable per profile load.
    void setModel(SyncStatusModel *model);

private slots:
    void render();              // full re-render from the model

private:
    void setupUI();
    void renderConduits();
    void applyHeadline();   // sets headline text, with spinner prefix while syncing

    SyncStatusModel *m_model = nullptr;

    QLabel *m_deviceIconLabel = nullptr;
    QLabel *m_deviceNameLabel = nullptr;
    QLabel *m_deviceStatusLabel = nullptr;
    QLabel *m_deviceDetailsLabel = nullptr;
    QLabel *m_profileNameLabel = nullptr;
    QLabel *m_lastSyncLabel = nullptr;
    QLabel *m_autoSyncLabel = nullptr;
    QLabel *m_headlineLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_primaryButton = nullptr;
    QPushButton *m_conflictButton = nullptr;
    QHBoxLayout *m_conduitRow = nullptr;
    QTimer *m_relativeTimer = nullptr;   // refreshes "synced N ago"
    QTimer *m_spinTimer = nullptr;
    int m_spinPhase = 0;
};

#endif // DASHBOARDWIDGET_H
