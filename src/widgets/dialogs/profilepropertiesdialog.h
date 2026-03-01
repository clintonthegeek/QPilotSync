#ifndef PROFILEPROPERTIESDIALOG_H
#define PROFILEPROPERTIESDIALOG_H

#include <KPageDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class Profile;
class ConduitManager;

/**
 * @brief Profile settings dialog with Device, Conduits, and Conflict pages
 *
 * A KPageDialog opened from File -> Profile Settings.
 * The profile is the sole source of truth for which conduits are enabled
 * and how the device/sync connection is configured.
 */
class ProfilePropertiesDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit ProfilePropertiesDialog(Profile *profile,
                                      ConduitManager *conduitManager,
                                      QWidget *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();

private Q_SLOTS:
    void onApply();

private:
    void loadSettings();
    void saveSettings();
    QWidget* createDevicePage();
    QWidget* createConduitsPage();
    QWidget* createConflictPage();

    Profile *m_profile;
    ConduitManager *m_conduitManager;

    // Device page
    QLineEdit *m_devicePathEdit;
    QComboBox *m_baudRateCombo;
    QComboBox *m_connectionModeCombo;
    QCheckBox *m_autoSyncCheck;
    QComboBox *m_defaultSyncTypeCombo;

    // Conduits page
    QMap<QString, QCheckBox*> m_conduitChecks;

    // Conflict page
    QComboBox *m_autoResolveCombo;
    QComboBox *m_fallbackCombo;
    QComboBox *m_promptStrategyCombo;
    QComboBox *m_connectionBehaviorCombo;
    QSpinBox *m_timeoutSpinBox;
};

#endif // PROFILEPROPERTIESDIALOG_H
