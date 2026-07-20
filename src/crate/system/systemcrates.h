#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>

#include "library/trackset/crate/crateid.h"
#include "track/trackid.h"

class TrackCollection;

namespace crate {

// Crate v2 rotation/triage state lives in ordinary Mixxx crates (zero schema
// migration, following the fork's "keep new state out of the versioned schema"
// precedent). Two reserved crates are special-cased by name and hidden from the
// normal crate list:
//   - "Demoted"  : tracks pulled out of rotation. Excluded from the galaxy
//                  entirely but still searchable/loadable in the library.
//   - "Reviewed" : tracks the user has KEEP-ed out of the TRIAGE inbox. A track
//                  is "unreviewed" iff it entered the library at/after the triage
//                  epoch AND is not a member of this crate (so KEEP is a durable
//                  per-track flag with no insertion hook and no day-one backlog).
//
// SystemCrates is a thin, idempotent wrapper over the existing crate storage
// APIs so both the galaxy and the track-table menu drive the same state.
class SystemCrates {
  public:
    static QString demotedCrateName();
    static QString reviewedCrateName();
    // True for any crate name Crate v2 reserves for internal state. Used to hide
    // these crates from the normal crate list UI.
    static bool isReservedCrateName(const QString& name);

    explicit SystemCrates(TrackCollection* pCollection)
            : m_pCollection(pCollection) {
    }

    // Get-or-create the reserved crate idempotently. With createIfMissing=false
    // returns an invalid CrateId when the crate does not exist yet (no write).
    CrateId demotedCrateId(bool createIfMissing = true) const;
    CrateId reviewedCrateId(bool createIfMissing = true) const;

    // Demote / restore. Idempotent: adding uses INSERT OR IGNORE, removing a
    // non-member is a no-op. Returns false only on a storage failure.
    bool setDemoted(const QList<TrackId>& trackIds, bool demoted) const;
    bool isDemoted(TrackId trackId) const;
    QSet<TrackId> demotedTrackIds() const;

    // TRIAGE. markReviewed = KEEP (durable). isReviewed reflects membership only;
    // "unreviewed" combines this with the triage epoch (see unreviewedTrackIds).
    bool markReviewed(const QList<TrackId>& trackIds) const;
    bool isReviewed(TrackId trackId) const;
    QSet<TrackId> reviewedTrackIds() const;

    // Track ids that are unreviewed as of `since`: entered the library at/after
    // `since` and are not in the Reviewed crate, newest first. Existing tracks
    // (added before the epoch) are never returned, so shipping the feature does
    // not create a day-one backlog.
    QList<TrackId> unreviewedTrackIds(const QDateTime& since) const;

  private:
    CrateId getOrCreate(const QString& name, bool createIfMissing) const;
    bool inCrate(CrateId crateId, TrackId trackId) const;
    QSet<TrackId> crateTrackIds(CrateId crateId) const;

    TrackCollection* m_pCollection;
};

} // namespace crate
