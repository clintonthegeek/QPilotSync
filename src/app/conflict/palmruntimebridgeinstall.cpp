// palmruntimebridgeinstall.cpp — libkalburator-only TU.
//
// IMPORTANT: Do NOT include any WP-local QSyncCore headers here (no
// sync/qsynccore/*.h, no conflictdialog.h, etc.).  This TU exists to keep
// KalburatorInteractiveConflictHandler construction and PalmRuntime wiring in
// a TU that only sees libkalburator QSyncCore types, avoiding the include-guard
// collision with WP-local QSyncCore headers.

#include "conflictdialogbridge.h"
#include "kalburatorinteractiveconflicthandler.h"

// palmruntime.h only forward-declares Kalburator::Sync::QSyncCore::ConflictHandler
// (no WP-local QSyncCore headers pulled in), so it is safe to include here.
#include "../../runtime/palmruntime.h"

namespace ConflictDialogBridge {

QObject *createAndInstall(
    WildPalms::Runtime::PalmRuntime *runtime,
    QWidget *parentWidget,
    QObject *qobjParent)
{
    if (!runtime)
        return nullptr;

    auto *handler = new KalburatorInteractiveConflictHandler(
        nullptr,       // ConflictStore not yet bridged (M5b chore)
        parentWidget,
        qobjParent);

    runtime->setConflictHandler(handler);
    return handler;
}

void destroyHandler(QObject *handler)
{
    delete handler;
}

} // namespace ConflictDialogBridge
