#include "conduitcatalog.h"

#include "plugins/calendar/calendarbackendplugin.h"
#include "plugins/contacts/contactsbackendplugin.h"
#include "plugins/memo/memobackendplugin.h"
#include "plugins/todos/todobackendplugin.h"

namespace WildPalms::Runtime {

std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> createStockConduits()
{
    std::vector<std::unique_ptr<WildPalms::Plugins::PimPlugin>> out;
    out.push_back(std::make_unique<WildPalms::CalendarPlugin::CalendarBackendPlugin>());
    out.push_back(std::make_unique<WildPalms::ContactsPlugin::ContactsBackendPlugin>());
    out.push_back(std::make_unique<WildPalms::Memo::MemoPlugin>());
    out.push_back(std::make_unique<WildPalms::TodoPlugin::TodoBackendPlugin>());
    return out;
}

} // namespace WildPalms::Runtime
