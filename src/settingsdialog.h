#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;

/**
 * @brief Global settings dialog using KPageDialog (icon-sidebar layout)
 *
 * Provides configuration for global (non-profile-specific) settings:
 *   - Profiles: Default profile, recent profiles list
 *   - Devices: View registered devices
 *   - Advanced: System tray, debug options
 */
class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();

private Q_SLOTS:
    void onSetDefaultProfile();
    void onBrowseDefaultProfile();
    void onClearDefaultProfile();
    void onRemoveRecentProfile();
    void onClearRecentProfiles();
    void onClearDeviceRegistry();
    void onApply();

private:
    void loadSettings();
    void saveSettings();
    QWidget* createProfilesPage();
    QWidget* createDevicesPage();
    QWidget* createAdvancedPage();

    // Profiles page
    QLineEdit *m_defaultProfileEdit;
    QPushButton *m_browseProfileBtn;
    QPushButton *m_clearProfileBtn;
    QListWidget *m_recentProfilesList;
    QPushButton *m_setDefaultBtn;
    QPushButton *m_removeRecentBtn;
    QPushButton *m_clearRecentBtn;

    // Devices page
    QListWidget *m_deviceRegistryList;
    QPushButton *m_clearRegistryBtn;

    // Advanced page
    QCheckBox *m_minimizeToTrayCheck;
    QCheckBox *m_debugLoggingCheck;
    QLabel *m_configFileLabel;
};

#endif // SETTINGSDIALOG_H
