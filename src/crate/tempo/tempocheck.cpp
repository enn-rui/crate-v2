#include "crate/tempo/tempocheck.h"

#include <cmath>
#include <algorithm>

#include "track/track.h"

namespace {

enum class Match { None, Halve, Double };

Match octaveMatch(double bpm, double reference) {
    if (!(bpm > 0.0) || !(reference > 0.0) ||
            !std::isfinite(bpm) || !std::isfinite(reference)) {
        return Match::None;
    }
    const double ratio = bpm / reference;
    if (std::abs(ratio - 2.0) / 2.0 <= 0.04) {
        return Match::Halve;
    }
    if (std::abs(ratio - 0.5) / 0.5 <= 0.04) {
        return Match::Double;
    }
    return Match::None;
}

} // namespace

namespace crate {

TempoResult evaluateTempo(const TempoProbe& probe) {
    TempoResult result;
    result.probe = probe;
    result.oldBpm = probe.bpm;
    QVector<Match> matches;
    if (probe.sidecarBpm) {
        matches.append(octaveMatch(probe.bpm, *probe.sidecarBpm));
    }
    if (probe.clusterMedianBpm) {
        matches.append(octaveMatch(probe.bpm, *probe.clusterMedianBpm));
    }
    if (matches.isEmpty()) {
        return result;
    }
    const auto close = [&probe](double reference) {
        return reference > 0.0 &&
                std::abs(probe.bpm / reference - 1.0) <= 0.04;
    };
    const bool allClose = (!probe.sidecarBpm || close(*probe.sidecarBpm)) &&
            (!probe.clusterMedianBpm || close(*probe.clusterMedianBpm));
    if (allClose) {
        return result;
    }
    const Match first = matches.first();
    const bool allAgree = first != Match::None &&
            std::all_of(matches.cbegin(), matches.cend(),
                    [first](Match match) { return match == first; });
    if (allAgree) {
        result.verdict = first == Match::Halve
                ? TempoVerdict::FixHalve
                : TempoVerdict::FixDouble;
        result.newBpm = first == Match::Halve ? probe.bpm * 0.5 : probe.bpm * 2.0;
        result.reason = QStringLiteral("clean octave mismatch");
        return result;
    }
    result.verdict = TempoVerdict::Suspect;
    result.reason = matches.size() > 1
            ? QStringLiteral("tempo references disagree")
            : QStringLiteral("tempo differs from its reference");
    return result;
}

bool scaleTrackTempo(const TrackPointer& track, mixxx::Beats::BpmScale scale) {
    if (!track || track->isBpmLocked()) {
        return false;
    }
    const mixxx::BeatsPointer beats = track->getBeats();
    if (!beats) {
        return false;
    }
    const auto scaled = beats->tryScale(scale);
    return scaled && track->trySetBeats(*scaled);
}

} // namespace crate
