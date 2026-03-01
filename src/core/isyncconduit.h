#pragma once

#include "iconduit.h"
#include <QList>
#include <functional>

class PilotRecord;

namespace QSyncCore {
struct RecordSnapshot;
}

namespace Sync {
class BackendRecord;
class SyncContext;
}

using Sync::BackendRecord;
using Sync::SyncContext;

class ISyncConduit : public IConduit
{
public:
    // Palm database identity
    virtual QString palmDatabaseName() const = 0;
    virtual QString fileExtension() const = 0;
    virtual bool canSyncToPalm() const = 0;
    virtual bool canSyncFromPalm() const = 0;

    // Record conversion (bidirectional)
    virtual BackendRecord *palmToBackend(PilotRecord *record,
                                         SyncContext *context) = 0;
    virtual PilotRecord *backendToPalm(BackendRecord *record,
                                        SyncContext *context) = 0;
    virtual bool recordsEqual(PilotRecord *palmRecord,
                               BackendRecord *backendRecord) const = 0;
    virtual QString palmRecordDescription(PilotRecord *record) const = 0;
    virtual BackendRecord *findMatch(PilotRecord *palmRecord,
                                      const QList<BackendRecord *> &candidates) = 0;

    // Category support
    virtual QString categoryNameForIndex(int categoryIndex) const = 0;
    virtual bool writeModifiedCategories(SyncContext *context) = 0;

    // Conflict display support
    virtual void enrichConflictSnapshot(QSyncCore::RecordSnapshot &snapshot,
                                         bool isSourceSide) const = 0;
    virtual QString formatConflictRecordHtml(const QSyncCore::RecordSnapshot &snapshot) const = 0;
};

/// Lookup function to find an ISyncConduit by conduit ID
using ConduitLookupFn = std::function<const ISyncConduit*(const QString &conduitId)>;

Q_DECLARE_INTERFACE(ISyncConduit, "ca.vibekoder.ISyncConduit/1.0")
