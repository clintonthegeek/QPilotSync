#ifndef WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H
#define WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H

#include "ipalmfileinstaller.h"

class KPilotLink;

namespace WildPalms::PalmSync {

/**
 * @brief pisock-backed IPalmFileInstaller.
 *
 * Borrows a non-owning KPilotLink* whose lifetime exceeds this
 * installer. installFile() opens the file via `pi_file_open`,
 * dispatches to `pi_file_install` against the link's socket,
 * and surfaces any non-zero return as an error.
 */
class PilotLinkPalmFileInstaller : public IPalmFileInstaller
{
public:
    explicit PilotLinkPalmFileInstaller(KPilotLink *link);
    ~PilotLinkPalmFileInstaller() override = default;

    bool installFile(const QString &path,
                      QString *errorMessage = nullptr) override;

private:
    KPilotLink *m_link = nullptr;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_DEVICE_PILOTLINKPALMFILEINSTALLER_H
