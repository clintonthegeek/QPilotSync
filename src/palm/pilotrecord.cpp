#include "pilotrecord.h"

PilotRecord::PilotRecord()
    : m_recordId(0)
    , m_category(0)
    , m_attributes(0)
{
}

PilotRecord::PilotRecord(int recordId, int category, int attributes, const QByteArray &data)
    : m_recordId(recordId)
    , m_category(category)
    , m_attributes(attributes)
    , m_data(data)
{
}

PilotRecord::~PilotRecord()
{
}
