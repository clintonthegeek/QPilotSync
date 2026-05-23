#ifndef WILDPALMS_APP_WIZARD_NAMEPAGE_H
#define WILDPALMS_APP_WIZARD_NAMEPAGE_H

#include <QWizardPage>

class QLineEdit;
class QLabel;

namespace WildPalms::Runtime { class ProfileRegistry; }

namespace WildPalms::Wizard {

struct WizardState;

class NamePage : public QWizardPage {
    Q_OBJECT
public:
    NamePage(WildPalms::Runtime::ProfileRegistry *registry,
             WizardState *state,
             QWidget *parent = nullptr);

    bool isComplete() const override;
    bool validatePage() override;

private:
    bool isUnique(const QString &name) const;

    WildPalms::Runtime::ProfileRegistry *m_registry;
    WizardState *m_state;
    QLineEdit   *m_edit {nullptr};
    QLabel      *m_warning {nullptr};
};

}  // namespace WildPalms::Wizard

#endif
