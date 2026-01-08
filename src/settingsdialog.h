#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>

/**
 * @brief Global settings dialog with tabbed interface
 *
 * Provides configuration for global (non-profile-specific) settings:
 *   - Profiles: Default profile, recent profiles list
 *   - Devices: View registered devices (read-only)
 *   - Advanced: Debug options
 *
 * Profile-specific settings (device port/baud, sync folder, conflict policy, conduits)
 * are configured through File → Profile Settings menu item.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // Load current settings into UI
    void loadSettings();

    // Save UI values to settings
    void saveSettings();

signals:
    void settingsChanged();

private slots:
    void onSetDefaultProfile();
    void onBrowseDefaultProfile();
    void onClearDefaultProfile();
    void onRemoveRecentProfile();
    void onClearRecentProfiles();
    void onClearDeviceRegistry();
    void onAccept();
    void onApply();

private:
    void setupUi();
    QWidget* createProfilesPage();
    QWidget* createDevicesPage();
    QWidget* createAdvancedPage();

    // Tab widget
    QTabWidget *m_tabWidget;

    // Profiles page widgets
    QLineEdit *m_defaultProfileEdit;
    QPushButton *m_browseProfileBtn;
    QPushButton *m_clearProfileBtn;
    QListWidget *m_recentProfilesList;
    QPushButton *m_setDefaultBtn;
    QPushButton *m_removeRecentBtn;
    QPushButton *m_clearRecentBtn;

    // Devices page widgets
    QListWidget *m_deviceRegistryList;
    QPushButton *m_clearRegistryBtn;

    // Advanced page widgets
    QCheckBox *m_debugLoggingCheck;
    QLabel *m_configFileLabel;

    // Dialog buttons
    QPushButton *m_okButton;
    QPushButton *m_cancelButton;
    QPushButton *m_applyButton;
};

#endif // SETTINGSDIALOG_H
