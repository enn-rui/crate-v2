#include "crate/data/playlistmigration.h"

#include "library/dao/playlistdao.h"
#include "library/trackcollection.h"
#include "library/trackset/crate/crate.h"
#include "preferences/configobject.h"

namespace {

const ConfigKey kMigrationKey(QStringLiteral("[Crate]"),
        QStringLiteral("playlists_migrated"));

QString availableCrateName(const CrateStorage& crates, const QString& playlistName) {
    if (!crates.readCrateByName(playlistName)) {
        return playlistName;
    }

    QString candidate = playlistName + QStringLiteral(" (playlist)");
    int suffix = 2;
    while (crates.readCrateByName(candidate)) {
        candidate = playlistName +
                QStringLiteral(" (playlist %1)").arg(suffix++);
    }
    return candidate;
}

} // namespace

namespace crate {

bool migratePlaylistsToCrates(
        TrackCollection* pTrackCollection, const UserSettingsPointer& pConfig) {
    if (!pTrackCollection || !pConfig || pConfig->getValue(kMigrationKey, 0) != 0) {
        return true;
    }

    auto& playlistDao = pTrackCollection->getPlaylistDAO();
    const auto playlists = playlistDao.getPlaylists(PlaylistDAO::PLHT_NOT_HIDDEN);
    QList<CrateId> createdCrateIds;
    const auto rollback = [&] {
        for (auto it = createdCrateIds.crbegin(); it != createdCrateIds.crend(); ++it) {
            pTrackCollection->deleteCrate(*it);
        }
    };
    for (const auto& playlist : playlists) {
        Crate crate;
        crate.setName(availableCrateName(pTrackCollection->crates(), playlist.second));

        CrateId crateId;
        if (!pTrackCollection->insertCrate(crate, &crateId)) {
            rollback();
            return false;
        }
        createdCrateIds.append(crateId);

        const auto trackIds = playlistDao.getTrackIds(playlist.first);
        if (!trackIds.isEmpty() &&
                !pTrackCollection->addCrateTracks(crateId, trackIds)) {
            rollback();
            return false;
        }
    }

    pConfig->setValue(kMigrationKey, 1);
    return true;
}

} // namespace crate
