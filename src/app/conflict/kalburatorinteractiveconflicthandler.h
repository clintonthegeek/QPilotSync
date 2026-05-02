#ifndef KALBURATORINTERACTIVECONFLICTHANDLER_H
#define KALBURATORINTERACTIVECONFLICTHANDLER_H

#include "conflictpolicy.h"   // libkalburator's interface (conflict/ is on the include path)

#include <QObject>
#include <QPointer>
#include <QWidget>   // needed for complete type in QPointer<QWidget>
#include <functional>

namespace Kalburator::Sync::QSyncCore {
    class ConflictStore;
}

class KalburatorInteractiveConflictHandler
    : public QObject,
      public Kalburator::Sync::QSyncCore::ConflictHandler
{
    Q_OBJECT
public:
    explicit KalburatorInteractiveConflictHandler(
        Kalburator::Sync::QSyncCore::ConflictStore *store = nullptr,
        QWidget *parentWidget = nullptr,
        QObject *parent = nullptr);
    ~KalburatorInteractiveConflictHandler() override = default;

    // ConflictHandler
    Kalburator::Sync::QSyncCore::ConflictDecision handleConflict(
        Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
        const Kalburator::Sync::QSyncCore::ConflictPolicy &policy)
        override;
    void onSyncStart() override;
    void onSyncEnd(bool hadConflicts, bool allResolved) override;
    bool canPrompt() const override
        { return m_parentWidget != nullptr; }
    bool shouldKeepConnectionAlive() const override
        { return m_keepAlive; }
    QList<Kalburator::Sync::QSyncCore::ConflictRecord>
        pendingConflicts() const override
        { return m_localPending; }

    void setParentWidget(QWidget *w) { m_parentWidget = w; }

    using OnGuiThreadHook = std::function<
        Kalburator::Sync::QSyncCore::ConflictDecision(
            Kalburator::Sync::QSyncCore::ConflictRecord &,
            const Kalburator::Sync::QSyncCore::ConflictPolicy &)>;
    void setOnGuiThreadHook(OnGuiThreadHook fn) { m_hook = std::move(fn); }

signals:
    void keepAliveRequested();
    void conflictProgress(int current, int total,
                          const QString &description);

private:
    // Called on the GUI thread only — via BlockingQueuedConnection from handleConflict
    // (cross-thread) or directly (same-thread). Not a Qt slot: the return type is a
    // Kalburator namespace type that MOC cannot resolve in this TU due to the
    // local QSyncCore namespace collision. Called via functor invokeMethod instead.
    Kalburator::Sync::QSyncCore::ConflictDecision
        handleConflictOnGuiThread(
            Kalburator::Sync::QSyncCore::ConflictRecord &conflict,
            const Kalburator::Sync::QSyncCore::ConflictPolicy &policy);

    OnGuiThreadHook                             m_hook;
    Kalburator::Sync::QSyncCore::ConflictStore *m_store;
    QPointer<QWidget>                           m_parentWidget;
    QList<Kalburator::Sync::QSyncCore::ConflictRecord>
                                                m_localPending;
    int                                         m_conflictsHandled = 0;
    int                                         m_conflictsDeferred = 0;
    bool                                        m_keepAlive = true;
};

#endif // KALBURATORINTERACTIVECONFLICTHANDLER_H
