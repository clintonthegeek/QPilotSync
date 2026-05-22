#include <QtTest/QtTest>

#include "../../src/runtime/massdeleteguardpresenter.h"
#include "../wildpalms_qtest_main.h"

class TstMassDeleteGuardPresenter : public QObject
{
    Q_OBJECT
private slots:
    void confirmMassDeleteRoutesThroughPromptUser();
    void confirmReturnsPromptResult();
    void promptArgsCarryAllFields();
};

namespace {

class PresenterFixture : public WildPalms::Runtime::MassDeleteGuardPresenter {
public:
    using MassDeleteGuardPresenter::MassDeleteGuardPresenter;

    int    invocations = 0;
    QString lastMapping;
    QString lastBackend;
    int    lastProposed = -1;
    int    lastBaseline = -1;
    bool   nextAnswer   = true;

protected:
    bool promptUser(const QString &mappingId,
                    const QString &targetBackendId,
                    int proposedDeletes,
                    int baselineCount) override {
        ++invocations;
        lastMapping  = mappingId;
        lastBackend  = targetBackendId;
        lastProposed = proposedDeletes;
        lastBaseline = baselineCount;
        return nextAnswer;
    }
};

} // namespace

void TstMassDeleteGuardPresenter::confirmMassDeleteRoutesThroughPromptUser()
{
    PresenterFixture p(nullptr);
    p.nextAnswer = true;
    const bool result = p.confirmMassDelete(
        QStringLiteral("default-contacts-palm_contact_0"),
        QStringLiteral("rawfiles-contacts-palm_contact_0"),
        84, 84);
    QCOMPARE(p.invocations, 1);
    QVERIFY(result);
}

void TstMassDeleteGuardPresenter::confirmReturnsPromptResult()
{
    PresenterFixture p(nullptr);
    p.nextAnswer = false;
    const bool result = p.confirmMassDelete(
        QStringLiteral("X"), QStringLiteral("Y"), 50, 100);
    QVERIFY(!result);
}

void TstMassDeleteGuardPresenter::promptArgsCarryAllFields()
{
    PresenterFixture p(nullptr);
    p.confirmMassDelete(
        QStringLiteral("MAP"),
        QStringLiteral("BACK"),
        12, 40);
    QCOMPARE(p.lastMapping,  QStringLiteral("MAP"));
    QCOMPARE(p.lastBackend,  QStringLiteral("BACK"));
    QCOMPARE(p.lastProposed, 12);
    QCOMPARE(p.lastBaseline, 40);
}

WILDPALMS_QTEST_MAIN(TstMassDeleteGuardPresenter)
#include "tst_massdeleteguardpresenter.moc"
