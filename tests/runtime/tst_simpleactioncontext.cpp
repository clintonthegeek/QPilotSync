#include <QtTest/QtTest>
#include <QSignalSpy>

#include "runtime/simpleactioncontext.h"

using WildPalms::SimpleActionContext;

class TestSimpleActionContext : public QObject
{
    Q_OBJECT
private slots:
    void setTotalEmitsProgress();
    void setCurrentEmitsProgress();
    void logEmitsMessage();
    void cancelFlipsFlag();
};

void TestSimpleActionContext::setTotalEmitsProgress()
{
    SimpleActionContext ctx;
    QSignalSpy spy(&ctx, &SimpleActionContext::progress);
    ctx.setTotal(10);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 0);  // current
    QCOMPARE(spy.first().at(1).toInt(), 10); // total
    QCOMPARE(ctx.total(), 10);
}

void TestSimpleActionContext::setCurrentEmitsProgress()
{
    SimpleActionContext ctx;
    ctx.setTotal(10);
    QSignalSpy spy(&ctx, &SimpleActionContext::progress);
    ctx.setCurrent(3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3);
    QCOMPARE(spy.first().at(1).toInt(), 10);
    QCOMPARE(ctx.current(), 3);
}

void TestSimpleActionContext::logEmitsMessage()
{
    SimpleActionContext ctx;
    QSignalSpy spy(&ctx, &SimpleActionContext::message);
    ctx.log(QStringLiteral("hello"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("hello"));
}

void TestSimpleActionContext::cancelFlipsFlag()
{
    SimpleActionContext ctx;
    QVERIFY(!ctx.isCancelled());
    ctx.cancel();
    QVERIFY(ctx.isCancelled());
}

QTEST_MAIN(TestSimpleActionContext)
#include "tst_simpleactioncontext.moc"
