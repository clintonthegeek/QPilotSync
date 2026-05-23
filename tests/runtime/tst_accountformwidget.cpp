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
};

void TstAccountFormWidget::widgetExposesKindCombo()
{
    BackendRegistry reg;
    AccountFormWidget w(&reg);
    auto *combo = w.findChild<QComboBox*>();
    QVERIFY2(combo, "AccountFormWidget must own a QComboBox for kind selection");
}

WILDPALMS_QTEST_MAIN(TstAccountFormWidget)
#include "tst_accountformwidget.moc"
