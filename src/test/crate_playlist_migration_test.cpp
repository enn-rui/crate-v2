#include "crate/data/playlistmigration.h"

#include "library/dao/playlistdao.h"
#include "library/trackset/crate/crate.h"
#include "preferences/configobject.h"
#include "test/librarytest.h"
#include "track/track.h"

class CratePlaylistMigrationTest : public LibraryTest {
};

TEST_F(CratePlaylistMigrationTest, MigratesVisiblePlaylistsOnceWithoutModifyingThem) {
    auto* pCollection = internalCollection();
    auto& playlistDao = pCollection->getPlaylistDAO();

    const auto pTrack1 = getOrAddTrackByLocation(getTestDataDir().filePath("one.mp3"));
    const auto pTrack2 = getOrAddTrackByLocation(getTestDataDir().filePath("two.mp3"));
    ASSERT_TRUE(pTrack1);
    ASSERT_TRUE(pTrack2);

    Crate existingCrate;
    existingCrate.setName(QStringLiteral("Clash"));
    ASSERT_TRUE(pCollection->insertCrate(existingCrate));

    const int clashPlaylist = playlistDao.createPlaylist(QStringLiteral("Clash"));
    const int duplicatePlaylist = playlistDao.createPlaylist(QStringLiteral("Duplicates"));
    const int hiddenPlaylist = playlistDao.createPlaylist(
            QStringLiteral("History"), PlaylistDAO::PLHT_SET_LOG);
    ASSERT_NE(clashPlaylist, -1);
    ASSERT_NE(duplicatePlaylist, -1);
    ASSERT_NE(hiddenPlaylist, -1);
    ASSERT_TRUE(playlistDao.appendTrackToPlaylist(pTrack1->getId(), clashPlaylist));
    ASSERT_TRUE(playlistDao.appendTracksToPlaylist(
            {pTrack1->getId(), pTrack1->getId(), pTrack2->getId()}, duplicatePlaylist));
    ASSERT_TRUE(playlistDao.appendTrackToPlaylist(pTrack2->getId(), hiddenPlaylist));

    EXPECT_TRUE(crate::migratePlaylistsToCrates(pCollection, config()));

    Crate clashCrate;
    ASSERT_TRUE(pCollection->crates().readCrateByName(
            QStringLiteral("Clash (playlist)"), &clashCrate));
    EXPECT_EQ(pCollection->crates().countCrateTracks(clashCrate.getId()), 1U);

    Crate duplicateCrate;
    ASSERT_TRUE(pCollection->crates().readCrateByName(
            QStringLiteral("Duplicates"), &duplicateCrate));
    EXPECT_EQ(pCollection->crates().countCrateTracks(duplicateCrate.getId()), 2U);
    EXPECT_FALSE(pCollection->crates().readCrateByName(QStringLiteral("History")));

    EXPECT_EQ(playlistDao.getPlaylists(PlaylistDAO::PLHT_NOT_HIDDEN).size(), 2);
    EXPECT_EQ(playlistDao.getTrackIdsInPlaylistOrder(duplicatePlaylist).size(), 3);
    EXPECT_EQ(playlistDao.getPlaylists(PlaylistDAO::PLHT_SET_LOG).size(), 1);
    EXPECT_EQ(config()->getValue(
                      ConfigKey("[Crate]", "playlists_migrated"), 0),
            1);

    const uint crateCount = pCollection->crates().countCrates();
    EXPECT_TRUE(crate::migratePlaylistsToCrates(pCollection, config()));
    EXPECT_EQ(pCollection->crates().countCrates(), crateCount);
}
