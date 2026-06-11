#include "localfolderprovider.h"

#include <markdownfilesbackend.h>
#include <rawfilesbackend.h>

#include <QDir>
#include <QFutureInterface>

namespace WildPalms::Runtime {

LocalFolderProvider::LocalFolderProvider(QObject *parent)
    : Kalburator::Sync::IProvider(parent) {}

void LocalFolderProvider::load(const Kalburator::Sync::BackendConfiguration &cfg)
{
    m_cfg = cfg;
    m_entries.clear();
    const QVariantList list =
        cfg.connectionParams.value(QStringLiteral("entries")).toList();
    int i = 0;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        Entry e;
        e.path   = m.value(QStringLiteral("path")).toString();
        e.domain = m.value(QStringLiteral("domain")).toString();
        // Stable, path-independent collection id within this provider.
        e.collectionId = QStringLiteral("folder-%1").arg(i++);
        if (!e.path.isEmpty() && !e.domain.isEmpty())
            m_entries.append(e);
    }
}

QFuture<bool> LocalFolderProvider::connect()
{
    // Synchronous: validate paths, build collections, resolve immediately.
    m_lastError.clear();
    m_collections.clear();
    bool ok = !m_entries.isEmpty();
    if (m_entries.isEmpty())
        m_lastError = QStringLiteral("No folders configured");
    for (const auto &e : m_entries) {
        if (!QDir(e.path).exists()) {
            ok = false;
            m_lastError = QStringLiteral("Folder does not exist: %1").arg(e.path);
            break;
        }
        Kalburator::Sync::CollectionInfo ci;
        ci.id       = e.collectionId;
        ci.name     = QDir(e.path).dirName();
        ci.type     = e.domain;
        ci.readOnly = false;
        m_collections.append(ci);
    }
    if (!ok) {
        m_collections.clear();
        emit error(m_lastError);
    } else {
        m_connected = true;
        emit collectionsChanged();
        emit connectionStateChanged(true);
    }
    QFutureInterface<bool> fi;
    fi.reportStarted();
    fi.reportResult(ok);
    fi.reportFinished();
    return fi.future();
}

void LocalFolderProvider::disconnect()
{
    if (!m_connected) return;
    m_connected = false;
    m_collections.clear();
    emit connectionStateChanged(false);
}

std::unique_ptr<Kalburator::Sync::IBlobBackend>
LocalFolderProvider::createBackend(const QString &collectionId)
{
    if (!m_connected) return nullptr;
    for (const auto &e : m_entries) {
        if (e.collectionId != collectionId) continue;
        // v1 dispatch (substrate spec A2): note -> Markdown; rest -> RawFiles.
        if (e.domain == QLatin1String("note"))
            return std::make_unique<Kalburator::Sinks::MarkdownFilesBackend>(e.path);
        return std::make_unique<Kalburator::Sinks::RawFilesBackend>(e.path);
    }
    return nullptr;
}

QWidget *LocalFolderProvider::createConfigWidget(QWidget *parent)
{
    // Minimal v1: the lib's ProviderConfigDialog tolerates a null widget;
    // a folder-list editor widget ships with sub-project C's source UI.
    Q_UNUSED(parent);
    return nullptr;
}

} // namespace WildPalms::Runtime
