#ifndef WILDPALMS_RUNTIME_PALMDEVICEACCESS_H
#define WILDPALMS_RUNTIME_PALMDEVICEACCESS_H

#include <QObject>
#include <QThread>
#include <memory>

#include "palm/sync/ipalmdatabaseaccess.h"

namespace WildPalms::Runtime {

class PalmDeviceAccess : public QObject,
                         public WildPalms::PalmSync::IPalmDatabaseAccess
{
    Q_OBJECT
public:
    explicit PalmDeviceAccess(
        std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> impl,
        QObject *parent = nullptr);
    ~PalmDeviceAccess() override;

    QStringList availableDatabases() const override;
    bool        hasDatabase(const QString &dbName) const override;
    bool        createDatabase(const QString &dbName) override;
    QList<WildPalms::PalmSync::PalmRecord>
                readAllRecords(const QString &dbName) const override;
    std::optional<WildPalms::PalmSync::PalmRecord>
                readRecord(const QString &dbName, std::uint32_t recordId) const override;
    std::uint32_t createRecord(const QString &dbName,
                               const WildPalms::PalmSync::PalmRecord &record) override;
    bool        updateRecord(const QString &dbName,
                             const WildPalms::PalmSync::PalmRecord &record) override;
    bool        deleteRecord(const QString &dbName,
                             std::uint32_t recordId) override;
    QList<WildPalms::PalmSync::PalmRecord>
                recordsModifiedSince(const QString &dbName,
                                     const QDateTime &since) const override;
    QList<std::uint32_t>
                recordsDeletedSince(const QString &dbName,
                                    const QDateTime &since) const override;
    QByteArray  readAppBlock(const QString &dbName) const override;
    bool        supportsDeleteTracking() const override;

    QThread *linkThread() const { return m_linkThread.get(); }

private:
    std::unique_ptr<WildPalms::PalmSync::IPalmDatabaseAccess> m_impl;
    std::unique_ptr<QThread>                                  m_linkThread;
    QObject                                                   *m_implOwner = nullptr;
};

} // namespace WildPalms::Runtime

#endif
