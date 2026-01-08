#include "exporthandler.h"
#include "logwidget.h"
#include "../palm/kpilotdevicelink.h"
#include "../palm/pilotrecord.h"
#include "../palm/categoryinfo.h"
#include "../mappers/memomapper.h"
#include "../mappers/contactmapper.h"
#include "../mappers/calendarmapper.h"
#include "../mappers/todomapper.h"
#include "../settings.h"

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
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString exportDir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select Memos Export Directory",
        Settings::instance().lastExportPath(),
        QFileDialog::ShowDirsOnly);

    if (exportDir.isEmpty()) {
        return;
    }

    Settings::instance().setLastExportPath(exportDir);

    if (m_logWidget) m_logWidget->logInfo(QString("Exporting memos to: %1").arg(exportDir));

    int dbHandle = m_deviceLink->openDatabase("MemoDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open MemoDB");
        QMessageBox::warning(m_parentWidget, "Error", "Failed to open MemoDB on Palm device");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
        if (m_logWidget) m_logWidget->logInfo("Loaded category names from database");
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

    if (records.isEmpty()) {
        m_deviceLink->closeDatabase(dbHandle);
        if (m_logWidget) m_logWidget->logWarning("No memo records found or device disconnected");
        QMessageBox::warning(m_parentWidget, "Warning",
            "No memo records found or device was disconnected during read");
        return;
    }

    if (m_logWidget) m_logWidget->logInfo(QString("Found %1 memo records").arg(records.size()));

    int exportedCount = 0;
    int skippedCount = 0;

    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        MemoMapper::Memo memo = MemoMapper::unpackMemo(record);

        if (memo.text.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(memo.category);
        QString markdown = MemoMapper::memoToMarkdown(memo, categoryName);
        QString filename = MemoMapper::generateFilename(memo);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 3) + QString("_%1.md").arg(suffix);
            suffix++;
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << markdown;
            file.close();
            exportedCount++;
            if (m_logWidget) m_logWidget->logInfo(QString("Exported: %1").arg(filename));
        } else {
            if (m_logWidget) m_logWidget->logError(QString("Failed to write: %1").arg(filename));
        }

        delete record;
    }

    m_deviceLink->closeDatabase(dbHandle);

    QString summary = QString("Export complete!\n\nExported: %1 memos\nSkipped: %2 (deleted or empty)")
        .arg(exportedCount).arg(skippedCount);

    if (m_logWidget) m_logWidget->logInfo(summary);
    QMessageBox::information(m_parentWidget, "Memo Export Complete", summary);

    emit exportComplete("memos", exportedCount, skippedCount);
}

void ExportHandler::exportContacts()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString exportDir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select Contacts Export Directory",
        Settings::instance().lastExportPath(),
        QFileDialog::ShowDirsOnly);

    if (exportDir.isEmpty()) {
        return;
    }

    Settings::instance().setLastExportPath(exportDir);

    if (m_logWidget) m_logWidget->logInfo(QString("Exporting contacts to: %1").arg(exportDir));

    int dbHandle = m_deviceLink->openDatabase("AddressDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open AddressDB");
        QMessageBox::warning(m_parentWidget, "Error", "Failed to open AddressDB on Palm device");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
        if (m_logWidget) m_logWidget->logInfo("Loaded category names from database");
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

    if (records.isEmpty()) {
        m_deviceLink->closeDatabase(dbHandle);
        if (m_logWidget) m_logWidget->logWarning("No contact records found or device disconnected");
        QMessageBox::warning(m_parentWidget, "Warning",
            "No contact records found or device was disconnected during read");
        return;
    }

    if (m_logWidget) m_logWidget->logInfo(QString("Found %1 contact records").arg(records.size()));

    int exportedCount = 0;
    int skippedCount = 0;

    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        ContactMapper::Contact contact = ContactMapper::unpackContact(record);

        if (contact.firstName.isEmpty() && contact.lastName.isEmpty() &&
            contact.company.isEmpty() && contact.phone1.isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(contact.category);
        QString vcard = ContactMapper::contactToVCard(contact, categoryName);
        QString filename = ContactMapper::generateFilename(contact);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.vcf").arg(suffix);
            suffix++;
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << vcard;
            file.close();
            exportedCount++;
            if (m_logWidget) m_logWidget->logInfo(QString("Exported: %1").arg(filename));
        } else {
            if (m_logWidget) m_logWidget->logError(QString("Failed to write: %1").arg(filename));
        }

        delete record;
    }

    m_deviceLink->closeDatabase(dbHandle);

    QString summary = QString("Export complete!\n\nExported: %1 contacts\nSkipped: %2 (deleted or empty)")
        .arg(exportedCount).arg(skippedCount);

    if (m_logWidget) m_logWidget->logInfo(summary);
    QMessageBox::information(m_parentWidget, "Contact Export Complete", summary);

    emit exportComplete("contacts", exportedCount, skippedCount);
}

void ExportHandler::exportCalendar()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString exportDir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select Calendar Export Directory",
        Settings::instance().lastExportPath(),
        QFileDialog::ShowDirsOnly);

    if (exportDir.isEmpty()) {
        return;
    }

    Settings::instance().setLastExportPath(exportDir);

    if (m_logWidget) m_logWidget->logInfo(QString("Exporting calendar to: %1").arg(exportDir));

    int dbHandle = m_deviceLink->openDatabase("DatebookDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open DatebookDB");
        QMessageBox::warning(m_parentWidget, "Error", "Failed to open DatebookDB on Palm device");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
        if (m_logWidget) m_logWidget->logInfo("Loaded category names from database");
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

    if (records.isEmpty()) {
        m_deviceLink->closeDatabase(dbHandle);
        if (m_logWidget) m_logWidget->logWarning("No calendar records found or device disconnected");
        QMessageBox::warning(m_parentWidget, "Warning",
            "No calendar records found or device was disconnected during read");
        return;
    }

    if (m_logWidget) m_logWidget->logInfo(QString("Found %1 calendar records").arg(records.size()));

    int exportedCount = 0;
    int skippedCount = 0;

    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        CalendarMapper::Event event = CalendarMapper::unpackEvent(record);

        if (event.description.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(event.category);
        QString ical = CalendarMapper::eventToICal(event, categoryName);
        QString filename = CalendarMapper::generateFilename(event);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix);
            suffix++;
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << ical;
            file.close();
            exportedCount++;
            if (m_logWidget) m_logWidget->logInfo(QString("Exported: %1").arg(filename));
        } else {
            if (m_logWidget) m_logWidget->logError(QString("Failed to write: %1").arg(filename));
        }

        delete record;
    }

    m_deviceLink->closeDatabase(dbHandle);

    QString summary = QString("Export complete!\n\nExported: %1 events\nSkipped: %2 (deleted or empty)")
        .arg(exportedCount).arg(skippedCount);

    if (m_logWidget) m_logWidget->logInfo(summary);
    QMessageBox::information(m_parentWidget, "Calendar Export Complete", summary);

    emit exportComplete("calendar", exportedCount, skippedCount);
}

void ExportHandler::exportTodos()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString exportDir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select ToDo Export Directory",
        Settings::instance().lastExportPath(),
        QFileDialog::ShowDirsOnly);

    if (exportDir.isEmpty()) {
        return;
    }

    Settings::instance().setLastExportPath(exportDir);

    if (m_logWidget) m_logWidget->logInfo(QString("Exporting todos to: %1").arg(exportDir));

    int dbHandle = m_deviceLink->openDatabase("ToDoDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logError("Failed to open ToDoDB");
        QMessageBox::warning(m_parentWidget, "Error", "Failed to open ToDoDB on Palm device");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
        if (m_logWidget) m_logWidget->logInfo("Loaded category names from database");
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);

    if (records.isEmpty()) {
        m_deviceLink->closeDatabase(dbHandle);
        if (m_logWidget) m_logWidget->logWarning("No todo records found or device disconnected");
        QMessageBox::warning(m_parentWidget, "Warning",
            "No todo records found or device was disconnected during read");
        return;
    }

    if (m_logWidget) m_logWidget->logInfo(QString("Found %1 todo records").arg(records.size()));

    int exportedCount = 0;
    int skippedCount = 0;

    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        TodoMapper::Todo todo = TodoMapper::unpackTodo(record);

        if (todo.description.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(todo.category);
        QString ical = TodoMapper::todoToICal(todo, categoryName);
        QString filename = TodoMapper::generateFilename(todo);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix);
            suffix++;
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << ical;
            file.close();
            exportedCount++;
            if (m_logWidget) m_logWidget->logInfo(QString("Exported: %1").arg(filename));
        } else {
            if (m_logWidget) m_logWidget->logError(QString("Failed to write: %1").arg(filename));
        }

        delete record;
    }

    m_deviceLink->closeDatabase(dbHandle);

    QString summary = QString("Export complete!\n\nExported: %1 todos\nSkipped: %2 (deleted or empty)")
        .arg(exportedCount).arg(skippedCount);

    if (m_logWidget) m_logWidget->logInfo(summary);
    QMessageBox::information(m_parentWidget, "ToDo Export Complete", summary);

    emit exportComplete("todos", exportedCount, skippedCount);
}

void ExportHandler::exportAll()
{
    if (!m_deviceLink) {
        if (m_logWidget) m_logWidget->logError("No device connected");
        return;
    }

    QString baseDir = QFileDialog::getExistingDirectory(m_parentWidget,
        "Select Export Base Directory",
        Settings::instance().lastExportPath(),
        QFileDialog::ShowDirsOnly);

    if (baseDir.isEmpty()) {
        return;
    }

    Settings::instance().setLastExportPath(baseDir);

    if (m_logWidget) m_logWidget->logInfo(QString("=== Exporting all data to: %1 ===").arg(baseDir));

    // Create subdirectories
    QDir dir(baseDir);
    dir.mkpath("memos");
    dir.mkpath("contacts");
    dir.mkpath("calendar");
    dir.mkpath("todos");

    int totalExported = 0;
    int totalSkipped = 0;

    // Export each type
    int exported, skipped;

    if (m_logWidget) m_logWidget->logInfo("--- Exporting Memos ---");
    exportMemosToDir(dir.filePath("memos"), exported, skipped);
    if (m_logWidget) m_logWidget->logInfo(QString("Memos: %1 exported, %2 skipped").arg(exported).arg(skipped));
    totalExported += exported;
    totalSkipped += skipped;

    if (m_logWidget) m_logWidget->logInfo("--- Exporting Contacts ---");
    exportContactsToDir(dir.filePath("contacts"), exported, skipped);
    if (m_logWidget) m_logWidget->logInfo(QString("Contacts: %1 exported, %2 skipped").arg(exported).arg(skipped));
    totalExported += exported;
    totalSkipped += skipped;

    if (m_logWidget) m_logWidget->logInfo("--- Exporting Calendar ---");
    exportCalendarToDir(dir.filePath("calendar"), exported, skipped);
    if (m_logWidget) m_logWidget->logInfo(QString("Calendar: %1 exported, %2 skipped").arg(exported).arg(skipped));
    totalExported += exported;
    totalSkipped += skipped;

    if (m_logWidget) m_logWidget->logInfo("--- Exporting Todos ---");
    exportTodosToDir(dir.filePath("todos"), exported, skipped);
    if (m_logWidget) m_logWidget->logInfo(QString("Todos: %1 exported, %2 skipped").arg(exported).arg(skipped));
    totalExported += exported;
    totalSkipped += skipped;

    QString summary = QString("Export Complete!\n\n"
                              "Total exported: %1 records\n"
                              "Total skipped: %2 records\n\n"
                              "Files saved to:\n%3")
        .arg(totalExported).arg(totalSkipped).arg(baseDir);

    if (m_logWidget) m_logWidget->logInfo(QString("=== Export complete: %1 exported, %2 skipped ===")
        .arg(totalExported).arg(totalSkipped));

    QMessageBox::information(m_parentWidget, "Export Complete", summary);

    emit exportComplete("all", totalExported, totalSkipped);
}

// Batch export helpers

void ExportHandler::exportMemosToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    exportedCount = 0;
    skippedCount = 0;

    if (!m_deviceLink) return;

    int dbHandle = m_deviceLink->openDatabase("MemoDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logWarning("Could not open MemoDB");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        MemoMapper::Memo memo = MemoMapper::unpackMemo(record);
        if (memo.text.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(memo.category);
        QString markdown = MemoMapper::memoToMarkdown(memo, categoryName);
        QString filename = MemoMapper::generateFilename(memo);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 3) + QString("_%1.md").arg(suffix++);
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << markdown;
            file.close();
            exportedCount++;
        }
        delete record;
    }
    m_deviceLink->closeDatabase(dbHandle);
}

void ExportHandler::exportContactsToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    exportedCount = 0;
    skippedCount = 0;

    if (!m_deviceLink) return;

    int dbHandle = m_deviceLink->openDatabase("AddressDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logWarning("Could not open AddressDB");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        ContactMapper::Contact contact = ContactMapper::unpackContact(record);
        if (contact.firstName.isEmpty() && contact.lastName.isEmpty() &&
            contact.company.isEmpty() && contact.phone1.isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(contact.category);
        QString vcard = ContactMapper::contactToVCard(contact, categoryName);
        QString filename = ContactMapper::generateFilename(contact);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.vcf").arg(suffix++);
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << vcard;
            file.close();
            exportedCount++;
        }
        delete record;
    }
    m_deviceLink->closeDatabase(dbHandle);
}

void ExportHandler::exportCalendarToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    exportedCount = 0;
    skippedCount = 0;

    if (!m_deviceLink) return;

    int dbHandle = m_deviceLink->openDatabase("DatebookDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logWarning("Could not open DatebookDB");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        CalendarMapper::Event event = CalendarMapper::unpackEvent(record);
        if (event.description.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(event.category);
        QString ical = CalendarMapper::eventToICal(event, categoryName);
        QString filename = CalendarMapper::generateFilename(event);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix++);
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << ical;
            file.close();
            exportedCount++;
        }
        delete record;
    }
    m_deviceLink->closeDatabase(dbHandle);
}

void ExportHandler::exportTodosToDir(const QString &exportDir, int &exportedCount, int &skippedCount)
{
    exportedCount = 0;
    skippedCount = 0;

    if (!m_deviceLink) return;

    int dbHandle = m_deviceLink->openDatabase("ToDoDB");
    if (dbHandle < 0) {
        if (m_logWidget) m_logWidget->logWarning("Could not open ToDoDB");
        return;
    }

    CategoryInfo categories;
    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);
    if (m_deviceLink->readAppBlock(dbHandle, appInfoBuf, &appInfoSize)) {
        categories.parse(appInfoBuf, appInfoSize);
    }

    QList<PilotRecord*> records = m_deviceLink->readAllRecords(dbHandle);
    for (PilotRecord *record : records) {
        if (record->isDeleted()) {
            skippedCount++;
            delete record;
            continue;
        }

        TodoMapper::Todo todo = TodoMapper::unpackTodo(record);
        if (todo.description.trimmed().isEmpty()) {
            skippedCount++;
            delete record;
            continue;
        }

        QString categoryName = categories.categoryName(todo.category);
        QString ical = TodoMapper::todoToICal(todo, categoryName);
        QString filename = TodoMapper::generateFilename(todo);
        QString filepath = QDir(exportDir).filePath(filename);

        int suffix = 1;
        QString basePath = filepath;
        while (QFile::exists(filepath)) {
            filepath = basePath.left(basePath.length() - 4) + QString("_%1.ics").arg(suffix++);
        }

        QFile file(filepath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << ical;
            file.close();
            exportedCount++;
        }
        delete record;
    }
    m_deviceLink->closeDatabase(dbHandle);
}
