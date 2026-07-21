#include "crate/cull/cullworkflow.h"

#include <QHash>
#include <QMessageBox>

#include "control/controlobject.h"
#include "crate/cull/cullclient.h"
#include "crate/grab/grabclient.h"
#include "library/trackcollectionmanager.h"

namespace crate {

void startCullWorkflow(QWidget* pParent,
        const UserSettingsPointer& pConfig,
        TrackCollectionManager* pTrackCollectionManager,
        const QList<TrackRef>& trackRefs,
        std::function<void()> changed) {
    if (pTrackCollectionManager == nullptr || trackRefs.isEmpty()) {
        return;
    }
    const QString baseUrl = GrabClient::configuredBaseUrl(pConfig);
    if (baseUrl.isEmpty()) {
        QMessageBox::warning(pParent, QObject::tr("Cull to Trash"),
                QObject::tr("The trash service is not configured. Set it in Crate preferences."));
        return;
    }
    const QString musicRoot = pConfig->getValue(
            ConfigKey("[Crate]", "music_root"), QString());
    QStringList paths;
    QHash<QString, TrackRef> refByRelpath;
    for (const TrackRef& trackRef : trackRefs) {
        paths.append(trackRef.getLocation());
        const QString relpath = CullClient::relpathForLocation(
                trackRef.getLocation(), musicRoot);
        if (!relpath.isEmpty()) {
            refByRelpath.insert(relpath, trackRef);
        }
    }
    if (refByRelpath.isEmpty()) {
        QMessageBox::warning(pParent, QObject::tr("Cull to Trash"),
                QObject::tr("Could not resolve the file against the configured music library. Set the music library in Crate preferences."));
        return;
    }
    if (QMessageBox::question(pParent, QObject::tr("Cull to Trash"),
                QObject::tr("Move to Trash?\n\n%1").arg(paths.join(QChar('\n'))),
                QMessageBox::Cancel | QMessageBox::Yes,
                QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    auto* pClient = new CullClient(
            baseUrl, GrabClient::configuredToken(pConfig), pParent);
    QObject::connect(pClient, &CullClient::cullSucceeded, pParent,
            [pTrackCollectionManager, refByRelpath, changed](const QString& relpath) {
                const auto it = refByRelpath.constFind(relpath);
                if (it != refByRelpath.constEnd()) {
                    pTrackCollectionManager->purgeTracks(QList<TrackRef>{it.value()});
                    ControlObject::set(ConfigKey("[Crate]", "galaxy_reload"), 1.0);
                    ControlObject::set(ConfigKey("[Crate]", "galaxy_reload"), 0.0);
                    if (changed) {
                        changed();
                    }
                }
            });
    QObject::connect(pClient, &CullClient::cullFailed, pParent,
            [pParent](const QString& error) {
                QMessageBox::warning(pParent, QObject::tr("Cull to Trash"), error);
            });
    for (auto it = refByRelpath.constBegin(); it != refByRelpath.constEnd(); ++it) {
        pClient->cull(it.key());
    }
}

} // namespace crate
