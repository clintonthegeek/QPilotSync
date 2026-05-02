#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QSpinBox;
class Profile;

/**
 * @brief Global settings dialog using KPageDialog (icon-sidebar layout)
 *
 * Provides configuration for global (non-profile-specific) settings:
 *   - Profiles: Default profile, recent profiles list
 *   - Devices: View registered devices
 *   - Sync: Default conflict policies + per-conduit enable (per-profile)
 *   - Advanced: System tray, debug options
 *
 * The Sync page is only populated when a Profile is supplied via the
 * constructor. Sync settings round-trip through Profile::conflict* and
 * Profile::conduitEnabled accessors and are saved via Profile::save().
 */
class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr, Profile *profile = nullptr);

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
    QWidget* createSyncPage();
    void loadSyncSettings();
    void saveSyncSettings();

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

    // Sync page (per-profile defaults; only present when m_profile is set)
    Profile *m_profile = nullptr;
    QComboBox *m_syncAutoResolveCombo = nullptr;
    QComboBox *m_syncFallbackCombo = nullptr;
    QComboBox *m_syncPromptCombo = nullptr;
    QComboBox *m_syncConnectionCombo = nullptr;
    QSpinBox *m_syncTimeoutSpin = nullptr;
    QListWidget *m_syncConduitList = nullptr;
};

#endif // SETTINGSDIALOG_H
