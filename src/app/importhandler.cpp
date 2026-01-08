#include "importhandler.h"
#include "logwidget.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../mappers/memomapper.h"
#include "../mappers/contactmapper.h"
#include "../mappers/calendarmapper.h"
#include "../mappers/todomapper.h"
#include "../settings.h"

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
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(m_parentWidget,
        "Select Markdown Memo File",
        Settings::instance().lastExportPath(),
        "Markdown Files (*.md);;All Files (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (m_logWidget) m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (m_logWidget) m_logWidget->logInfo(QString("Importing memo from: %1").arg(filePath));

    // Parse markdown to memo
    MemoMapper::Memo memo = MemoMapper::markdownToMemo(content);
    memo.recordId = 0;  // Create new record (ID 0 = new)

    // Pack to Palm record
    PilotRecord *record = MemoMapper::packMemo(memo);
    if (!record) {
        if (m_logWidget) m_logWidget->logError("Failed to pack memo");
        return;
    }

    // Open database for write
    int dbHandle = m_deviceLink->openDatabase("MemoDB", true);  // read-write
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open MemoDB for writing");
        delete record;
        return;
    }

    // Write record
    if (m_deviceLink->writeRecord(dbHandle, record)) {
        if (m_logWidget) m_logWidget->logInfo(QString("Memo imported successfully! New ID: %1").arg(record->id()));
        QMessageBox::information(m_parentWidget, "Import Complete",
            QString("Memo imported successfully!\nNew record ID: %1").arg(record->id()));
        emit importComplete("memo", record->id());
    } else {
        if (m_logWidget) m_logWidget->logError("Failed to write memo record");
        QMessageBox::warning(m_parentWidget, "Import Failed", "Failed to write memo to Palm device.");
        emit importError("Failed to write memo record");
    }

    m_deviceLink->closeDatabase(dbHandle);
    delete record;
}

void ImportHandler::importContact()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(m_parentWidget,
        "Select vCard File",
        Settings::instance().lastExportPath(),
        "vCard Files (*.vcf);;All Files (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (m_logWidget) m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (m_logWidget) m_logWidget->logInfo(QString("Importing contact from: %1").arg(filePath));

    // Parse vCard to contact
    ContactMapper::Contact contact = ContactMapper::vCardToContact(content);
    contact.recordId = 0;  // Create new record

    // Pack to Palm record
    PilotRecord *record = ContactMapper::packContact(contact);
    if (!record) {
        if (m_logWidget) m_logWidget->logError("Failed to pack contact");
        return;
    }

    // Open database for write
    int dbHandle = m_deviceLink->openDatabase("AddressDB", true);
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open AddressDB for writing");
        delete record;
        return;
    }

    // Write record
    if (m_deviceLink->writeRecord(dbHandle, record)) {
        if (m_logWidget) m_logWidget->logInfo(QString("Contact imported successfully! New ID: %1").arg(record->id()));
        QMessageBox::information(m_parentWidget, "Import Complete",
            QString("Contact imported successfully!\nNew record ID: %1").arg(record->id()));
        emit importComplete("contact", record->id());
    } else {
        if (m_logWidget) m_logWidget->logError("Failed to write contact record");
        QMessageBox::warning(m_parentWidget, "Import Failed", "Failed to write contact to Palm device.");
        emit importError("Failed to write contact record");
    }

    m_deviceLink->closeDatabase(dbHandle);
    delete record;
}

void ImportHandler::importEvent()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(m_parentWidget,
        "Select iCalendar Event File",
        Settings::instance().lastExportPath(),
        "iCalendar Files (*.ics);;All Files (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (m_logWidget) m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (m_logWidget) m_logWidget->logInfo(QString("Importing event from: %1").arg(filePath));

    // Parse iCal to event
    CalendarMapper::Event event = CalendarMapper::iCalToEvent(content);
    event.recordId = 0;  // Create new record

    // Pack to Palm record
    PilotRecord *record = CalendarMapper::packEvent(event);
    if (!record) {
        if (m_logWidget) m_logWidget->logError("Failed to pack event");
        return;
    }

    // Open database for write
    int dbHandle = m_deviceLink->openDatabase("DatebookDB", true);
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open DatebookDB for writing");
        delete record;
        return;
    }

    // Write record
    if (m_deviceLink->writeRecord(dbHandle, record)) {
        if (m_logWidget) m_logWidget->logInfo(QString("Event imported successfully! New ID: %1").arg(record->id()));
        QMessageBox::information(m_parentWidget, "Import Complete",
            QString("Event imported successfully!\nNew record ID: %1").arg(record->id()));
        emit importComplete("event", record->id());
    } else {
        if (m_logWidget) m_logWidget->logError("Failed to write event record");
        QMessageBox::warning(m_parentWidget, "Import Failed", "Failed to write event to Palm device.");
        emit importError("Failed to write event record");
    }

    m_deviceLink->closeDatabase(dbHandle);
    delete record;
}

void ImportHandler::importTodo()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString filePath = QFileDialog::getOpenFileName(m_parentWidget,
        "Select iCalendar ToDo File",
        Settings::instance().lastExportPath(),
        "iCalendar Files (*.ics);;All Files (*)");

    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (m_logWidget) m_logWidget->logError(QString("Failed to open file: %1").arg(filePath));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (m_logWidget) m_logWidget->logInfo(QString("Importing todo from: %1").arg(filePath));

    // Parse iCal to todo
    TodoMapper::Todo todo = TodoMapper::iCalToTodo(content);
    todo.recordId = 0;  // Create new record

    // Pack to Palm record
    PilotRecord *record = TodoMapper::packTodo(todo);
    if (!record) {
        if (m_logWidget) m_logWidget->logError("Failed to pack todo");
        return;
    }

    // Open database for write
    int dbHandle = m_deviceLink->openDatabase("ToDoDB", true);
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open ToDoDB for writing");
        delete record;
        return;
    }

    // Write record
    if (m_deviceLink->writeRecord(dbHandle, record)) {
        if (m_logWidget) m_logWidget->logInfo(QString("ToDo imported successfully! New ID: %1").arg(record->id()));
        QMessageBox::information(m_parentWidget, "Import Complete",
            QString("ToDo imported successfully!\nNew record ID: %1").arg(record->id()));
        emit importComplete("todo", record->id());
    } else {
        if (m_logWidget) m_logWidget->logError("Failed to write todo record");
        QMessageBox::warning(m_parentWidget, "Import Failed", "Failed to write ToDo to Palm device.");
        emit importError("Failed to write todo record");
    }

    m_deviceLink->closeDatabase(dbHandle);
    delete record;
}
