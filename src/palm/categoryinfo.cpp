#include "categoryinfo.h"
#include <cstring>
#include <QDebug>

// Windows-1252 to Unicode mapping table for 0x80-0x9F
static const unsigned short cp1252_to_unicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

// Helper to decode Palm text (Windows-1252 encoding)
static QString decodePalmText(const char *palmText)
{
    if (!palmText || palmText[0] == '\0') {
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

// Static helper to encode Unicode to Windows-1252
QByteArray CategoryInfo::encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());

    for (QChar ch : text) {
        ushort unicode = ch.unicode();

        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && !(unicode >= 0x80 && unicode <= 0x9F)) {
            // Latin-1 supplement range (excluding 0x80-0x9F)
            result.append(static_cast<char>(unicode));
        } else {
            // Try to find in Windows-1252 special range
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (cp1252_to_unicode[i] == unicode) {
                    result.append(static_cast<char>(0x80 + i));
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.append('?');  // Unknown character
            }
        }
    }

    return result;
}

CategoryInfo::CategoryInfo()
    : m_valid(false)
    , m_dirty(false)
{
    memset(&m_categories, 0, sizeof(m_categories));
}

bool CategoryInfo::parse(const unsigned char *appInfoData, size_t size)
{
    if (!appInfoData || size < sizeof(CategoryAppInfo_t)) {
        m_valid = false;
        return false;
    }

    if (unpack_CategoryAppInfo(&m_categories, appInfoData, size) < 0) {
        m_valid = false;
        return false;
    }

    m_valid = true;
    m_dirty = false;
    return true;
}

QString CategoryInfo::categoryName(int index) const
{
    if (!m_valid || index < 0 || index >= MAX_CATEGORIES) {
        return QString();
    }

    return decodePalmText(m_categories.name[index]);
}

int CategoryInfo::categoryIndex(const QString &name) const
{
    if (!m_valid || name.isEmpty()) {
        return -1;  // Not found
    }

    // Search for matching category name (case-insensitive)
    for (int i = 0; i < MAX_CATEGORIES; i++) {
        QString catName = categoryName(i);
        if (!catName.isEmpty() && catName.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }

    return -1;  // Not found
}

int CategoryInfo::getOrCreateCategory(const QString &name)
{
    if (name.isEmpty()) {
        return 0;  // Unfiled
    }

    // First, try to find existing category
    int index = categoryIndex(name);
    if (index >= 0) {
        return index;
    }

    // Not found - create it
    index = addCategory(name);
    if (index >= 0) {
        return index;
    }

    // No slots available - return Unfiled
    qWarning() << "[CategoryInfo] No slots available for category:" << name;
    return 0;
}

QStringList CategoryInfo::allCategories() const
{
    QStringList list;
    for (int i = 0; i < MAX_CATEGORIES; i++) {
        list.append(categoryName(i));
    }
    return list;
}

QStringList CategoryInfo::usedCategories() const
{
    QStringList list;
    for (int i = 0; i < MAX_CATEGORIES; i++) {
        QString name = categoryName(i);
        if (!name.isEmpty()) {
            list.append(name);
        }
    }
    return list;
}

// ========== Modification Methods ==========

int CategoryInfo::addCategory(const QString &name)
{
    if (!m_valid || name.isEmpty()) {
        return -1;
    }

    // Check if already exists
    int existing = categoryIndex(name);
    if (existing >= 0) {
        return existing;
    }

    // Find an empty slot (skip slot 0 which is Unfiled)
    int slot = findEmptySlot();
    if (slot < 0) {
        return -1;  // No slots available
    }

    // Set the category
    if (setCategory(slot, name)) {
        qDebug() << "[CategoryInfo] Added category" << name << "at slot" << slot;
        return slot;
    }

    return -1;
}

bool CategoryInfo::setCategory(int index, const QString &name)
{
    if (!m_valid || index < 0 || index >= MAX_CATEGORIES) {
        return false;
    }

    // Encode to Windows-1252
    QByteArray encoded = encodePalmText(name);

    // Truncate if necessary
    if (encoded.size() > MAX_CATEGORY_NAME_LEN) {
        encoded = encoded.left(MAX_CATEGORY_NAME_LEN);
    }

    // Copy to category name buffer (with null terminator)
    memset(m_categories.name[index], 0, MAX_CATEGORY_NAME_LEN + 1);
    memcpy(m_categories.name[index], encoded.constData(), encoded.size());

    // Assign a unique ID if this is a new category (ID was 0)
    if (m_categories.ID[index] == 0 && index > 0) {
        // Find next available ID
        int maxId = 0;
        for (int i = 0; i < MAX_CATEGORIES; i++) {
            if (m_categories.ID[i] > maxId) {
                maxId = m_categories.ID[i];
            }
        }
        m_categories.ID[index] = maxId + 1;
    }

    m_dirty = true;
    return true;
}

int CategoryInfo::findEmptySlot() const
{
    if (!m_valid) {
        return -1;
    }

    // Start from 1 (slot 0 is Unfiled)
    for (int i = 1; i < MAX_CATEGORIES; i++) {
        if (m_categories.name[i][0] == '\0') {
            return i;
        }
    }

    return -1;  // All slots full
}

// ========== Serialization ==========

size_t CategoryInfo::packSize() const
{
    // CategoryAppInfo_t is a fixed size structure
    return sizeof(CategoryAppInfo_t);
}

int CategoryInfo::pack(unsigned char *buffer, size_t bufferSize) const
{
    if (!m_valid || !buffer) {
        return -1;
    }

    if (bufferSize < packSize()) {
        return -1;
    }

    int result = pack_CategoryAppInfo(&m_categories, buffer, bufferSize);
    if (result < 0) {
        qWarning() << "[CategoryInfo] pack_CategoryAppInfo failed:" << result;
        return -1;
    }

    return result;
}
