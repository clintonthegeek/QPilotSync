#ifndef PROFILEPROPERTIESDIALOG_H
#define PROFILEPROPERTIESDIALOG_H

#include <KPageDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class Profile;

/**
 * @brief Profile settings dialog with Device and Conflict pages
 *
 * A KPageDialog opened from File -> Profile Settings.
 * The profile is the sole source of truth for how the device/sync
 * connection is configured.
 */
class ProfilePropertiesDialog : public KPageDialog
{
    Q_OBJECT

public:
    explicit ProfilePropertiesDialog(Profile *profile,
                                      QWidget *parent = nullptr);

Q_SIGNALS:
    void settingsChanged();
    void renameRequested(QString id, QString newName);

private Q_SLOTS:
    void onApply();

private:
    void loadSettings();
    void saveSettings();
    QWidget* createGeneralPage();
    QWidget* createDevicePage();
    QWidget* createConflictPage();

    Profile *m_profile;

    // General page (F.1b)
    QLineEdit *m_nameEdit;

    // Device page
    QLineEdit *m_devicePathEdit;
    QComboBox *m_baudRateCombo;
    QComboBox *m_connectionModeCombo;
    QCheckBox *m_autoSyncCheck;
    QComboBox *m_defaultSyncTypeCombo;

    // Conflict page
    QComboBox *m_autoResolveCombo;
    QComboBox *m_fallbackCombo;
    QComboBox *m_promptStrategyCombo;
    QComboBox *m_connectionBehaviorCombo;
    QSpinBox *m_timeoutSpinBox;
};

#endif // PROFILEPROPERTIESDIALOG_H
