#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <memory>

#include "runtime/palmruntime.h"
#include "runtime/palmdeviceaccess.h"
#include "palm/sync/mockpalmdatabaseaccess.h"

class TstPalmRuntimeEmitsSyncCompleted : public QObject
{
    Q_OBJECT
private slots:
    void emitsOnceAfterHotSync();
};

void TstPalmRuntimeEmitsSyncCompleted::emitsOnceAfterHotSync()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    WildPalms::Runtime::PalmRuntime rt(tmp.path());

    auto mockDb = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    auto device = std::make_unique<WildPalms::Runtime::PalmDeviceAccess>(
        std::move(mockDb), nullptr);
    rt.setDeviceAccessForTest(std::move(device));

    QSignalSpy spy(&rt, &WildPalms::Runtime::PalmRuntime::syncCompleted);

    auto fut = rt.hotSync();
    QTRY_VERIFY_WITH_TIMEOUT(fut.isFinished(), 10000);

    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TstPalmRuntimeEmitsSyncCompleted)
#include "tst_palm_runtime_emits_sync_completed.moc"
