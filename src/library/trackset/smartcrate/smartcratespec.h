#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariant>

// Smart crates (rule-based, dynamic playlists) ported from the v1 Crate tool
// (apps/crate/library.py _smart_condition_sql / evaluate_smart_crate).
//
// A smart crate is a saved rule SPEC, not a folder of files. It resolves LIVE
// against the Mixxx library table every time it is opened (rekordbox
// "Intelligent Playlist" model). The spec shape mirrors v1 exactly so a future
// v1 importer is trivial:
//   { "match": "all"|"any",
//     "conditions": [ { "field": ..., "op": ..., "value": ... }, ... ] }
//
// This module is a pure, unit-testable translator: spec -> parameterized SQL
// WHERE fragment + ordered bind args. It never touches the filesystem, GUI, or a
// live database, and values NEVER appear in the SQL text (everything is a "?"
// placeholder). Column names come from fixed whitelists, never from user input.
namespace SmartCrate {

// The parameterized WHERE fragment produced from a spec. `sql` is the fragment
// WITHOUT the leading "WHERE" (each condition is parenthesised and combined with
// AND / OR); `bindArgs` are the values in positional order for the "?"
// placeholders. `isValid()` is false when the spec had no usable condition.
struct WhereClause {
    QString sql;
    QList<QVariant> bindArgs;

    bool isValid() const {
        return !sql.isEmpty();
    }
};

// Translate one condition object into (fragment, args). Returns false and leaves
// the outputs untouched for a malformed / unsupported condition (mirrors v1:
// malformed conditions are skipped, never fatal).
bool translateCondition(const QJsonObject& condition,
        QString* pFragment,
        QList<QVariant>* pArgs);

// Translate a whole spec. "any" OR-combines the conditions, anything else (incl.
// missing) AND-combines them. A spec with no usable condition yields an invalid
// WhereClause (empty sql) -> the feature should resolve to zero tracks, exactly
// like v1's evaluate_smart_crate returning [].
WhereClause translate(const QJsonObject& spec);

} // namespace SmartCrate
