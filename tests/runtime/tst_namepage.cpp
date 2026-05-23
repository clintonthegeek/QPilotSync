// tests/runtime/tst_namepage.cpp
#include <QtTest/QtTest>
#include <QLineEdit>
#include <QTemporaryDir>

#include "../wildpalms_qtest_main.h"

#include "app/wizard/namepage.h"
#include "app/wizard/wizardstate.h"
#include "runtime/profileregistry.h"

#include <KSharedConfig>

using WildPalms::Wizard::NamePage;
using WildPalms::Wizard::WizardState;
using WildPalms::Runtime::ProfileRegistry;

class TstNamePage : public QObject {
    Q_OBJECT
private slots:
    void emptyNameBlocksNext();
    void duplicateNameBlocksNext();
    void uniqueNameWritesToState();
};

namespace {
std::unique_ptr<ProfileRegistry> makeRegistry(QTemporaryDir &dir) {
    auto cfg = KSharedConfig::openConfig(
        dir.path() + QStringLiteral("/wildpalmsrc"));
    auto r = std::make_unique<ProfileRegistry>(cfg);
    r->setDefaultRoot(dir.path() + QStringLiteral("/wp-root"));
    return r;
}
} // namespace

void TstNamePage::emptyNameBlocksNext()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QString());
    QVERIFY(!page.isComplete());
}

void TstNamePage::duplicateNameBlocksNext()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    QVERIFY(reg->registerNew(QStringLiteral("Palm")).isValid());
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("palm"));   // case-insensitive
    QVERIFY(!page.isComplete());
}

void TstNamePage::uniqueNameWritesToState()
{
    QTemporaryDir d; QVERIFY(d.isValid());
    auto reg = makeRegistry(d);
    WizardState s;
    NamePage page(reg.get(), &s);
    auto *edit = page.findChild<QLineEdit*>();
    QVERIFY(edit);
    edit->setText(QStringLiteral("New"));
    QVERIFY(page.isComplete());
    QVERIFY(page.validatePage());
    QCOMPARE(s.profileName, QStringLiteral("New"));
}

WILDPALMS_QTEST_MAIN(TstNamePage)
#include "tst_namepage.moc"
