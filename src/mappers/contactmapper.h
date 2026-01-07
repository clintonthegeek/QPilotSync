#ifndef CONTACTMAPPER_H
#define CONTACTMAPPER_H

#include <QString>
#include <QStringList>
#include <QObject>
#include "../palm/pilotrecord.h"

/**
 * @brief Mapper for converting Palm Address records to vCard format
 *
 * Palm AddressDB stores contacts with multiple phone numbers, addresses,
 * and custom fields. This mapper converts them to standard vCard 3.0 format.
 */
class ContactMapper : public QObject
{
    Q_OBJECT

public:
    struct Contact {
        int recordId;
        int category;

        QString lastName;
        QString firstName;
        QString company;
        QString title;

        QString phone1;
        QString phone2;
        QString phone3;
        QString phone4;
        QString phone5;
        QStringList phoneLabels;  // Labels for the 5 phone fields
        int showPhone;            // Which phone to display (0-4)

        QString address;
        QString city;
        QString state;
        QString zip;
        QString country;

        QString custom1;
        QString custom2;
        QString custom3;
        QString custom4;

        QString note;

        bool isPrivate;
        bool isDirty;
        bool isDeleted;
    };

    explicit ContactMapper(QObject *parent = nullptr);
    ~ContactMapper();

    /**
     * @brief Unpack a Palm address record into a Contact structure
     * @param record The Palm record to unpack
     * @return Contact structure with parsed data, or empty Contact if invalid
     */
    static Contact unpackContact(const PilotRecord *record);

    /**
     * @brief Convert a Contact to vCard 3.0 format
     * @param contact The contact to convert
     * @param categoryName Optional category name
     * @return vCard string (RFC 2426)
     */
    static QString contactToVCard(const Contact &contact, const QString &categoryName = QString());

    /**
     * @brief Generate a safe filename from contact name
     * @param contact The contact
     * @return Safe filename for the vCard
     */
    static QString generateFilename(const Contact &contact);

signals:
    void logMessage(const QString &message);
    void errorOccurred(const QString &error);
};

#endif // CONTACTMAPPER_H
