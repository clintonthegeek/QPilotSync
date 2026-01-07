#include "contactmapper.h"
#include <pi-address.h>
#include <QRegularExpression>
#include <QStringConverter>

// Helper to decode Palm text which uses Windows-1252 encoding
// Palm OS uses Windows-1252 (not pure Latin-1) for the 0x80-0x9F range
// This includes characters like ™ (0x99), smart quotes, em/en dashes, etc.
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);

    // Manually map Windows-1252 characters in the 0x80-0x9F range
    // This range is undefined in ISO-8859-1 but defined in Windows-1252
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            // Map Windows-1252 C1 controls to their Unicode equivalents
            static const unsigned short cp1252_to_unicode[] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
            };

            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

// Helper function to fold vCard lines to 75 octets as per RFC 6350 section 3.2
static QString foldLine(const QString &line)
{
    const int MAX_LINE_LENGTH = 75;  // octets, excluding CRLF

    QByteArray utf8 = line.toUtf8();
    if (utf8.length() <= MAX_LINE_LENGTH) {
        return line + "\r\n";
    }

    QString result;
    int pos = 0;

    while (pos < utf8.length()) {
        int chunkSize = MAX_LINE_LENGTH;

        // For first line, use full 75 octets
        // For continuation lines, use 74 octets (to account for leading space)
        if (pos > 0) {
            chunkSize = MAX_LINE_LENGTH - 1;
        }

        // Don't split in the middle of a UTF-8 multi-byte character
        // UTF-8 continuation bytes have the form 10xxxxxx (0x80-0xBF)
        while (chunkSize > 0 && pos + chunkSize < utf8.length() &&
               (utf8[pos + chunkSize] & 0xC0) == 0x80) {
            chunkSize--;
        }

        // Extract chunk and convert back to QString
        QByteArray chunk = utf8.mid(pos, chunkSize);
        QString chunkStr = QString::fromUtf8(chunk);

        if (pos == 0) {
            result += chunkStr + "\r\n";
        } else {
            result += " " + chunkStr + "\r\n";  // Continuation line starts with space
        }

        pos += chunkSize;
    }

    return result;
}

ContactMapper::ContactMapper(QObject *parent)
    : QObject(parent)
{
}

ContactMapper::~ContactMapper()
{
}

ContactMapper::Contact ContactMapper::unpackContact(const PilotRecord *record)
{
    Contact contact;
    contact.recordId = record->recordId();
    contact.category = record->category();
    contact.isPrivate = record->isSecret();
    contact.isDirty = record->isDirty();
    contact.isDeleted = record->isDeleted();

    // Unpack using pilot-link's address parser
    Address_t address;
    memset(&address, 0, sizeof(address));

    pi_buffer_t *buf = pi_buffer_new(record->size());
    memcpy(buf->data, record->rawData(), record->size());
    buf->used = record->size();

    if (unpack_Address(&address, buf, address_v1) < 0) {
        pi_buffer_free(buf);
        return contact;  // Return empty contact on error
    }

    pi_buffer_free(buf);

    // Extract fields from entry array
    if (address.entry[entryLastname])
        contact.lastName = decodePalmText(address.entry[entryLastname]);
    if (address.entry[entryFirstname])
        contact.firstName = decodePalmText(address.entry[entryFirstname]);
    if (address.entry[entryCompany])
        contact.company = decodePalmText(address.entry[entryCompany]);
    if (address.entry[entryTitle])
        contact.title = decodePalmText(address.entry[entryTitle]);

    // Phone numbers
    if (address.entry[entryPhone1])
        contact.phone1 = decodePalmText(address.entry[entryPhone1]);
    if (address.entry[entryPhone2])
        contact.phone2 = decodePalmText(address.entry[entryPhone2]);
    if (address.entry[entryPhone3])
        contact.phone3 = decodePalmText(address.entry[entryPhone3]);
    if (address.entry[entryPhone4])
        contact.phone4 = decodePalmText(address.entry[entryPhone4]);
    if (address.entry[entryPhone5])
        contact.phone5 = decodePalmText(address.entry[entryPhone5]);

    // Phone labels (Work, Home, Fax, Other, E-mail, Main, Pager, Mobile)
    for (int i = 0; i < 5; i++) {
        contact.phoneLabels.append(QString::number(address.phoneLabel[i]));
    }
    contact.showPhone = address.showPhone;

    // Address fields
    if (address.entry[entryAddress])
        contact.address = decodePalmText(address.entry[entryAddress]);
    if (address.entry[entryCity])
        contact.city = decodePalmText(address.entry[entryCity]);
    if (address.entry[entryState])
        contact.state = decodePalmText(address.entry[entryState]);
    if (address.entry[entryZip])
        contact.zip = decodePalmText(address.entry[entryZip]);
    if (address.entry[entryCountry])
        contact.country = decodePalmText(address.entry[entryCountry]);

    // Custom fields
    if (address.entry[entryCustom1])
        contact.custom1 = decodePalmText(address.entry[entryCustom1]);
    if (address.entry[entryCustom2])
        contact.custom2 = decodePalmText(address.entry[entryCustom2]);
    if (address.entry[entryCustom3])
        contact.custom3 = decodePalmText(address.entry[entryCustom3]);
    if (address.entry[entryCustom4])
        contact.custom4 = decodePalmText(address.entry[entryCustom4]);

    // Note
    if (address.entry[entryNote])
        contact.note = decodePalmText(address.entry[entryNote]);

    free_Address(&address);

    return contact;
}

QString ContactMapper::contactToVCard(const Contact &contact, const QString &categoryName)
{
    QString vcard;

    // vCard 4.0 format (RFC 6350) - use VERSION:4.0 for full RFC 6350 compliance
    vcard += "BEGIN:VCARD\r\n";
    vcard += "VERSION:4.0\r\n";

    // Full name (FN) - required field
    QString fullName;
    if (!contact.firstName.isEmpty() && !contact.lastName.isEmpty()) {
        fullName = QString("%1 %2").arg(contact.firstName, contact.lastName);
    } else if (!contact.firstName.isEmpty()) {
        fullName = contact.firstName;
    } else if (!contact.lastName.isEmpty()) {
        fullName = contact.lastName;
    } else if (!contact.company.isEmpty()) {
        fullName = contact.company;
    } else {
        fullName = "Unknown";
    }
    vcard += foldLine(QString("FN:%1").arg(fullName));

    // Structured name (N) - Family;Given;Middle;Prefix;Suffix
    vcard += foldLine(QString("N:%1;%2;;;")
        .arg(contact.lastName.isEmpty() ? "" : contact.lastName)
        .arg(contact.firstName.isEmpty() ? "" : contact.firstName));

    // Organization
    if (!contact.company.isEmpty()) {
        vcard += foldLine(QString("ORG:%1").arg(contact.company));
    }

    // Title
    if (!contact.title.isEmpty()) {
        vcard += foldLine(QString("TITLE:%1").arg(contact.title));
    }

    // Phone numbers with type labels
    // Palm phone labels: 0=Work, 1=Home, 2=Fax, 3=Other, 4=E-mail, 5=Main, 6=Pager, 7=Mobile
    QStringList phones = {contact.phone1, contact.phone2, contact.phone3, contact.phone4, contact.phone5};
    QStringList phoneTypeMap = {"work,voice", "home,voice", "work,fax", "voice", "internet", "pref,voice", "pager", "cell"};

    for (int i = 0; i < phones.size(); i++) {
        if (!phones[i].isEmpty()) {
            int labelIndex = (i < contact.phoneLabels.size()) ? contact.phoneLabels[i].toInt() : 3;
            if (labelIndex >= 0 && labelIndex < phoneTypeMap.size()) {
                QString phoneType = phoneTypeMap[labelIndex];

                // Handle email separately
                if (labelIndex == 4) {
                    vcard += foldLine(QString("EMAIL;TYPE=internet:%1").arg(phones[i]));
                } else {
                    vcard += foldLine(QString("TEL;TYPE=%1:%2").arg(phoneType, phones[i]));
                }
            }
        }
    }

    // Address
    if (!contact.address.isEmpty() || !contact.city.isEmpty() ||
        !contact.state.isEmpty() || !contact.zip.isEmpty() || !contact.country.isEmpty()) {
        // ADR format: ;;street;city;state;postal;country
        vcard += foldLine(QString("ADR;TYPE=work:;;%1;%2;%3;%4;%5")
            .arg(contact.address, contact.city, contact.state, contact.zip, contact.country));
    }

    // Custom fields as X- properties
    if (!contact.custom1.isEmpty()) {
        vcard += foldLine(QString("X-PALM-CUSTOM1:%1").arg(contact.custom1));
    }
    if (!contact.custom2.isEmpty()) {
        vcard += foldLine(QString("X-PALM-CUSTOM2:%1").arg(contact.custom2));
    }
    if (!contact.custom3.isEmpty()) {
        vcard += foldLine(QString("X-PALM-CUSTOM3:%1").arg(contact.custom3));
    }
    if (!contact.custom4.isEmpty()) {
        vcard += foldLine(QString("X-PALM-CUSTOM4:%1").arg(contact.custom4));
    }

    // Note
    if (!contact.note.isEmpty()) {
        vcard += foldLine(QString("NOTE:%1").arg(contact.note));
    }

    // Category
    if (!categoryName.isEmpty()) {
        vcard += foldLine(QString("CATEGORIES:%1").arg(categoryName));
    }

    // UID using Palm record ID
    vcard += foldLine(QString("UID:palm-%1").arg(contact.recordId));

    vcard += "END:VCARD\r\n";

    return vcard;
}

QString ContactMapper::generateFilename(const Contact &contact)
{
    QString filename;

    // Use name as base filename
    if (!contact.firstName.isEmpty() && !contact.lastName.isEmpty()) {
        filename = QString("%1_%2").arg(contact.firstName, contact.lastName);
    } else if (!contact.firstName.isEmpty()) {
        filename = contact.firstName;
    } else if (!contact.lastName.isEmpty()) {
        filename = contact.lastName;
    } else if (!contact.company.isEmpty()) {
        filename = contact.company;
    } else {
        filename = QString("contact_%1").arg(contact.recordId);
    }

    // Sanitize filename
    static QRegularExpression invalidChars("[^a-zA-Z0-9_\\-. ]");
    filename.replace(invalidChars, "_");

    // Replace multiple spaces with single underscore
    static QRegularExpression multiSpace("\\s+");
    filename.replace(multiSpace, "_");

    // Remove leading/trailing underscores
    filename = filename.trimmed();
    while (filename.startsWith('_')) filename.remove(0, 1);
    while (filename.endsWith('_')) filename.chop(1);

    // Add .vcf extension
    filename += ".vcf";

    return filename;
}
