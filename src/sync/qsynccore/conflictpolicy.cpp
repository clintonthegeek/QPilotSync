#include "conflictpolicy.h"
#include "conflictstore.h"

namespace QSyncCore {

// ========== ConflictPolicy ==========

bool ConflictPolicy::shouldAutoResolve(const ConflictRecord &conflict) const
{
    // Never auto-resolve if policy says None
    if (autoResolve == AutoResolveStrategy::None) {
        return false;
    }

    // Never auto-resolve deletions if safety option is on
    if (requireConfirmForDeletes &&
        (conflict.type == ConflictType::ModifiedVsDeleted ||
         conflict.type == ConflictType::DeletedVsModified)) {
        return false;
    }

    // Check prompt strategy
    if (promptStrategy == PromptStrategy::Always) {
        return false;
    }

    if (promptStrategy == PromptStrategy::WhenComplex &&
        conflict.complexity == ConflictComplexity::Complex) {
        return false;
    }

    if (promptStrategy == PromptStrategy::WhenDelete &&
        (conflict.type == ConflictType::ModifiedVsDeleted ||
         conflict.type == ConflictType::DeletedVsModified)) {
        return false;
    }

    return true;
}

ConflictDecision ConflictPolicy::getAutoDecision(const ConflictRecord &conflict) const
{
    switch (autoResolve) {
        case AutoResolveStrategy::None:
            return ConflictDecision::Pending;

        case AutoResolveStrategy::SourceAlwaysWins:
            if (conflict.type == ConflictType::DeletedVsModified) {
                return ConflictDecision::DeleteBoth;
            }
            return ConflictDecision::UseSource;

        case AutoResolveStrategy::TargetAlwaysWins:
            if (conflict.type == ConflictType::ModifiedVsDeleted) {
                return ConflictDecision::DeleteBoth;
            }
            return ConflictDecision::UseTarget;

        case AutoResolveStrategy::NewerWins:
            if (conflict.source.lastModified > conflict.target.lastModified) {
                return ConflictDecision::UseSource;
            } else {
                return ConflictDecision::UseTarget;
            }

        case AutoResolveStrategy::OlderWins:
            if (conflict.source.lastModified < conflict.target.lastModified) {
                return ConflictDecision::UseSource;
            } else {
                return ConflictDecision::UseTarget;
            }

        case AutoResolveStrategy::LargerWins:
            if (conflict.source.content.size() >= conflict.target.content.size()) {
                return ConflictDecision::UseSource;
            } else {
                return ConflictDecision::UseTarget;
            }

        case AutoResolveStrategy::DuplicateAll:
            return ConflictDecision::UseBoth;
    }

    return ConflictDecision::Pending;
}

bool ConflictPolicy::shouldPrompt(const ConflictRecord &conflict) const
{
    switch (promptStrategy) {
        case PromptStrategy::Never:
            return false;

        case PromptStrategy::Always:
            return true;

        case PromptStrategy::WhenComplex:
            return conflict.complexity == ConflictComplexity::Complex;

        case PromptStrategy::WhenDelete:
            return conflict.type == ConflictType::ModifiedVsDeleted ||
                   conflict.type == ConflictType::DeletedVsModified;

        case PromptStrategy::OnFirstConflict:
            // Caller needs to track this externally
            return true;
    }

    return true;
}

QJsonObject ConflictPolicy::toJson() const
{
    QJsonObject obj;
    obj["autoResolve"] = autoResolveStrategyToString(autoResolve);
    obj["promptStrategy"] = promptStrategyToString(promptStrategy);
    obj["promptTimeoutSeconds"] = promptTimeoutSeconds;
    obj["timeoutDecision"] = conflictDecisionToString(timeoutDecision);
    obj["fallback"] = fallbackBehaviorToString(fallback);
    obj["connectionBehavior"] = connectionBehaviorToString(connectionBehavior);
    obj["connectionTimeoutSeconds"] = connectionTimeoutSeconds;
    obj["allowBatchReview"] = allowBatchReview;
    obj["showPreviewBeforeSync"] = showPreviewBeforeSync;
    obj["maxAutoResolvePerSync"] = maxAutoResolvePerSync;
    obj["requireConfirmForDeletes"] = requireConfirmForDeletes;
    obj["logAllDecisions"] = logAllDecisions;
    return obj;
}

ConflictPolicy ConflictPolicy::fromJson(const QJsonObject &json)
{
    ConflictPolicy policy;
    policy.autoResolve = autoResolveStrategyFromString(json["autoResolve"].toString());
    policy.promptStrategy = promptStrategyFromString(json["promptStrategy"].toString());
    policy.promptTimeoutSeconds = json["promptTimeoutSeconds"].toInt(60);
    policy.timeoutDecision = conflictDecisionFromString(json["timeoutDecision"].toString());
    policy.fallback = fallbackBehaviorFromString(json["fallback"].toString());
    policy.connectionBehavior = connectionBehaviorFromString(json["connectionBehavior"].toString());
    policy.connectionTimeoutSeconds = json["connectionTimeoutSeconds"].toInt(300);
    policy.allowBatchReview = json["allowBatchReview"].toBool(true);
    policy.showPreviewBeforeSync = json["showPreviewBeforeSync"].toBool(false);
    policy.maxAutoResolvePerSync = json["maxAutoResolvePerSync"].toInt(100);
    policy.requireConfirmForDeletes = json["requireConfirmForDeletes"].toBool(true);
    policy.logAllDecisions = json["logAllDecisions"].toBool(true);
    return policy;
}

ConflictPolicy ConflictPolicy::autoSourceWins()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::SourceAlwaysWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;
    return policy;
}

ConflictPolicy ConflictPolicy::autoTargetWins()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::TargetAlwaysWins;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::UseDefault;
    policy.requireConfirmForDeletes = false;
    return policy;
}

ConflictPolicy ConflictPolicy::deferAll()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.promptStrategy = PromptStrategy::Never;
    policy.fallback = FallbackBehavior::Defer;
    policy.connectionBehavior = ConnectionBehavior::DisconnectAndDefer;
    return policy;
}

ConflictPolicy ConflictPolicy::interactive()
{
    ConflictPolicy policy;
    policy.autoResolve = AutoResolveStrategy::None;
    policy.promptStrategy = PromptStrategy::Always;
    policy.promptTimeoutSeconds = 60;
    policy.timeoutDecision = ConflictDecision::Skip;
    policy.fallback = FallbackBehavior::Defer;
    policy.connectionBehavior = ConnectionBehavior::TimeoutThenDefer;
    policy.connectionTimeoutSeconds = 300;
    policy.allowBatchReview = true;
    return policy;
}

// ========== AutomaticConflictHandler ==========

AutomaticConflictHandler::AutomaticConflictHandler(ConflictStore *store)
    : m_store(store)
{
}

ConflictDecision AutomaticConflictHandler::handleConflict(ConflictRecord &conflict,
                                                           const ConflictPolicy &policy)
{
    // Try auto-resolution first
    if (policy.shouldAutoResolve(conflict)) {
        ConflictDecision decision = policy.getAutoDecision(conflict);
        if (decision != ConflictDecision::Pending) {
            conflict.decision = decision;
            conflict.resolvedAt = QDateTime::currentDateTime();
            conflict.resolvedBy = QString("policy:%1")
                .arg(autoResolveStrategyToString(policy.autoResolve));
            return decision;
        }
    }

    // Can't prompt, so defer or use fallback
    switch (policy.fallback) {
        case FallbackBehavior::Defer:
            conflict.decision = ConflictDecision::Pending;
            m_pending.append(conflict);
            if (m_store) {
                m_store->addConflict(conflict);
            }
            return ConflictDecision::Pending;

        case FallbackBehavior::Skip:
            conflict.decision = ConflictDecision::Skip;
            conflict.resolvedAt = QDateTime::currentDateTime();
            conflict.resolvedBy = "fallback:skip";
            return ConflictDecision::Skip;

        case FallbackBehavior::UseDefault:
            {
                ConflictDecision decision = policy.getAutoDecision(conflict);
                if (decision == ConflictDecision::Pending) {
                    decision = ConflictDecision::Skip;
                }
                conflict.decision = decision;
                conflict.resolvedAt = QDateTime::currentDateTime();
                conflict.resolvedBy = "fallback:default";
                return decision;
            }

        case FallbackBehavior::Abort:
            // Return Skip but caller should check this and abort
            conflict.decision = ConflictDecision::Skip;
            return ConflictDecision::Skip;
    }

    return ConflictDecision::Pending;
}

// ========== String Conversion ==========

QString autoResolveStrategyToString(AutoResolveStrategy strategy)
{
    switch (strategy) {
        case AutoResolveStrategy::None:             return "None";
        case AutoResolveStrategy::SourceAlwaysWins: return "SourceAlwaysWins";
        case AutoResolveStrategy::TargetAlwaysWins: return "TargetAlwaysWins";
        case AutoResolveStrategy::NewerWins:        return "NewerWins";
        case AutoResolveStrategy::OlderWins:        return "OlderWins";
        case AutoResolveStrategy::LargerWins:       return "LargerWins";
        case AutoResolveStrategy::DuplicateAll:     return "DuplicateAll";
    }
    return "None";
}

AutoResolveStrategy autoResolveStrategyFromString(const QString &str)
{
    if (str == "SourceAlwaysWins") return AutoResolveStrategy::SourceAlwaysWins;
    if (str == "TargetAlwaysWins") return AutoResolveStrategy::TargetAlwaysWins;
    if (str == "NewerWins")        return AutoResolveStrategy::NewerWins;
    if (str == "OlderWins")        return AutoResolveStrategy::OlderWins;
    if (str == "LargerWins")       return AutoResolveStrategy::LargerWins;
    if (str == "DuplicateAll")     return AutoResolveStrategy::DuplicateAll;
    return AutoResolveStrategy::None;
}

QString promptStrategyToString(PromptStrategy strategy)
{
    switch (strategy) {
        case PromptStrategy::Never:           return "Never";
        case PromptStrategy::Always:          return "Always";
        case PromptStrategy::WhenComplex:     return "WhenComplex";
        case PromptStrategy::WhenDelete:      return "WhenDelete";
        case PromptStrategy::OnFirstConflict: return "OnFirstConflict";
    }
    return "Always";
}

PromptStrategy promptStrategyFromString(const QString &str)
{
    if (str == "Never")           return PromptStrategy::Never;
    if (str == "Always")          return PromptStrategy::Always;
    if (str == "WhenComplex")     return PromptStrategy::WhenComplex;
    if (str == "WhenDelete")      return PromptStrategy::WhenDelete;
    if (str == "OnFirstConflict") return PromptStrategy::OnFirstConflict;
    return PromptStrategy::Always;
}

QString fallbackBehaviorToString(FallbackBehavior behavior)
{
    switch (behavior) {
        case FallbackBehavior::Defer:      return "Defer";
        case FallbackBehavior::Skip:       return "Skip";
        case FallbackBehavior::UseDefault: return "UseDefault";
        case FallbackBehavior::Abort:      return "Abort";
    }
    return "Defer";
}

FallbackBehavior fallbackBehaviorFromString(const QString &str)
{
    if (str == "Defer")      return FallbackBehavior::Defer;
    if (str == "Skip")       return FallbackBehavior::Skip;
    if (str == "UseDefault") return FallbackBehavior::UseDefault;
    if (str == "Abort")      return FallbackBehavior::Abort;
    return FallbackBehavior::Defer;
}

QString connectionBehaviorToString(ConnectionBehavior behavior)
{
    switch (behavior) {
        case ConnectionBehavior::KeepAlive:          return "KeepAlive";
        case ConnectionBehavior::DisconnectAndDefer: return "DisconnectAndDefer";
        case ConnectionBehavior::TimeoutThenDefer:   return "TimeoutThenDefer";
    }
    return "TimeoutThenDefer";
}

ConnectionBehavior connectionBehaviorFromString(const QString &str)
{
    if (str == "KeepAlive")          return ConnectionBehavior::KeepAlive;
    if (str == "DisconnectAndDefer") return ConnectionBehavior::DisconnectAndDefer;
    if (str == "TimeoutThenDefer")   return ConnectionBehavior::TimeoutThenDefer;
    return ConnectionBehavior::TimeoutThenDefer;
}

} // namespace QSyncCore
