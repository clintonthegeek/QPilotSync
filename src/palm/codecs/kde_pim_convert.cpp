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

KCalendarCore::Todo::Ptr toKCalTodo(const Todo &t)
{
    auto kcal = KCalendarCore::Todo::Ptr(new KCalendarCore::Todo);
    kcal->setSummary(t.description);
    kcal->setDescription(t.note);
    if (!t.hasIndefiniteDue && t.due.isValid()) {
        kcal->setDtDue(t.due, true);
        kcal->setAllDay(true);
    }
    // Palm priority 1..5 maps 1:1 to iCal priority 1..5.
    kcal->setPriority(qBound(1, t.priority, 5));
    if (t.isComplete) {
        kcal->setCompleted(QDateTime::currentDateTime());
    }
    return kcal;
}

Todo fromKCalTodo(const KCalendarCore::Todo::Ptr &kcal)
{
    Todo t;
    if (!kcal) return t;
    t.description = kcal->summary();
    t.note        = kcal->description();
    if (kcal->hasDueDate() && kcal->dtDue().isValid()) {
        t.hasIndefiniteDue = false;
        t.due = kcal->dtDue();
    } else {
        t.hasIndefiniteDue = true;
    }
    // iCal priority 0 ("no priority") -> 1. 6..9 clamp to 5.
    const int p = kcal->priority();
    if      (p <= 0) t.priority = 1;
    else if (p > 5)  t.priority = 5;
    else             t.priority = p;
    t.isComplete = kcal->isCompleted();
    return t;
}

} // namespace WildPalms::PalmCodecs
