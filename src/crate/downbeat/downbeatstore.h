#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSqlDatabase>

#include "track/trackid.h"

namespace crate {

// Process-wide store for the per-track downbeat offset (0..3) that positions
// 4/4 bar markers in the waveforms and the rekordbox export.
//
// The offset lives in an additive, Crate-owned table `crate_track_meta` in the
// main mixxxdb (created on demand; stock table schemas are never touched). The
// store owns an in-memory cache so the waveform renderers -- which only hold a
// TrackPointer and have no database handle -- can read the offset every frame
// without touching SQL. It is a singleton because those renderers and the
// per-deck ControlObject have no clean path to a TrackCollection.
//
// All access is on the GUI thread in production, but the cache is mutex-guarded
// because the render loop may read it from a render thread.
class DownbeatStore : public QObject {
    Q_OBJECT
  public:
    static DownbeatStore& instance();

    // Attach the mixxxdb connection, (re)create the table if needed and warm
    // the cache from it. Called once at startup; calling again reloads the
    // cache (used by tests to simulate a restart).
    void connectDatabase(const QSqlDatabase& database);
    void disconnectDatabase();

    // Downbeat offset in [0,3]; 0 for unset or invalid tracks.
    int offset(TrackId trackId) const;

    // Persist and cache a new offset (normalized to [0,3]). Emits offsetChanged.
    void setOffset(TrackId trackId, int offset);

    // Rotate the track's offset +1 (mod 4), persist it and return the new value.
    // This is the push semantics behind [ChannelN],crate_downbeat_shift.
    int rotateDownbeat(TrackId trackId);

  signals:
    void offsetChanged(TrackId trackId, int offset);

  private:
    DownbeatStore() = default;

    bool persist(int trackId, int offset);

    mutable QMutex m_mutex;
    QSqlDatabase m_database;
    QHash<int, int> m_cache;
};

} // namespace crate
