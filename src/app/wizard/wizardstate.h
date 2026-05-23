#ifndef WILDPALMS_APP_WIZARD_WIZARDSTATE_H
#define WILDPALMS_APP_WIZARD_WIZARDSTATE_H

#include <backendconfiguration.h>

#include <QList>
#include <QString>

namespace WildPalms::Wizard {

enum class TargetKind {
    RawFiles,
    RemoteNew,        // user picked "Add new …" — credentials captured on Page 3
};

struct PendingAccount {
    QString id;       // wizard-local UUID; reused as the on-disk account id
    QString kind;     // "caldav" | "carddav" | "akonadi"
    Kalburator::Sync::BackendConfiguration config;
};

struct MappingSpec {
    QString    pluginId;     // calendar | contacts | memo | todo (matches plugin->pluginId())
    TargetKind kind = TargetKind::RawFiles;
    QString    accountRef;   // PendingAccount.id for RemoteNew; empty for RawFiles
    QString    collectionId; // resolved by DiscoveryPage; empty for RawFiles
};

struct WizardState {
    QString               profileName;
    QList<PendingAccount> pendingAccounts;
    QList<MappingSpec>    mappings;   // exactly four, keyed by pluginId in insertion order
};

}  // namespace WildPalms::Wizard

#endif
