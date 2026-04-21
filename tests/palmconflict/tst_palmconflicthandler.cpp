#include <QtTest/QtTest>

#include "conflictpolicy.h"
#include "conflictrecord.h"

#include "mockpalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmbackendconfig.h"
#include "palmconflicthandler.h"

using Kalburator::Sync::QSyncCore::AutoResolveStrategy;
using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::ConflictType;
using Kalburator::Sync::QSyncCore::FallbackBehavior;
using Kalburator::Sync::QSyncCore::PromptStrategy;
using Kalburator::Sync::QSyncCore::RecordSnapshot;
using WildPalms::PalmConflict::ConnectionBehavior;
using WildPalms::PalmConflict::PalmBackendConfig;
using WildPalms::PalmConflict::PalmConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

namespace {

ConflictRecord makeBothModifiedConflict(
    const QString &sourceId, const QString &targetId,
    const QDateTime &sourceTime, const QDateTime &targetTime)
{
    ConflictRecord cr;
    cr.type = ConflictType::BothModified;
    cr.source.id = sourceId;
    cr.source.content = QByteArrayLiteral("src");
    cr.source.lastModified = sourceTime;
    cr.target.id = targetId;
    cr.target.content = QByteArrayLiteral("tgt");
    cr.target.lastModified = targetTime;
    return cr;
}

} // namespace

class TestPalmConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void sourceAlwaysWinsPolicyYieldsUseSource();
    void targetAlwaysWinsPolicyYieldsUseTarget();
    void newerWinsRespectsTimestamps();
    void deferFallbackAccumulatesPending();
    void skipFallbackReturnsSkip();
    void nonPalmIdsFallThroughUnchanged();
};

void TestPalmConflictHandler::sourceAlwaysWinsPolicyYieldsUseSource()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QCOMPARE(cr.decision, ConflictDecision::UseSource);
}

void TestPalmConflictHandler::targetAlwaysWinsPolicyYieldsUseTarget()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoTargetWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseTarget);
}

void TestPalmConflictHandler::newerWinsRespectsTimestamps()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    const auto now = QDateTime::currentDateTimeUtc();
    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        /*sourceTime=*/now,
        /*targetTime=*/now.addSecs(-60));

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::NewerWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
}

void TestPalmConflictHandler::deferFallbackAccumulatesPending()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);
    handler.onSyncStart();

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::deferAll();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Pending);
    QCOMPARE(handler.pendingConflicts().size(), 1);
}

void TestPalmConflictHandler::skipFallbackReturnsSkip()
{
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        PalmBackend::encodeRecordId(QStringLiteral("MemoDB"), 1),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::Skip;
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::Skip);
}

void TestPalmConflictHandler::nonPalmIdsFallThroughUnchanged()
{
    // Non-Palm ids: no prefix "palm:", so decodeRecordId fails and
    // overlays should be no-ops. Base policy decision stands.
    MockPalmDatabaseAccess dev;
    PalmBackendConfig cfg;
    PalmConflictHandler handler(&dev, &cfg);

    ConflictRecord cr = makeBothModifiedConflict(
        QStringLiteral("local:memo:1"),
        QStringLiteral("local:memo:1"),
        QDateTime::currentDateTimeUtc(),
        QDateTime::currentDateTimeUtc());

    ConflictPolicy policy = ConflictPolicy::autoSourceWins();
    QCOMPARE(handler.handleConflict(cr, policy), ConflictDecision::UseSource);
    QVERIFY(handler.lastOverlay().isEmpty());
}

QTEST_MAIN(TestPalmConflictHandler)
#include "tst_palmconflicthandler.moc"
