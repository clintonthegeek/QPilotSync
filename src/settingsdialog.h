#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <KPageDialog>
#include <QMap>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class KPageWidgetItem;
class ConduitManager;

/**
 * @brief Global settings dialog using KPageDialog (icon-sidebar layout)
 *
 * Provides configuration for global (non-profile-specific) settings:
 *   - Conduits: Enable/disable conduits grouped by Palm creator ID
 *   - Profiles: Default profile, recent profiles list
 *   - Devices: View registered devices
 *   - Advanced: System tray, debug options
 *
 * Conduit config pages are dynamically added/removed in the sidebar
 * when conduits are enabled or disabled.
 */
class SettingsDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(ConduitManager *conduitManager,
                            QWidget *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();

private Q_SLOTS:
    void onConduitToggled(QTreeWidgetItem *item, int column);
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
    QWidget* createConduitsPage();
    QWidget* createProfilesPage();
    QWidget* createDevicesPage();
    QWidget* createAdvancedPage();

    void addConduitConfigPages(const QString &conduitId);
    void removeConduitConfigPages(const QString &conduitId);

    ConduitManager *m_conduitManager;

    // Conduits page
    QTreeWidget *m_conduitTree;
    QLabel *m_conduitDetailLabel;
    QMap<QString, QList<KPageWidgetItem*>> m_conduitConfigPages;

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
