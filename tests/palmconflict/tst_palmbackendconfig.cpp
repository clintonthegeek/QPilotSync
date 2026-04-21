#include <QtTest/QtTest>

#include "palmbackendconfig.h"

using WildPalms::PalmConflict::ConnectionBehavior;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::connectionBehaviorFromString;
using WildPalms::PalmConflict::connectionBehaviorToString;

class TestPalmBackendConfig : public QObject
{
    Q_OBJECT
private slots:
    void defaultsAreSensible();
    void equalityIsStructural();
    void connectionBehaviorStringRoundTrip();
    void unknownBehaviorStringFallsBackToKeepAlive();
};

void TestPalmBackendConfig::defaultsAreSensible()
{
    PalmBackendConfig cfg;
    QCOMPARE(cfg.connectionBehavior, ConnectionBehavior::KeepAlive);
    QCOMPARE(cfg.connectionTimeoutSeconds, 60);
    QCOMPARE(cfg.hotSyncTickleIntervalSeconds, 5);
    QVERIFY(cfg.userName.isEmpty());
}

void TestPalmBackendConfig::equalityIsStructural()
{
    PalmBackendConfig a;
    PalmBackendConfig b;
    QCOMPARE(a, b);

    b.connectionTimeoutSeconds = 120;
    QVERIFY(!(a == b));
}

void TestPalmBackendConfig::connectionBehaviorStringRoundTrip()
{
    for (auto b : { ConnectionBehavior::KeepAlive,
                    ConnectionBehavior::DisconnectAndDefer,
                    ConnectionBehavior::TimeoutThenDefer }) {
        const auto s = connectionBehaviorToString(b);
        QCOMPARE(connectionBehaviorFromString(s), b);
    }
}

void TestPalmBackendConfig::unknownBehaviorStringFallsBackToKeepAlive()
{
    QCOMPARE(connectionBehaviorFromString(QStringLiteral("nonsense")),
             ConnectionBehavior::KeepAlive);
}

QTEST_MAIN(TestPalmBackendConfig)
#include "tst_palmbackendconfig.moc"
