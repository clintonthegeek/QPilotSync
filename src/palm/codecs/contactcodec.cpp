#include "contactcodec.h"

#include <cstring>

#include "palmtext.h"

extern "C" {
#include <pi-address.h>
#include <pi-buffer.h>
}

namespace WildPalms::PalmCodecs {

namespace {

struct ScopedBuffer {
    pi_buffer_t *buf = nullptr;
    explicit ScopedBuffer(std::size_t initial = 512) { buf = pi_buffer_new(initial); }
    ~ScopedBuffer() { if (buf) pi_buffer_free(buf); }
    ScopedBuffer(const ScopedBuffer &) = delete;
    ScopedBuffer &operator=(const ScopedBuffer &) = delete;
};

struct ScopedAddress {
    Address_t a{};
    ~ScopedAddress() { free_Address(&a); }
    ScopedAddress() = default;
    ScopedAddress(const ScopedAddress &) = delete;
    ScopedAddress &operator=(const ScopedAddress &) = delete;
};

// Default Palm phone-label table. Real devices override these via
// AppInfo (E.10/E.17 will plumb that); the codec uses the stock labels
// as its source of truth until then.
const char *kDefaultPhoneLabels[8] = {
    "Work", "Home", "Fax", "Other", "E-mail", "Main", "Pager", "Mobile"
};

int labelStringToIndex(const QString &label)
{
    for (int i = 0; i < 8; ++i) {
        if (label.compare(QString::fromLatin1(kDefaultPhoneLabels[i]),
                          Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 3;  // fall back to "Other"
}

// Strdup a QString into a freshly-malloc'd Windows-1252 C string, or
// return nullptr for an empty string (pisock represents "no value" as
// nullptr on Address_t::entry[]).
char *qstringToPalmDup(const QString &s)
{
    if (s.isEmpty()) {
        return nullptr;
    }
    const QByteArray wire = encodePalmText(s);
    char *out = static_cast<char *>(std::malloc(wire.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, wire.constData(), wire.size());
    out[wire.size()] = '\0';
    return out;
}

QString palmDupToQString(const char *s)
{
    return s ? decodePalmText(s) : QString();
}

} // namespace

QByteArray encodeContact(const Contact &c)
{
    ScopedAddress wrap;
    Address_t &addr = wrap.a;

    addr.entry[entryLastname]  = qstringToPalmDup(c.lastName);
    addr.entry[entryFirstname] = qstringToPalmDup(c.firstName);
    addr.entry[entryCompany]   = qstringToPalmDup(c.company);
    addr.entry[entryPhone1]    = qstringToPalmDup(c.phone[0]);
    addr.entry[entryPhone2]    = qstringToPalmDup(c.phone[1]);
    addr.entry[entryPhone3]    = qstringToPalmDup(c.phone[2]);
    addr.entry[entryPhone4]    = qstringToPalmDup(c.phone[3]);
    addr.entry[entryPhone5]    = qstringToPalmDup(c.phone[4]);
    addr.entry[entryAddress]   = qstringToPalmDup(c.address);
    addr.entry[entryCity]      = qstringToPalmDup(c.city);
    addr.entry[entryState]     = qstringToPalmDup(c.state);
    addr.entry[entryZip]       = qstringToPalmDup(c.zip);
    addr.entry[entryCountry]   = qstringToPalmDup(c.country);
    addr.entry[entryTitle]     = qstringToPalmDup(c.title);
    addr.entry[entryCustom1]   = qstringToPalmDup(c.custom[0]);
    addr.entry[entryCustom2]   = qstringToPalmDup(c.custom[1]);
    addr.entry[entryCustom3]   = qstringToPalmDup(c.custom[2]);
    addr.entry[entryCustom4]   = qstringToPalmDup(c.custom[3]);
    addr.entry[entryNote]      = qstringToPalmDup(c.note);

    for (int i = 0; i < 5; ++i) {
        addr.phoneLabel[i] = (i < c.phoneLabels.size())
            ? labelStringToIndex(c.phoneLabels[i])
            : i;  // default: slot N -> label index N
    }
    addr.showPhone = c.showPhone;

    ScopedBuffer out(1024);
    if (!out.buf) return {};
    if (pack_Address(&addr, out.buf, address_v1) < 0) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char *>(out.buf->data),
                      static_cast<int>(out.buf->used));
}

std::optional<Contact> decodeContact(QByteArrayView bytes)
{
    if (bytes.isEmpty()) {
        return std::nullopt;
    }
    ScopedBuffer in(bytes.size());
    if (!in.buf) return std::nullopt;
    std::memcpy(in.buf->data, bytes.data(), bytes.size());
    in.buf->used = bytes.size();

    ScopedAddress wrap;
    Address_t &addr = wrap.a;
    if (unpack_Address(&addr, in.buf, address_v1) < 0) {
        return std::nullopt;
    }

    Contact c;
    c.lastName  = palmDupToQString(addr.entry[entryLastname]);
    c.firstName = palmDupToQString(addr.entry[entryFirstname]);
    c.company   = palmDupToQString(addr.entry[entryCompany]);
    c.phone[0]  = palmDupToQString(addr.entry[entryPhone1]);
    c.phone[1]  = palmDupToQString(addr.entry[entryPhone2]);
    c.phone[2]  = palmDupToQString(addr.entry[entryPhone3]);
    c.phone[3]  = palmDupToQString(addr.entry[entryPhone4]);
    c.phone[4]  = palmDupToQString(addr.entry[entryPhone5]);
    c.address   = palmDupToQString(addr.entry[entryAddress]);
    c.city      = palmDupToQString(addr.entry[entryCity]);
    c.state     = palmDupToQString(addr.entry[entryState]);
    c.zip       = palmDupToQString(addr.entry[entryZip]);
    c.country   = palmDupToQString(addr.entry[entryCountry]);
    c.title     = palmDupToQString(addr.entry[entryTitle]);
    c.custom[0] = palmDupToQString(addr.entry[entryCustom1]);
    c.custom[1] = palmDupToQString(addr.entry[entryCustom2]);
    c.custom[2] = palmDupToQString(addr.entry[entryCustom3]);
    c.custom[3] = palmDupToQString(addr.entry[entryCustom4]);
    c.note      = palmDupToQString(addr.entry[entryNote]);

    // Only emit a phone label for slots that actually have data.
    // Empty slots have no meaningful label; callers must not index
    // phoneLabels beyond the number of non-empty phone strings.
    c.phoneLabels.clear();
    for (int i = 0; i < 5; ++i) {
        if (!c.phone[i].isEmpty()) {
            int idx = addr.phoneLabel[i];
            if (idx < 0 || idx >= 8) idx = 3;  // Other
            c.phoneLabels.append(QString::fromLatin1(kDefaultPhoneLabels[idx]));
        }
    }
    c.showPhone = addr.showPhone;

    return c;
}

} // namespace WildPalms::PalmCodecs
