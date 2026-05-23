// tests/runtime/tst_accountformwidget.cpp
#include <QtTest/QtTest>
#include <QComboBox>
#include "../wildpalms_qtest_main.h"

#include "app/accounts/accountformwidget.h"
#include <backendregistry.h>
#include <backendconfiguration.h>

using WildPalms::App::Accounts::AccountFormWidget;
using Kalburator::Sync::BackendRegistry;

class TstAccountFormWidget : public QObject
{
    Q_OBJECT
private slots:
    void widgetExposesKindCombo();
    void emptyRegistryYieldsEmptySelectedKind();
    void emptyRegistryYieldsInvalidConfiguration();
    void lockedKindOnEmptyRegistryDoesNotHideCombo();
};

void TstAccountFormWidget::widgetExposesKindCombo()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY2(combo, "AccountFormWidget must own a QComboBox for kind selection");
}

void TstAccountFormWidget::emptyRegistryYieldsEmptySelectedKind()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    QCOMPARE(w.selectedKind(), QString());
}

void TstAccountFormWidget::emptyRegistryYieldsInvalidConfiguration()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    QVERIFY(!w.isValid());
    const auto cfg = w.configuration();
    QVERIFY(!cfg.isValid());
}

void TstAccountFormWidget::lockedKindOnEmptyRegistryDoesNotHideCombo()
{
    // With no contributions registered, the locked-kind lookup falls through
    // (lockedIndex stays -1) and the combo stays visible per the fallback
    // branch. This documents the contract for the wizard's AddAccountsPage:
    // a locked kind that isn't in the registry won't silently hide UI.
    BackendRegistry reg;
    AccountFormWidget w(&reg, QStringLiteral("caldav"));
    w.show();
    QTest::qWait(20);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY(combo);
    QVERIFY2(combo->isVisible(),
             "Locked kind not found in registry: combo must remain visible "
             "as a fallback");
}

WILDPALMS_QTEST_MAIN(TstAccountFormWidget)
#include "tst_accountformwidget.moc"
