#include "ibackendplugin.h"

#include "conflictrecord.h"

#include <QString>

namespace WildPalms {

QString IBackendPlugin::formatConflictRecordHtml(
    const Kalburator::Conflict::RecordSnapshot &snapshot) const
{
    const QString body = QString::fromUtf8(snapshot.content).toHtmlEscaped();
    return QStringLiteral("<pre>%1</pre>").arg(body);
}

} // namespace WildPalms
