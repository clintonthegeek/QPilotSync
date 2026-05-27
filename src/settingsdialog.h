#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QPushButton;
class QLabel;
class QSpinBox;
class Profile;

namespace WildPalms::Runtime {
    class AccountController;
    class PalmRuntime;
    class ProfileRegistry;
}
namespace WildPalms::AppMapping {
    class SyncMappingsPage;
}
namespace WildPalms::App::Accounts {
    class AccountsPage;
}
class KPageWidgetItem;

/**
 * @brief Global settings dialog using KPageDialog (icon-sidebar layout)
 *
 * Provides configuration for global (non-profile-specific) settings:
 *   - Profiles: Default profile, recent profiles list
 *   - Devices: View registered devices
 *   - Sync: Default conflict policies (per-profile)
 *   - Advanced: System tray, debug options
 *
 * The Sync page is only populated when a Profile is supplied via the
 * constructor. Sync settings round-trip through Profile::conflict* accessors
 * and are saved via Profile::save().
 */
class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr, Profile *profile = nullptr);

    /// F.3: borrow AccountController for the Accounts + Sync Mappings
    /// pages. Must be called BEFORE exec() and BEFORE setPalmRuntime()
    /// (the Sync Mappings page is added only when both are non-null).
    /// Non-owning — must outlive the dialog.
    void setAccountController(WildPalms::Runtime::AccountController *ac);

    /// F.3: borrow PalmRuntime for the Sync Mappings page (read-only
    /// banner + reloadMappings on apply). Non-owning. nullptr leaves
    /// the Sync Mappings page unbuilt.
    void setPalmRuntime(WildPalms::Runtime::PalmRuntime *palmRuntime);

    /// Borrow the app-level ProfileRegistry so the Profiles page can show
    /// real display names, ids/paths and last-used times (instead of folder
    /// basenames read from the KF6Settings "recent paths" list). Non-owning;
    /// must outlive the dialog. Call before exec().
    void setProfileRegistry(WildPalms::Runtime::ProfileRegistry *registry);

    /// F.3: navigate to the Sync Mappings page (no-op if not present).
    /// Used by KF6MainWindow::onConfigureMappings to deep-link.
    void navigateToSyncMappings();

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
    // F.3: forward Apply to per-page handlers.
    void onApplyAccountsAndMappings();

private:
    void loadSettings();
    void saveSettings();
    void buildAccountsAndMappingsPagesIfReady();
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
    QTreeWidget *m_profilesTree;
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

    // F.3: Accounts + Sync Mappings pages (added when controllers supplied)
    WildPalms::Runtime::AccountController *m_accountController = nullptr;
    WildPalms::Runtime::PalmRuntime       *m_palmRuntime = nullptr;
    WildPalms::Runtime::ProfileRegistry   *m_profileRegistry = nullptr;
    WildPalms::App::Accounts::AccountsPage *m_accountsPage = nullptr;
    WildPalms::AppMapping::SyncMappingsPage *m_syncMappingsPage = nullptr;
    KPageWidgetItem                       *m_syncMappingsPageItem = nullptr;
};

#endif // SETTINGSDIALOG_H
