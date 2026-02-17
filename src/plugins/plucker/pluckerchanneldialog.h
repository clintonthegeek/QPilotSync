#ifndef PLUCKERCHANNELDIALOG_H
#define PLUCKERCHANNELDIALOG_H

#include <QDialog>
#include "pluckerconfig.h"

class QTabWidget;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QRadioButton;
class QLabel;
class QDialogButtonBox;

class PluckerChannelDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PluckerChannelDialog(QWidget *parent = nullptr);
    PluckerChannelDialog(const PluckerChannel &channel, QWidget *parent = nullptr);

    PluckerChannel channel() const;

private:
    void setupUI();
    void loadFromChannel();
    void applyToChannel();

    PluckerChannel m_channel;

    // Tab 1: Starting Page
    QLineEdit *m_urlEdit;
    QLineEdit *m_nameEdit;
    QComboBox *m_categoryCombo;

    // Tab 2: Spidering
    QSpinBox *m_maxDepthSpin;
    QCheckBox *m_stayOnHostCheck;
    QRadioButton *m_breadthFirstRadio;
    QRadioButton *m_depthFirstRadio;
    QLineEdit *m_urlPatternEdit;
    QLineEdit *m_userAgentEdit;

    // Tab 3: Images
    QComboBox *m_bppCombo;
    QSpinBox *m_maxWidthSpin;
    QSpinBox *m_maxHeightSpin;
    QSpinBox *m_altMaxWidthSpin;
    QSpinBox *m_altMaxHeightSpin;
    QSpinBox *m_imageCompLimitSpin;

    // Tab 4: Destination
    QRadioButton *m_ramRadio;
    QRadioButton *m_sdRadio;
    QRadioButton *m_msRadio;
    QRadioButton *m_cfRadio;
    QLineEdit *m_cardDirEdit;
    QComboBox *m_compressionCombo;

    // Tab 5: Scheduling
    QCheckBox *m_autoUpdateCheck;
    QSpinBox *m_frequencySpin;
    QComboBox *m_periodCombo;
    QLabel *m_nextDueLabel;
};

#endif // PLUCKERCHANNELDIALOG_H
