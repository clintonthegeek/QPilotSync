#include "categoryreconciler.h"
#include "palm/categoryinfo.h"

#include <cstring>

namespace WildPalms::Runtime {

CategoryReconcileResult reconcileCategories(const QByteArray &appInfoBlock,
                                            const QStringList &desiredNames)
{
    CategoryReconcileResult out;

    CategoryInfo ci;
    if (!ci.parse(reinterpret_cast<const unsigned char *>(appInfoBlock.constData()),
                  static_cast<size_t>(appInfoBlock.size())))
        return out;   // unparseable block: bind nothing, write nothing

    bool mutated = false;
    for (const QString &name : desiredNames) {
        if (name.isEmpty()) continue;
        int slot = ci.categoryIndex(name);          // case-insensitive
        if (slot < 0) {
            slot = ci.addCategory(name);            // claims first free 1..15
            if (slot < 0) {
                out.noFreeSlot.append(name);
                continue;
            }
            mutated = true;
        }
        out.bound.insert(name, slot);
    }

    if (mutated) {
        // pack() requires a packSize()-sized buffer but pack_CategoryAppInfo
        // only writes the (smaller) wire-format category region and returns
        // its length. Pack into a scratch buffer, then splice exactly those
        // bytes over the head of the original block — preserving any
        // app-specific tail beyond the category region.
        QByteArray packed(static_cast<int>(ci.packSize()), '\0');
        const int written =
            ci.pack(reinterpret_cast<unsigned char *>(packed.data()),
                    static_cast<size_t>(packed.size()));
        if (written > 0 && written <= appInfoBlock.size()) {
            QByteArray block = appInfoBlock;            // preserve app-specific tail
            std::memcpy(block.data(), packed.constData(),
                        static_cast<size_t>(written));
            out.updatedAppInfoBlock = block;
        }
        // If pack failed or the block is somehow shorter than the packed
        // region, refuse to write rather than corrupt: leave the block empty.
    }
    return out;
}

} // namespace WildPalms::Runtime
