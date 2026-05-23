#include <QtTest/QtTest>
#include "../wildpalms_qtest_main.h"
#include "profile.h"

using MR = DeviceFingerprint::MatchResult;

class TstDeviceFingerprintCompare : public QObject {
    Q_OBJECT
private slots:
    void serialPriorityMatch();
    void serialPriorityMismatch();
    void userIdFallbackWhenSerialEmptyOnOneSide();
    void userNameFallbackWhenIdEmptyOnOneSide();
    void indeterminateWhenNoOverlap();
    void symmetric();
    void matchesWrapperPreserved();
};

namespace {
DeviceFingerprint makeFp(const QString &serial = {},
                         quint32 userId = 0,
                         const QString &userName = {}) {
    DeviceFingerprint fp;
    fp.usbSerialNumber = serial;
    fp.userId = userId;
    fp.userName = userName;
    return fp;
}
} // namespace

void TstDeviceFingerprintCompare::serialPriorityMatch()
{
    auto a = makeFp(QStringLiteral("S1"), 1, QStringLiteral("X"));
    auto b = makeFp(QStringLiteral("S1"), 2, QStringLiteral("Y"));
    QCOMPARE(a.compare(b), MR::Match);   // serial wins
}

void TstDeviceFingerprintCompare::serialPriorityMismatch()
{
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S2"));
    QCOMPARE(a.compare(b), MR::MismatchKnown);
}

void TstDeviceFingerprintCompare::userIdFallbackWhenSerialEmptyOnOneSide()
{
    auto a = makeFp(QString(), 42);
    auto b = makeFp(QStringLiteral("S2"), 42);
    QCOMPARE(a.compare(b), MR::Match);   // serial check skipped; userId match
}

void TstDeviceFingerprintCompare::userNameFallbackWhenIdEmptyOnOneSide()
{
    auto a = makeFp(QString(), 0, QStringLiteral("clinton"));
    auto b = makeFp(QString(), 42, QStringLiteral("clinton"));
    QCOMPARE(a.compare(b), MR::Match);
}

void TstDeviceFingerprintCompare::indeterminateWhenNoOverlap()
{
    auto a = makeFp(QStringLiteral("S1"));   // serial only
    auto b = makeFp(QString(), 0, QStringLiteral("X"));   // username only
    QCOMPARE(a.compare(b), MR::Indeterminate);
}

void TstDeviceFingerprintCompare::symmetric()
{
    auto a = makeFp(QStringLiteral("S1"), 42);
    auto b = makeFp(QStringLiteral("S2"), 42);
    QCOMPARE(a.compare(b), b.compare(a));
}

void TstDeviceFingerprintCompare::matchesWrapperPreserved()
{
    auto a = makeFp(QStringLiteral("S1"));
    auto b = makeFp(QStringLiteral("S1"));
    QVERIFY(a.matches(b));
    auto c = makeFp(QStringLiteral("S2"));
    QVERIFY(!a.matches(c));
}

WILDPALMS_QTEST_GUILESS_MAIN(TstDeviceFingerprintCompare)
#include "tst_devicefingerprint_compare.moc"
