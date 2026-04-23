#include "simpleactioncontext.h"

namespace WildPalms {

SimpleActionContext::SimpleActionContext(QObject *parent)
    : IPluginAction::ActionContext(parent)
{
}

void SimpleActionContext::setTotal(int total)
{
    m_total = total;
    emit progress(m_current, m_total);
}

void SimpleActionContext::setCurrent(int current)
{
    m_current = current;
    emit progress(m_current, m_total);
}

void SimpleActionContext::log(const QString &msg)
{
    emit message(msg);
}

bool SimpleActionContext::isCancelled() const { return m_cancelled; }

void SimpleActionContext::cancel() { m_cancelled = true; }

} // namespace WildPalms
