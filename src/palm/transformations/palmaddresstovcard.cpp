#include "palmaddresstovcard.h"

namespace WildPalms::Palm::Transformations {

QByteArray PalmAddressToVCard::transform(const QByteArray &sourceBytes) const
{
    // Stub: real Palm address → vCard decoding is a G.10 item.
    // Returns the raw bytes unchanged for now; BlobDomainAdapter hashes
    // the data so baseline comparison still works correctly.
    return sourceBytes;
}

} // namespace WildPalms::Palm::Transformations
