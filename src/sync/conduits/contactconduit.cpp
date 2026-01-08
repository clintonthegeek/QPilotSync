#include "contactconduit.h"
#include "../../mappers/contactmapper.h"
#include "../../palm/pilotrecord.h"
#include "../../palm/categoryinfo.h"
#include "../../palm/kpilotdevicelink.h"
#include "../localfilebackend.h"

#include <QDebug>

namespace Sync {

ContactConduit::ContactConduit(QObject *parent)
    : Conduit(parent)
{
}

ContactConduit::~ContactConduit()
{
    delete m_categories;
}

void ContactConduit::loadCategories(SyncContext *context)
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

QString ContactConduit::categoryName(int categoryIndex) const
{
    if (m_categories) {
        return m_categories->categoryName(categoryIndex);
    }
    return QString();
}

BackendRecord* ContactConduit::palmToBackend(PilotRecord *palmRecord,
                                              SyncContext *context)
{
    if (!palmRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Unpack Palm contact
    ContactMapper::Contact contact = ContactMapper::unpackContact(palmRecord);

    // Convert to vCard
    QString catName = categoryName(contact.category);
    QString vcard = ContactMapper::contactToVCard(contact, catName);

    // Create backend record
    BackendRecord *record = new BackendRecord();
    record->data = vcard.toUtf8();
    record->type = "contact";
    record->contentHash = LocalFileBackend::calculateHash(record->data);
    record->lastModified = QDateTime::currentDateTime();

    // Set display name from contact name
    QStringList parts;
    if (!contact.firstName.isEmpty()) parts << contact.firstName;
    if (!contact.lastName.isEmpty()) parts << contact.lastName;
    QString name = parts.join(" ");
    if (name.isEmpty() && !contact.company.isEmpty()) {
        name = contact.company;
    }
    if (name.isEmpty()) {
        name = contact.phone1;
    }
    record->displayName = name;

    return record;
}

PilotRecord* ContactConduit::backendToPalm(BackendRecord *backendRecord,
                                            SyncContext *context)
{
    if (!backendRecord) return nullptr;

    // Ensure categories are loaded
    if (!m_categories) {
        loadCategories(context);
    }

    // Parse vCard content
    QString content = QString::fromUtf8(backendRecord->data);
    ContactMapper::Contact contact = ContactMapper::vCardToContact(content);

    // Look up or create category from name
    if (!contact.categoryName.isEmpty() && m_categories) {
        contact.category = m_categories->getOrCreateCategory(contact.categoryName);
        qDebug() << "[ContactConduit] Category" << contact.categoryName << "-> index" << contact.category;
    }

    // Pack to Palm record
    PilotRecord *record = ContactMapper::packContact(contact);

    return record;
}

bool ContactConduit::recordsEqual(PilotRecord *palm, BackendRecord *backend) const
{
    if (!palm || !backend) return false;

    // Unpack Palm contact
    ContactMapper::Contact palmContact = ContactMapper::unpackContact(palm);

    // Parse backend content
    QString backendContent = QString::fromUtf8(backend->data);
    ContactMapper::Contact backendContact = ContactMapper::vCardToContact(backendContent);

    // Compare key fields
    if (palmContact.firstName != backendContact.firstName) return false;
    if (palmContact.lastName != backendContact.lastName) return false;
    if (palmContact.company != backendContact.company) return false;

    // Compare phone numbers (at least first one)
    if (palmContact.phone1 != backendContact.phone1) return false;

    return true;
}

QString ContactConduit::palmRecordDescription(PilotRecord *record) const
{
    if (!record) return QString();

    ContactMapper::Contact contact = ContactMapper::unpackContact(record);

    // Build display name
    QStringList parts;
    if (!contact.firstName.isEmpty()) parts << contact.firstName;
    if (!contact.lastName.isEmpty()) parts << contact.lastName;

    QString name = parts.join(" ");
    if (name.isEmpty() && !contact.company.isEmpty()) {
        name = contact.company;
    }
    if (name.isEmpty()) {
        name = contact.phone1;  // Fallback to phone
    }
    if (name.isEmpty()) {
        name = "<Unnamed>";
    }

    return name;
}

bool ContactConduit::writeModifiedCategories(SyncContext *context)
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
