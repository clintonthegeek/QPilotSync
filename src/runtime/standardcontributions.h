#ifndef WILDPALMS_RUNTIME_STANDARDCONTRIBUTIONS_H
#define WILDPALMS_RUNTIME_STANDARDCONTRIBUTIONS_H

namespace Kalburator::Sync { class BackendRegistry; }

namespace WildPalms::Runtime {

/// Register CalDAV, CardDAV, and (if compiled in) Akonadi backend
/// contributions into the given registry. Used by PalmRuntime's
/// ctor (per-profile registry) and by KF6MainWindow's ctor
/// (app-level registry used by the NewProfileWizard for discovery
/// before a profile exists).
void registerStandardContributions(Kalburator::Sync::BackendRegistry *registry);

}  // namespace WildPalms::Runtime

#endif
