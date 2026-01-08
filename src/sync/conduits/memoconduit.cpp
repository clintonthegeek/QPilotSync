#include "memoconduit.h"
#include "../../mappers/memomapper.h"
#include "../../palm/pilotrecord.h"
#include "../../palm/categoryinfo.h"
#include "../../palm/kpilotdevicelink.h"
#include "../localfilebackend.h"

#include <QDebug>

namespace Sync {

MemoConduit::MemoConduit(QObject *parent)
    : Conduit(parent)
{
}

MemoConduit::~MemoConduit()
{
    delete m_categories;
}

void MemoConduit::loadCategories(SyncContext *context)
{
    if (m_categories) {
        delete m_categories;
        m_categories = nullptr;
    }
    m_originalAppInfo.clear();

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        return;
    }

    m_categories = new CategoryInfo();

    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);

    if (context->deviceLink->readAppBlock(m_dbHandle, appInfoBuf, &appInfoSize)) {
        // Store original AppInfo block for later write-back
        m_originalAppInfo = QByteArray(reinterpret_cast<const char*>(appInfoBuf), appInfoSize);

        m_categories->parse(appInfoBuf, appInfoSize);
        emit logMessage(QString("Loaded %1 categories").arg(m_categories->usedCategories().size()));
    }
}

QString MemoConduit::categoryName(int categoryIndex) const
{
    if (m_categories) {
        return m_categories->categoryName(categoryIndex);
    }
    return QString();
}

BackendRecord* MemoConduit::palmToBackend(PilotRecord *palmRecord,
                                           SyncContext *context)
{
    if (!palmRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Unpack Palm memo
    MemoMapper::Memo memo = MemoMapper::unpackMemo(palmRecord);

    // Convert to Markdown
    QString catName = categoryName(memo.category);
    QString markdown = MemoMapper::memoToMarkdown(memo, catName);

    // Create backend record
    BackendRecord *record = new BackendRecord();
    record->data = markdown.toUtf8();
    record->type = "memo";
    record->contentHash = LocalFileBackend::calculateHash(record->data);
    record->lastModified = QDateTime::currentDateTime();

    // Set display name from first line of memo
    QString text = memo.text.trimmed();
    int newlinePos = text.indexOf('\n');
    if (newlinePos > 0) {
        text = text.left(newlinePos).trimmed();
    }
    if (text.length() > 50) {
        text = text.left(50);
    }
    record->displayName = text;

    return record;
}

PilotRecord* MemoConduit::backendToPalm(BackendRecord *backendRecord,
                                         SyncContext *context)
{
    if (!backendRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Parse Markdown content
    QString content = QString::fromUtf8(backendRecord->data);
    MemoMapper::Memo memo = MemoMapper::markdownToMemo(content);

    // Look up or create category from name
    if (!memo.categoryName.isEmpty() && m_categories) {
        memo.category = m_categories->getOrCreateCategory(memo.categoryName);
        qDebug() << "[MemoConduit] Category" << memo.categoryName << "-> index" << memo.category;
    }

    // Pack to Palm record
    PilotRecord *record = MemoMapper::packMemo(memo);

    return record;
}

bool MemoConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend) const
{
    if (!palm || !backend) return false;

    // Unpack Palm memo
    MemoMapper::Memo palmMemo = MemoMapper::unpackMemo(palm);

    // Parse backend content
    QString backendContent = QString::fromUtf8(backend->data);
    MemoMapper::Memo backendMemo = MemoMapper::markdownToMemo(backendContent);

    // Compare text content (main comparison)
    QString palmText = palmMemo.text.trimmed();
    QString backendText = backendMemo.text.trimmed();

    return palmText == backendText;
}

QString MemoConduit::palmRecordDescription(PilotRecord *record) const
{
    if (!record) return QString();

    MemoMapper::Memo memo = MemoMapper::unpackMemo(record);

    // Use first line as description
    QString text = memo.text.trimmed();
    int newlinePos = text.indexOf('\n');
    if (newlinePos > 0) {
        text = text.left(newlinePos).trimmed();
    }

    // Limit length
    if (text.length() > 60) {
        text = text.left(57) + "...";
    }

    return text;
}

bool MemoConduit::writeModifiedCategories(SyncContext *context)
{
    // Check if we have categories that were modified
    if (!m_categories || !m_categories->isDirty()) {
        return true;  // Nothing to write
    }

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        emit logMessage("Warning: Cannot write categories - no device connection");
        return false;
    }

    emit logMessage("Writing modified categories back to Palm...");

    size_t catSize = m_categories->packSize();

    if (m_originalAppInfo.isEmpty()) {
        // No original - just write categories
        QByteArray buffer(catSize, 0);
        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()), buffer.size());
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), packed)) {
            emit logMessage("Warning: Failed to write categories to Palm");
            return false;
        }
    } else {
        // We have original AppInfo - update category portion and preserve the rest
        QByteArray buffer = m_originalAppInfo;

        // Pack categories into the beginning of the buffer
        int packed = m_categories->pack(reinterpret_cast<unsigned char*>(buffer.data()),
                                         qMin(static_cast<size_t>(buffer.size()), catSize));
        if (packed < 0) {
            emit logMessage("Warning: Failed to pack categories");
            return false;
        }

        if (!context->deviceLink->writeAppBlock(m_dbHandle,
                reinterpret_cast<const unsigned char*>(buffer.constData()), buffer.size())) {
            emit logMessage("Warning: Failed to write AppInfo block to Palm");
            return false;
        }
    }

    m_categories->clearDirty();
    emit logMessage("Categories updated on Palm");
    return true;
}

} // namespace Sync
