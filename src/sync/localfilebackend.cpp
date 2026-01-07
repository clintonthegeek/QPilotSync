#include "localfilebackend.h"

#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QDebug>

namespace Sync {

const QStringList LocalFileBackend::s_defaultCollections = {
    "memos", "contacts", "calendar", "todos"
};

LocalFileBackend::LocalFileBackend(const QString &basePath, QObject *parent)
    : SyncBackend(parent)
    , m_basePath(basePath)
{
    // Set default extensions
    m_extensions["memos"] = ".md";
    m_extensions["contacts"] = ".vcf";
    m_extensions["calendar"] = ".ics";
    m_extensions["todos"] = ".ics";
}

bool LocalFileBackend::isAvailable() const
{
    QDir dir(m_basePath);
    return dir.exists() || dir.mkpath(".");
}

// ========== Collection Management ==========

QList<CollectionInfo> LocalFileBackend::availableCollections()
{
    QList<CollectionInfo> collections;

    for (const QString &id : s_defaultCollections) {
        CollectionInfo info;
        info.id = id;
        info.name = id.left(1).toUpper() + id.mid(1);  // Capitalize
        info.path = collectionPath(id);
        info.type = id;
        info.isDefault = true;
        collections.append(info);
    }

    return collections;
}

CollectionInfo LocalFileBackend::collectionInfo(const QString &collectionId)
{
    CollectionInfo info;
    info.id = collectionId;
    info.name = collectionId.left(1).toUpper() + collectionId.mid(1);
    info.path = collectionPath(collectionId);
    info.type = collectionId;
    info.isDefault = s_defaultCollections.contains(collectionId);
    return info;
}

QString LocalFileBackend::createCollection(const CollectionInfo &info)
{
    QString path = collectionPath(info.id);
    QDir dir(path);

    if (dir.exists()) {
        return info.id;  // Already exists
    }

    if (dir.mkpath(".")) {
        qDebug() << "[LocalFileBackend] Created collection directory:" << path;
        return info.id;
    }

    emit errorOccurred(QString("Failed to create collection: %1").arg(path));
    return QString();
}

// ========== Record Operations ==========

QList<BackendRecord*> LocalFileBackend::loadRecords(const QString &collectionId)
{
    QList<BackendRecord*> records;

    QString path = collectionPath(collectionId);
    QDir dir(path);

    if (!dir.exists()) {
        // Create collection directory if it doesn't exist
        dir.mkpath(".");
        return records;  // Empty list
    }

    QString ext = fileExtension(collectionId);
    QStringList filters;
    filters << "*" + ext;

    QDirIterator it(path, filters, QDir::Files);
    while (it.hasNext()) {
        QString filePath = it.next();

        BackendRecord *record = loadRecord(filePath);
        if (record) {
            records.append(record);
        }
    }

    qDebug() << "[LocalFileBackend] Loaded" << records.size()
             << "records from" << collectionId;
    return records;
}

BackendRecord* LocalFileBackend::loadRecord(const QString &recordId)
{
    QFile file(recordId);
    if (!file.exists()) {
        return nullptr;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QString("Failed to open file: %1").arg(recordId));
        return nullptr;
    }

    QByteArray data = file.readAll();
    file.close();

    QFileInfo info(recordId);

    BackendRecord *record = new BackendRecord();
    record->id = recordId;
    record->data = data;
    record->contentHash = calculateHash(data);
    record->lastModified = info.lastModified();
    record->isDeleted = false;

    // Determine type from extension
    QString ext = info.suffix().toLower();
    if (ext == "md") {
        record->type = "memo";
    } else if (ext == "vcf") {
        record->type = "contact";
    } else if (ext == "ics") {
        // Could be event or todo - would need to parse to determine
        // For now, use the parent directory name
        QString parentDir = info.dir().dirName();
        if (parentDir == "calendar") {
            record->type = "event";
        } else if (parentDir == "todos") {
            record->type = "todo";
        } else {
            record->type = "icalendar";
        }
    }

    return record;
}

QString LocalFileBackend::createRecord(const QString &collectionId,
                                        const BackendRecord &record)
{
    // Ensure collection exists
    QString colPath = collectionPath(collectionId);
    QDir dir(colPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Generate filename from record description or use hash
    QString baseName = record.description();
    if (baseName.isEmpty()) {
        baseName = calculateHash(record.data).left(12);
    }

    QString ext = fileExtension(collectionId);
    QString filename = generateUniqueFilename(collectionId, baseName, ext);
    QString filePath = recordPath(collectionId, filename);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("Failed to create file: %1").arg(filePath));
        return QString();
    }

    file.write(record.data);
    file.close();

    emit recordCreated(filePath);
    return filePath;
}

bool LocalFileBackend::updateRecord(const BackendRecord &record)
{
    if (record.id.isEmpty()) {
        emit errorOccurred("Cannot update record with empty ID");
        return false;
    }

    QFile file(record.id);
    if (!file.exists()) {
        emit errorOccurred(QString("Record not found: %1").arg(record.id));
        return false;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(QString("Failed to open file for writing: %1").arg(record.id));
        return false;
    }

    file.write(record.data);
    file.close();

    emit recordUpdated(record.id);
    return true;
}

bool LocalFileBackend::deleteRecord(const QString &recordId)
{
    if (recordId.isEmpty()) {
        return false;
    }

    QFile file(recordId);
    if (!file.exists()) {
        return true;  // Already gone
    }

    if (file.remove()) {
        emit recordDeleted(recordId);
        return true;
    }

    emit errorOccurred(QString("Failed to delete file: %1").arg(recordId));
    return false;
}

// ========== Change Detection ==========

QList<BackendRecord*> LocalFileBackend::modifiedSince(const QString &collectionId,
                                                       const QDateTime &since)
{
    QList<BackendRecord*> modified;

    QString path = collectionPath(collectionId);
    QString ext = fileExtension(collectionId);
    QStringList filters;
    filters << "*" + ext;

    QDirIterator it(path, filters, QDir::Files);
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo info(filePath);

        if (info.lastModified() > since) {
            BackendRecord *record = loadRecord(filePath);
            if (record) {
                modified.append(record);
            }
        }
    }

    return modified;
}

QStringList LocalFileBackend::deletedSince(const QString &collectionId,
                                            const QDateTime &since)
{
    Q_UNUSED(collectionId)
    Q_UNUSED(since)

    // File-based backends can't track deletions directly
    // This would require maintaining a deletion log or using SyncState
    // For now, return empty - deletion detection happens via SyncState
    return QStringList();
}

// ========== Configuration ==========

void LocalFileBackend::setFileExtension(const QString &collectionId,
                                         const QString &extension)
{
    m_extensions[collectionId] = extension.startsWith('.') ? extension : '.' + extension;
}

QString LocalFileBackend::fileExtension(const QString &collectionId) const
{
    return m_extensions.value(collectionId, ".txt");
}

QString LocalFileBackend::calculateHash(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(16)
    );
}

// ========== Private Helpers ==========

QString LocalFileBackend::collectionPath(const QString &collectionId) const
{
    return QDir(m_basePath).filePath(collectionId);
}

QString LocalFileBackend::recordPath(const QString &collectionId,
                                      const QString &filename) const
{
    return QDir(collectionPath(collectionId)).filePath(filename);
}

QString LocalFileBackend::sanitizeFilename(const QString &name) const
{
    QString safe = name;

    // Replace problematic characters
    safe.replace('/', '_');
    safe.replace('\\', '_');
    safe.replace(':', '_');
    safe.replace('*', '_');
    safe.replace('?', '_');
    safe.replace('"', '_');
    safe.replace('<', '_');
    safe.replace('>', '_');
    safe.replace('|', '_');
    safe.replace('\n', ' ');
    safe.replace('\r', ' ');

    // Collapse multiple spaces/underscores
    while (safe.contains("  ")) {
        safe.replace("  ", " ");
    }
    while (safe.contains("__")) {
        safe.replace("__", "_");
    }

    // Trim and limit length
    safe = safe.trimmed();
    if (safe.length() > 100) {
        safe = safe.left(100);
    }

    // Ensure not empty
    if (safe.isEmpty()) {
        safe = "unnamed";
    }

    return safe;
}

QString LocalFileBackend::generateUniqueFilename(const QString &collectionId,
                                                  const QString &baseName,
                                                  const QString &extension) const
{
    QString safeName = sanitizeFilename(baseName);
    QString filename = safeName + extension;
    QString fullPath = recordPath(collectionId, filename);

    // If file doesn't exist, use this name
    if (!QFile::exists(fullPath)) {
        return filename;
    }

    // Otherwise, add numeric suffix
    int suffix = 1;
    while (QFile::exists(fullPath)) {
        filename = QString("%1_%2%3").arg(safeName).arg(suffix).arg(extension);
        fullPath = recordPath(collectionId, filename);
        suffix++;

        if (suffix > 10000) {
            // Failsafe - use hash
            filename = calculateHash(baseName.toUtf8()).left(12) + extension;
            break;
        }
    }

    return filename;
}

} // namespace Sync
