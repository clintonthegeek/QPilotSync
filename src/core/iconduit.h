#pragma once

#include <QString>
#include <QIcon>
#include <QWidget>

class KXMLGUIClient;

namespace Sync {
class SyncContext;
struct SyncResult;
}

/**
 * @brief Minimal base interface that ALL conduits must implement
 *
 * This is a pure abstract C++ interface (not a QObject) so that plugin
 * classes can inherit it alongside QObject without diamond inheritance.
 *
 * Conduit types:
 *   - ISyncConduit  : extends IConduit with Palm sync operations
 *   - IToolConduit  : extends IConduit for standalone tools (Install, Plucker, etc.)
 *
 * All conduits share this common surface for identity, capability queries,
 * UI contribution, and configuration.
 */
class IConduit
{
public:
    virtual ~IConduit() = default;

    // ========== Identity ==========

    /** @brief Unique identifier for this conduit (e.g., "memos", "contacts") */
    virtual QString conduitId() const = 0;

    /** @brief Human-readable display name */
    virtual QString displayName() const = 0;

    /** @brief Icon for UI display */
    virtual QIcon icon() const = 0;

    /** @brief Brief description of what this conduit does */
    virtual QString description() const = 0;

    /** @brief Version string for this conduit */
    virtual QString version() const = 0;

    // ========== Capabilities ==========

    /** @brief Whether this conduit requires a Palm device connection */
    virtual bool requiresDevice() const = 0;

    // ========== Sync Entry Point ==========

    /** @brief Perform the sync/run operation */
    virtual Sync::SyncResult sync(Sync::SyncContext *context) = 0;

    /** @brief Check if conduit can sync in the current state */
    virtual bool canSync(const Sync::SyncContext *context) const = 0;

    /** @brief Check if conduit should run in this sync cycle */
    virtual bool shouldRun(const Sync::SyncContext *context) const = 0;

    // ========== UI Contribution ==========

    /** @brief Whether this conduit provides a browser/editor view */
    virtual bool hasView() const = 0;

    /** @brief Create the conduit's main view widget (caller owns) */
    virtual QWidget *createView(QWidget *parent) = 0;

    /** @brief Display name for the view tab/page */
    virtual QString viewName() const = 0;

    /** @brief Icon for the view tab/page */
    virtual QIcon viewIcon() const = 0;

    /** @brief Create a KXMLGUIClient for menu/toolbar contributions */
    virtual KXMLGUIClient *createGUIClient() { return nullptr; }

    // ========== Configuration ==========

    /** @brief Number of config pages this conduit provides */
    virtual int configPages() const { return 0; }

    /** @brief Create a config page widget (caller owns) */
    virtual QWidget *createConfigPage(int index, QWidget *parent) {
        Q_UNUSED(index) Q_UNUSED(parent) return nullptr;
    }

    /** @brief Load conduit settings from persistent storage */
    virtual void loadSettings() {}

    /** @brief Save conduit settings to persistent storage */
    virtual void saveSettings() {}
};

Q_DECLARE_INTERFACE(IConduit, "org.qpilotsync.IConduit/1.0")
