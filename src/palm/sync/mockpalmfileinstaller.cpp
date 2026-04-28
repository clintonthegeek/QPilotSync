#include "mockpalmfileinstaller.h"

namespace WildPalms::PalmSync {

bool MockPalmFileInstaller::installFile(const QString &path,
                                          QString       *errorMessage)
{
    m_paths.append(path);

    if (!m_queue.isEmpty()) {
        const auto next = m_queue.dequeue();
        if (errorMessage) *errorMessage = next.second;
        return next.first;
    }
    const bool ok = m_blanket.value_or(true);
    if (errorMessage) errorMessage->clear();
    return ok;
}

void MockPalmFileInstaller::setNextResult(bool success, const QString &errorMsg)
{
    m_queue.enqueue({success, errorMsg});
}

void MockPalmFileInstaller::setAllowAll(bool ok)
{
    m_blanket = ok;
}

} // namespace WildPalms::PalmSync
