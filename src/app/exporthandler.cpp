#include "exporthandler.h"
#include "logwidget.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../settings.h"

// Mappers have been migrated to conduit plugins. Export functionality
// in this legacy handler is disabled until re-implemented through the
// conduit plugin interface (IConduit::exportData or similar).
// See src/plugins/{memo,contacts,calendar,todos}/ for mapper sources.

#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QTextStream>

ExportHandler::ExportHandler(QWidget *parent)
    : QObject(parent)
    , m_parentWidget(parent)
{
}

void ExportHandler::exportMemos()
{
    if (m_logWidget) m_logWidget->logWarning("Export is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Memo export has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for export functionality.");
}

void ExportHandler::exportContacts()
{
    if (m_logWidget) m_logWidget->logWarning("Export is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Contact export has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for export functionality.");
}

void ExportHandler::exportCalendar()
{
    if (m_logWidget) m_logWidget->logWarning("Export is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Calendar export has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for export functionality.");
}

void ExportHandler::exportTodos()
{
    if (m_logWidget) m_logWidget->logWarning("Export is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Todo export has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for export functionality.");
}

void ExportHandler::exportAll()
{
    if (m_logWidget) m_logWidget->logWarning("Export is not available in the legacy UI. Use the KF6 main window.");
    QMessageBox::information(m_parentWidget, "Not Available",
        "Export All has been moved to the conduit plugin system.\n"
        "Please use the KF6 main window for export functionality.");
}

// Batch export helpers (disabled - mappers are now in conduit plugins)

void ExportHandler::exportMemosToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    Q_UNUSED(exportDir);
    exportedCount = 0;
    skippedCount = 0;
}

void ExportHandler::exportContactsToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    Q_UNUSED(exportDir);
    exportedCount = 0;
    skippedCount = 0;
}

void ExportHandler::exportCalendarToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    Q_UNUSED(exportDir);
    exportedCount = 0;
    skippedCount = 0;
}

void ExportHandler::exportTodosToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    Q_UNUSED(exportDir);
    exportedCount = 0;
    skippedCount = 0;
}
