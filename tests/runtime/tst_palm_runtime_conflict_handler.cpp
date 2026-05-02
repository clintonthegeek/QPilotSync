#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "runtime/palmruntime.h"
#include "conflictpolicy.h"
#include "conflicthandlerregistry.h"

namespace KSync = Kalburator::Sync;

class StubKalburatorHandler : public KSync::QSyncCore::ConflictHandler
{
public:
    KSync::QSyncCore::ConflictDecision handleConflict(
        KSync::QSyncCore::ConflictRecord &,
        const KSync::QSyncCore::ConflictPolicy &) override
    {
        return KSync::QSyncCore::ConflictDecision::UseSource;
    }
};

class TstPalmRuntimeConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void install_handler_makes_it_default();
};

void TstPalmRuntimeConflictHandler::install_handler_makes_it_default()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WildPalms::Runtime::PalmRuntime runtime(tmp.path());

    StubKalburatorHandler handler;
    runtime.setConflictHandler(&handler);

    QVERIFY(runtime.conflictHandlerForTest() == &handler);
}

QTEST_MAIN(TstPalmRuntimeConflictHandler)
#include "tst_palm_runtime_conflict_handler.moc"
