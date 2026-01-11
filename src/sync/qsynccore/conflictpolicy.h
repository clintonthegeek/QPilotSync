#ifndef QSYNCCORE_CONFLICTPOLICY_H
#define QSYNCCORE_CONFLICTPOLICY_H

/**
 * @file conflictpolicy.h
 * @brief Configuration and handling interface for conflict resolution
 *
 * Defines policies for how conflicts should be handled during sync,
 * and an abstract interface for conflict resolution handlers.
 *
 * The separation between Policy (configuration) and Handler (implementation)
 * allows the same policy to be applied across different UIs or automation.
 */

#include "conflictrecord.h"

#include <QObject>
#include <QJsonObject>

namespace QSyncCore {

// Forward declaration
class ConflictStore;

/**
 * @brief Automatic resolution strategy
 */
enum class AutoResolveStrategy
{
    None,               ///< Never auto-resolve, always defer or ask
    SourceAlwaysWins,   ///< Source overwrites target automatically
    TargetAlwaysWins,   ///< Target overwrites source automatically
    NewerWins,          ///< Most recently modified version wins
    OlderWins,          ///< Preserve the older version (conservative)
    LargerWins,         ///< Keep the version with more content
    DuplicateAll        ///< Always create duplicates (never lose data)
};

/**
 * @brief When to prompt user for conflict resolution
 */
enum class PromptStrategy
{
    Never,              ///< Never prompt, use auto-resolve or defer
    Always,             ///< Always prompt for each conflict
    WhenComplex,        ///< Prompt only for complex conflicts
    WhenDelete,         ///< Prompt only when deletion is involved
    OnFirstConflict     ///< Prompt on first, apply same to rest
};

/**
 * @brief What to do when prompt is needed but not possible
 *
 * E.g., running in batch mode, user not present, timeout
 */
enum class FallbackBehavior
{
    Defer,              ///< Save for later resolution
    Skip,               ///< Skip this record, don't sync
    UseDefault,         ///< Use the autoResolveStrategy
    Abort               ///< Abort the entire sync operation
};

/**
 * @brief Connection behavior during interactive resolution
 */
enum class ConnectionBehavior
{
    KeepAlive,          ///< Keep connection alive during prompts (tickle)
    DisconnectAndDefer, ///< Disconnect immediately, defer all conflicts
    TimeoutThenDefer    ///< Keep alive with timeout, defer if too slow
};

/**
 * @brief Complete conflict resolution policy
 */
struct ConflictPolicy
{
    // Automatic resolution
    AutoResolveStrategy autoResolve = AutoResolveStrategy::None;

    // User prompting
    PromptStrategy promptStrategy = PromptStrategy::Always;
    int promptTimeoutSeconds = 60;  ///< Timeout for user decision (0 = no timeout)
    ConflictDecision timeoutDecision = ConflictDecision::Skip;

    // Fallback when prompt not available
    FallbackBehavior fallback = FallbackBehavior::Defer;

    // Connection handling
    ConnectionBehavior connectionBehavior = ConnectionBehavior::TimeoutThenDefer;
    int connectionTimeoutSeconds = 300;  ///< Max time to keep connection alive for resolution

    // Batch review
    bool allowBatchReview = true;       ///< Allow "review all after sync" option
    bool showPreviewBeforeSync = false; ///< Show conflicts before applying any changes

    // Safety options
    int maxAutoResolvePerSync = 100;    ///< Safety limit on auto-resolutions (0 = unlimited)
    bool requireConfirmForDeletes = true; ///< Always prompt when delete is involved
    bool logAllDecisions = true;        ///< Log every resolution decision

    /**
     * @brief Should this conflict be auto-resolved based on policy?
     */
    bool shouldAutoResolve(const ConflictRecord &conflict) const;

    /**
     * @brief Get the auto-resolution decision for a conflict
     */
    ConflictDecision getAutoDecision(const ConflictRecord &conflict) const;

    /**
     * @brief Should user be prompted for this conflict?
     */
    bool shouldPrompt(const ConflictRecord &conflict) const;

    /**
     * @brief Serialize to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Load from JSON
     */
    static ConflictPolicy fromJson(const QJsonObject &json);

    /**
     * @brief Create a policy for fully automatic operation (source wins)
     */
    static ConflictPolicy autoSourceWins();

    /**
     * @brief Create a policy for fully automatic operation (target wins)
     */
    static ConflictPolicy autoTargetWins();

    /**
     * @brief Create a policy that defers all conflicts
     */
    static ConflictPolicy deferAll();

    /**
     * @brief Create the default interactive policy
     */
    static ConflictPolicy interactive();
};

/**
 * @brief Abstract interface for handling conflicts during sync
 *
 * Implement this interface to provide custom conflict resolution behavior.
 * The sync engine calls this handler when conflicts are detected.
 */
class ConflictHandler
{
public:
    virtual ~ConflictHandler() = default;

    /**
     * @brief Handle a detected conflict
     *
     * Called by the sync engine when a conflict is detected.
     * Implementation should:
     *   1. Check policy for auto-resolution
     *   2. If prompt needed, show UI or defer
     *   3. Return the decision
     *
     * @param conflict The conflict to resolve
     * @param policy The resolution policy to apply
     * @return The decision made (may be Pending if deferred)
     */
    virtual ConflictDecision handleConflict(ConflictRecord &conflict,
                                             const ConflictPolicy &policy) = 0;

    /**
     * @brief Called when sync starts
     *
     * Opportunity to prepare UI, reset counters, etc.
     */
    virtual void onSyncStart() {}

    /**
     * @brief Called when sync ends
     *
     * @param hadConflicts Whether any conflicts were detected
     * @param allResolved Whether all conflicts were resolved
     */
    virtual void onSyncEnd(bool hadConflicts, bool allResolved) { Q_UNUSED(hadConflicts); Q_UNUSED(allResolved); }

    /**
     * @brief Check if handler supports interactive prompts
     *
     * Return false for batch/headless operation.
     */
    virtual bool canPrompt() const { return false; }

    /**
     * @brief Check if connection should be kept alive
     *
     * Called periodically during interactive resolution.
     */
    virtual bool shouldKeepConnectionAlive() const { return true; }

    /**
     * @brief Get the accumulated pending conflicts
     *
     * Returns conflicts that were deferred during this session.
     */
    virtual QList<ConflictRecord> pendingConflicts() const { return {}; }
};

/**
 * @brief Simple handler that applies policy automatically (no UI)
 *
 * Use this for batch operation or testing.
 */
class AutomaticConflictHandler : public ConflictHandler
{
public:
    explicit AutomaticConflictHandler(ConflictStore *store = nullptr);

    ConflictDecision handleConflict(ConflictRecord &conflict,
                                     const ConflictPolicy &policy) override;

    bool canPrompt() const override { return false; }

    QList<ConflictRecord> pendingConflicts() const override { return m_pending; }

private:
    ConflictStore *m_store;
    QList<ConflictRecord> m_pending;
};

// String conversion helpers
QString autoResolveStrategyToString(AutoResolveStrategy strategy);
AutoResolveStrategy autoResolveStrategyFromString(const QString &str);

QString promptStrategyToString(PromptStrategy strategy);
PromptStrategy promptStrategyFromString(const QString &str);

QString fallbackBehaviorToString(FallbackBehavior behavior);
FallbackBehavior fallbackBehaviorFromString(const QString &str);

QString connectionBehaviorToString(ConnectionBehavior behavior);
ConnectionBehavior connectionBehaviorFromString(const QString &str);

} // namespace QSyncCore

#endif // QSYNCCORE_CONFLICTPOLICY_H
