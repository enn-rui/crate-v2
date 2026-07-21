#include "crate/intelligence/suggestions.h"

#include <algorithm>
#include <cmath>

#include <QSet>

#include "crate/intelligence/scores.h"

namespace crate {
namespace {

double dot(const SonicVectors::Vector& a, const SonicVectors::Vector& b) {
    double result = 0.0;
    for (int i = 0; i < SonicVectors::kDimensions; ++i) {
        result += static_cast<double>(a[i]) * b[i];
    }
    return result;
}

SonicVectors::Vector centroid(const QVector<SuggestTrack>& members) {
    SonicVectors::Vector result{};
    for (const auto& member : members) {
        for (int i = 0; i < SonicVectors::kDimensions; ++i) {
            result[i] += member.vector[i];
        }
    }
    double norm = std::sqrt(std::max(0.0, dot(result, result)));
    if (norm > 1e-12) {
        for (float& value : result) {
            value = static_cast<float>(value / norm);
        }
    }
    return result;
}

double playability(const SuggestTrack& candidate,
        const QVector<SuggestTrack>& members) {
    // Use the closest sonic member as the musical hand-off reference. Ties are
    // harmless because the maximum compatibility across equally close members
    // is deterministic and avoids depending on input order.
    double closest = -2.0;
    double best = 0.0;
    for (const auto& member : members) {
        const double sonic = dot(candidate.vector, member.vector);
        const bool candidateHasKey = !candidate.keyCamelot.isEmpty();
        const bool memberHasKey = !member.keyCamelot.isEmpty();
        const bool candidateHasBpm = candidate.bpm > 0.0;
        const bool memberHasBpm = member.bpm > 0.0;
        const double compatibility = scores::mixability(1.0,
                candidateHasKey,
                memberHasKey,
                scores::keyScore(candidate.keyCamelot, member.keyCamelot),
                candidateHasBpm,
                memberHasBpm,
                scores::bpmScore(candidate.bpm, member.bpm),
                std::nullopt);
        if (sonic > closest + 1e-12) {
            closest = sonic;
            best = compatibility;
        } else if (std::abs(sonic - closest) <= 1e-12) {
            best = std::max(best, compatibility);
        }
    }
    return best;
}

} // namespace

QVector<Suggestion> Suggestions::rank(const QVector<SuggestTrack>& members,
        const QVector<SuggestTrack>& universe,
        SuggestMode mode,
        int limit) {
    if (members.isEmpty() || limit <= 0) {
        return {};
    }
    QSet<QString> memberPaths;
    for (const auto& member : members) {
        memberPaths.insert(member.relpath);
    }
    const auto center = centroid(members);
    if (dot(center, center) < 0.5) {
        return {};
    }

    QVector<Suggestion> result;
    result.reserve(universe.size());
    for (const auto& candidate : universe) {
        if (candidate.demoted || memberPaths.contains(candidate.relpath)) {
            continue;
        }
        const double sonic = dot(candidate.vector, center);
        double score = sonic;
        if (mode == SuggestMode::Mix) {
            score *= playability(candidate, members);
        } else if (mode == SuggestMode::Gap) {
            double nearest = -2.0;
            for (const auto& member : members) {
                nearest = std::max(nearest, dot(candidate.vector, member.vector));
            }
            score -= nearest;
        }
        if (std::isfinite(score)) {
            result.append({candidate.relpath, score});
        }
    }
    std::sort(result.begin(), result.end(), [](const Suggestion& a, const Suggestion& b) {
        if (std::abs(a.score - b.score) > 1e-12) {
            return a.score > b.score;
        }
        return a.relpath < b.relpath;
    });
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

} // namespace crate
