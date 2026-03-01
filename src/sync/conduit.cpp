#include "conduit.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Sync {

bool SyncConduitBase::canSync(const SyncContext *context) const
{
    if (!context) return false;
    if (!context->deviceLink) return false;
    if (!context->backend) return false;
    if (!context->state) return false;
    if (!context->deviceLink->isConnected()) return false;
    return true;
}

SyncResult SyncConduitBase::sync(SyncContext *context)
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

    // Load categories from Palm AppInfo block
    loadCategories(context);

    // Determine if this is a first sync
    context->isFirstSync = context->state->isFirstSync();

    // Apply any pre-resolved conflicts from previous sessions
    if (!context->isFirstSync) {
        SyncStats conflictPalmStats, conflictPcStats;
        int appliedConflicts = applyResolvedConflicts(context, conflictPalmStats, conflictPcStats);
        if (appliedConflicts > 0) {
            // Merge conflict resolution stats into result
            result.palmStats = result.palmStats + conflictPalmStats;
            result.pcStats = result.pcStats + conflictPcStats;
        }
    }

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
            case SyncMode::Backup:
                result = backup(context);
                break;
            case SyncMode::Restore:
                result = restore(context);
                break;
            default:
                result = hotSync(context);
                break;
        }
    }

    // If sync was successful, clean up and reset flags
    // Skip this for Backup mode - backup shouldn't modify Palm state
    if (result.success && context->mode != SyncMode::Backup) {
        // Write modified categories back to Palm (if any were added)
        if (!writeModifiedCategories(context)) {
            emit logMessage("Warning: Failed to write modified categories");
        }

        // Persist final category list for conduit views (after sync has
        // merged desktop categories into the Palm's list)
        persistCategoriesForViews(context);

        // Clean up deleted records from Palm database
        context->deviceLink->cleanUpDatabase(m_dbHandle);

        // Reset dirty flags on Palm records
        context->deviceLink->resetSyncFlags(m_dbHandle);

        emit logMessage("Cleared Palm sync flags");
    }

    // Close Palm database
    context->deviceLink->closeDatabase(m_dbHandle);
    m_dbHandle = -1;

    // Update sync state
    if (result.success) {
        if (context->mode == SyncMode::HotSync && !context->isFirstSync) {
            // Incremental baseline: only update changed records
            saveBaselineIncremental(context,
                                    context->baselineUpdatedPcIds,
                                    context->baselineDeletedPcIds);
        } else {
            // Full baseline for FullSync, FirstSync, etc.
            saveBaseline(context);
        }

        context->state->setLastSyncTime(QDateTime::currentDateTime());
        context->state->save();
    }

    result.endTime = QDateTime::currentDateTime();

    emit logMessage(QString("Sync complete: %1 (%2 ms)")
        .arg(result.success ? "Success" : "Failed")
        .arg(result.durationMs()));

    return result;
}

SyncResult SyncConduitBase::hotSync(SyncContext *context)
{
    emit logMessage("Performing HotSync (modified records only)...");

    QElapsedTimer totalTimer;
    totalTimer.start();

    SyncResult result;
    result.success = true;

    // Track which Palm records we've processed (by ID)
    QSet<QString> processedPalmIds;
    // Track which backend records we've processed
    QSet<QString> processedBackendIds;

    // ── Phase 1: Read dirty Palm records (only modified transfer over USB) ──
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    QList<PilotRecord*> palmRecords = readPalmRecords(context, true);
    qDebug() << "[HotSync] Phase 1: readModifiedRecords:" << palmRecords.size()
             << "dirty records in" << phaseTimer.elapsed() << "ms";
    emit logMessage(QString("Found %1 modified Palm records").arg(palmRecords.size()));

    // ── Phase 2: Load backend records, build hash index for O(1) lookup ──
    phaseTimer.restart();
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);
    QHash<QString, BackendRecord*> backendIndex;
    backendIndex.reserve(backendRecords.size());
    for (BackendRecord *rec : backendRecords) {
        backendIndex.insert(rec->id, rec);
    }
    qDebug() << "[HotSync] Phase 2: loaded" << backendRecords.size()
             << "backend records, built index in" << phaseTimer.elapsed() << "ms";
    emit logMessage(QString("Loaded %1 backend records").arg(backendRecords.size()));

    // ── Phase 3: Process dirty Palm records ──
    phaseTimer.restart();
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled || isCancelled()) break;

        QString palmId = QString::number(palmRecord->id());
        processedPalmIds.insert(palmId);
        QString pcId = context->state->pcIdForPalm(palmId);

        BackendRecord *backendRecord = nullptr;
        if (!pcId.isEmpty()) {
            backendRecord = backendIndex.value(pcId, nullptr);
        }

        syncRecord(palmRecord, backendRecord, context, result.palmStats, result.pcStats);

        // Track baseline updates for incremental save
        if (!palmRecord->isDeleted()) {
            QString categoryName = categoryNameForIndex(palmRecord->category());
            context->state->updateCategories(palmId, categoryName, {});
            // Re-query pcId after syncRecord (it may have created a new mapping)
            QString newPcId = context->state->pcIdForPalm(palmId);
            if (!newPcId.isEmpty()) {
                context->baselineUpdatedPcIds.insert(newPcId);
            }
        } else {
            // Palm record deleted — PC side may also be deleted
            if (!pcId.isEmpty()) {
                context->baselineDeletedPcIds.insert(pcId);
            }
        }

        if (backendRecord) {
            processedBackendIds.insert(backendRecord->id);
        }
    }
    qDebug() << "[HotSync] Phase 3: processed" << palmRecords.size()
             << "dirty Palm records in" << phaseTimer.elapsed() << "ms";

    // ── Phase 4: Category-change detection (single full read) ──
    // Palm doesn't set the dirty flag when only the category changes,
    // so we read ALL records once and check mapped records for differences
    phaseTimer.restart();
    QList<PilotRecord*> allPalmRecords = readPalmRecords(context, false);
    int categoryChanges = 0;
    for (PilotRecord *palmRecord : allPalmRecords) {
        if (context->cancelled || isCancelled()) break;

        QString palmId = QString::number(palmRecord->id());

        // Skip if already processed (was dirty)
        if (processedPalmIds.contains(palmId)) continue;

        // Skip deleted records
        if (palmRecord->isDeleted()) continue;

        // Check if we have a mapping for this record
        QString pcId = context->state->pcIdForPalm(palmId);
        if (pcId.isEmpty()) continue;

        // Get stored category from mapping
        IDMapping mapping = context->state->getMapping(palmId);
        QString storedCategory = mapping.palmCategory;
        QString currentCategory = categoryNameForIndex(palmRecord->category());

        // Compare categories (case-insensitive, treating empty/"Unfiled" as equivalent)
        QString normalizedStored = storedCategory;
        QString normalizedCurrent = currentCategory;
        if (normalizedStored.compare(QStringLiteral("Unfiled"), Qt::CaseInsensitive) == 0) {
            normalizedStored.clear();
        }
        if (normalizedCurrent.compare(QStringLiteral("Unfiled"), Qt::CaseInsensitive) == 0) {
            normalizedCurrent.clear();
        }

        if (normalizedStored.compare(normalizedCurrent, Qt::CaseInsensitive) != 0) {
            qDebug() << "[HotSync] Phase 4: Category change detected:"
                     << palmRecordDescription(palmRecord)
                     << "stored=" << (storedCategory.isEmpty() ? "Unfiled" : storedCategory)
                     << "current=" << (currentCategory.isEmpty() ? "Unfiled" : currentCategory);
            emit logMessage(QString("Category changed on Palm: %1 (%2 → %3)")
                .arg(palmRecordDescription(palmRecord))
                .arg(storedCategory.isEmpty() ? QStringLiteral("Unfiled") : storedCategory)
                .arg(currentCategory.isEmpty() ? QStringLiteral("Unfiled") : currentCategory));

            BackendRecord *backendRecord = backendIndex.value(pcId, nullptr);

            if (backendRecord) {
                BackendRecord *updated = palmToBackend(palmRecord, context);
                if (updated) {
                    updated->id = backendRecord->id;
                    context->backend->updateRecord(*updated);
                    delete updated;
                    result.pcStats.updated++;
                    categoryChanges++;
                    processedBackendIds.insert(backendRecord->id);
                    context->baselineUpdatedPcIds.insert(backendRecord->id);
                }
            }

            context->state->updateCategories(palmId, currentCategory, {});
            processedPalmIds.insert(palmId);
        }
    }
    qDeleteAll(allPalmRecords);

    qDebug() << "[HotSync] Phase 4: category scan complete -"
             << categoryChanges << "changes in" << phaseTimer.elapsed() << "ms";
    if (categoryChanges > 0) {
        emit logMessage(QString("Synced %1 category-only changes from Palm").arg(categoryChanges));
    }

    // ── Phase 5: Process modified backend records ──
    phaseTimer.restart();
    int backendChanges = 0;
    int backendNew = 0;
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled || isCancelled()) break;
        if (processedBackendIds.contains(backendRecord->id)) continue;

        QString currentHash = backendRecord->contentHash;
        QString baselineHash = context->state->baselineHash(backendRecord->id);

        QString palmId = context->state->palmIdForPC(backendRecord->id);
        bool isNew = palmId.isEmpty();
        bool isModified = !baselineHash.isEmpty() && (currentHash != baselineHash);

        if (isNew || isModified) {
            qDebug() << "[HotSync] Phase 5: Backend change:"
                     << backendRecord->id
                     << (isNew ? "NEW" : "MODIFIED");

            PilotRecord *palmRecord = nullptr;
            bool ownsPalmRecord = false;

            if (!palmId.isEmpty()) {
                palmRecord = context->deviceLink->readRecordById(m_dbHandle, palmId.toUInt());
                ownsPalmRecord = true;
                if (palmRecord) {
                    emit logMessage(QString("PC modified: %1 → updating Palm")
                        .arg(backendRecord->description()));
                }
            } else {
                emit logMessage(QString("New on PC: %1 → creating on Palm")
                    .arg(backendRecord->description()));
            }

            syncRecord(palmRecord, backendRecord, context, result.palmStats, result.pcStats);
            context->baselineUpdatedPcIds.insert(backendRecord->id);

            if (isNew) backendNew++;
            else backendChanges++;

            if (ownsPalmRecord) {
                delete palmRecord;
            }
        }
    }
    qDebug() << "[HotSync] Phase 5:" << backendChanges << "modified,"
             << backendNew << "new backend records in" << phaseTimer.elapsed() << "ms";

    // ── Phase 6: Detect deleted PC files ──
    phaseTimer.restart();
    QSet<QString> existingPcIds;
    existingPcIds.reserve(backendRecords.size());
    for (BackendRecord *rec : backendRecords) {
        existingPcIds.insert(rec->id);
    }

    int pcDeleted = 0;
    QStringList allMappedPcIds = context->state->allPCIds();
    for (const QString &pcId : allMappedPcIds) {
        if (context->cancelled || isCancelled()) break;
        if (existingPcIds.contains(pcId)) continue;

        QString palmId = context->state->palmIdForPC(pcId);
        if (!palmId.isEmpty()) {
            qDebug() << "[HotSync] Phase 6: PC file deleted:" << pcId
                     << "→ deleting Palm record" << palmId;
            emit logMessage(QString("PC file deleted, removing from Palm: %1").arg(pcId));
            if (deletePalmRecord(palmId, context)) {
                context->state->removePCMapping(pcId);
                result.palmStats.deleted++;
                context->baselineDeletedPcIds.insert(pcId);
                pcDeleted++;
            }
        }
    }
    qDebug() << "[HotSync] Phase 6:" << pcDeleted << "deleted PC files in"
             << phaseTimer.elapsed() << "ms";

    // Cleanup
    qDeleteAll(palmRecords);
    qDeleteAll(backendRecords);

    qDebug() << "[HotSync] TOTAL: completed in" << totalTimer.elapsed() << "ms"
             << "| Palm: created=" << result.palmStats.created
             << "updated=" << result.palmStats.updated
             << "deleted=" << result.palmStats.deleted
             << "| PC: created=" << result.pcStats.created
             << "updated=" << result.pcStats.updated
             << "deleted=" << result.pcStats.deleted;

    return result;
}

SyncResult SyncConduitBase::fullSync(SyncContext *context)
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
        if (context->cancelled || isCancelled()) break;

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
        if (context->cancelled || isCancelled()) break;
        if (processedBackendIds.contains(backendRecord->id)) continue;

        syncRecord(nullptr, backendRecord, context, result.palmStats, result.pcStats);
    }

    // Cleanup
    qDeleteAll(palmRecords);
    qDeleteAll(backendRecords);

    return result;
}

SyncResult SyncConduitBase::firstSync(SyncContext *context)
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
        if (context->cancelled || isCancelled()) break;
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
            // Store initial category for change tracking
            QString categoryName = categoryNameForIndex(palmRecord->category());
            context->state->updateCategories(palmId, categoryName, {});
            matchedBackendIds.insert(match->id);
            result.palmStats.unchanged++;
        } else {
            // No match - create new backend record
            BackendRecord *newRecord = palmToBackend(palmRecord, context);
            if (newRecord) {
                QString newId = context->backend->createRecord(context->collectionId, *newRecord);
                if (!newId.isEmpty()) {
                    context->state->mapIds(palmId, newId);
                    // Store initial category for change tracking
                    QString categoryName = categoryNameForIndex(palmRecord->category());
                    context->state->updateCategories(palmId, categoryName, {});
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
        if (context->cancelled || isCancelled()) break;
        if (matchedBackendIds.contains(backendRecord->id)) continue;
        if (backendRecord->isDeleted) continue;

        // Create on Palm
        PilotRecord *palmRecord = backendToPalm(backendRecord, context);
        if (palmRecord) {
            if (writePalmRecord(palmRecord, context)) {
                QString palmId = QString::number(palmRecord->id());
                context->state->mapIds(palmId, backendRecord->id);
                // Store initial category for change tracking
                QString catName = categoryNameForIndex(palmRecord->category());
                context->state->updateCategories(palmId, catName, {});
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

SyncResult SyncConduitBase::copyPalmToPC(SyncContext *context)
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
        if (context->cancelled || isCancelled()) break;
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

SyncResult SyncConduitBase::copyPCToPalm(SyncContext *context)
{
    emit logMessage("Copying PC → Palm...");

    SyncResult result;
    result.success = true;

    // Load all backend records
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);

    int count = 0;
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled || isCancelled()) break;
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

SyncResult SyncConduitBase::backup(SyncContext *context)
{
    emit logMessage("Backing up Palm → PC (preserving old files)...");

    SyncResult result;
    result.success = true;

    // Load all Palm records
    QList<PilotRecord*> palmRecords = readPalmRecords(context, false);
    emit logMessage(QString("Found %1 Palm records to backup").arg(palmRecords.size()));

    int count = 0;
    for (PilotRecord *palmRecord : palmRecords) {
        if (context->cancelled || isCancelled()) break;
        if (palmRecord->isDeleted()) continue;

        QString palmId = QString::number(palmRecord->id());

        // Convert to backend format
        BackendRecord *backendRecord = palmToBackend(palmRecord, context);
        if (backendRecord) {
            QString existingId = context->state->pcIdForPalm(palmId);

            if (existingId.isEmpty()) {
                // Create new backup file
                QString newId = context->backend->createRecord(context->collectionId, *backendRecord);
                if (!newId.isEmpty()) {
                    context->state->mapIds(palmId, newId);
                    result.pcStats.created++;
                }
            } else {
                // Update existing backup file
                backendRecord->id = existingId;
                if (context->backend->updateRecord(*backendRecord)) {
                    result.pcStats.updated++;
                }
            }
            delete backendRecord;
        }

        count++;
        if (count % 50 == 0) {
            emit progressUpdated(count, palmRecords.size(), "Backing up...");
        }
    }

    // Note: Unlike copyPalmToPC, we do NOT delete PC files
    // This preserves old backups even if records are deleted on Palm

    qDeleteAll(palmRecords);

    emit logMessage(QString("Backup complete: %1 created, %2 updated")
        .arg(result.pcStats.created).arg(result.pcStats.updated));

    return result;
}

SyncResult SyncConduitBase::restore(SyncContext *context)
{
    emit logMessage("Restoring PC → Palm (full restore)...");

    SyncResult result;
    result.success = true;

    // Load all backend records
    QList<BackendRecord*> backendRecords = context->backend->loadRecords(context->collectionId);
    emit logMessage(QString("Found %1 PC records to restore").arg(backendRecords.size()));

    // Load all existing Palm records (to find ones to delete)
    QList<PilotRecord*> existingPalmRecords = readPalmRecords(context, false);
    QSet<QString> restoredPalmIds;

    int count = 0;
    for (BackendRecord *backendRecord : backendRecords) {
        if (context->cancelled || isCancelled()) break;
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
                restoredPalmIds.insert(QString::number(palmRecord->id()));
            }
            delete palmRecord;
        }

        count++;
        if (count % 50 == 0) {
            emit progressUpdated(count, backendRecords.size(), "Restoring...");
        }
    }

    // Delete Palm records that no longer exist on PC
    for (PilotRecord *existingRecord : existingPalmRecords) {
        if (context->cancelled || isCancelled()) break;
        QString palmId = QString::number(existingRecord->id());

        if (!restoredPalmIds.contains(palmId)) {
            // This Palm record has no corresponding PC file - delete it
            if (deletePalmRecord(palmId, context)) {
                context->state->removePalmMapping(palmId);
                result.palmStats.deleted++;
                emit logMessage(QString("Deleted from Palm: %1")
                    .arg(palmRecordDescription(existingRecord)));
            }
        }
    }

    qDeleteAll(backendRecords);
    qDeleteAll(existingPalmRecords);

    emit logMessage(QString("Restore complete: %1 created, %2 updated, %3 deleted")
        .arg(result.palmStats.created)
        .arg(result.palmStats.updated)
        .arg(result.palmStats.deleted));

    return result;
}

void SyncConduitBase::syncRecord(PilotRecord *palmRecord,
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

        // Debug: Log record state
        qDebug() << "[SyncConduitBase::syncRecord] Palm ID:" << palmRecord->id()
                 << "category:" << palmRecord->category()
                 << "attr:" << Qt::hex << palmRecord->attributes() << Qt::dec
                 << "dirty:" << palmModified
                 << "deleted:" << palmDeleted;

        // Detect backend modifications using baseline hash comparison
        QString currentHash = backendRecord->contentHash;
        QString baselineHash = context->state->baselineHash(backendRecord->id);
        bool backendModified = !baselineHash.isEmpty() && (currentHash != baselineHash);

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
            // Neither content modified - but check for category-only changes
            // Palm doesn't set dirty flag when only category changes
            QString palmId = QString::number(palmRecord->id());
            IDMapping mapping = context->state->getMapping(palmId);
            QString storedCategory = mapping.palmCategory;
            QString currentCategory = categoryNameForIndex(palmRecord->category());

            // Normalize categories (empty and "Unfiled" are equivalent)
            QString normalizedStored = storedCategory;
            QString normalizedCurrent = currentCategory;
            if (normalizedStored.compare("Unfiled", Qt::CaseInsensitive) == 0) {
                normalizedStored.clear();
            }
            if (normalizedCurrent.compare("Unfiled", Qt::CaseInsensitive) == 0) {
                normalizedCurrent.clear();
            }

            if (!normalizedStored.isEmpty() || !normalizedCurrent.isEmpty()) {
                // Only check if at least one has a non-Unfiled category
                if (normalizedStored.compare(normalizedCurrent, Qt::CaseInsensitive) != 0) {
                    // Category changed on Palm - update backend
                    emit logMessage(QString("Category changed: %1 (%2 → %3)")
                        .arg(palmRecordDescription(palmRecord))
                        .arg(storedCategory.isEmpty() ? "Unfiled" : storedCategory)
                        .arg(currentCategory.isEmpty() ? "Unfiled" : currentCategory));

                    BackendRecord *updated = palmToBackend(palmRecord, context);
                    if (updated) {
                        updated->id = backendRecord->id;
                        context->backend->updateRecord(*updated);
                        delete updated;
                        pcStats.updated++;

                        // Update category tracking
                        context->state->updateCategories(palmId, currentCategory, {});
                    }
                } else {
                    palmStats.unchanged++;
                }
            } else {
                palmStats.unchanged++;
            }

            // Always update category tracking if not already set
            if (storedCategory.isEmpty() && !currentCategory.isEmpty()) {
                context->state->updateCategories(palmId, currentCategory, {});
            }
        }
    }
    // Only Palm record exists (new or orphaned)
    else if (palmRecord && !backendRecord) {
        if (palmRecord->isDeleted()) {
            // Was deleted and mapping already gone
            palmStats.deleted++;
        } else {
            // New on Palm - create on backend
            emit logMessage(QString("Creating PC file from Palm record %1: %2")
                .arg(palmRecord->id()).arg(palmRecordDescription(palmRecord)));
            BackendRecord *newRecord = palmToBackend(palmRecord, context);
            if (newRecord) {
                emit logMessage(QString("  Converted to backend record, size=%1 bytes").arg(newRecord->data.size()));
                QString newId = context->backend->createRecord(context->collectionId, *newRecord);
                if (!newId.isEmpty()) {
                    emit logMessage(QString("  Created file: %1").arg(newId));
                    QString palmId = QString::number(palmRecord->id());
                    context->state->mapIds(palmId, newId);
                    // Store category for change tracking
                    QString catName = categoryNameForIndex(palmRecord->category());
                    context->state->updateCategories(palmId, catName, {});
                    pcStats.created++;
                } else {
                    emit logMessage("  ERROR: Failed to create file on PC!");
                }
                delete newRecord;
            } else {
                emit logMessage("  ERROR: palmToBackend() returned null!");
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
            emit logMessage(QString("Creating Palm record from PC: %1").arg(backendRecord->description()));
            PilotRecord *newRecord = backendToPalm(backendRecord, context);
            if (newRecord) {
                emit logMessage(QString("  Converted to Palm record, size=%1 bytes").arg(newRecord->size()));
                if (writePalmRecord(newRecord, context)) {
                    emit logMessage(QString("  Written successfully, new Palm ID: %1").arg(newRecord->id()));
                    QString palmId = QString::number(newRecord->id());
                    context->state->mapIds(palmId, backendRecord->id);
                    // Store category for change tracking
                    QString catName = categoryNameForIndex(newRecord->category());
                    context->state->updateCategories(palmId, catName, {});
                    palmStats.created++;
                } else {
                    emit logMessage("  ERROR: Failed to write Palm record!");
                }
                delete newRecord;
            } else {
                emit logMessage("  ERROR: backendToPalm() returned null!");
            }
        }
    }
}

bool SyncConduitBase::resolveConflict(PilotRecord *palmRecord,
                               BackendRecord *backendRecord,
                               SyncContext *context,
                               SyncStats &palmStats,
                               SyncStats &pcStats)
{
    emit conflictDetected(
        palmRecordDescription(palmRecord),
        backendRecord->description()
    );

    // Use new conflict handler if available
    if (context->conflictHandler) {
        return resolveConflictWithHandler(palmRecord, backendRecord, context, palmStats, pcStats);
    }

    // Legacy conflict resolution
    return resolveConflictLegacy(palmRecord, backendRecord, context, palmStats, pcStats);
}

bool SyncConduitBase::resolveConflictWithHandler(PilotRecord *palmRecord,
                                          BackendRecord *backendRecord,
                                          SyncContext *context,
                                          SyncStats &palmStats,
                                          SyncStats &pcStats)
{
    using namespace QSyncCore;

    // Build conflict record with snapshots
    ConflictRecord conflict;
    conflict.conflictId = ConflictRecord::generateId();
    conflict.conduitId = conduitId();
    conflict.detectedAt = QDateTime::currentDateTime();
    conflict.syncSessionId = context->syncSessionId;

    // Determine conflict type
    bool palmDeleted = palmRecord && palmRecord->isDeleted();
    bool backendDeleted = backendRecord && backendRecord->isDeleted;

    if (palmDeleted && !backendDeleted) {
        conflict.type = ConflictType::DeletedVsModified;
    } else if (!palmDeleted && backendDeleted) {
        conflict.type = ConflictType::ModifiedVsDeleted;
    } else {
        conflict.type = ConflictType::BothModified;
    }

    // Create source snapshot (Palm)
    if (palmRecord && !palmRecord->isDeleted()) {
        conflict.source.id = QString::number(palmRecord->id());
        conflict.source.description = palmRecordDescription(palmRecord);
        conflict.source.content = palmRecord->data();
        conflict.source.contentType = backendRecord ? backendRecord->type : QStringLiteral("application/octet-stream");
        // Palm doesn't track modification time well, use current time
        conflict.source.lastModified = QDateTime::currentDateTime();
    }

    // Create target snapshot (PC/Backend)
    if (backendRecord && !backendRecord->isDeleted) {
        conflict.target.id = backendRecord->id;
        conflict.target.description = backendRecord->description();
        conflict.target.content = backendRecord->data;
        conflict.target.contentHash = backendRecord->contentHash;
        conflict.target.contentType = backendRecord->type;  // Use type (memo, contact, etc.)
        conflict.target.lastModified = backendRecord->lastModified;
    }

    // Enrich snapshots with human-readable content and metadata
    enrichConflictSnapshot(conflict.source, /*isSourceSide=*/true);
    enrichConflictSnapshot(conflict.target, /*isSourceSide=*/false);

    // Set category from Palm category index
    if (palmRecord && !palmRecord->isDeleted()) {
        conflict.source.category = categoryNameForIndex(palmRecord->category());
    }

    // Assess complexity
    conflict.assessComplexity();

    emit logMessage(QString("Conflict detected: %1 [%2]")
        .arg(conflict.summary())
        .arg(conflict.complexity == ConflictComplexity::Simple ? "Simple" :
             conflict.complexity == ConflictComplexity::Moderate ? "Moderate" : "Complex"));

    // Ask handler to resolve
    ConflictDecision decision = context->conflictHandler->handleConflict(
        conflict, context->conflictSettings);

    // Apply the decision
    return applyConflictDecision(conflict, decision, palmRecord, backendRecord,
                                  context, palmStats, pcStats);
}

bool SyncConduitBase::applyConflictDecision(const QSyncCore::ConflictRecord &conflict,
                                     QSyncCore::ConflictDecision decision,
                                     PilotRecord *palmRecord,
                                     BackendRecord *backendRecord,
                                     SyncContext *context,
                                     SyncStats &palmStats,
                                     SyncStats &pcStats)
{
    using namespace QSyncCore;

    switch (decision) {
        case ConflictDecision::UseSource: {
            // Palm wins - update backend
            if (palmRecord && !palmRecord->isDeleted()) {
                BackendRecord *updated = palmToBackend(palmRecord, context);
                if (updated) {
                    updated->id = backendRecord->id;
                    context->backend->updateRecord(*updated);
                    delete updated;
                    pcStats.updated++;
                    emit logMessage(QString("Resolved: Using source (Palm) for %1")
                        .arg(conflict.source.description));
                }
            } else {
                // Source was deleted - delete target
                context->backend->deleteRecord(backendRecord->id);
                context->state->removePCMapping(backendRecord->id);
                pcStats.deleted++;
            }
            return true;
        }

        case ConflictDecision::UseTarget: {
            // PC wins - update Palm
            if (backendRecord && !backendRecord->isDeleted) {
                PilotRecord *updated = backendToPalm(backendRecord, context);
                if (updated) {
                    updated->setId(palmRecord->id());
                    writePalmRecord(updated, context);
                    delete updated;
                    palmStats.updated++;
                    emit logMessage(QString("Resolved: Using target (PC) for %1")
                        .arg(conflict.target.description));
                }
            } else {
                // Target was deleted - delete source
                deletePalmRecord(QString::number(palmRecord->id()), context);
                context->state->removePalmMapping(QString::number(palmRecord->id()));
                palmStats.deleted++;
            }
            return true;
        }

        case ConflictDecision::UseBoth: {
            // Duplicate - keep both
            // Create Palm version on backend with new ID
            if (palmRecord && !palmRecord->isDeleted()) {
                BackendRecord *newBackend = palmToBackend(palmRecord, context);
                if (newBackend) {
                    QString newId = context->backend->createRecord(context->collectionId, *newBackend);
                    if (!newId.isEmpty()) {
                        context->state->mapIds(QString::number(palmRecord->id()), newId);
                        pcStats.created++;
                    }
                    delete newBackend;
                }
            }

            // Create backend version on Palm with new ID
            if (backendRecord && !backendRecord->isDeleted) {
                PilotRecord *newPalm = backendToPalm(backendRecord, context);
                if (newPalm) {
                    newPalm->setId(0);  // Force new ID
                    if (writePalmRecord(newPalm, context)) {
                        context->state->mapIds(QString::number(newPalm->id()), backendRecord->id);
                        palmStats.created++;
                    }
                    delete newPalm;
                }
            }
            emit logMessage(QString("Resolved: Keeping both versions for %1")
                .arg(conflict.summary()));
            return true;
        }

        case ConflictDecision::Skip:
            emit logMessage(QString("Resolved: Skipping conflict for %1")
                .arg(conflict.summary()));
            pcStats.conflicts++;
            return false;

        case ConflictDecision::Pending:
            // Deferred - save to conflict store for batch review
            if (context->state && context->state->conflictStore()) {
                context->state->conflictStore()->addConflict(conflict);
                emit logMessage(QString("Deferred: %1 saved for later review")
                    .arg(conflict.summary()));
            } else {
                emit logMessage(QString("Warning: No conflict store available, cannot defer %1")
                    .arg(conflict.summary()));
            }
            pcStats.conflicts++;
            return false;

        case ConflictDecision::DeleteBoth:
            // Delete from both sides
            if (palmRecord) {
                deletePalmRecord(QString::number(palmRecord->id()), context);
                context->state->removePalmMapping(QString::number(palmRecord->id()));
                palmStats.deleted++;
            }
            if (backendRecord) {
                context->backend->deleteRecord(backendRecord->id);
                context->state->removePCMapping(backendRecord->id);
                pcStats.deleted++;
            }
            emit logMessage(QString("Resolved: Deleted both versions of %1")
                .arg(conflict.summary()));
            return true;

        case ConflictDecision::Merge:
            // Merge not yet implemented - fall through to skip
            emit logMessage("Merge not implemented, skipping conflict");
            pcStats.conflicts++;
            return false;
    }

    return false;
}

bool SyncConduitBase::resolveConflictLegacy(PilotRecord *palmRecord,
                                     BackendRecord *backendRecord,
                                     SyncContext *context,
                                     SyncStats &palmStats,
                                     SyncStats &pcStats)
{
    // Original legacy implementation for backwards compatibility
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
            emit logMessage("Conflict requires user resolution - skipping (no handler configured)");
            pcStats.conflicts++;
            return false;

        default:
            return false;
    }
}

int SyncConduitBase::applyResolvedConflicts(SyncContext *context,
                                     SyncStats &palmStats,
                                     SyncStats &pcStats)
{
    if (!context->state || !context->state->conflictStore()) {
        return 0;
    }

    QSyncCore::ConflictStore *store = context->state->conflictStore();
    QList<QSyncCore::ConflictRecord> conflicts =
        store->resolvedUnappliedConflictsForConduit(conduitId());

    if (conflicts.isEmpty()) {
        return 0;
    }

    emit logMessage(QString("Applying %1 pre-resolved conflicts...").arg(conflicts.size()));

    int appliedCount = 0;

    for (const QSyncCore::ConflictRecord &conflict : conflicts) {
        if (context->cancelled || isCancelled()) break;

        emit logMessage(QString("  Applying resolution for: %1 [%2]")
            .arg(conflict.summary())
            .arg(QSyncCore::conflictDecisionToString(conflict.decision)));

        bool success = false;
        QString errorMsg;

        // Load current records based on stored IDs
        PilotRecord *palmRecord = nullptr;
        BackendRecord *backendRecord = nullptr;
        bool ownsPalmRecord = false;
        bool ownsBackendRecord = false;

        // Load Palm record if source ID is available
        if (!conflict.source.id.isEmpty()) {
            bool ok;
            quint32 palmId = conflict.source.id.toUInt(&ok);
            if (ok) {
                palmRecord = context->deviceLink->readRecordById(m_dbHandle, palmId);
                ownsPalmRecord = true;
            }
        }

        // Load backend record if target ID is available
        if (!conflict.target.id.isEmpty()) {
            QList<BackendRecord*> allRecords = context->backend->loadRecords(context->collectionId);
            for (BackendRecord *rec : allRecords) {
                if (rec->id == conflict.target.id) {
                    backendRecord = rec;
                } else {
                    delete rec;
                }
            }
            ownsBackendRecord = true;
        }

        // Apply the decision
        switch (conflict.decision) {
            case QSyncCore::ConflictDecision::UseSource:
                // Palm wins - copy Palm to PC
                if (palmRecord) {
                    BackendRecord *updated = palmToBackend(palmRecord, context);
                    if (updated) {
                        if (backendRecord) {
                            // Update existing record
                            updated->id = backendRecord->id;
                            context->backend->updateRecord(*updated);
                            pcStats.updated++;
                        } else {
                            // Create new record (target was deleted)
                            QString newId = context->backend->createRecord(context->collectionId, *updated);
                            if (!newId.isEmpty()) {
                                context->state->mapIds(conflict.source.id, newId);
                                pcStats.created++;
                            }
                        }
                        delete updated;
                        success = true;
                    }
                } else if (conflict.source.isDeleted()) {
                    // Source was deleted - delete target too
                    if (backendRecord) {
                        context->backend->deleteRecord(backendRecord->id);
                        context->state->removePCMapping(backendRecord->id);
                        pcStats.deleted++;
                        success = true;
                    }
                }
                break;

            case QSyncCore::ConflictDecision::UseTarget:
                // PC wins - copy PC to Palm
                if (backendRecord) {
                    PilotRecord *updated = backendToPalm(backendRecord, context);
                    if (updated) {
                        if (palmRecord) {
                            // Update existing record
                            updated->setId(palmRecord->id());
                            writePalmRecord(updated, context);
                            palmStats.updated++;
                        } else {
                            // Create new record (source was deleted)
                            updated->setId(0);
                            if (writePalmRecord(updated, context)) {
                                context->state->mapIds(QString::number(updated->id()), backendRecord->id);
                                palmStats.created++;
                            }
                        }
                        delete updated;
                        success = true;
                    }
                } else if (conflict.target.isDeleted()) {
                    // Target was deleted - delete source too
                    if (palmRecord) {
                        deletePalmRecord(QString::number(palmRecord->id()), context);
                        context->state->removePalmMapping(QString::number(palmRecord->id()));
                        palmStats.deleted++;
                        success = true;
                    }
                }
                break;

            case QSyncCore::ConflictDecision::UseBoth:
                // Keep both - duplicate the records
                if (palmRecord) {
                    BackendRecord *newBackend = palmToBackend(palmRecord, context);
                    if (newBackend) {
                        QString newId = context->backend->createRecord(context->collectionId, *newBackend);
                        if (!newId.isEmpty()) {
                            context->state->mapIds(conflict.source.id, newId);
                            pcStats.created++;
                        }
                        delete newBackend;
                    }
                }
                if (backendRecord && palmRecord) {
                    // Create backend record on Palm
                    PilotRecord *newPalm = backendToPalm(backendRecord, context);
                    if (newPalm) {
                        newPalm->setId(0);
                        if (writePalmRecord(newPalm, context)) {
                            context->state->mapIds(QString::number(newPalm->id()), backendRecord->id);
                            palmStats.created++;
                        }
                        delete newPalm;
                    }
                }
                success = true;
                break;

            case QSyncCore::ConflictDecision::DeleteBoth:
                // Delete from both sides
                if (palmRecord) {
                    deletePalmRecord(QString::number(palmRecord->id()), context);
                    context->state->removePalmMapping(QString::number(palmRecord->id()));
                    palmStats.deleted++;
                }
                if (backendRecord) {
                    context->backend->deleteRecord(backendRecord->id);
                    context->state->removePCMapping(backendRecord->id);
                    pcStats.deleted++;
                }
                success = true;
                break;

            case QSyncCore::ConflictDecision::Skip:
                // Skip - do nothing, just mark as applied
                success = true;
                break;

            case QSyncCore::ConflictDecision::Pending:
            case QSyncCore::ConflictDecision::Merge:
                // These shouldn't be in resolved-unapplied list
                errorMsg = "Unexpected decision state";
                break;
        }

        // Mark the conflict as applied
        store->markApplied(conflict.conflictId, success, errorMsg);

        if (success) {
            appliedCount++;
            emit logMessage(QString("    Applied successfully"));
        } else {
            emit logMessage(QString("    Failed to apply: %1").arg(errorMsg.isEmpty() ? "Unknown error" : errorMsg));
        }

        // Cleanup
        if (ownsPalmRecord && palmRecord) {
            delete palmRecord;
        }
        if (ownsBackendRecord && backendRecord) {
            delete backendRecord;
        }
    }

    emit logMessage(QString("Applied %1 of %2 conflict resolutions")
        .arg(appliedCount).arg(conflicts.size()));

    return appliedCount;
}

BackendRecord* SyncConduitBase::findMatch(PilotRecord *palmRecord,
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

QList<PilotRecord*> SyncConduitBase::readPalmRecords(SyncContext *context, bool modifiedOnly)
{
    if (m_dbHandle < 0) return {};

    if (!modifiedOnly) {
        return context->deviceLink->readAllRecords(m_dbHandle);
    }

    // Use dlp_ReadNextModifiedRec — only dirty/deleted records transfer over USB
    QElapsedTimer timer;
    timer.start();
    QList<PilotRecord*> records = context->deviceLink->readModifiedRecords(m_dbHandle);
    qDebug() << "[SyncConduit] readPalmRecords(modifiedOnly=true):"
             << records.size() << "records in" << timer.elapsed() << "ms";
    return records;
}

bool SyncConduitBase::writePalmRecord(PilotRecord *record, SyncContext *context)
{
    if (m_dbHandle < 0) return false;
    return context->deviceLink->writeRecord(m_dbHandle, record);
}

bool SyncConduitBase::deletePalmRecord(const QString &palmId, SyncContext *context)
{
    if (m_dbHandle < 0) return false;
    return context->deviceLink->deleteRecord(m_dbHandle, palmId.toUInt());
}

bool SyncConduitBase::checkVolatility(const SyncStats &stats, int totalRecords, int threshold)
{
    if (totalRecords == 0) return true;

    int changePercent = ((stats.created + stats.updated + stats.deleted) * 100) / totalRecords;

    if (changePercent > threshold) {
        emit logMessage(QString("Warning: High volatility detected (%1% changes)").arg(changePercent));
        return false;
    }

    return true;
}

void SyncConduitBase::saveBaseline(SyncContext *context)
{
    QElapsedTimer timer;
    timer.start();

    // Load all current backend records and save their hashes
    QList<BackendRecord*> records = context->backend->loadRecords(context->collectionId);

    QMap<QString, QString> hashes;
    for (BackendRecord *record : records) {
        hashes[record->id] = record->contentHash;
    }

    context->state->saveBaseline(hashes);

    qDeleteAll(records);

    qDebug() << "[SyncConduit] saveBaseline: saved" << hashes.size()
             << "hashes in" << timer.elapsed() << "ms";
}

void SyncConduitBase::saveBaselineIncremental(SyncContext *context,
                                               const QSet<QString> &updatedPcIds,
                                               const QSet<QString> &deletedPcIds)
{
    QElapsedTimer timer;
    timer.start();

    QSyncCore::BaselineStore *baseline = context->state->baselineStore();

    // Update hashes for created/modified PC records
    int updated = 0;
    for (const QString &pcId : updatedPcIds) {
        BackendRecord *record = context->backend->loadRecord(pcId);
        if (record) {
            baseline->setHash(pcId, record->contentHash);
            delete record;
            updated++;
        }
    }

    // Remove hashes for deleted PC records
    int removed = 0;
    for (const QString &pcId : deletedPcIds) {
        baseline->removeHash(pcId);
        removed++;
    }

    // Persist the updated baseline
    context->state->save();

    qDebug() << "[SyncConduit] saveBaselineIncremental: updated=" << updated
             << "removed=" << removed << "in" << timer.elapsed() << "ms";
}

SyncConduitBase::~SyncConduitBase()
{
    delete m_categories;
}

// ========== Conflict Display Defaults ==========

void SyncConduitBase::enrichConflictSnapshot(QSyncCore::RecordSnapshot &snapshot,
                                              bool isSourceSide) const
{
    if (snapshot.content.isEmpty()) return;

    // Best-effort: for source (Palm binary), try UTF-8 decode
    // For target (PC text), content is already readable
    if (isSourceSide) {
        // Attempt UTF-8 decode of binary content
        QString text = QString::fromUtf8(snapshot.content);
        bool looksLikeText = true;
        for (int i = 0; i < qMin(200, text.length()); ++i) {
            QChar c = text[i];
            if (!c.isPrint() && !c.isSpace()) {
                looksLikeText = false;
                break;
            }
        }
        if (looksLikeText) {
            snapshot.content = text.toUtf8();
        }
    }

    snapshot.metadata[QStringLiteral("raw_size")] = snapshot.content.size();
}

QString SyncConduitBase::formatConflictRecordHtml(const QSyncCore::RecordSnapshot &snapshot) const
{
    QString html;

    // Show metadata as a definition list if present
    if (!snapshot.metadata.isEmpty()) {
        html += QStringLiteral("<dl>");
        for (auto it = snapshot.metadata.constBegin(); it != snapshot.metadata.constEnd(); ++it) {
            html += QStringLiteral("<dt>%1</dt><dd>%2</dd>")
                .arg(it.key().toHtmlEscaped(), it.value().toString().toHtmlEscaped());
        }
        html += QStringLiteral("</dl><hr>");
    }

    // Show content in <pre> with HTML escaping
    QString content = QString::fromUtf8(snapshot.content);
    html += QStringLiteral("<pre>%1</pre>").arg(content.toHtmlEscaped());

    return html;
}

void SyncConduitBase::loadCategories(SyncContext *context)
{
    if (m_categories) {
        delete m_categories;
        m_categories = nullptr;
    }
    m_originalAppInfo.clear();

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        return;
    }

    m_categories = new CategoryInfo();

    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);

    if (context->deviceLink->readAppBlock(m_dbHandle, appInfoBuf, &appInfoSize)) {
        m_originalAppInfo = QByteArray(reinterpret_cast<const char*>(appInfoBuf), appInfoSize);
        m_categories->parse(appInfoBuf, appInfoSize);
        emit logMessage(QString("Loaded %1 categories").arg(m_categories->usedCategories().size()));
    }
}

void SyncConduitBase::persistCategoriesForViews(SyncContext *context)
{
    if (!m_categories || !m_categories->isValid()) {
        return;
    }

    if (!context || context->syncFolderPath.isEmpty()) {
        return;
    }

    // Build path: <syncFolder>/.wildpalms.state/categories-<conduitId>.json
    QString stateDir = context->syncFolderPath + QStringLiteral("/.wildpalms.state");
    QDir().mkpath(stateDir);

    QString filePath = stateDir + QStringLiteral("/categories-%1.json").arg(conduitId());

    // Collect non-empty category names from Palm's AppInfo block
    QJsonArray categoriesArray;
    categoriesArray.append(QStringLiteral("Unfiled"));
    for (int i = 1; i < CategoryInfo::MAX_CATEGORIES; ++i) {
        QString name = m_categories->categoryName(i);
        if (!name.isEmpty()) {
            categoriesArray.append(name);
        }
    }

    QJsonObject root;
    root[QStringLiteral("categories")] = categoriesArray;

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
        emit logMessage(QString("Persisted %1 categories for views").arg(categoriesArray.size()));
    }
}

QString SyncConduitBase::categoryName(int categoryIndex) const
{
    if (m_categories) {
        return m_categories->categoryName(categoryIndex);
    }
    return QString();
}

bool SyncConduitBase::writeModifiedCategories(SyncContext *context)
{
    if (!m_categories || !m_categories->isDirty()) {
        return true;
    }

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        emit logMessage("Warning: Cannot write categories - no device connection");
        return false;
    }

    emit logMessage("Writing modified categories back to Palm...");

    size_t catSize = m_categories->packSize();

    if (m_originalAppInfo.isEmpty()) {
        QByteArray buffer(catSize, 0);
        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()), buffer.size());
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), packed)) {
            emit logMessage("Warning: Failed to write categories to Palm");
            return false;
        }
    } else {
        QByteArray buffer = m_originalAppInfo;

        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()),
                                         qMin(static_cast<size_t>(buffer.size()), catSize));
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), buffer.size())) {
            emit logMessage("Warning: Failed to write AppInfo block to Palm");
            return false;
        }
    }

    m_categories->clearDirty();
    emit logMessage("Categories updated on Palm");
    return true;
}

} // namespace Sync
