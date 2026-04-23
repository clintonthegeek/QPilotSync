#include "palmtext.h"

#include <QChar>

namespace WildPalms::PalmCodecs {

namespace {

// Windows-1252 to Unicode mapping table for 0x80-0x9F. The rest of
// the 0xA0-0xFF range matches ISO-8859-1 directly, so only the
// Windows-specific 0x80-0x9F range needs explicit translation.
constexpr unsigned short kCp1252ToUnicode[] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

} // namespace

QString decodePalmText(const char *palmText)
{
    if (!palmText) {
        return QString();
    }

    QByteArray data(palmText);
    QByteArray fixed;
    fixed.reserve(data.size());
    for (unsigned char byte : data) {
        if (byte >= 0x80 && byte <= 0x9F) {
            ushort unicode = kCp1252ToUnicode[byte - 0x80];
            fixed.append(QString(QChar(unicode)).toUtf8());
        } else if (byte >= 0xA0) {
            // 0xA0-0xFF: same code points as Latin-1 / Unicode; emit
            // the proper 2-byte UTF-8 sequence rather than a raw byte.
            fixed.append(QString(QChar(static_cast<ushort>(byte))).toUtf8());
        } else {
            fixed.append(static_cast<char>(byte));
        }
    }
    return QString::fromUtf8(fixed);
}

QByteArray encodePalmText(const QString &text)
{
    QByteArray result;
    result.reserve(text.size());
    for (QChar ch : text) {
        ushort unicode = ch.unicode();
        if (unicode < 0x80) {
            result.append(static_cast<char>(unicode));
        } else if (unicode <= 0xFF && (unicode < 0x80 || unicode > 0x9F)) {
            result.append(static_cast<char>(unicode));
        } else {
            bool found = false;
            for (int i = 0; i < 32; ++i) {
                if (kCp1252ToUnicode[i] == unicode) {
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

} // namespace WildPalms::PalmCodecs
