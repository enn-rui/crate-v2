#pragma once

#include "preferences/usersettings.h"

class TrackCollection;

namespace crate {

bool migratePlaylistsToCrates(
        TrackCollection* pTrackCollection, const UserSettingsPointer& pConfig);

} // namespace crate
