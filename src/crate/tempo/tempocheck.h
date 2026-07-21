#pragma once

#include <optional>

#include <QString>
#include <QVector>

#include "track/beats.h"
#include "track/track_decl.h"

namespace crate {

struct TempoProbe {
    QString relpath;
    QString artist;
    QString title;
    double bpm = 0.0;
    std::optional<double> sidecarBpm;
    std::optional<double> clusterMedianBpm;
    TrackPointer track;
};

enum class TempoVerdict { Consistent, FixHalve, FixDouble, Suspect };

struct TempoResult {
    TempoProbe probe;
    TempoVerdict verdict = TempoVerdict::Consistent;
    double oldBpm = 0.0;
    double newBpm = 0.0;
    QString reason;
};

TempoResult evaluateTempo(const TempoProbe& probe);
bool scaleTrackTempo(const TrackPointer& track, mixxx::Beats::BpmScale scale);

} // namespace crate
