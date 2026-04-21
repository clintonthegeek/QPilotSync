#include "palmconflicthandler.h"

#include <QDateTime>

#include "ipalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmrecord.h"

namespace WildPalms::PalmConflict {

using Kalburator::Sync::QSyncCore::ConflictDecision;
using Kalburator::Sync::QSyncCore::ConflictPolicy;
using Kalburator::Sync::QSyncCore::ConflictRecord;
using Kalburator::Sync::QSyncCore::FallbackBehavior;
using WildPalms::PalmSync::IPalmDatabaseAccess;
using WildPalms::PalmSync::PalmBackend;
using WildPalms::PalmSync::PalmRecord;

PalmConflictHandler::PalmConflictHandler(IPalmDatabaseAccess *device,
                                         const PalmBackendConfig *config)
    : m_device(device)
    , m_config(config)
{
}

ConflictDecision PalmConflictHandler::handleConflict(
    ConflictRecord &conflict, const ConflictPolicy &policy)
{
    m_lastOverlay.clear();

    // Stage 1: compute base decision, mirroring AutomaticConflictHandler.
    ConflictDecision baseDecision = ConflictDecision::Pending;
    QString resolvedBy;

    if (policy.shouldAutoResolve(conflict)) {
        baseDecision = policy.getAutoDecision(conflict);
        if (baseDecision != ConflictDecision::Pending) {
            resolvedBy = QStringLiteral("policy:palm");
        }
    }

    if (baseDecision == ConflictDecision::Pending) {
        switch (policy.fallback) {
            case FallbackBehavior::Defer:
                conflict.decision = ConflictDecision::Pending;
                m_pending.append(conflict);
                return ConflictDecision::Pending;
            case FallbackBehavior::Skip:
                baseDecision = ConflictDecision::Skip;
                resolvedBy = QStringLiteral("fallback:skip");
                break;
            case FallbackBehavior::UseDefault:
                baseDecision = policy.getAutoDecision(conflict);
                if (baseDecision == ConflictDecision::Pending) {
                    baseDecision = ConflictDecision::Skip;
                }
                resolvedBy = QStringLiteral("fallback:default");
                break;
            case FallbackBehavior::Abort:
                baseDecision = ConflictDecision::Skip;
                resolvedBy = QStringLiteral("fallback:abort");
                break;
        }
    }

    // Stage 2: Palm overlays may override.
    const auto finalDecision = applyOverlays(conflict, baseDecision);

    conflict.decision = finalDecision;
    conflict.resolvedAt = QDateTime::currentDateTime();
    conflict.resolvedBy = m_lastOverlay.isEmpty()
        ? resolvedBy
        : QStringLiteral("palm-overlay:%1").arg(m_lastOverlay);
    return finalDecision;
}

bool PalmConflictHandler::shouldKeepConnectionAlive() const
{
    if (!m_config) return true;
    return m_config->connectionBehavior == ConnectionBehavior::KeepAlive
        || m_config->connectionBehavior == ConnectionBehavior::TimeoutThenDefer;
    // TimeoutThenDefer is "alive until timeout"; a per-session timer
    // lives in the runtime (E.16). For the handler's static answer we
    // treat it as "yes, keep alive" — the runtime clips it when the
    // timer fires.
}

void PalmConflictHandler::onSyncStart()
{
    m_pending.clear();
    m_lastOverlay.clear();
}

void PalmConflictHandler::onSyncEnd(bool, bool) {}

std::optional<PalmRecord> PalmConflictHandler::lookupPalmRecord(
    const QString &encodedId) const
{
    if (!m_device) return std::nullopt;
    QString dbName;
    std::uint32_t numericId = 0;
    if (!PalmBackend::decodeRecordId(encodedId, &dbName, &numericId)) {
        return std::nullopt;
    }
    return m_device->readRecord(dbName, numericId);
}

ConflictDecision PalmConflictHandler::applyOverlays(
    ConflictRecord &conflict, ConflictDecision baseDecision)
{
    using Kalburator::Sync::QSyncCore::ConflictType;

    // Archive-bit safety: never destroy an archived record on the Palm
    // side. Only fires on destructive decisions (DeleteBoth, UseTarget
    // for a DeletedVsModified, UseSource for a ModifiedVsDeleted).
    const auto sourcePalm = lookupPalmRecord(conflict.source.id);
    const auto targetPalm = lookupPalmRecord(conflict.target.id);
    const bool sourceArchived = sourcePalm && sourcePalm->isArchived();
    const bool targetArchived = targetPalm && targetPalm->isArchived();

    if (sourceArchived || targetArchived) {
        // Destructive directional decisions: a decision that removes
        // the archived side must flip to preserve it.
        if (baseDecision == ConflictDecision::DeleteBoth) {
            m_lastOverlay = QStringLiteral("archive");
            if (sourceArchived) return ConflictDecision::UseSource;
            return ConflictDecision::UseTarget;
        }
        if (baseDecision == ConflictDecision::UseTarget
            && conflict.type == ConflictType::ModifiedVsDeleted
            && sourceArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseSource;
        }
        if (baseDecision == ConflictDecision::UseSource
            && conflict.type == ConflictType::DeletedVsModified
            && targetArchived) {
            m_lastOverlay = QStringLiteral("archive");
            return ConflictDecision::UseTarget;
        }
    }

    // Secret-flag protection: UseBoth on a BothModified where exactly
    // one side is secret would duplicate the record without the secret
    // bit; flip to keep only the secret side.
    const bool sourceSecret = sourcePalm && sourcePalm->isSecret();
    const bool targetSecret = targetPalm && targetPalm->isSecret();
    if (baseDecision == ConflictDecision::UseBoth
        && conflict.type == ConflictType::BothModified
        && (sourceSecret != targetSecret)) {
        m_lastOverlay = QStringLiteral("secret");
        return sourceSecret ? ConflictDecision::UseSource
                            : ConflictDecision::UseTarget;
    }

    return baseDecision;
}

} // namespace WildPalms::PalmConflict
