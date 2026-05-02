#include <QtTest/QtTest>
#include "kalburatorinteractiveconflicthandler.h"
#include "conflicthandlerregistry.h"

class TstKalburatorInteractiveConflictHandler : public QObject
{
    Q_OBJECT
private slots:
    void registers_into_libkalburator_registry();
};

void TstKalburatorInteractiveConflictHandler::registers_into_libkalburator_registry()
{
    KalburatorInteractiveConflictHandler handler(nullptr, nullptr);

    Kalburator::Sync::QSyncCore::ConflictHandlerRegistry registry;
    registry.setDefaultHandler(&handler);

    QVERIFY(registry.handlerFor("nonexistent-backend") == &handler);
}

QTEST_MAIN(TstKalburatorInteractiveConflictHandler)
#include "tst_kalburator_interactive_conflict_handler.moc"
