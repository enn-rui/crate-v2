#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

#include "analyzer/analyzerscheduledtrack.h"
#include "preferences/usersettings.h"
#include "util/math.h"

namespace crate {

inline bool autoAnalyzeEnabled(const UserSettingsPointer& pConfig) {
    return pConfig->getValue(ConfigKey("[Crate]", "auto_analyze"), 1) != 0;
}

inline int analyzerThreadCount(
        const UserSettingsPointer& pConfig,
        int idealThreadCount = QThread::idealThreadCount()) {
    const ConfigKey key("[Crate]", "analyzer_threads");
    if (pConfig->exists(key)) {
        return math_max(1, pConfig->getValue(key, 1));
    }
    return math_max(1, idealThreadCount / 2);
}

// Only tracks actually missing analysis results. Enqueueing the whole library
// and letting AnalyzerBeats::shouldAnalyze skip per-track was correct but not
// free: every launch loaded every Track object and churned the analysis queue
// through N "skip" decisions — on a fully-analyzed library that read as
// "it reanalyzes all of my tracks every time". A library with nothing missing
// enqueues nothing and startup stays quiet.
inline QList<AnalyzerScheduledTrack> unanalyzedLibraryTracks(
        const QSqlDatabase& database) {
    QList<AnalyzerScheduledTrack> tracks;
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
            "SELECT library.id FROM library "
            "INNER JOIN track_locations ON library.location=track_locations.id "
            "WHERE mixxx_deleted=0 AND fs_deleted=0 "
            "AND (library.bpm IS NULL OR library.bpm <= 0 "
            "OR library.key IS NULL OR library.key = '')"));
    if (!query.exec()) {
        return tracks;
    }
    while (query.next()) {
        tracks.append(TrackId(query.value(0)));
    }
    return tracks;
}

inline QList<AnalyzerScheduledTrack> autoAnalyzeTracks(
        const UserSettingsPointer& pConfig,
        const QSqlDatabase& database) {
    return autoAnalyzeEnabled(pConfig)
            ? unanalyzedLibraryTracks(database)
            : QList<AnalyzerScheduledTrack>();
}

} // namespace crate
