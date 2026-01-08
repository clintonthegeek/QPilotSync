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

void MemoConduit::loadCategories(SyncContext *context)
{
    if (m_categories) {
        delete m_categories;
        m_categories = nullptr;
    }

    if (!context || !context->deviceLink || m_dbHandle < 0) {
        return;
    }

    m_categories = new CategoryInfo();

    unsigned char appInfoBuf[4096];
    size_t appInfoSize = sizeof(appInfoBuf);

    if (context->deviceLink->readAppBlock(m_dbHandle, appInfoBuf, &appInfoSize)) {
        m_categories->parse(appInfoBuf, appInfoSize);
        emit logMessage("Loaded memo categories");
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

    // Look up category index from name if available
    if (!memo.categoryName.isEmpty() && m_categories) {
        memo.category = m_categories->categoryIndex(memo.categoryName);
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

} // namespace Sync
