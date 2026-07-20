#include "crate/system/systemcrates.h"

#include <QSqlError>
#include <QSqlQuery>

#include "library/trackcollection.h"
#include "library/trackset/crate/crate.h"
#include "library/trackset/crate/cratestorage.h"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("SystemCrates");
} // namespace

namespace crate {

QString SystemCrates::demotedCrateName() {
    return QStringLiteral("Demoted");
}

QString SystemCrates::reviewedCrateName() {
    return QStringLiteral("Reviewed");
}

bool SystemCrates::isReservedCrateName(const QString& name) {
    return name == demotedCrateName() || name == reviewedCrateName();
}

CrateId SystemCrates::getOrCreate(const QString& name, bool createIfMissing) const {
    if (m_pCollection == nullptr) {
        return CrateId();
    }
    Crate crate;
    if (m_pCollection->crates().readCrateByName(name, &crate)) {
        return crate.getId();
    }
    if (!createIfMissing) {
        return CrateId();
    }
    Crate toCreate;
    toCreate.setName(name);
    CrateId createdId;
    if (!m_pCollection->insertCrate(toCreate, &createdId)) {
        kLogger.warning() << "failed to create reserved crate" << name;
        return CrateId();
    }
    return createdId;
}

CrateId SystemCrates::demotedCrateId(bool createIfMissing) const {
    return getOrCreate(demotedCrateName(), createIfMissing);
}

CrateId SystemCrates::reviewedCrateId(bool createIfMissing) const {
    return getOrCreate(reviewedCrateName(), createIfMissing);
}

QSet<TrackId> SystemCrates::crateTrackIds(CrateId crateId) const {
    QSet<TrackId> ids;
    if (m_pCollection == nullptr || !crateId.isValid()) {
        return ids;
    }
    CrateTrackSelectResult result =
            m_pCollection->crates().selectCrateTracksSorted(crateId);
    while (result.next()) {
        ids.insert(result.trackId());
    }
    return ids;
}

bool SystemCrates::inCrate(CrateId crateId, TrackId trackId) const {
    if (m_pCollection == nullptr || !crateId.isValid() || !trackId.isValid()) {
        return false;
    }
    CrateTrackSelectResult result =
            m_pCollection->crates().selectTrackCratesSorted(trackId);
    while (result.next()) {
        if (result.crateId() == crateId) {
            return true;
        }
    }
    return false;
}

bool SystemCrates::setDemoted(const QList<TrackId>& trackIds, bool demoted) const {
    if (m_pCollection == nullptr || trackIds.isEmpty()) {
        return false;
    }
    // Only materialize the crate when demoting; a restore against a missing
    // crate is a no-op (nothing was ever demoted).
    const CrateId crateId = demotedCrateId(demoted);
    if (!crateId.isValid()) {
        return !demoted;
    }
    return demoted ? m_pCollection->addCrateTracks(crateId, trackIds)
                   : m_pCollection->removeCrateTracks(crateId, trackIds);
}

bool SystemCrates::isDemoted(TrackId trackId) const {
    return inCrate(demotedCrateId(false), trackId);
}

QSet<TrackId> SystemCrates::demotedTrackIds() const {
    return crateTrackIds(demotedCrateId(false));
}

bool SystemCrates::markReviewed(const QList<TrackId>& trackIds) const {
    if (m_pCollection == nullptr || trackIds.isEmpty()) {
        return false;
    }
    const CrateId crateId = reviewedCrateId(true);
    if (!crateId.isValid()) {
        return false;
    }
    return m_pCollection->addCrateTracks(crateId, trackIds);
}

bool SystemCrates::isReviewed(TrackId trackId) const {
    return inCrate(reviewedCrateId(false), trackId);
}

QSet<TrackId> SystemCrates::reviewedTrackIds() const {
    return crateTrackIds(reviewedCrateId(false));
}

QList<TrackId> SystemCrates::unreviewedTrackIds(const QDateTime& since) const {
    QList<TrackId> ids;
    if (m_pCollection == nullptr || !since.isValid()) {
        return ids;
    }
    // datetime_added is a SQLite CURRENT_TIMESTAMP string in UTC; compare against
    // the epoch spelled the same way so lexical ordering matches chronology.
    const QString sinceText =
            since.toUTC().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    const CrateId reviewedId = reviewedCrateId(false);
    QString sql = QStringLiteral(
            "SELECT id FROM library "
            "WHERE datetime_added >= :since AND mixxx_deleted = 0");
    if (reviewedId.isValid()) {
        sql += QStringLiteral(" AND id NOT IN (%1)")
                       .arg(CrateStorage::formatSubselectQueryForCrateTrackIds(
                               reviewedId));
    }
    sql += QStringLiteral(" ORDER BY datetime_added DESC, id DESC");

    QSqlQuery query(m_pCollection->database());
    query.prepare(sql);
    query.bindValue(QStringLiteral(":since"), sinceText);
    if (!query.exec()) {
        kLogger.warning() << "unreviewed query failed:" << query.lastError();
        return ids;
    }
    while (query.next()) {
        const TrackId trackId(query.value(0));
        if (trackId.isValid()) {
            ids.append(trackId);
        }
    }
    return ids;
}

} // namespace crate
