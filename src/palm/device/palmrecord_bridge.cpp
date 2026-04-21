#include "palmrecord_bridge.h"

namespace WildPalms::PalmDevice {

WildPalms::PalmSync::PalmRecord fromPilotRecord(const PilotRecord &src)
{
    WildPalms::PalmSync::PalmRecord out;
    out.recordId   = static_cast<std::uint32_t>(src.recordId());
    out.category   = static_cast<std::uint8_t>(src.category() & 0x0F);
    out.attributes = static_cast<std::uint8_t>(src.attributes() & 0xFF);
    out.data       = src.data();
    // lastModified stays default-constructed; callers stamp as needed.
    return out;
}

PilotRecord toPilotRecord(const WildPalms::PalmSync::PalmRecord &src)
{
    return PilotRecord(
        static_cast<int>(src.recordId),
        static_cast<int>(src.category),
        static_cast<int>(src.attributes),
        src.data);
}

} // namespace WildPalms::PalmDevice
