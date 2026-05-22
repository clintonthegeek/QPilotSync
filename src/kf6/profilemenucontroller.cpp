#include "profilemenucontroller.h"
#include "../runtime/profileregistry.h"

#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <QAction>
#include <QIcon>
#include <QMenu>

using namespace WildPalms::Runtime;

ProfileMenuController::ProfileMenuController(ProfileRegistry *registry,
                                              KActionCollection *actionCollection,
                                              QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_actionCollection(actionCollection)
{
    m_switchMenu = new KActionMenu(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        i18n("&Switch Profile"), this);
    m_actionCollection->addAction(
        QStringLiteral("file_switch_profile"), m_switchMenu);

    m_forgetMenu = new KActionMenu(
        QIcon::fromTheme(QStringLiteral("edit-delete")),
        i18n("&Forget Profile"), this);
    m_actionCollection->addAction(
        QStringLiteral("file_forget_profile"), m_forgetMenu);

    connect(m_registry, &ProfileRegistry::registryChanged,
            this, &ProfileMenuController::rebuild);
    connect(m_registry, &ProfileRegistry::entryUpdated,
            this, [this](QString) { rebuild(); });

    rebuild();
}

ProfileMenuController::~ProfileMenuController() = default;

void ProfileMenuController::setActiveProfileId(const QString &id)
{
    if (m_activeId == id) return;
    m_activeId = id;
    rebuild();
}

void ProfileMenuController::rebuild()
{
    m_switchMenu->menu()->clear();
    m_forgetMenu->menu()->clear();

    const auto entries = m_registry->entries();
    const bool any = !entries.isEmpty();
    m_switchMenu->setEnabled(any);
    m_forgetMenu->setEnabled(any);
    if (!any) return;

    for (const auto &e : entries) {
        const bool isActive = (e.id == m_activeId);

        QAction *sw = new QAction(e.name, m_switchMenu);
        sw->setCheckable(true);
        sw->setChecked(isActive);
        sw->setEnabled(!isActive);
        sw->setData(e.id);
        connect(sw, &QAction::triggered, this,
                [this, id = e.id]() { emit switchRequested(id); });
        m_switchMenu->addAction(sw);

        QAction *fg = new QAction(e.name, m_forgetMenu);
        fg->setEnabled(!isActive);
        fg->setData(e.id);
        connect(fg, &QAction::triggered, this,
                [this, id = e.id]() { emit forgetRequested(id); });
        m_forgetMenu->addAction(fg);
    }
}
