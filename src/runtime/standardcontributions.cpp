#include "standardcontributions.h"

#include <backendregistry.h>
#include <caldavbackendcontribution.h>
#include <carddavbackendcontribution.h>
#ifdef HAVE_AKONADI
#include <akonadibackendcontribution.h>
#endif

#include <memory>

namespace WildPalms::Runtime {

void registerStandardContributions(Kalburator::Sync::BackendRegistry *registry)
{
    if (!registry) return;
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::CalDavBackendContribution>());
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::CardDavBackendContribution>());
#ifdef HAVE_AKONADI
    registry->registerContribution(
        std::make_shared<Kalburator::Sync::AkonadiBackendContribution>());
#endif
}

}  // namespace WildPalms::Runtime
