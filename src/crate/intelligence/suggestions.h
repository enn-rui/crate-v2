#pragma once

#include <QString>
#include <QVector>

#include "crate/intelligence/sonicvectors.h"

namespace crate {

enum class SuggestMode {
    Sound,
    Mix,
    Gap,
};

struct SuggestTrack {
    QString relpath;
    SonicVectors::Vector vector{};
    QString keyCamelot;
    double bpm = 0.0;
    bool demoted = false;
};

struct Suggestion {
    QString relpath;
    double score = 0.0;
};

class Suggestions {
  public:
    static constexpr int kDefaultLimit = 20;

    static QVector<Suggestion> rank(const QVector<SuggestTrack>& members,
            const QVector<SuggestTrack>& universe,
            SuggestMode mode,
            int limit = kDefaultLimit);
};

} // namespace crate
