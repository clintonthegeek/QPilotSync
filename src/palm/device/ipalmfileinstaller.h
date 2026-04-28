#ifndef WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H
#define WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H

#include <QString>

namespace WildPalms::PalmSync {

/**
 * @brief Installs whole-database files (.prc / .pdb) onto a Palm.
 *
 * Sibling abstraction to IPalmDatabaseAccess. Kept distinct because
 * record-shaped operations and whole-DB-from-disk install live at
 * different layers; mixing them blurs the contract for the four
 * backends already implementing IPalmDatabaseAccess.
 */
class IPalmFileInstaller
{
public:
    virtual ~IPalmFileInstaller() = default;

    /// Install `path` onto the connected device. Returns true on
    /// success. On failure, populates `errorMessage` (when non-null)
    /// with a human-readable diagnostic.
    virtual bool installFile(const QString &path,
                              QString *errorMessage = nullptr) = 0;
};

} // namespace WildPalms::PalmSync

#endif // WILDPALMS_PALM_DEVICE_IPALMFILEINSTALLER_H
