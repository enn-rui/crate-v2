#pragma once

#include <QSet>
#include <QString>
#include <QVector>

#include "crate/data/cratesidecars.h"
#include "preferences/usersettings.h"

namespace crate {

// Persists successfully culled sidecar relpaths until a later sidecar snapshot
// proves that each track has disappeared. This bridges the interval between a
// successful trash move and the box regenerating umap.sqlite.
class CulledTracks {
  public:
    static void record(const UserSettingsPointer& pConfig, const QString& relpath);
    static void excludeAndPrune(
            const UserSettingsPointer& pConfig, QVector<GalaxyNode>* pNodes);
    static QSet<QString> load(const UserSettingsPointer& pConfig);
};

} // namespace crate
