#include "conduit.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"

#include <QDebug>

namespace Sync {

bool Conduit::canSync(const SyncContext *context) const
{
    if (!context) return false;
    if (!context->deviceLink) return false;
    if (!context->backend) return false;
    if (!context->state) return false;
    if (!context->deviceLink->isConnected()) return false;
    return true;
}

SyncResult Conduit::sync(SyncContext *context)
{
    SyncResult result;
    result.startTime = QDateTime::currentDateTime();

    if (!canSync(context)) {
        result.success = false;
        result.errorMessage = "Sync prerequisites not met";
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    emit logMessage(QString("Starting %1 sync...").arg(displayName()));

    // Open Palm database
    m_dbHandle = context->deviceLink->openDatabase(palmDatabaseName(), true);
    if (m_dbHandle < 0) {
        result.success = false;
        result.errorMessage = QString("Failed to open Palm database: %1").arg(palmDatabaseName());
        result.endTime = QDateTime::currentDateTime();
        return result;
    }

    // Determine if this is a first sync
    context->isFirstSync = context->state->isFirstSync();

    // Run appropriate sync algorithm
    if (context->isFirstSync) {
        emit logMessage("First sync detected - matching records by content");
        result = firstSync(context);
    } else {
        switch (context->mode) {
            case SyncMode::HotSync:
                result = hotSync(context);
                break;
            case SyncMode::FullSync:
                result = fullSync(context);
                break;
            case SyncMode::CopyPalmToPC:
                result = copyPalmToPC(context);
                break;
            case SyncMode::CopyPCToPalm:
                result = copyPCToPalm(context);
                break;
            default:
                result = hotSync(context);
                break;
        }
    }

    // Close Palm database
    context->deviceLink->closeDatabase(m_dbHandle);
    m_dbHandle = -1;

    // Update sync state
    if (result.success) {
        context->state->setLastSyncTime(QDateTime::currentDateTime());
        context->state->save();
    }

    result.endTime = QDateTime::currentDateTime();

    emit logMessage(QString("Sync complete: %1 (%2 ms)")
        .arg(result.success ? "Success" : "Failed")
        .arg(result.durationMs()));

    return result;
}

SyncResult Conduit::hotSync(SyncContext *context)
{
    emit logMessage("Performing HotSync (modified records only)...");

    SyncResult result;
    result.success = true;

    // Load modified Palm records
    QList<PilotRecord*> palmRecords = readPalmRecords(context, true);
    emit logMessage(QString("Found %1 modified Palm records").arg(palmRecords.size()));

    // Load all backend records (we need full set for lookups)
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);
    emit logMessage(QString("Loaded %1 backend records").arg(backendRecords.size()));

    // Track which backend records we've processed
    QSet<QString> processedBackendIds;

    // Process modified Palm records
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled) break;

        QString palmId = QString::number(palmRecord->id());
        QString pcId = context->state->pcIdForPalm(palmId);

        BackendRecord *backendRecord = nullptr;
        if (!pcId.isEmpty()) {
            // Find matching backend record
            for (BackendRecord *rec : backendRecords) {
                if (rec->id == pcId) {
                    backendRecord = rec;
                    break;
                }
            }
        }

        syncRecord(palmRecord, backendRecord, context, result.palmStats, result.pcStats);

        if (backendRecord) {
            processedBackendIds.insert(backendRecord->id);
        }
    }

    // TODO: Process modified backend records that weren't already handled
    // This requires change detection on the backend side

    // Cleanup
    qDeleteAll(palmRecords);
    qDeleteAll(backendRecords);

    return result;
}

SyncResult Conduit::fullSync(SyncContext *context)
{
    emit logMessage("Performing FullSync (all records)...");

    SyncResult result;
    result.success = true;

    // Load all Palm records
    QList<PilotRecord*> palmRecords = readPalmRecords(context, false);
    emit logMessage(QString("Loaded %1 Palm records").arg(palmRecords.size()));

    // Load all backend records
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);
    emit logMessage(QString("Loaded %1 backend records").arg(backendRecords.size()));

    // Track processed records
    QSet<QString> processedPalmIds;
    QSet<QString> processedBackendIds;

    // Process all Palm records
    int count = 0;
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled) break;

        QString palmId = QString::number(palmRecord->id());
        QString pcId = context->state->pcIdForPalm(palmId);

        BackendRecord *backendRecord = nullptr;
        if (!pcId.isEmpty()) {
            for (BackendRecord *rec : backendRecords) {
                if (rec->id == pcId) {
                    backendRecord = rec;
                    break;
                }
            }
        }

        syncRecord(palmRecord, backendRecord, context, result.palmStats, result.pcStats);

        processedPalmIds.insert(palmId);
        if (backendRecord) {
            processedBackendIds.insert(backendRecord->id);
        }

        count++;
        if (count % 50 == 0) {
            emit progressUpdated(count, palmRecords.size(), "Processing Palm records...");
        }
    }

    // Process backend records without Palm mappings (new on PC)
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled) break;
        if (processedBackendIds.contains(backendRecord->id)) continue;

        syncRecord(nullptr, backendRecord, context, result.palmStats, result.pcStats);
    }

    // Cleanup
    qDeleteAll(palmRecords);
    qDeleteAll(backendRecords);

    return result;
}

SyncResult Conduit::firstSync(SyncContext *context)
{
    emit logMessage("Performing FirstSync (matching by content)...");

    SyncResult result;
    result.success = true;

    // Load all Palm records
    QList<PilotRecord*> palmRecords = readPalmRecords(context, false);
    emit logMessage(QString("Loaded %1 Palm records").arg(palmRecords.size()));

    // Load all backend records
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);
    emit logMessage(QString("Loaded %1 backend records").arg(backendRecords.size()));

    // Track matched records
    QSet<QString> matchedBackendIds;

    // Try to match Palm records to existing backend records
    int count = 0;
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled) break;
        if (palmRecord->isDeleted()) {
            result.palmStats.deleted++;
            continue;
        }

        QString palmId = QString::number(palmRecord->id());

        // Build candidate list (excluding already matched)
        QList<BackendRecord*> candidates;
        for (BackendRecord *rec : backendRecords) {
            if (!matchedBackendIds.contains(rec->id)) {
                candidates.append(rec);
            }
        }

        // Try to find a match
        BackendRecord *match = findMatch(palmRecord, candidates);

        if (match) {
            // Found match - create mapping
            emit logMessage(QString("Matched: %1 ↔ %2")
                .arg(palmRecordDescription(palmRecord))
                .arg(match->description()));

            context->state->mapIds(palmId, match->id);
            matchedBackendIds.insert(match->id);
            result.palmStats.unchanged++;
        } else {
            // No match - create new backend record
            BackendRecord *newRecord = palmToBackend(palmRecord, context);
            if (newRecord) {
                QString newId = context->backend->createRecord(context->collectionId, *newRecord);
                if (!newId.isEmpty()) {
                    context->state->mapIds(palmId, newId);
                    result.pcStats.created++;
                }
                delete newRecord;
            }
        }

        count++;
        if (count % 20 == 0) {
            emit progressUpdated(count, palmRecords.size(), "Matching records...");
        }
    }

    // Handle unmatched backend records (new on PC, need to create on Palm)
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled) break;
        if (matchedBackendIds.contains(backendRecord->id)) continue;
        if (backendRecord->isDeleted) continue;

        // Create on Palm
        PilotRecord *palmRecord = backendToPalm(backendRecord, context);
        if (palmRecord) {
            if (writePalmRecord(palmRecord, context)) {
                context->state->mapIds(QString::number(palmRecord->id()), backendRecord->id);
                result.palmStats.created++;
            }
            delete palmRecord;
        }
    }

    // Cleanup
    qDeleteAll(palmRecords);
    qDeleteAll(backendRecords);

    return result;
}

SyncResult Conduit::copyPalmToPC(SyncContext *context)
{
    emit logMessage("Copying Palm → PC...");

    SyncResult result;
    result.success = true;

    // Load all Palm records
    QList<PilotRecord*> palmRecords = readPalmRecords(context, false);

    // Clear existing backend records in collection (or just overwrite)
    QList<BackendRecord*> existingRecords = context->backend->loadRecords(context->collectionId);

    int count = 0;
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled) break;
        if (palmRecord->isDeleted()) continue;

        QString palmId = QString::number(palmRecord->id());

        // Convert and create/update
        BackendRecord *backendRecord = palmToBackend(palmRecord, context);
        if (backendRecord) {
            QString existingId = context->state->pcIdForPalm(palmId);

            if (existingId.isEmpty()) {
                // Create new
                QString newId = context->backend->createRecord(context->collectionId, *backendRecord);
                if (!newId.isEmpty()) {
                    context->state->mapIds(palmId, newId);
                    result.pcStats.created++;
                }
            } else {
                // Update existing
                backendRecord->id = existingId;
                if (context->backend->updateRecord(*backendRecord)) {
                    result.pcStats.updated++;
                }
            }
            delete backendRecord;
        }

        count++;
        if (count % 50 == 0) {
            emit progressUpdated(count, palmRecords.size(), "Copying to PC...");
        }
    }

    // Delete PC records that no longer exist on Palm
    QStringList palmIds;
    for (PilotRecord *rec : palmRecords) {
        palmIds << QString::number(rec->id());
    }

    for (BackendRecord *existingRec : existingRecords) {
        QString palmId = context->state->palmIdForPC(existingRec->id);
        if (!palmId.isEmpty() && !palmIds.contains(palmId)) {
            context->backend->deleteRecord(existingRec->id);
            context->state->removePCMapping(existingRec->id);
            result.pcStats.deleted++;
        }
    }

    qDeleteAll(palmRecords);
    qDeleteAll(existingRecords);

    return result;
}

SyncResult Conduit::copyPCToPalm(SyncContext *context)
{
    emit logMessage("Copying PC → Palm...");

    SyncResult result;
    result.success = true;

    // Load all backend records
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);

    int count = 0;
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled) break;
        if (backendRecord->isDeleted) continue;

        QString palmId = context->state->palmIdForPC(backendRecord->id);

        PilotRecord *palmRecord = backendToPalm(backendRecord, context);
        if (palmRecord) {
            if (!palmId.isEmpty()) {
                palmRecord->setId(palmId.toUInt());
            }

            if (writePalmRecord(palmRecord, context)) {
                if (palmId.isEmpty()) {
                    context->state->mapIds(QString::number(palmRecord->id()), backendRecord->id);
                    result.palmStats.created++;
                } else {
                    result.palmStats.updated++;
                }
            }
            delete palmRecord;
        }

        count++;
        if (count % 50 == 0) {
            emit progressUpdated(count, backendRecords.size(), "Copying to Palm...");
        }
    }

    // TODO: Delete Palm records that no longer exist on PC

    qDeleteAll(backendRecords);

    return result;
}

void Conduit::syncRecord(PilotRecord *palmRecord,
                          BackendRecord *backendRecord,
                          SyncContext *context,
                          SyncStats &palmStats,
                          SyncStats &pcStats)
{
    // Both exist
    if (palmRecord && backendRecord) {
        bool palmModified = palmRecord->isDirty();
        bool palmDeleted = palmRecord->isDeleted();
        bool backendDeleted = backendRecord->isDeleted;

        // TODO: Detect backend modifications using baseline hash
        bool backendModified = false;

        if (palmDeleted && backendDeleted) {
            // Both deleted - remove mapping
            QString palmId = QString::number(palmRecord->id());
            context->state->removePalmMapping(palmId);
            palmStats.deleted++;
            pcStats.deleted++;
        }
        else if (palmDeleted) {
            // Palm deleted - delete from backend
            context->backend->deleteRecord(backendRecord->id);
            context->state->removePalmMapping(QString::number(palmRecord->id()));
            pcStats.deleted++;
        }
        else if (backendDeleted) {
            // Backend deleted - delete from Palm
            deletePalmRecord(QString::number(palmRecord->id()), context);
            context->state->removePCMapping(backendRecord->id);
            palmStats.deleted++;
        }
        else if (palmModified && backendModified) {
            // Conflict!
            resolveConflict(palmRecord, backendRecord, context, palmStats, pcStats);
        }
        else if (palmModified) {
            // Palm modified - update backend
            BackendRecord *updated = palmToBackend(palmRecord, context);
            if (updated) {
                updated->id = backendRecord->id;
                context->backend->updateRecord(*updated);
                delete updated;
                pcStats.updated++;
            }
        }
        else if (backendModified) {
            // Backend modified - update Palm
            PilotRecord *updated = backendToPalm(backendRecord, context);
            if (updated) {
                updated->setId(palmRecord->id());
                writePalmRecord(updated, context);
                delete updated;
                palmStats.updated++;
            }
        }
        else {
            // Neither modified
            palmStats.unchanged++;
        }
    }
    // Only Palm record exists (new or orphaned)
    else if (palmRecord && !backendRecord) {
        if (palmRecord->isDeleted()) {
            // Was deleted and mapping already gone
            palmStats.deleted++;
        } else {
            // New on Palm - create on backend
            BackendRecord *newRecord = palmToBackend(palmRecord, context);
            if (newRecord) {
                QString newId = context->backend->createRecord(context->collectionId, *newRecord);
                if (!newId.isEmpty()) {
                    context->state->mapIds(QString::number(palmRecord->id()), newId);
                    pcStats.created++;
                }
                delete newRecord;
            }
        }
    }
    // Only backend record exists (new on PC or Palm deleted)
    else if (!palmRecord && backendRecord) {
        if (backendRecord->isDeleted) {
            // Was deleted
            pcStats.deleted++;
        } else {
            // New on PC - create on Palm
            PilotRecord *newRecord = backendToPalm(backendRecord, context);
            if (newRecord) {
                if (writePalmRecord(newRecord, context)) {
                    context->state->mapIds(QString::number(newRecord->id()), backendRecord->id);
                    palmStats.created++;
                }
                delete newRecord;
            }
        }
    }
}

bool Conduit::resolveConflict(PilotRecord *palmRecord,
                               BackendRecord *backendRecord,
                               SyncContext *context,
                               SyncStats &palmStats,
                               SyncStats &pcStats)
{
    emit conflictDetected(
        palmRecordDescription(palmRecord),
        backendRecord->description()
    );

    switch (context->conflictPolicy) {
        case ConflictResolution::PalmWins: {
            BackendRecord *updated = palmToBackend(palmRecord, context);
            if (updated) {
                updated->id = backendRecord->id;
                context->backend->updateRecord(*updated);
                delete updated;
                pcStats.updated++;
            }
            return true;
        }

        case ConflictResolution::PCWins: {
            PilotRecord *updated = backendToPalm(backendRecord, context);
            if (updated) {
                updated->setId(palmRecord->id());
                writePalmRecord(updated, context);
                delete updated;
                palmStats.updated++;
            }
            return true;
        }

        case ConflictResolution::Duplicate: {
            // Create Palm record on backend (new ID)
            BackendRecord *newBackend = palmToBackend(palmRecord, context);
            if (newBackend) {
                QString newId = context->backend->createRecord(context->collectionId, *newBackend);
                if (!newId.isEmpty()) {
                    // Update mapping to point to new record
                    context->state->mapIds(QString::number(palmRecord->id()), newId);
                    pcStats.created++;
                }
                delete newBackend;
            }

            // Create backend record on Palm (new ID)
            PilotRecord *newPalm = backendToPalm(backendRecord, context);
            if (newPalm) {
                newPalm->setId(0);  // Force new ID
                if (writePalmRecord(newPalm, context)) {
                    context->state->mapIds(QString::number(newPalm->id()), backendRecord->id);
                    palmStats.created++;
                }
                delete newPalm;
            }
            return true;
        }

        case ConflictResolution::Skip:
            pcStats.conflicts++;
            return false;

        case ConflictResolution::AskUser:
            // TODO: Emit signal and wait for user response
            emit logMessage("Conflict requires user resolution - skipping for now");
            pcStats.conflicts++;
            return false;

        default:
            return false;
    }
}

BackendRecord* Conduit::findMatch(PilotRecord *palmRecord,
                                   const QList<BackendRecord*> &candidates)
{
    QString palmDesc = palmRecordDescription(palmRecord).toLower().trimmed();
    if (palmDesc.isEmpty()) return nullptr;

    for (BackendRecord *candidate : candidates) {
        QString candidateDesc = candidate->description().toLower().trimmed();
        if (candidateDesc == palmDesc) {
            return candidate;
        }
    }

    return nullptr;
}

QList<PilotRecord*> Conduit::readPalmRecords(SyncContext *context, bool modifiedOnly)
{
    if (m_dbHandle < 0) return {};

    QList<PilotRecord*> allRecords = context->deviceLink->readAllRecords(m_dbHandle);

    if (!modifiedOnly) {
        return allRecords;
    }

    // Filter to only modified records
    QList<PilotRecord*> modifiedRecords;
    for (PilotRecord *record : allRecords) {
        if (record->isDirty() || record->isDeleted()) {
            modifiedRecords.append(record);
        } else {
            delete record;  // Free unneeded records
        }
    }
    return modifiedRecords;
}

bool Conduit::writePalmRecord(PilotRecord *record, SyncContext *context)
{
    if (m_dbHandle < 0) return false;
    return context->deviceLink->writeRecord(m_dbHandle, record);
}

bool Conduit::deletePalmRecord(const QString &palmId, SyncContext *context)
{
    if (m_dbHandle < 0) return false;
    return context->deviceLink->deleteRecord(m_dbHandle, palmId.toUInt());
}

bool Conduit::checkVolatility(const SyncStats &stats, int totalRecords, int threshold)
{
    if (totalRecords == 0) return true;

    int changePercent = ((stats.created + stats.updated + stats.deleted) * 100) / totalRecords;

    if (changePercent > threshold) {
        emit logMessage(QString("Warning: High volatility detected (%1% changes)").arg(changePercent));
        return false;
    }

    return true;
}

} // namespace Sync
