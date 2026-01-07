#include "categoryinfo.h"
#include <cstring>

// Helper to decode Palm text (Windows-1252 encoding)
static QString decodePalmText(const char *palmText)
{
    if (!palmText || palmText[0] == '\0') {
        return QString();
    }

    QByteArray data(palmText);

    // Manually map Windows-1252 characters in the 0x80-0x9F range
    QByteArray fixed;
    fixed.reserve(data.size());

    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            static const unsigned short cp1252_to_unicode[] = {
                0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
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

CategoryInfo::CategoryInfo()
    : m_valid(false)
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
    return true;
}

QString CategoryInfo::categoryName(int index) const
{
    if (!m_valid || index < 0 || index >= 16) {
        return QString();
    }

    // Category names are stored as 15 chars + null terminator
    return decodePalmText(m_categories.name[index]);
}

QStringList CategoryInfo::allCategories() const
{
    QStringList list;
    for (int i = 0; i < 16; i++) {
        list.append(categoryName(i));
    }
    return list;
}
