#ifndef PILOTRECORD_H
#define PILOTRECORD_H

#include <QByteArray>
#include <QString>

// Forward declare pilot-link structs (defined in pi-dlp.h)
struct PilotUser;
struct SysInfo;

/**
 * @brief Wrapper for Palm database records
 *
 * This class encapsulates a Palm database record with its metadata.
 * It holds the raw binary data along with attributes like category,
 * ID, and status flags.
 */
class PilotRecord
{
public:
    /**
     * Record attribute flags (from pilot-link)
     */
    enum Attribute {
        AttrDeleted  = 0x80,  // Record deleted
        AttrDirty    = 0x40,  // Record modified
        AttrBusy     = 0x20,  // Record in use
        AttrSecret   = 0x10,  // Record is private
        AttrArchived = 0x08   // Record archived
    };

    PilotRecord();
    PilotRecord(int recordId, int category, int attributes, const QByteArray &data);
    ~PilotRecord();

    // Accessors
    int recordId() const { return m_recordId; }
    void setRecordId(int id) { m_recordId = id; }

    int category() const { return m_category; }
    void setCategory(int cat) { m_category = cat; }

    int attributes() const { return m_attributes; }
    void setAttributes(int attr) { m_attributes = attr; }

    QByteArray data() const { return m_data; }
    void setData(const QByteArray &data) { m_data = data; }

    // Convenience methods
    bool isDeleted() const { return m_attributes & AttrDeleted; }
    bool isDirty() const { return m_attributes & AttrDirty; }
    bool isSecret() const { return m_attributes & AttrSecret; }
    bool isArchived() const { return m_attributes & AttrArchived; }

    size_t size() const { return m_data.size(); }
    const unsigned char* rawData() const {
        return reinterpret_cast<const unsigned char*>(m_data.constData());
    }

private:
    int m_recordId;
    int m_category;
    int m_attributes;
    QByteArray m_data;
};

#endif // PILOTRECORD_H
