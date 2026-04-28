#ifndef WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H
#define WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H

#include <QQueue>
#include <QPair>
#include <QString>
#include <QStringList>
#include <optional>

#include "palm/device/ipalmfileinstaller.h"

namespace WildPalms::PalmSync {

/**
 * @brief In-memory recording mock for IPalmFileInstaller.
 *
 * Default behaviour: every installFile() returns true and records the
 * path. Tests override per-call via setNextResult() (one-shot queue)
 * or setAllowAll(bool) (sticky blanket).
 */
class MockPalmFileInstaller : public IPalmFileInstaller
{
public:
    MockPalmFileInstaller() = default;

    bool installFile(const QString &path,
                      QString *errorMessage = nullptr) override;

    QStringList installedPaths() const { return m_paths; }
    void        clear()                { m_paths.clear(); }

    /// Push a one-shot result to the front of the response queue.
    /// Subsequent calls (after the queue empties) fall back to
    /// `setAllowAll`'s value, or to default-true if neither was set.
    void setNextResult(bool success, const QString &errorMsg = {});

    /// Sticky: every call returns `ok` (and an empty error). Overrides
    /// queued results once they're consumed. Call again to flip.
    void setAllowAll(bool ok);

private:
    QStringList                       m_paths;
    QQueue<QPair<bool, QString>>      m_queue;
    std::optional<bool>               m_blanket;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_SYNC_MOCKPALMFILEINSTALLER_H
