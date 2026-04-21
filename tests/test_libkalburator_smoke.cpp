// Phase A linkage smoke test for the libkalburator integration.
// Proves WP can link Kalburator::Sync and resolve its headers; does not
// exercise any sync behaviour. See docs/plans/2026-04-20-libkalburator-integration.md.

#include <QtTest/QtTest>

#include "mockbackend.h"
#include "conflictmanager.h"

namespace Kalburator::Sync {}
using namespace Kalburator::Sync;

class TestLibkalburatorSmoke : public QObject
{
    Q_OBJECT

private slots:
    void backendTypeIsPopulated();
    void conflictManagerHasExpectedDefaults();
};

void TestLibkalburatorSmoke::backendTypeIsPopulated()
{
    MockBackend backend;
    QVERIFY(!backend.backendType().isEmpty());
    QCOMPARE(backend.backendType(), MockBackend::BackendTypeName);
}

void TestLibkalburatorSmoke::conflictManagerHasExpectedDefaults()
{
    ConflictManager manager;
    QCOMPARE(manager.workflowMode(), ConflictManager::WorkflowMode::Immediate);
    QCOMPARE(manager.hybridThreshold(), 3);
}

QTEST_MAIN(TestLibkalburatorSmoke)
#include "test_libkalburator_smoke.moc"
