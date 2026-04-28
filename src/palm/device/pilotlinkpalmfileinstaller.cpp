#include "pilotlinkpalmfileinstaller.h"

#include "palm/kpilotdevicelink.h"

extern "C" {
#include <pi-file.h>
}

namespace WildPalms::PalmSync {

PilotLinkPalmFileInstaller::PilotLinkPalmFileInstaller(KPilotLink *link)
    : m_link(link)
{
}

bool PilotLinkPalmFileInstaller::installFile(const QString &path,
                                                QString       *errorMessage)
{
    if (!m_link) {
        if (errorMessage) *errorMessage = QStringLiteral("no link");
        return false;
    }

    auto *deviceLink = dynamic_cast<KPilotDeviceLink *>(m_link);
    if (!deviceLink) {
        if (errorMessage) *errorMessage = QStringLiteral("link is not a real device");
        return false;
    }

    pi_file_t *pf = pi_file_open(path.toLocal8Bit().constData());
    if (!pf) {
        if (errorMessage) *errorMessage = QStringLiteral("pi_file_open failed for %1").arg(path);
        return false;
    }

    const int rc = pi_file_install(pf, deviceLink->socketDescriptor(), 0, nullptr);
    pi_file_close(pf);

    if (rc < 0) {
        if (errorMessage) *errorMessage = QStringLiteral("pi_file_install rc=%1").arg(rc);
        return false;
    }
    return true;
}

} // namespace WildPalms::PalmSync
