#include "conflictresolver_wp.h"

namespace WildPalms::Runtime {

Kalburator::Sync::ConflictResolution ConflictResolver_WP::resolveConflict(
    const Kalburator::Sync::ConflictInfo &conflict,
    QWidget *parentWidget)
{
    Q_UNUSED(conflict);
    Q_UNUSED(parentWidget);
    ++m_resolveCount;
    return Kalburator::Sync::ConflictResolution::SourceWins;
}

} // namespace WildPalms::Runtime
