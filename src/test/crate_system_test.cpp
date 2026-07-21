// Tests for crate::SystemCrates: the reserved-crate engine behind DEMOTE and the
// TRIAGE inbox. Runs against a real in-memory library DB (LibraryTest), so the
// crate membership + datetime_added semantics are exercised end to end.

#include "crate/system/systemcrates.h"

#include <gtest/gtest.h>

#include <QDateTime>
#include <QSqlQuery>
#include <QTimeZone>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/crate.h"
#include "library/trackset/crate/cratestorage.h"
#include "test/librarytest.h"
#include "track/track.h"

class CrateSystemTest : public LibraryTest {
  protected:
    TrackId addTrack(const QString& name) {
        const auto pTrack = getOrAddTrackByLocation(getTestDataDir().filePath(name));
        EXPECT_TRUE(pTrack);
        return pTrack ? pTrack->getId() : TrackId();
    }
};

TEST_F(CrateSystemTest, ReservedNamesAreRecognized) {
    EXPECT_TRUE(crate::SystemCrates::isReservedCrateName(QStringLiteral("Demoted")));
    EXPECT_TRUE(crate::SystemCrates::isReservedCrateName(QStringLiteral("Reviewed")));
    EXPECT_FALSE(crate::SystemCrates::isReservedCrateName(QStringLiteral("House")));
}

TEST_F(CrateSystemTest, DemoteIsLazyIdempotentAndReversible) {
    auto* pCollection = internalCollection();
    crate::SystemCrates systemCrates(pCollection);

    const TrackId id1 = addTrack(QStringLiteral("one.mp3"));
    const TrackId id2 = addTrack(QStringLiteral("two.mp3"));
    ASSERT_TRUE(id1.isValid());
    ASSERT_TRUE(id2.isValid());

    // The reserved crate is created lazily: nothing exists until the first demote.
    EXPECT_FALSE(systemCrates.demotedCrateId(false).isValid());
    EXPECT_FALSE(pCollection->crates().readCrateByName(QStringLiteral("Demoted")));

    ASSERT_TRUE(systemCrates.setDemoted({id1}, true));
    EXPECT_TRUE(systemCrates.isDemoted(id1));
    EXPECT_FALSE(systemCrates.isDemoted(id2));
    EXPECT_EQ(systemCrates.demotedTrackIds(), QSet<TrackId>{id1});

    // Idempotent: demoting again does not double-add.
    ASSERT_TRUE(systemCrates.setDemoted({id1}, true));
    const CrateId demotedId = systemCrates.demotedCrateId(false);
    ASSERT_TRUE(demotedId.isValid());
    EXPECT_EQ(pCollection->crates().countCrateTracks(demotedId), 1U);

    // Restore removes membership; the crate itself may remain.
    ASSERT_TRUE(systemCrates.setDemoted({id1}, false));
    EXPECT_FALSE(systemCrates.isDemoted(id1));
    EXPECT_TRUE(systemCrates.demotedTrackIds().isEmpty());
}

TEST_F(CrateSystemTest, TriageNewTracksUnreviewedKeepClearsExistingStayReviewed) {
    crate::SystemCrates systemCrates(internalCollection());

    // Epoch captured before the tracks are added -> they count as "new".
    const QDateTime epoch = QDateTime::currentDateTimeUtc();
    const TrackId id1 = addTrack(QStringLiteral("one.mp3"));
    const TrackId id2 = addTrack(QStringLiteral("two.mp3"));
    ASSERT_TRUE(id1.isValid());
    ASSERT_TRUE(id2.isValid());

    // Both new tracks default to unreviewed, newest first (id DESC tiebreak).
    QList<TrackId> unreviewed = systemCrates.unreviewedTrackIds(epoch);
    ASSERT_EQ(unreviewed.size(), 2);
    EXPECT_TRUE(unreviewed.contains(id1));
    EXPECT_TRUE(unreviewed.contains(id2));

    // KEEP clears the flag durably; the kept track leaves triage, the other stays.
    ASSERT_TRUE(systemCrates.markReviewed({id1}));
    EXPECT_TRUE(systemCrates.isReviewed(id1));
    unreviewed = systemCrates.unreviewedTrackIds(epoch);
    ASSERT_EQ(unreviewed.size(), 1);
    EXPECT_EQ(unreviewed.first(), id2);

    // An epoch AFTER the tracks were added models "existing library on ship day":
    // nothing is unreviewed, so there is no day-one backlog.
    const QDateTime laterEpoch = QDateTime::currentDateTimeUtc().addDays(1);
    EXPECT_TRUE(systemCrates.unreviewedTrackIds(laterEpoch).isEmpty());
}

TEST_F(CrateSystemTest, UnreviewedHandlesMixedDatetimeAddedFormats) {
    // Real libraries hold BOTH datetime_added spellings: SQLite
    // CURRENT_TIMESTAMP ("yyyy-MM-dd hh:mm:ss") and Qt ISO with 'T'/'Z'
    // ("yyyy-MM-ddThh:mm:ss.zzzZ"). Raw string comparison made every ISO row
    // look newer than any epoch ('T' > ' '), flooding TRIAGE with old tracks.
    auto* pCollection = internalCollection();
    crate::SystemCrates systemCrates(pCollection);

    const TrackId oldIso = addTrack(QStringLiteral("one.mp3"));
    const TrackId newIso = addTrack(QStringLiteral("two.mp3"));
    ASSERT_TRUE(oldIso.isValid());
    ASSERT_TRUE(newIso.isValid());

    // Fixed SAME-DAY timestamps: the lexical bug only bites when the date
    // digits match and the first differing character is 'T' vs ' ' (index 10).
    // A different-day fixture passes even on broken code — do not "simplify".
    const QDateTime epoch(
            QDate(2026, 1, 15), QTime(12, 0, 0), QTimeZone::utc());
    QSqlQuery query(pCollection->database());
    // ISO-with-Z spelling, FOUR HOURS BEFORE the epoch, same date: must not
    // be unreviewed (raw string compare says it is, because 'T' > ' ').
    query.prepare(QStringLiteral(
            "UPDATE library SET datetime_added=:added WHERE id=:id"));
    query.bindValue(QStringLiteral(":added"),
            QStringLiteral("2026-01-15T08:00:00.000Z"));
    query.bindValue(QStringLiteral(":id"), oldIso.toVariant());
    ASSERT_TRUE(query.exec());
    // ISO-with-Z spelling, six hours AFTER the epoch: must be unreviewed.
    query.bindValue(QStringLiteral(":added"),
            QStringLiteral("2026-01-15T18:00:00.000Z"));
    query.bindValue(QStringLiteral(":id"), newIso.toVariant());
    ASSERT_TRUE(query.exec());

    const QList<TrackId> unreviewed = systemCrates.unreviewedTrackIds(epoch);
    EXPECT_FALSE(unreviewed.contains(oldIso));
    EXPECT_TRUE(unreviewed.contains(newIso));
}
