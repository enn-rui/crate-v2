#include "crate/intelligence/scores.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace crate::scores {
namespace {
struct Camelot {
    int number;
    QChar letter;
};

std::optional<Camelot> parseCamelot(const QString& code) {
    if (code.size() < 2) {
        return std::nullopt;
    }
    bool ok = false;
    const int number = code.left(code.size() - 1).toInt(&ok);
    const QChar letter = code.back().toUpper();
    if (!ok || number < 1 || number > 12 ||
            (letter != QLatin1Char('A') && letter != QLatin1Char('B'))) {
        return std::nullopt;
    }
    return Camelot{number, letter};
}

QString code(int number, QChar letter) {
    return QString::number((number - 1 + 12) % 12 + 1) + letter;
}
} // namespace

QMap<QString, QString> camelotNeighbors(const QString& value) {
    const auto parsed = parseCamelot(value);
    if (!parsed) {
        return {};
    }
    const QChar other = parsed->letter == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
    return {{code(parsed->number, parsed->letter), QStringLiteral("same key")},
            {code(parsed->number, other), QStringLiteral("relative major/minor")},
            {code(parsed->number - 1, parsed->letter), QStringLiteral("-1 (energy down)")},
            {code(parsed->number + 1, parsed->letter), QStringLiteral("+1 (energy up)")}};
}

double bpmScore(double a, double b, double tolerance) {
    if (!std::isfinite(a) || !std::isfinite(b) || a <= 0.0 || b <= 0.0) {
        return 0.0;
    }
    double best = std::numeric_limits<double>::infinity();
    for (const double target : {b, b / 2.0, b * 2.0}) {
        best = std::min(best, std::abs(a - target) / target);
    }
    return best <= tolerance ? std::max(0.0, 1.0 - best / tolerance) : 0.0;
}

double keyScore(const QString& a, const QString& b) {
    if (a.isEmpty() || b.isEmpty()) {
        return 0.0;
    }
    if (b == a) {
        return 1.0;
    }
    const QString relation = camelotNeighbors(a).value(b);
    if (relation == QStringLiteral("relative major/minor")) {
        return 0.85;
    }
    if (relation == QStringLiteral("-1 (energy down)") ||
            relation == QStringLiteral("+1 (energy up)")) {
        return 0.8;
    }
    return 0.0;
}

double mixability(std::optional<double> sonic,
        bool keyAPresent,
        bool keyBPresent,
        double keyS,
        bool bpmAPresent,
        bool bpmBPresent,
        double bpmS,
        std::optional<double> transition) {
    const double transitionFactor = transition ? 0.88 + 0.12 * *transition : 1.0;
    if (!sonic) {
        return 0.5 * (keyS + bpmS) * transitionFactor;
    }
    const double keyFactor = keyAPresent && keyBPresent ? 0.35 + 0.65 * keyS : 0.80;
    const double bpmFactor = bpmAPresent && bpmBPresent ? 0.25 + 0.75 * bpmS : 0.75;
    return *sonic * keyFactor * bpmFactor * transitionFactor;
}

} // namespace crate::scores
