#include "palmconflicthandler.h"

#include <QDateTime>

#include "ipalmdatabaseaccess.h"
#include "palmbackend.h"
#include "palmrecord.h"

namespace WildPalms::PalmConflict {

using Kalburator::Conflict::ConflictDecision;
using Kalburator::Conflict::ConflictPolicy;
using Kalburator::Conflict::ConflictRecord;
using Kalburator::Conflict::FallbackBehavior;
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
    const auto finalDecision = applyOverlays(conflict, policy, baseDecision);

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
    ConflictRecord &conflict, const ConflictPolicy &policy,
    ConflictDecision baseDecision)
{
    using Kalburator::Conflict::AutoResolveStrategy;
    using Kalburator::Conflict::ConflictType;

    const auto sourcePalm = lookupPalmRecord(conflict.source.id);
    const auto targetPalm = lookupPalmRecord(conflict.target.id);
    const bool sourceArchived = sourcePalm && sourcePalm->isArchived();
    const bool targetArchived = targetPalm && targetPalm->isArchived();

    // Archive safety.
    if (sourceArchived || targetArchived) {
        if (baseDecision == ConflictDecision::DeleteBoth) {
            m_lastOverlay = QStringLiteral("archive");
            return sourceArchived ? ConflictDecision::UseSource
                                  : ConflictDecision::UseTarget;
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

    // Secret protection.
    const bool sourceSecret = sourcePalm && sourcePalm->isSecret();
    const bool targetSecret = targetPalm && targetPalm->isSecret();
    if (baseDecision == ConflictDecision::UseBoth
        && conflict.type == ConflictType::BothModified
        && (sourceSecret != targetSecret)) {
        m_lastOverlay = QStringLiteral("secret");
        return sourceSecret ? ConflictDecision::UseSource
                            : ConflictDecision::UseTarget;
    }

    // Category tie-break.
    const bool timestampStrategy =
        policy.autoResolve == AutoResolveStrategy::NewerWins
        || policy.autoResolve == AutoResolveStrategy::OlderWins;
    if (timestampStrategy
        && conflict.type == ConflictType::BothModified
        && conflict.source.lastModified == conflict.target.lastModified
        && sourcePalm && targetPalm) {
        const bool sourceUnfiled = sourcePalm->category == 0;
        const bool targetUnfiled = targetPalm->category == 0;
        if (sourceUnfiled != targetUnfiled) {
            m_lastOverlay = QStringLiteral("category");
            return sourceUnfiled ? ConflictDecision::UseTarget
                                 : ConflictDecision::UseSource;
        }
    }

    return baseDecision;
}

} // namespace WildPalms::PalmConflict
