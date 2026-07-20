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

inline QList<AnalyzerScheduledTrack> allLibraryTracks(const QSqlDatabase& database) {
    QList<AnalyzerScheduledTrack> tracks;
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
            "SELECT library.id FROM library "
            "INNER JOIN track_locations ON library.location=track_locations.id "
            "WHERE mixxx_deleted=0 AND fs_deleted=0"));
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
            ? allLibraryTracks(database)
            : QList<AnalyzerScheduledTrack>();
}

} // namespace crate
