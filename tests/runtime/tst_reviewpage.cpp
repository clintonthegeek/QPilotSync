// tests/runtime/tst_reviewpage.cpp
#include <QtTest/QtTest>
#include <QLabel>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/reviewpage.h"
#include "app/wizard/wizardstate.h"

using WildPalms::Wizard::ReviewPage;
using WildPalms::Wizard::WizardState;
using WildPalms::Wizard::WizardAccount;
using WildPalms::Wizard::MappingSpec;
using WildPalms::Wizard::TargetKind;

class TstReviewPage : public QObject {
    Q_OBJECT
private slots:
    void allLocalRendersBareSummary();
    void mixedRendersAccountsAndCollections();
};

namespace {
WizardState seed(const QString &name) {
    WizardState s;
    s.profileName = name;
    for (const auto &pid : { QStringLiteral("calendar"),
                              QStringLiteral("contacts"),
                              QStringLiteral("memo"),
                              QStringLiteral("todo") }) {
        MappingSpec m; m.pluginId = pid; m.kind = TargetKind::RawFiles;
        s.mappings.append(m);
    }
    return s;
}
} // namespace

void TstReviewPage::allLocalRendersBareSummary()
{
    auto s = seed(QStringLiteral("Palm m505"));
    ReviewPage page(&s);
    page.initializePage();
    auto *label = page.findChild<QLabel*>();
    QVERIFY(label);
    QVERIFY(label->text().contains(QStringLiteral("Palm m505")));
    QVERIFY(label->text().contains(QStringLiteral("Local")));
}

void TstReviewPage::mixedRendersAccountsAndCollections()
{
    auto s = seed(QStringLiteral("Mixed"));
    WizardAccount a;
    a.id   = QStringLiteral("a-id");
    a.kind = QStringLiteral("multiproto-dav");
    a.config.displayName = QStringLiteral("Fastmail");
    s.accounts.append(a);
    s.mappings[0].kind         = TargetKind::Account;
    s.mappings[0].accountRef   = a.id;
    s.mappings[0].collectionId = QStringLiteral("personal");

    ReviewPage page(&s);
    page.initializePage();
    auto *label = page.findChild<QLabel*>();
    QVERIFY(label);
    const QString text = label->text();
    QVERIFY(text.contains(QStringLiteral("Fastmail")));
    QVERIFY(text.contains(QStringLiteral("personal")));
    QVERIFY(text.contains(QStringLiteral("multiproto-dav"), Qt::CaseInsensitive));
}

WILDPALMS_QTEST_MAIN(TstReviewPage)
#include "tst_reviewpage.moc"
