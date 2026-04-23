#include <QtTest/QtTest>
#include <QPointer>
#include <QSignalSpy>

#include "core/ipluginaction.h"
#include "runtime/pluginactionmanager.h"
#include "runtime/simpleactioncontext.h"

namespace {

class FakeActionPlugin : public QObject, public WildPalms::IPluginAction
{
    Q_OBJECT
    Q_INTERFACES(WildPalms::IPluginAction)
public:
    QString pluginId() const override    { return QStringLiteral("fake-action"); }
    QString displayName() const override { return QStringLiteral("Fake Action"); }
    QIcon   icon() const override        { return {}; }
    QString description() const override { return {}; }
    QString version() const override     { return QStringLiteral("1.0"); }

    bool execute(ActionContext *ctx, PalmDeviceConnection *,
                 const QJsonObject &) override
    {
        if (ctx) {
            ctx->setTotal(2);
            ctx->setCurrent(1);
            ctx->log(QStringLiteral("did work"));
            ctx->setCurrent(2);
        }
        return true;
    }

    Preconditions preconditions() const override
    {
        Preconditions p;
        p.requiresDeviceConnection = false;
        return p;
    }
};

class TestablePluginActionManager : public WildPalms::PluginActionManager
{
public:
    using PluginActionManager::PluginActionManager;
    using PluginActionManager::registerInstanceForTest;
};

} // namespace

class TestPluginActionManager : public QObject
{
    Q_OBJECT
private slots:
    void registerInjectedActionShowsUpInQueries();
    void unloadInjectedActionClearsInstance();
    void executeInjectedActionDrivesContext();
};

void TestPluginActionManager::registerInjectedActionShowsUpInQueries()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    QVERIFY(mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake));
    QCOMPARE(mgr.actions().size(), 1);
    QCOMPARE(mgr.action(QStringLiteral("fake-action")), fake);
}

void TestPluginActionManager::unloadInjectedActionClearsInstance()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake);

    QSignalSpy spy(&mgr, &WildPalms::PluginActionManager::actionUnloading);
    mgr.unloadAction(QStringLiteral("fake-action"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mgr.actions().size(), 0);
}

void TestPluginActionManager::executeInjectedActionDrivesContext()
{
    TestablePluginActionManager mgr(nullptr);
    auto *fake = new FakeActionPlugin();
    mgr.registerInstanceForTest(QStringLiteral("fake-action"), fake);

    WildPalms::SimpleActionContext ctx;
    QSignalSpy progressSpy(&ctx, &WildPalms::SimpleActionContext::progress);
    QSignalSpy messageSpy(&ctx, &WildPalms::SimpleActionContext::message);

    QVERIFY(fake->execute(&ctx, nullptr, QJsonObject()));

    QCOMPARE(ctx.total(), 2);
    QCOMPARE(ctx.current(), 2);
    QCOMPARE(progressSpy.count(), 3);   // setTotal + setCurrent(1) + setCurrent(2)
    QCOMPARE(messageSpy.count(), 1);
}

QTEST_MAIN(TestPluginActionManager)
#include "tst_pluginactionmanager.moc"
