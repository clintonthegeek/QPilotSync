#include "palmruntime.h"
#include "palmdeviceaccess.h"

#include <QPromise>

// Complete-type includes required by unique_ptr destructors and virtual dispatch.
// Forward declarations in the header are insufficient once these types are
// dereferenced or destroyed in this translation unit.
#include "backendregistry.h"
#include "syncengine.h"
#include "core/ibackendplugin_v2.h"

namespace WildPalms::Runtime {

PalmRuntime::PalmRuntime(const QString &profilePath, QObject *parent)
    : QObject(parent)
    , m_profilePath(profilePath)
{
    qRegisterMetaType<PalmRunResult>();
}

PalmRuntime::~PalmRuntime() = default;

void PalmRuntime::connectDevice(KPilotLink *link) {
    Q_UNUSED(link);
    // Implemented in Task 10.
}

void PalmRuntime::disconnectDevice() {
    m_device.reset();
    emit deviceDisconnected();
}

bool PalmRuntime::isDeviceConnected() const {
    return m_device != nullptr;
}

void PalmRuntime::setDeviceAccessForTest(std::unique_ptr<PalmDeviceAccess> device) {
    m_device = std::move(device);
    emit deviceConnected();
}

void PalmRuntime::registerPluginForTest(std::shared_ptr<WildPalms::IBackendPluginV2> plugin) {
    m_plugins.append(std::move(plugin));
}

void PalmRuntime::setMappingsForTest(QList<Kalburator::Sync::SyncMapping> mappings) {
    m_mappings = std::move(mappings);
}

QList<QString> PalmRuntime::enabledPluginIds() const {
    QList<QString> ids;
    for (const auto &p : m_plugins) ids.append(p->pluginId());
    return ids;
}

QList<Kalburator::Sync::SyncMapping> PalmRuntime::palmMappings() const {
    return m_mappings;
}

static QFuture<PalmRunResult> makeSuccessFuture() {
    QPromise<PalmRunResult> p;
    auto f = p.future();
    p.start();
    PalmRunResult r;
    r.success = true;
    r.startTime = r.endTime = QDateTime::currentDateTimeUtc();
    p.addResult(std::move(r));
    p.finish();
    return f;
}

QFuture<PalmRunResult> PalmRuntime::hotSync()      { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::fullSync()      { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::copyPalmToPC()  { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::copyPCToPalm()  { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::backup()        { return makeSuccessFuture(); }
QFuture<PalmRunResult> PalmRuntime::restore()       { return makeSuccessFuture(); }

}  // namespace WildPalms::Runtime
