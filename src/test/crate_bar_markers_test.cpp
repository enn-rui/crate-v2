#include <gtest/gtest.h>

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"
#include "crate/downbeat/barphase.h"
#include "crate/downbeat/downbeatstore.h"
#include "crate/export/rekordboxxml.h"
#include "track/beats.h"
#include "track/bpm.h"
#include "track/track.h"
#include "track/trackid.h"

namespace {

// ---------------------------------------------------------------------------
// T1: Bar computation
// ---------------------------------------------------------------------------

TEST(CrateBarMarkersTest, BattitoRotatesWithOffset) {
    // Grid anchor is beat index 0. Battito 1 is the downbeat.
    // offset shifts which beat becomes the downbeat.
    EXPECT_EQ(crate::barBattito(0, 0), 1);
    EXPECT_EQ(crate::barBattito(1, 0), 2);
    EXPECT_EQ(crate::barBattito(2, 0), 3);
    EXPECT_EQ(crate::barBattito(3, 0), 4);
    EXPECT_EQ(crate::barBattito(4, 0), 1);

    // Anchor with offset 1/2/3: the downbeat moves forward one beat each step.
    EXPECT_TRUE(crate::isDownbeat(1, 1));
    EXPECT_TRUE(crate::isDownbeat(2, 2));
    EXPECT_TRUE(crate::isDownbeat(3, 3));
    EXPECT_FALSE(crate::isDownbeat(0, 1));

    // Anchor's Battito under each offset (used by the rekordbox export).
    EXPECT_EQ(crate::barBattito(0, 0), 1);
    EXPECT_EQ(crate::barBattito(0, 1), 4);
    EXPECT_EQ(crate::barBattito(0, 2), 3);
    EXPECT_EQ(crate::barBattito(0, 3), 2);

    // Negative indices (beats before the anchor / preroll) stay consistent.
    EXPECT_EQ(crate::barBattito(-1, 0), 4);
    EXPECT_EQ(crate::barBattito(-4, 0), 1);

    // Out-of-range offsets normalize into [0,3].
    EXPECT_EQ(crate::barBattito(0, 4), crate::barBattito(0, 0));
    EXPECT_EQ(crate::barBattito(0, -1), crate::barBattito(0, 3));
}

TEST(CrateBarMarkersTest, BeatIndexCountsThroughTempoSections) {
    // A variable-tempo grid: section 1 has 6 beats, section 2 has 4 beats.
    // Bar counting must follow the beat sequence across the marker boundary
    // without resetting, so `it - cfirstmarker()` must increment by exactly one
    // per beat the whole way through.
    const auto rate = mixxx::audio::SampleRate(48000);
    std::vector<mixxx::BeatMarker> markers{
            {mixxx::audio::FramePos(0), 6},
            {mixxx::audio::FramePos(96000), 4}};
    auto pBeats = mixxx::Beats::fromBeatMarkers(
            rate, markers, mixxx::audio::FramePos(160000), mixxx::Bpm(120.0));
    ASSERT_TRUE(pBeats);

    const auto anchor = pBeats->cfirstmarker();
    int expected = 0;
    for (auto it = pBeats->cfirstmarker();
            it != pBeats->clastmarker() + 1;
            ++it, ++expected) {
        EXPECT_EQ(it - anchor, expected) << "drift at beat " << expected;
    }
    // Beats 0..10 inclusive (last marker is beat index 10) = 11 beats visited,
    // counted without drift across the 6-beat / 4-beat section boundary.
    EXPECT_EQ(expected, 11);

    // The second marker sits at beat index 6; it is bar phase 3 at offset 0.
    EXPECT_DOUBLE_EQ((*(pBeats->cfirstmarker() + 6)).value(), 96000.0);
    EXPECT_EQ(crate::barBattito(6, 0), 3);
    // Same beat becomes the downbeat when offset rotates to 2.
    EXPECT_TRUE(crate::isDownbeat(6, 2));
}

// ---------------------------------------------------------------------------
// T2 / T3: Persistence + rotate cycling, via the DownbeatStore + real sqlite
// ---------------------------------------------------------------------------

class DownbeatStoreTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_dir.isValid());
        m_db = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), QStringLiteral("downbeat_test"));
        m_db.setDatabaseName(m_dir.filePath(QStringLiteral("meta.sqlite")));
        ASSERT_TRUE(m_db.open());
        crate::DownbeatStore::instance().connectDatabase(m_db);
    }
    void TearDown() override {
        crate::DownbeatStore::instance().disconnectDatabase();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("downbeat_test"));
    }
    QTemporaryDir m_dir;
    QSqlDatabase m_db;
};

TEST_F(DownbeatStoreTest, SurvivesReloadAndDefaultsToZero) {
    auto& store = crate::DownbeatStore::instance();
    const TrackId id(QVariant(42));

    // Unset track defaults to 0.
    EXPECT_EQ(store.offset(id), 0);
    // Invalid track id is a no-op / 0.
    EXPECT_EQ(store.offset(TrackId()), 0);
    store.setOffset(TrackId(), 2);
    EXPECT_EQ(store.offset(TrackId()), 0);

    store.setOffset(id, 3);
    EXPECT_EQ(store.offset(id), 3);

    // Simulate a restart / library rescan: drop the cache and reload from disk.
    store.connectDatabase(m_db);
    EXPECT_EQ(store.offset(id), 3) << "offset must survive reload";
    EXPECT_EQ(store.offset(TrackId(QVariant(7))), 0) << "unrelated track stays 0";
}

TEST_F(DownbeatStoreTest, RotateCyclesAndPersistsEachStep) {
    auto& store = crate::DownbeatStore::instance();
    const TrackId id(QVariant(99));

    const int expected[] = {1, 2, 3, 0, 1};
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(store.rotateDownbeat(id), expected[i]) << "step " << i;
        // Persisted each step: reload from disk and confirm.
        store.connectDatabase(m_db);
        EXPECT_EQ(store.offset(id), expected[i]) << "persist at step " << i;
    }
}

// ---------------------------------------------------------------------------
// T4: rekordbox XML Battito follows the offset
// ---------------------------------------------------------------------------

QList<QXmlStreamAttributes> tempos(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    QXmlStreamReader reader(&file);
    QList<QXmlStreamAttributes> result;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() &&
                reader.name().toString() == QStringLiteral("TEMPO")) {
            result.append(reader.attributes());
        }
    }
    EXPECT_FALSE(reader.hasError());
    return result;
}

TEST(CrateBarMarkersTest, ExportBattitoFollowsOffsetConstantGrid) {
    // Anchor at frame 0 so firstBeat() is the grid anchor (beat index 0); its
    // Battito rotates 1 -> 4 -> 3 -> 2 as the downbeat offset advances.
    const int expectedBattito[] = {1, 4, 3, 2};
    for (int offset = 0; offset < 4; ++offset) {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        auto track = Track::newTemporary(dir.filePath(QStringLiteral("c.flac")));
        track->setDuration(120.0);
        ASSERT_TRUE(track->trySetBeats(mixxx::Beats::fromConstTempo(
                mixxx::audio::SampleRate(48000),
                mixxx::audio::FramePos(0),
                mixxx::Bpm(120.0))));
        const QString out = dir.filePath(QStringLiteral("out.xml"));
        const auto result = mixxx::RekordboxXmlExport::write(
                {{QStringLiteral("C"), {track}}},
                out,
                QStringLiteral("test"),
                [offset](const TrackPointer&) { return offset; });
        ASSERT_TRUE(result.ok) << result.error.toStdString();
        const auto anchors = tempos(out);
        ASSERT_EQ(anchors.size(), 1);
        EXPECT_EQ(anchors[0].value(QStringLiteral("Metro")), QStringLiteral("4/4"));
        EXPECT_EQ(anchors[0].value(QStringLiteral("Battito")).toInt(),
                expectedBattito[offset])
                << "offset " << offset;
    }
}

TEST(CrateBarMarkersTest, ExportBattitoCarriesBarPhaseAcrossSection) {
    // Section of 6 beats: the trailing tempo anchor sits at beat index 6, which
    // is bar phase 3 at offset 0 -- proving the exported grid carries bar phase
    // rather than a constant 1.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto track = Track::newTemporary(dir.filePath(QStringLiteral("v.wav")));
    track->setDuration(120.0);
    const auto rate = mixxx::audio::SampleRate(48000);
    std::vector<mixxx::BeatMarker> markers{{mixxx::audio::FramePos(0), 6}};
    ASSERT_TRUE(track->trySetBeats(mixxx::Beats::fromBeatMarkers(
            rate, markers, mixxx::audio::FramePos(96000), mixxx::Bpm(120.0))));
    const QString out = dir.filePath(QStringLiteral("out.xml"));
    ASSERT_TRUE(mixxx::RekordboxXmlExport::write(
            {{QStringLiteral("V"), {track}}}, out, QStringLiteral("test"))
                        .ok);
    const auto anchors = tempos(out);
    ASSERT_EQ(anchors.size(), 2);
    EXPECT_EQ(anchors[0].value(QStringLiteral("Battito")).toInt(), 1);
    EXPECT_EQ(anchors[1].value(QStringLiteral("Battito")).toInt(), 3);
}

} // namespace
