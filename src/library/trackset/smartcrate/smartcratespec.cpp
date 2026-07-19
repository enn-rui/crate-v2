#include "library/trackset/smartcrate/smartcratespec.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include "crate/intelligence/scores.h"

namespace SmartCrate {
namespace {

// Fields that map 1:1 to a numeric-comparable column in the Mixxx library table.
// (year is stored as TEXT in Mixxx but SQLite compares it numerically fine for
//  the 4-digit years we deal with; see the caveat in the commit message.)
const QSet<QString> kNumFields = {
        QStringLiteral("bpm"),
        QStringLiteral("rating"),
        QStringLiteral("year"),
        QStringLiteral("duration")};

// Text columns that exist directly on the Mixxx library table.
const QSet<QString> kTextFields = {
        QStringLiteral("artist"),
        QStringLiteral("title"),
        QStringLiteral("album"),
        QStringLiteral("comment")};

// Parse a JSON value as a double. Accepts a JSON number or a numeric string.
// Returns false (skip the condition) for anything non-numeric, mirroring v1's
// float(val) throwing -> None.
bool asDouble(const QJsonValue& value, double* pOut) {
    if (value.isDouble()) {
        *pOut = value.toDouble();
        return true;
    }
    if (value.isString()) {
        bool ok = false;
        const double parsed = value.toString().toDouble(&ok);
        if (ok) {
            *pOut = parsed;
            return true;
        }
    }
    return false;
}

// Coerce a JSON value to the string we compare/LIKE against. Numbers become
// their plain text so e.g. an artist typed as a number still works.
QString asString(const QJsonValue& value) {
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        // Avoid scientific notation / trailing zeros surprises for whole numbers.
        const double d = value.toDouble();
        if (d == static_cast<double>(static_cast<long long>(d))) {
            return QString::number(static_cast<long long>(d));
        }
        return QString::number(d);
    }
    return QString();
}

// SQL LIKE pattern for a "contains" match: %text%.
QString likePattern(const QString& text) {
    return QStringLiteral("%") + text + QStringLiteral("%");
}

} // namespace

bool translateCondition(const QJsonObject& condition,
        QString* pFragment,
        QList<QVariant>* pArgs) {
    const QString field = condition.value(QStringLiteral("field")).toString().toLower();
    const QString op = condition.value(QStringLiteral("op")).toString().toLower();
    const QJsonValue value = condition.value(QStringLiteral("value"));

    if (kNumFields.contains(field)) {
        // The column name is taken from a fixed whitelist, never from raw input.
        const QString col = field;
        if (op == QStringLiteral("between")) {
            if (!value.isArray()) {
                return false;
            }
            const QJsonArray arr = value.toArray();
            double lo = 0.0;
            double hi = 0.0;
            if (arr.size() != 2 || !asDouble(arr.at(0), &lo) || !asDouble(arr.at(1), &hi)) {
                return false;
            }
            *pFragment = QStringLiteral("(%1 IS NOT NULL AND %1 BETWEEN ? AND ?)").arg(col);
            pArgs->append(lo);
            pArgs->append(hi);
            return true;
        }
        double num = 0.0;
        if (!asDouble(value, &num)) {
            return false;
        }
        if (op == QStringLiteral(">=") || op == QStringLiteral("gte")) {
            *pFragment = QStringLiteral("(%1 IS NOT NULL AND %1 >= ?)").arg(col);
            pArgs->append(num);
            return true;
        }
        if (op == QStringLiteral("<=") || op == QStringLiteral("lte")) {
            *pFragment = QStringLiteral("(%1 IS NOT NULL AND %1 <= ?)").arg(col);
            pArgs->append(num);
            return true;
        }
        if (op == QStringLiteral("=") || op == QStringLiteral("is")) {
            *pFragment = QStringLiteral("(%1 = ?)").arg(col);
            pArgs->append(num);
            return true;
        }
        return false;
    }

    if (field == QStringLiteral("key")) {
        const QString key = asString(value);
        if (key.isEmpty()) {
            return false;
        }
        if (op == QStringLiteral("harmonic")) {
            // key + all Camelot-compatible neighbours (same key, relative
            // major/minor, +/-1). Reuses the vetted camelot logic in
            // crate/intelligence so harmonic maths stays in one place.
            QStringList neighbours = crate::scores::camelotNeighbors(key).keys();
            if (neighbours.isEmpty()) {
                // Not a Camelot code: fall back to an exact match on the value.
                neighbours.append(key);
            }
            QStringList placeholders;
            for (const QString& code : neighbours) {
                placeholders.append(QStringLiteral("?"));
                pArgs->append(code);
            }
            *pFragment = QStringLiteral("(key IN (%1))").arg(placeholders.join(QStringLiteral(",")));
            return true;
        }
        if (op == QStringLiteral("is") || op == QStringLiteral("=")) {
            *pFragment = QStringLiteral("(key = ?)");
            pArgs->append(key);
            return true;
        }
        return false;
    }

    if (kTextFields.contains(field)) {
        const QString text = asString(value);
        if (op == QStringLiteral("is") || op == QStringLiteral("=")) {
            *pFragment = QStringLiteral("(%1 = ?)").arg(field);
            pArgs->append(text);
            return true;
        }
        if (op == QStringLiteral("not_contains") || op == QStringLiteral("excludes")) {
            *pFragment = QStringLiteral("(COALESCE(%1,'') NOT LIKE ?)").arg(field);
            pArgs->append(likePattern(text));
            return true;
        }
        // default: contains
        *pFragment = QStringLiteral("(%1 LIKE ?)").arg(field);
        pArgs->append(likePattern(text));
        return true;
    }

    if (field == QStringLiteral("text")) {
        // free-text across artist + title + album
        const QString like = likePattern(asString(value));
        *pFragment = QStringLiteral("(artist LIKE ? OR title LIKE ? OR album LIKE ?)");
        pArgs->append(like);
        pArgs->append(like);
        pArgs->append(like);
        return true;
    }

    // Unknown field (v1's energy/danceability/lufs/bucket/tag/color live in the
    // sidecar and are intentionally out of scope this slice) -> skip.
    return false;
}

WhereClause translate(const QJsonObject& spec) {
    WhereClause clause;
    const QJsonArray conditions = spec.value(QStringLiteral("conditions")).toArray();
    QStringList fragments;
    for (const QJsonValue& raw : conditions) {
        if (!raw.isObject()) {
            continue;
        }
        QString fragment;
        QList<QVariant> condArgs;
        if (translateCondition(raw.toObject(), &fragment, &condArgs)) {
            fragments.append(fragment);
            clause.bindArgs.append(condArgs);
        }
    }
    if (fragments.isEmpty()) {
        clause.bindArgs.clear();
        return clause; // invalid: empty sql -> resolves to zero tracks
    }
    const QString joiner = spec.value(QStringLiteral("match")).toString() == QStringLiteral("any")
            ? QStringLiteral(" OR ")
            : QStringLiteral(" AND ");
    clause.sql = fragments.join(joiner);
    return clause;
}

} // namespace SmartCrate
