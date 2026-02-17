#include "importhandler.h"
#include "logwidget.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../settings.h"

// Mappers have been migrated to conduit plugins. Import functionality
// in this legacy handler is disabled until re-implemented through the
// conduit plugin interface.
// See src/plugins/{memo,contacts,calendar,todos}/ for mapper sources.

#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>

ImportHandler::ImportHandler(QWidget *parent)
    : QObject(parent)
    , m_parentWidget(parent)
{
}

void ImportHandler::importMemo()
{
    if (m_logWidget) m_logWidget->logWarning("Import is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Memo import has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for import functionality.");
}

void ImportHandler::importContact()
{
    if (m_logWidget) m_logWidget->logWarning("Import is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Contact import has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for import functionality.");
}

void ImportHandler::importEvent()
{
    if (m_logWidget) m_logWidget->logWarning("Import is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Event import has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for import functionality.");
}

void ImportHandler::importTodo()
{
    if (m_logWidget) m_logWidget->logWarning("Import is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Todo import has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for import functionality.");
}
