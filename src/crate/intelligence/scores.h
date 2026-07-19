#pragma once

#include <optional>

#include <QMap>
#include <QString>

namespace crate::scores {

QMap<QString, QString> camelotNeighbors(const QString& code);
double bpmScore(double a, double b, double tolerance = 0.08);
double keyScore(const QString& a, const QString& b);
double mixability(std::optional<double> sonic,
        bool keyAPresent,
        bool keyBPresent,
        double keyScore,
        bool bpmAPresent,
        bool bpmBPresent,
        double bpmScore,
        std::optional<double> transition);

} // namespace crate::scores
