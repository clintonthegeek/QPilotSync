#ifndef WILDPALMS_APP_WIZARD_DOMAINFILTER_H
#define WILDPALMS_APP_WIZARD_DOMAINFILTER_H

#include <QString>

namespace Kalburator::Sync { struct CollectionInfo; }

namespace WildPalms::Wizard {

/// True when a discovered collection can serve as the sync target for the
/// given Palm conduit pluginId (calendar|contacts|memo|todo). Matches on
/// CollectionInfo::type with a contentTypes fallback (DAV servers report
/// VEVENT/VTODO/VCARD).
bool collectionMatchesDomain(const Kalburator::Sync::CollectionInfo &c,
                             const QString &pluginId);

}  // namespace WildPalms::Wizard

#endif
