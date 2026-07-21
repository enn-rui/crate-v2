#pragma once

#include <functional>

#include <QList>

#include "preferences/usersettings.h"
#include "track/trackref.h"

class TrackCollectionManager;
class QWidget;

namespace crate {

// Shared UI flow for moving library tracks to the box trash. Each successful
// request is purged independently; failed requests leave their rows untouched.
void startCullWorkflow(QWidget* pParent,
        const UserSettingsPointer& pConfig,
        TrackCollectionManager* pTrackCollectionManager,
        const QList<TrackRef>& trackRefs,
        std::function<void()> changed = {});

} // namespace crate
