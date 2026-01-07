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

void ContactConduit::loadCategories(SyncContext *context)
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
        emit logMessage("Loaded contact categories");
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

    return record;
}

PilotRecord* ContactConduit::backendToPalm(BackendRecord *backendRecord,
                                            SyncContext *context)
{
    Q_UNUSED(context)

    if (!backendRecord) return nullptr;

    // Parse vCard content
    QString content = QString::fromUtf8(backendRecord->data);
    ContactMapper::Contact contact = ContactMapper::vCardToContact(content);

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

} // namespace Sync
