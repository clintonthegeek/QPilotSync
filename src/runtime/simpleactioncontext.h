#ifndef WILDPALMS_SIMPLEACTIONCONTEXT_H
#define WILDPALMS_SIMPLEACTIONCONTEXT_H

#include "core/ipluginaction.h"

#include <atomic>

namespace WildPalms {

/**
 * @brief Minimal concrete ActionContext: emits signals, stores totals,
 *        supports cancellation via a flag toggled from any thread.
 *
 * Suitable for tests and for CLI-style action runs. UI callers may
 * prefer a richer subclass that routes log() into a log widget.
 */
class SimpleActionContext : public IPluginAction::ActionContext
{
    Q_OBJECT
public:
    explicit SimpleActionContext(QObject *parent = nullptr);

    void setTotal(int total) override;
    void setCurrent(int current) override;
    void log(const QString &msg) override;
    bool isCancelled() const override;

    void cancel();
    int  total() const   { return m_total; }
    int  current() const { return m_current; }

private:
    int               m_total     = 0;
    int               m_current   = 0;
    std::atomic<bool> m_cancelled{false};
};

} // namespace WildPalms

#endif // WILDPALMS_SIMPLEACTIONCONTEXT_H
