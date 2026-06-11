// src/app/patchbay/patchbaypage.h
#pragma once

#include <QWidget>

#include "patchbaymodel.h"

class Profile;
namespace WildPalms::Runtime {
class AccountController;
class PalmRuntime;
}

namespace WildPalms::AppPatchbay {

class SyncPatchbayView;
class PatchbayInspector;

/// Container gluing PatchbayModel ↔ Profile / AccountController /
/// PalmRuntime, with the graph view + inspector side panel. Persistence is
/// write-through (the patchbay is the living editor, spec §1): every model
/// mutation lands in Profile immediately and hot-reloads PalmRuntime when
/// no run is active.
class PatchbayPage : public QWidget {
    Q_OBJECT
public:
    PatchbayPage(Profile *profile,
                 WildPalms::Runtime::AccountController *accounts,
                 WildPalms::Runtime::PalmRuntime *palmRuntime,
                 QWidget *parent = nullptr);
    ~PatchbayPage() override;

    PatchbayModel *model() const { return m_model; }
    SyncPatchbayView *view() const { return m_view; }

private slots:
    void refreshInputs();    ///< re-gather Inputs from the three sources
    void onWireSelected(const QString &mappingId);
    void onAddCategoryRequested(const QString &domain, const QPointF &scenePos);

private:
    PatchbayModel::Inputs gatherInputs() const;

    Profile *m_profile;                                   // borrowed
    WildPalms::Runtime::AccountController *m_accounts;    // borrowed
    WildPalms::Runtime::PalmRuntime *m_runtime;           // borrowed
    PatchbayModel *m_model = nullptr;
    SyncPatchbayView *m_view = nullptr;
    PatchbayInspector *m_inspector = nullptr;
    bool m_deviceConnected = false;   ///< tracked from PalmRuntime signals
};

} // namespace WildPalms::AppPatchbay
