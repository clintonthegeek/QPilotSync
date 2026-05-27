// tst_tickle_phase.cpp
// P2: Unit tests for shouldPauseTickle() truth table.
// The function is pure (no side effects), so every case in the
// phase × role matrix can be verified without a device or engine.

#include <QtTest/QtTest>

#include "palmticklephase.h"

using WildPalms::Runtime::shouldPauseTickle;
using Phase = Kalburator::Engine::SyncEngine::SyncPhase;

class TestTicklePhase : public QObject
{
    Q_OBJECT
private slots:
    // Palm-is-source: only pause when fetching the source (Palm).
    void pauseDuringPalmFetch_source();
    void resumeDuringRemoteFetch_source();

    // Palm-is-target: only pause when fetching the target (Palm).
    void pauseDuringPalmFetch_target();
    void resumeDuringRemoteFetch_target();

    // Processing always pauses regardless of role.
    void pauseDuringProcessing_source();
    void pauseDuringProcessing_target();
    void pauseDuringProcessing_both();

    // Idle and Complete never pause.
    void resumeOnIdle();
    void resumeOnComplete();

    // Neither source nor target is Palm → never pause.
    void noPalmRole_neverPause();
};

void TestTicklePhase::pauseDuringPalmFetch_source()
{
    // Palm is source: FetchingSource → pause
    QVERIFY(shouldPauseTickle(Phase::FetchingSource, /*palmIsSource=*/true, /*palmIsTarget=*/false));
}

void TestTicklePhase::resumeDuringRemoteFetch_source()
{
    // Palm is source, but we're fetching the target (network) → resume
    QVERIFY(!shouldPauseTickle(Phase::FetchingTarget, /*palmIsSource=*/true, /*palmIsTarget=*/false));
}

void TestTicklePhase::pauseDuringPalmFetch_target()
{
    // Palm is target: FetchingTarget → pause
    QVERIFY(shouldPauseTickle(Phase::FetchingTarget, /*palmIsSource=*/false, /*palmIsTarget=*/true));
}

void TestTicklePhase::resumeDuringRemoteFetch_target()
{
    // Palm is target, but we're fetching the source (network) → resume
    QVERIFY(!shouldPauseTickle(Phase::FetchingSource, /*palmIsSource=*/false, /*palmIsTarget=*/true));
}

void TestTicklePhase::pauseDuringProcessing_source()
{
    // Processing always pauses: apply phase always touches Palm
    QVERIFY(shouldPauseTickle(Phase::Processing, /*palmIsSource=*/true, /*palmIsTarget=*/false));
}

void TestTicklePhase::pauseDuringProcessing_target()
{
    QVERIFY(shouldPauseTickle(Phase::Processing, /*palmIsSource=*/false, /*palmIsTarget=*/true));
}

void TestTicklePhase::pauseDuringProcessing_both()
{
    QVERIFY(shouldPauseTickle(Phase::Processing, /*palmIsSource=*/true, /*palmIsTarget=*/true));
}

void TestTicklePhase::resumeOnIdle()
{
    QVERIFY(!shouldPauseTickle(Phase::Idle, /*palmIsSource=*/true, /*palmIsTarget=*/true));
}

void TestTicklePhase::resumeOnComplete()
{
    QVERIFY(!shouldPauseTickle(Phase::Complete, /*palmIsSource=*/true, /*palmIsTarget=*/true));
}

void TestTicklePhase::noPalmRole_neverPause()
{
    // Neither backend is Palm (pure network-to-network mapping)
    QVERIFY(!shouldPauseTickle(Phase::FetchingSource, false, false));
    QVERIFY(!shouldPauseTickle(Phase::FetchingTarget, false, false));
    QVERIFY(shouldPauseTickle(Phase::Processing,     false, false)); // Processing still pauses conservatively
    QVERIFY(!shouldPauseTickle(Phase::Idle,          false, false));
    QVERIFY(!shouldPauseTickle(Phase::Complete,      false, false));
}

QTEST_MAIN(TestTicklePhase)
#include "tst_tickle_phase.moc"
