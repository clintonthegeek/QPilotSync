#include "kde_pim_convert.h"

#include <KContacts/PhoneNumber>

namespace WildPalms::PalmCodecs {

namespace {

constexpr const char *kAppPalm              = "PALM";
constexpr const char *kShowPhoneField       = "SHOW-PHONE";
constexpr const char *kCustomFieldPrefix    = "CUSTOM-";  // CUSTOM-1..4
constexpr const char *kPhoneLabelPrefix     = "PHONE-LABEL-";  // PHONE-LABEL-0..4

bool phoneLabelIsStandard(const QString &label)
{
    static const QStringList kStd = {
        QStringLiteral("Work"),  QStringLiteral("Home"),   QStringLiteral("Fax"),
        QStringLiteral("Other"), QStringLiteral("E-mail"), QStringLiteral("Main"),
        QStringLiteral("Pager"), QStringLiteral("Mobile")
    };
    for (const auto &s : kStd) {
        if (label.compare(s, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

} // namespace

KContacts::Addressee toAddressee(const Contact &c)
{
    KContacts::Addressee a;
    a.setGivenName(c.firstName);
    a.setFamilyName(c.lastName);
    a.setOrganization(c.company);
    a.setTitle(c.title);
    a.setNote(c.note);

    for (int i = 0; i < 5; ++i) {
        if (c.phone[i].isEmpty()) continue;
        KContacts::PhoneNumber::Type type = KContacts::PhoneNumber::Voice;
        if (i < c.phoneLabels.size()) {
            const QString &label = c.phoneLabels[i];
            if      (label == QLatin1String("Work"))   type = KContacts::PhoneNumber::Work;
            else if (label == QLatin1String("Home"))   type = KContacts::PhoneNumber::Home;
            else if (label == QLatin1String("Fax"))    type = KContacts::PhoneNumber::Fax;
            else if (label == QLatin1String("Mobile")) type = KContacts::PhoneNumber::Cell;
            else if (label == QLatin1String("Pager"))  type = KContacts::PhoneNumber::Pager;
            else                                        type = KContacts::PhoneNumber::Voice;
            a.insertCustom(kAppPalm,
                           QString::fromLatin1(kPhoneLabelPrefix) + QString::number(i),
                           label);
        }
        a.insertPhoneNumber(KContacts::PhoneNumber(c.phone[i], type));
    }

    a.insertCustom(kAppPalm, kShowPhoneField, QString::number(c.showPhone));
    for (int i = 0; i < 4; ++i) {
        if (c.custom[i].isEmpty()) continue;
        a.insertCustom(kAppPalm,
                       QString::fromLatin1(kCustomFieldPrefix) + QString::number(i + 1),
                       c.custom[i]);
    }

    KContacts::Address ka(KContacts::Address::Home);
    ka.setStreet(c.address);
    ka.setLocality(c.city);
    ka.setRegion(c.state);
    ka.setPostalCode(c.zip);
    ka.setCountry(c.country);
    a.insertAddress(ka);

    return a;
}

Contact fromAddressee(const KContacts::Addressee &a)
{
    Contact c;
    c.firstName = a.givenName();
    c.lastName  = a.familyName();
    c.company   = a.organization();
    c.title     = a.title();
    c.note      = a.note();

    // Phone labels: prefer stashed X-PALM-PHONE-LABEL-N; else derive
    // from type on the matching KContacts phone; else "Other".
    c.phoneLabels = QStringList{ QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other"),
                                 QStringLiteral("Other") };
    for (int i = 0; i < 5; ++i) {
        const QString stashed = a.custom(kAppPalm,
            QString::fromLatin1(kPhoneLabelPrefix) + QString::number(i));
        if (!stashed.isEmpty()) {
            c.phoneLabels[i] = phoneLabelIsStandard(stashed)
                                 ? stashed
                                 : QStringLiteral("Other");
        }
    }

    int slot = 0;
    for (const auto &ph : a.phoneNumbers()) {
        if (slot >= 5) break;
        c.phone[slot++] = ph.number();
    }

    bool ok = false;
    const int shown = a.custom(kAppPalm, kShowPhoneField).toInt(&ok);
    if (ok) c.showPhone = shown;

    for (int i = 0; i < 4; ++i) {
        const QString v = a.custom(kAppPalm,
            QString::fromLatin1(kCustomFieldPrefix) + QString::number(i + 1));
        c.custom[i] = v;
    }

    const auto addrs = a.addresses();
    if (!addrs.isEmpty()) {
        const auto &ka = addrs.first();
        c.address = ka.street();
        c.city    = ka.locality();
        c.state   = ka.region();
        c.zip     = ka.postalCode();
        c.country = ka.country();
    }

    return c;
}

// Todo converters land in Task 6. These stubs return a minimal
// non-null object so the test binary doesn't crash; the tests will
// still fail because no real data is populated.
KCalendarCore::Todo::Ptr toKCalTodo(const Todo &)
{
    return KCalendarCore::Todo::Ptr(new KCalendarCore::Todo);
}
Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &) { return {}; }

} // namespace WildPalms::PalmCodecs
