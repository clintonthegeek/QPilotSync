#ifndef SYNCSTATUSMODEL_H
#define SYNCSTATUSMODEL_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>

#include "../../runtime/palmrunresult.h"  // WildPalms::Runtime::PalmRunResult

/**
 * UI-free state model for the dashboard status panel.
 *
 * Fed by device-monitor, connection, and sync-run signals (translated to plain
 * values by KF6MainWindow). Emits changed() whenever any rendered state moves;
 * the view re-renders wholesale. Action requests originate here so KF6MainWindow
 * has a single wiring surface.
 */
class SyncStatusModel : public QObject
{
    Q_OBJECT
public:
    enum class LinkState { Listening, Detected, Handshaking, Connected, Syncing, Disconnected };
    Q_ENUM(LinkState)

    enum class ChipState { Pending, Active, Done, Error, Interrupted };
    Q_ENUM(ChipState)

    struct ConduitSeed {
        QString mappingId;
        QString label;
        QString iconName;
    };

    struct Conduit {
        QString mappingId;
        QString label;
        QString iconName;
        ChipState state = ChipState::Pending;
        int current = 0;
        int total = 0;
        int created = 0;
        int modified = 0;
        int deleted = 0;
    };

    struct Digest {
        bool valid = false;
        QString modeLabel;
        int totalChanges = 0;
        int conflicts = 0;
        qint64 durationMs = 0;
        bool success = true;
    };

    explicit SyncStatusModel(QObject *parent = nullptr);

    // --- rendered state getters ---
    LinkState linkState() const { return m_linkState; }
    const QVector<Conduit> &conduits() const { return m_conduits; }
    int progressCurrent() const { return m_progressCurrent; }
    int progressTotal() const { return m_progressTotal; }
    QString progressMessage() const { return m_progressMessage; }
    int conflictCount() const { return m_conflictCount; }
    Digest lastDigest() const { return m_digest; }
    QString errorText() const { return m_errorText; }

    QString deviceName() const { return m_deviceName; }
    QString deviceDetails() const { return m_deviceDetails; }
    QString profileName() const { return m_profileName; }
    QDateTime lastSyncTime() const { return m_lastSyncTime; }
    QString autoSyncPlan() const { return m_autoSyncPlan; }

    // --- derived view helpers ---
    QString headline() const;
    QString primaryActionLabel() const;   // "Sync Now" / "Cancel" / "" (hidden)
    bool primaryActionVisible() const { return !primaryActionLabel().isEmpty(); }

public slots:
    // device + profile snapshot (plain values — keeps the model Profile-free)
    void setDeviceInfo(const QString &name, const QString &details);
    void setProfileInfo(const QString &profileName, const QDateTime &lastSync,
                        const QString &autoSyncPlan);
    void seedConduits(const QVector<ConduitSeed> &conduits);

    // connection lifecycle
    void onDeviceDetected();
    void onDeviceLost();                                   // udev unplug OR link teardown
    void onConnectionStarted();
    void onConnectionComplete(bool success, const QString &error);

    // sync lifecycle
    void onRunStarted(const QString &modeLabel);
    void onRunProgress(int current, int total, const QString &message);
    void onRunFinished(const WildPalms::Runtime::PalmRunResult &result);
    void onMappingSyncStarted(const QString &mappingId, const QString &label,
                              const QString &iconName);
    void onMappingSyncProgress(const QString &mappingId, int phase, int current, int total);
    void onMappingSyncFinished(const QString &mappingId, int created, int modified,
                               int deleted, bool ok);

    void onConflictCountChanged(int count);

    // action requests (called by the view; the model picks the right signal)
    void triggerPrimaryAction();
    void triggerResolveConflicts();

signals:
    void changed();
    void syncRequested();
    void cancelRequested();
    void resolveConflictsRequested();

private:
    Conduit *findConduit(const QString &mappingId);     // nullptr if absent
    void setState(LinkState s);

    LinkState m_linkState = LinkState::Listening;
    QVector<Conduit> m_conduits;
    int m_progressCurrent = 0;
    int m_progressTotal = 0;
    QString m_progressMessage;
    int m_conflictCount = 0;
    Digest m_digest;
    QString m_errorText;
    QString m_currentRunLabel;
    int m_runChanges = 0;                                 // accumulated this run

    QString m_deviceName;
    QString m_deviceDetails;
    QString m_profileName;
    QDateTime m_lastSyncTime;
    QString m_autoSyncPlan;
};

#endif // SYNCSTATUSMODEL_H
