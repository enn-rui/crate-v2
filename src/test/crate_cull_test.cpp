#include <gtest/gtest.h>

#include "crate/cull/cullclient.h"

namespace crate {

TEST(CrateCullClient, ResolvesMappedDriveAgainstUncRoot) {
    EXPECT_EQ(QStringLiteral("music/dj/Artist/Track.flac"),
            CullClient::relpathForLocation(
                    QStringLiteral("Z:/music/dj/Artist/Track.flac"),
                    QStringLiteral("//music-box/media")));
}

TEST(CrateCullClient, ResolvesUncAgainstMappedDriveRoot) {
    EXPECT_EQ(QStringLiteral("music/dj/Artist/Track.flac"),
            CullClient::relpathForLocation(
                    QStringLiteral("//music-box/media/music/dj/Artist/Track.flac"),
                    QStringLiteral("Z:/")));
}

TEST(CrateCullClient, RejectsLocationWithoutRelativeTail) {
    EXPECT_TRUE(CullClient::relpathForLocation(
            QStringLiteral("Z:/"), QStringLiteral("Z:/")).isEmpty());
}

} // namespace crate
