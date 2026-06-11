#ifndef WILDPALMS_RUNTIME_CONDUITCATALOG_H
#define WILDPALMS_RUNTIME_CONDUITCATALOG_H

#include <memory>
#include <vector>

namespace WildPalms::Plugins { class PimPlugin; }

namespace WildPalms::Runtime {

/// Fresh instances of the stock conduit plugins. The single source of truth
/// for "which conduits exist": PalmRuntime::registerPalmPlugins() loads these
/// into its batch, and the wizard owns a transient set purely for descriptor
/// queries (matchesCollection, display names) — descriptor methods are const
/// and need no device or hub (substrate A1).
std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> createStockConduits();

} // namespace WildPalms::Runtime
#endif
