#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QDir>

#include "runtime/accountcontroller.h"
#include "runtime/palmruntime.h"
#include "profile.h"

#include "backendregistry.h"

class TstAccountController : public QObject {
    Q_OBJECT
private slots:
    void constructs_and_destructs_cleanly();
};

void TstAccountController::constructs_and_destructs_cleanly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Profile profile(dir.path());
    profile.initialize();

    WildPalms::Runtime::PalmRuntime rt(dir.path() + "/state");

    using AC = WildPalms::Runtime::AccountController;
    AC ac(dir.path(),
          &rt.backendRegistry(),
          &profile,
          &rt);

    QCOMPARE(ac.providers().size(), 0);
    QCOMPARE(ac.mappingCountFor("nonexistent"), 0);
}

QTEST_MAIN(TstAccountController)
#include "tst_account_controller.moc"
