#ifndef WILDPALMS_APP_WIZARD_REVIEWPAGE_H
#define WILDPALMS_APP_WIZARD_REVIEWPAGE_H

#include <QWizardPage>

class QLabel;

namespace WildPalms::Wizard {

struct WizardState;

class ReviewPage : public QWizardPage {
    Q_OBJECT
public:
    explicit ReviewPage(WizardState *state, QWidget *parent = nullptr);

    void initializePage() override;

private:
    WizardState *m_state;
    QLabel      *m_label {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
