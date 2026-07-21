#include "crate/downbeat/downbeatstore.h"

#include <QMutexLocker>
#include <QSqlError>
#include <QSqlQuery>

#include "crate/downbeat/barphase.h"
#include "moc_downbeatstore.cpp"
#include "util/logger.h"

namespace {
mixxx::Logger kLogger("DownbeatStore");

const QString kCreateTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS crate_track_meta ("
        "track_id INTEGER PRIMARY KEY, "
        "downbeat_offset INTEGER)");
} // namespace

namespace crate {

DownbeatStore& DownbeatStore::instance() {
    static DownbeatStore s_instance;
    return s_instance;
}

void DownbeatStore::connectDatabase(const QSqlDatabase& database) {
    QMutexLocker locked(&m_mutex);
    m_database = database;
    m_cache.clear();
    if (!m_database.isOpen()) {
        return;
    }
    QSqlQuery create(m_database);
    if (!create.exec(kCreateTable)) {
        kLogger.warning() << "Failed to create crate_track_meta:"
                          << create.lastError();
        return;
    }
    QSqlQuery load(m_database);
    if (!load.exec(QStringLiteral(
                "SELECT track_id, downbeat_offset FROM crate_track_meta"))) {
        kLogger.warning() << "Failed to load crate_track_meta:"
                          << load.lastError();
        return;
    }
    while (load.next()) {
        m_cache.insert(load.value(0).toInt(),
                normalizeDownbeatOffset(load.value(1).toInt()));
    }
}

void DownbeatStore::disconnectDatabase() {
    QMutexLocker locked(&m_mutex);
    m_database = QSqlDatabase();
    m_cache.clear();
}

int DownbeatStore::offset(TrackId trackId) const {
    if (!trackId.isValid()) {
        return 0;
    }
    QMutexLocker locked(&m_mutex);
    return m_cache.value(trackId.toVariant().toInt(), 0);
}

bool DownbeatStore::persist(int trackId, int offset) {
    if (!m_database.isOpen()) {
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral(
                "REPLACE INTO crate_track_meta (track_id, downbeat_offset) "
                "VALUES (:id, :offset)"))) {
        kLogger.warning() << "Failed to prepare crate_track_meta upsert:"
                          << query.lastError();
        return false;
    }
    query.bindValue(QStringLiteral(":id"), trackId);
    query.bindValue(QStringLiteral(":offset"), offset);
    if (!query.exec()) {
        kLogger.warning() << "Failed to persist downbeat offset:"
                          << query.lastError();
        return false;
    }
    return true;
}

void DownbeatStore::setOffset(TrackId trackId, int offset) {
    if (!trackId.isValid()) {
        return;
    }
    const int id = trackId.toVariant().toInt();
    const int normalized = normalizeDownbeatOffset(offset);
    {
        QMutexLocker locked(&m_mutex);
        m_cache.insert(id, normalized);
        persist(id, normalized);
    }
    emit offsetChanged(trackId, normalized);
}

int DownbeatStore::rotateDownbeat(TrackId trackId) {
    if (!trackId.isValid()) {
        return 0;
    }
    const int id = trackId.toVariant().toInt();
    int next;
    {
        QMutexLocker locked(&m_mutex);
        next = normalizeDownbeatOffset(m_cache.value(id, 0) + 1);
        m_cache.insert(id, next);
        persist(id, next);
    }
    emit offsetChanged(trackId, next);
    return next;
}

} // namespace crate
