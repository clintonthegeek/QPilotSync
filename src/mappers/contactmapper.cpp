#include "contactmapper.h"
#include <pi-address.h>
#include <QRegularExpression>
#include <QStringConverter>

// Windows-1252 to Unicode mapping table for 0x80-0x9F
static const unsigned short cp1252_to_unicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, // 0x80-0x87
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F, // 0x88-0x8F
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, // 0x90-0x97
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178  // 0x98-0x9F
};

// Helper to decode Palm text which uses Windows-1252 encoding
static QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = cp1252_to_unicode[byte - 0x80];
            QString unicodeChar = QString(QChar(unicode));
            fixed.append(unicodeChar.toUtf8());
        } else {
            fixed.append(byte);
        }
    }

    return QString::fromUtf8(fixed);
}

// Helper to encode Unicode text to Windows-1252 for Palm
static QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (QChar ch : text) {
        ushort unicode = ch.unicode();

        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && !(unicode >= 0x80 && unicode <= 0x9F)) {
            result.append(static_cast<char>(unicode));
        } else {
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (cp1252_to_unicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.append('?');
            }
        }
    }

    return result;
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

// ========== Reverse mapping: vCard → Palm ==========

// Helper to unfold vCard lines (reverse of foldLine)
static QString unfoldVCardContent(const QString &content)
{
    QString result = content;
    // Folded lines have CRLF followed by a space or tab
    result.replace("\r\n ", "");
    result.replace("\r\n\t", "");
    // Also handle just LF for flexibility
    result.replace("\n ", "");
    result.replace("\n\t", "");
    return result;
}

// Helper to map vCard phone TYPE to Palm phone label index
// Palm: 0=Work, 1=Home, 2=Fax, 3=Other, 4=E-mail, 5=Main, 6=Pager, 7=Mobile
static int phoneTypeToLabelIndex(const QString &type)
{
    QString t = type.toLower();
    if (t.contains("cell") || t.contains("mobile")) return 7;
    if (t.contains("fax")) return 2;
    if (t.contains("pager")) return 6;
    if (t.contains("home")) return 1;
    if (t.contains("work")) return 0;
    if (t.contains("pref") || t.contains("main")) return 5;
    return 3; // Other
}

ContactMapper::Contact ContactMapper::vCardToContact(const QString &vcard)
{
    Contact contact;
    contact.recordId = 0;
    contact.category = 0;
    contact.isPrivate = false;
    contact.isDirty = false;
    contact.isDeleted = false;
    contact.showPhone = 0;

    // Initialize phone labels to defaults
    contact.phoneLabels << "0" << "1" << "7" << "4" << "3"; // Work, Home, Mobile, Email, Other

    // Unfold the vCard content
    QString content = unfoldVCardContent(vcard);

    // Split into lines (handle CRLF or LF)
    QStringList lines = content.split(QRegularExpression("\r?\n"), Qt::SkipEmptyParts);

    int phoneIndex = 0;

    for (const QString &line : lines) {
        if (line.startsWith("BEGIN:") || line.startsWith("END:") || line.startsWith("VERSION:")) {
            continue;
        }

        // Parse property and value
        int colonPos = line.indexOf(':');
        if (colonPos == -1) continue;

        QString propertyPart = line.left(colonPos);
        QString value = line.mid(colonPos + 1);

        // Split property into name and parameters
        QStringList propertyParts = propertyPart.split(';');
        QString propertyName = propertyParts.first().toUpper();

        // Parse parameters
        QString typeParam;
        for (int i = 1; i < propertyParts.size(); i++) {
            if (propertyParts[i].toUpper().startsWith("TYPE=")) {
                typeParam = propertyParts[i].mid(5);
            }
        }

        if (propertyName == "FN") {
            // Full name - we'll use this if N is not present
            // But N is preferred, so we only use FN as fallback
        } else if (propertyName == "N") {
            // Structured name: Family;Given;Middle;Prefix;Suffix
            QStringList nameParts = value.split(';');
            if (nameParts.size() > 0) contact.lastName = nameParts[0];
            if (nameParts.size() > 1) contact.firstName = nameParts[1];
        } else if (propertyName == "ORG") {
            contact.company = value;
        } else if (propertyName == "TITLE") {
            contact.title = value;
        } else if (propertyName == "TEL") {
            // Phone number
            if (phoneIndex < 5) {
                int labelIndex = phoneTypeToLabelIndex(typeParam);
                switch (phoneIndex) {
                    case 0: contact.phone1 = value; break;
                    case 1: contact.phone2 = value; break;
                    case 2: contact.phone3 = value; break;
                    case 3: contact.phone4 = value; break;
                    case 4: contact.phone5 = value; break;
                }
                if (phoneIndex < contact.phoneLabels.size()) {
                    contact.phoneLabels[phoneIndex] = QString::number(labelIndex);
                }
                phoneIndex++;
            }
        } else if (propertyName == "EMAIL") {
            // Email stored as phone with label 4 (E-mail)
            if (phoneIndex < 5) {
                switch (phoneIndex) {
                    case 0: contact.phone1 = value; break;
                    case 1: contact.phone2 = value; break;
                    case 2: contact.phone3 = value; break;
                    case 3: contact.phone4 = value; break;
                    case 4: contact.phone5 = value; break;
                }
                if (phoneIndex < contact.phoneLabels.size()) {
                    contact.phoneLabels[phoneIndex] = "4"; // E-mail
                }
                phoneIndex++;
            }
        } else if (propertyName == "ADR") {
            // Address: PO;Ext;Street;City;State;ZIP;Country
            QStringList addrParts = value.split(';');
            if (addrParts.size() > 2) contact.address = addrParts[2];
            if (addrParts.size() > 3) contact.city = addrParts[3];
            if (addrParts.size() > 4) contact.state = addrParts[4];
            if (addrParts.size() > 5) contact.zip = addrParts[5];
            if (addrParts.size() > 6) contact.country = addrParts[6];
        } else if (propertyName == "NOTE") {
            contact.note = value;
        } else if (propertyName == "X-PALM-CUSTOM1") {
            contact.custom1 = value;
        } else if (propertyName == "X-PALM-CUSTOM2") {
            contact.custom2 = value;
        } else if (propertyName == "X-PALM-CUSTOM3") {
            contact.custom3 = value;
        } else if (propertyName == "X-PALM-CUSTOM4") {
            contact.custom4 = value;
        } else if (propertyName == "UID") {
            // Extract record ID from UID if it's in palm-XXXX format
            if (value.startsWith("palm-")) {
                bool ok;
                int id = value.mid(5).toInt(&ok);
                if (ok) contact.recordId = id;
            }
        }
    }

    return contact;
}

PilotRecord* ContactMapper::packContact(const Contact &contact)
{
    // Create Address structure
    Address_t address;
    memset(&address, 0, sizeof(address));

    // Helper lambda to set an entry field
    auto setEntry = [&address](int index, const QString &value) {
        if (!value.isEmpty()) {
            QByteArray palm = encodePalmText(value);
            address.entry[index] = strdup(palm.constData());
        }
    };

    // Set name fields
    setEntry(entryLastname, contact.lastName);
    setEntry(entryFirstname, contact.firstName);
    setEntry(entryCompany, contact.company);
    setEntry(entryTitle, contact.title);

    // Set phone fields
    setEntry(entryPhone1, contact.phone1);
    setEntry(entryPhone2, contact.phone2);
    setEntry(entryPhone3, contact.phone3);
    setEntry(entryPhone4, contact.phone4);
    setEntry(entryPhone5, contact.phone5);

    // Set phone labels
    for (int i = 0; i < 5 && i < contact.phoneLabels.size(); i++) {
        address.phoneLabel[i] = contact.phoneLabels[i].toInt();
    }
    address.showPhone = contact.showPhone;

    // Set address fields
    setEntry(entryAddress, contact.address);
    setEntry(entryCity, contact.city);
    setEntry(entryState, contact.state);
    setEntry(entryZip, contact.zip);
    setEntry(entryCountry, contact.country);

    // Set custom fields
    setEntry(entryCustom1, contact.custom1);
    setEntry(entryCustom2, contact.custom2);
    setEntry(entryCustom3, contact.custom3);
    setEntry(entryCustom4, contact.custom4);

    // Set note
    setEntry(entryNote, contact.note);

    // Pack to buffer
    pi_buffer_t *buf = pi_buffer_new(0xFFFF);
    int packResult = pack_Address(&address, buf, address_v1);

    // Free allocated strings
    free_Address(&address);

    if (packResult < 0) {
        pi_buffer_free(buf);
        return nullptr;
    }

    // Create QByteArray from buffer
    QByteArray data(reinterpret_cast<const char*>(buf->data), buf->used);
    pi_buffer_free(buf);

    // Create attributes from flags
    int attr = 0;
    if (contact.isPrivate) attr |= PilotRecord::AttrSecret;
    if (contact.isDirty) attr |= PilotRecord::AttrDirty;
    if (contact.isDeleted) attr |= PilotRecord::AttrDeleted;

    return new PilotRecord(contact.recordId, contact.category, attr, data);
}
