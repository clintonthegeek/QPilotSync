#include <QtTest/QtTest>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include "kalburatorinteractiveconflicthandler.h"
#include "conflicthandlerregistry.h"

class TstKalburatorInteractiveConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void registers_into_libkalburator_registry();
    void marshals_to_gui_thread();
    void hook_bypasses_dialog_when_set();
};

void TstKalburatorInteractiveConflictHandler::registers_into_libkalburator_registry()
{
    KalburatorInteractiveConflictHandler handler(nullptr, nullptr);

    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    registry.setDefaultHandler(&handler);

    QVERIFY(registry.handlerFor("nonexistent-backend") == &handler);
}

void TstKalburatorInteractiveConflictHandler::marshals_to_gui_thread()
{
    KalburatorInteractiveConflictHandler handler(nullptr, nullptr);

    Kalburator::Sync::QSyncCore::ConflictRecord conflict;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;

    QThread *mainThread = QThread::currentThread();
    QThread *observedThread = nullptr;

    handler.setOnGuiThreadHook([&](
        Kalburator::Sync::QSyncCore::ConflictRecord &,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &)
            -> Kalburator::Sync::QSyncCore::ConflictDecision {
        observedThread = QThread::currentThread();
        return Kalburator::Sync::QSyncCore::ConflictDecision::UseSource;
    });

    // Use QFutureWatcher + QEventLoop so the main thread keeps
    // pumping events while the worker thread is blocked waiting for
    // BlockingQueuedConnection to be dispatched. A plain .result()
    // would deadlock because the main thread would be blocked and
    // unable to process the queued invocation.
    QFutureWatcher<Kalburator::Sync::QSyncCore::ConflictDecision> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<Kalburator::Sync::QSyncCore::ConflictDecision>::finished,
                     &loop, &QEventLoop::quit);

    auto future = QtConcurrent::run([&]() {
        return handler.handleConflict(conflict, policy);
    });
    watcher.setFuture(future);
    loop.exec();

    auto result = future.result();

    QCOMPARE(observedThread, mainThread);
    QCOMPARE(result,
        Kalburator::Sync::QSyncCore::ConflictDecision::UseSource);
}

void TstKalburatorInteractiveConflictHandler::hook_bypasses_dialog_when_set()
{
    // With m_parentWidget set BUT m_hook also set, the hook wins.
    // Without this guarantee, unit tests can't run at all once
    // the dialog path is in place (dialog would block waiting for user input).
    QWidget parent;
    KalburatorInteractiveConflictHandler handler(nullptr, &parent);
    handler.setOnGuiThreadHook([](
        Kalburator::Sync::QSyncCore::ConflictRecord &,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &) {
            return Kalburator::Sync::QSyncCore::ConflictDecision::UseTarget;
    });

    Kalburator::Sync::QSyncCore::ConflictRecord conflict;
    Kalburator::Sync::QSyncCore::ConflictPolicy policy;
    QCOMPARE(handler.handleConflict(conflict, policy),
        Kalburator::Sync::QSyncCore::ConflictDecision::UseTarget);
}

QTEST_MAIN(TstKalburatorInteractiveConflictHandler)
#include "tst_kalburator_interactive_conflict_handler.moc"
