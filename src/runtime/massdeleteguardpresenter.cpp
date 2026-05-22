#include "massdeleteguardpresenter.h"

#include <KLocalizedString>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>

namespace WildPalms::Runtime {

MassDeleteGuardPresenter::MassDeleteGuardPresenter(QWidget *parent)
    : QObject(parent)
    , m_parentWidget(parent)
{
}

MassDeleteGuardPresenter::~MassDeleteGuardPresenter() = default;

bool MassDeleteGuardPresenter::confirmMassDelete(
    const QString &mappingId,
    const QString &targetBackendId,
    int proposedDeletes,
    int baselineCount)
{
    bool result = false;

    // Marshal to the GUI thread. If we're already on it (e.g. unit
    // tests), call promptUser directly.
    if (QThread::currentThread() == this->thread()) {
        result = promptUser(mappingId, targetBackendId,
                            proposedDeletes, baselineCount);
    } else {
        QMetaObject::invokeMethod(this,
            [&]() {
                result = promptUser(mappingId, targetBackendId,
                                    proposedDeletes, baselineCount);
            },
            Qt::BlockingQueuedConnection);
    }
    return result;
}

bool MassDeleteGuardPresenter::promptUser(
    const QString &mappingId,
    const QString &targetBackendId,
    int proposedDeletes,
    int baselineCount)
{
    Q_UNUSED(mappingId);
    const QString text = i18n(
        "WildPalms is about to delete %1 records from <b>%2</b>.\n\n"
        "The baseline for this mapping has %3 records, and the sync "
        "engine has concluded the PC side wants those records removed. "
        "If this is unexpected (for example you moved or deleted files "
        "outside WildPalms), choose <b>No</b> to skip the deletes "
        "this round.",
        proposedDeletes, targetBackendId, baselineCount);

    QMessageBox box(m_parentWidget);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(i18n("Confirm Mass Delete"));
    box.setText(text);
    box.setTextFormat(Qt::RichText);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

} // namespace WildPalms::Runtime
