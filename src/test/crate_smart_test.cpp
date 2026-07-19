#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>

#include "library/trackset/smartcrate/smartcratespec.h"
#include "library/trackset/smartcrate/smartcratestorage.h"

namespace {

using SmartCrate::WhereClause;

QJsonObject cond(const QString& field, const QString& op, const QJsonValue& value) {
    QJsonObject o;
    o.insert(QStringLiteral("field"), field);
    o.insert(QStringLiteral("op"), op);
    o.insert(QStringLiteral("value"), value);
    return o;
}

QJsonObject spec(const QString& match, const QJsonArray& conditions) {
    QJsonObject o;
    o.insert(QStringLiteral("match"), match);
    o.insert(QStringLiteral("conditions"), conditions);
    return o;
}

// Translate a single condition through the public spec entry point.
WhereClause one(const QJsonObject& c) {
    return SmartCrate::translate(spec(QStringLiteral("all"), QJsonArray{c}));
}

// ---- Translator: numeric fields ----------------------------------------------

TEST(CrateSmartTranslator, BpmBetween) {
    const WhereClause w = one(cond(QStringLiteral("bpm"),
            QStringLiteral("between"),
            QJsonArray{120, 128}));
    ASSERT_TRUE(w.isValid());
    EXPECT_EQ(w.sql, QStringLiteral("(bpm IS NOT NULL AND bpm BETWEEN ? AND ?)"));
    ASSERT_EQ(w.bindArgs.size(), 2);
    EXPECT_DOUBLE_EQ(w.bindArgs.at(0).toDouble(), 120.0);
    EXPECT_DOUBLE_EQ(w.bindArgs.at(1).toDouble(), 128.0);
}

TEST(CrateSmartTranslator, BpmGteAndLte) {
    const WhereClause gte = one(cond(QStringLiteral("bpm"), QStringLiteral(">="), 128));
    EXPECT_EQ(gte.sql, QStringLiteral("(bpm IS NOT NULL AND bpm >= ?)"));
    ASSERT_EQ(gte.bindArgs.size(), 1);
    EXPECT_DOUBLE_EQ(gte.bindArgs.at(0).toDouble(), 128.0);

    const WhereClause lte = one(cond(QStringLiteral("bpm"), QStringLiteral("lte"), 90));
    EXPECT_EQ(lte.sql, QStringLiteral("(bpm IS NOT NULL AND bpm <= ?)"));
    ASSERT_EQ(lte.bindArgs.size(), 1);
    EXPECT_DOUBLE_EQ(lte.bindArgs.at(0).toDouble(), 90.0);
}

TEST(CrateSmartTranslator, RatingIs) {
    const WhereClause w = one(cond(QStringLiteral("rating"), QStringLiteral("is"), 5));
    EXPECT_EQ(w.sql, QStringLiteral("(rating = ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_DOUBLE_EQ(w.bindArgs.at(0).toDouble(), 5.0);
}

TEST(CrateSmartTranslator, YearAndDurationAreNumeric) {
    const WhereClause year = one(cond(QStringLiteral("year"), QStringLiteral(">="), 2020));
    EXPECT_EQ(year.sql, QStringLiteral("(year IS NOT NULL AND year >= ?)"));
    const WhereClause dur = one(cond(QStringLiteral("duration"), QStringLiteral("<="), 300));
    EXPECT_EQ(dur.sql, QStringLiteral("(duration IS NOT NULL AND duration <= ?)"));
}

TEST(CrateSmartTranslator, NumericStringValueParsed) {
    const WhereClause w = one(cond(QStringLiteral("bpm"),
            QStringLiteral(">="),
            QStringLiteral("124")));
    ASSERT_TRUE(w.isValid());
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_DOUBLE_EQ(w.bindArgs.at(0).toDouble(), 124.0);
}

// ---- Translator: key / harmonic ----------------------------------------------

TEST(CrateSmartTranslator, KeyIsExact) {
    const WhereClause w = one(cond(QStringLiteral("key"),
            QStringLiteral("is"),
            QStringLiteral("8A")));
    EXPECT_EQ(w.sql, QStringLiteral("(key = ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_EQ(w.bindArgs.at(0).toString(), QStringLiteral("8A"));
}

TEST(CrateSmartTranslator, KeyHarmonicExpandsToCamelotNeighbors) {
    const WhereClause w = one(cond(QStringLiteral("key"),
            QStringLiteral("harmonic"),
            QStringLiteral("8A")));
    ASSERT_TRUE(w.isValid());
    // 4 Camelot-compatible keys: same (8A), relative major (8B), -1 (7A), +1 (9A).
    ASSERT_EQ(w.bindArgs.size(), 4);
    EXPECT_EQ(w.sql, QStringLiteral("(key IN (?,?,?,?))"));
    QStringList codes;
    for (const QVariant& v : w.bindArgs) {
        codes << v.toString();
    }
    EXPECT_TRUE(codes.contains(QStringLiteral("8A")));
    EXPECT_TRUE(codes.contains(QStringLiteral("8B")));
    EXPECT_TRUE(codes.contains(QStringLiteral("7A")));
    EXPECT_TRUE(codes.contains(QStringLiteral("9A")));
}

TEST(CrateSmartTranslator, KeyHarmonicWrapsAround) {
    // 12A neighbours wrap to 1A (+1) and 11A (-1) plus relative 12B.
    const WhereClause w = one(cond(QStringLiteral("key"),
            QStringLiteral("harmonic"),
            QStringLiteral("12A")));
    ASSERT_EQ(w.bindArgs.size(), 4);
    QStringList codes;
    for (const QVariant& v : w.bindArgs) {
        codes << v.toString();
    }
    EXPECT_TRUE(codes.contains(QStringLiteral("1A")));
    EXPECT_TRUE(codes.contains(QStringLiteral("11A")));
    EXPECT_TRUE(codes.contains(QStringLiteral("12B")));
}

TEST(CrateSmartTranslator, KeyHarmonicNonCamelotFallsBackToExact) {
    const WhereClause w = one(cond(QStringLiteral("key"),
            QStringLiteral("harmonic"),
            QStringLiteral("Cmaj")));
    ASSERT_TRUE(w.isValid());
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_EQ(w.bindArgs.at(0).toString(), QStringLiteral("Cmaj"));
    EXPECT_EQ(w.sql, QStringLiteral("(key IN (?))"));
}

// ---- Translator: text fields -------------------------------------------------

TEST(CrateSmartTranslator, ArtistContainsIsDefault) {
    const WhereClause w = one(cond(QStringLiteral("artist"),
            QStringLiteral("contains"),
            QStringLiteral("Aphex")));
    EXPECT_EQ(w.sql, QStringLiteral("(artist LIKE ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_EQ(w.bindArgs.at(0).toString(), QStringLiteral("%Aphex%"));
}

TEST(CrateSmartTranslator, ArtistIsExact) {
    const WhereClause w = one(cond(QStringLiteral("artist"),
            QStringLiteral("is"),
            QStringLiteral("Aphex Twin")));
    EXPECT_EQ(w.sql, QStringLiteral("(artist = ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_EQ(w.bindArgs.at(0).toString(), QStringLiteral("Aphex Twin"));
}

TEST(CrateSmartTranslator, TitleNotContains) {
    const WhereClause w = one(cond(QStringLiteral("title"),
            QStringLiteral("not_contains"),
            QStringLiteral("remix")));
    EXPECT_EQ(w.sql, QStringLiteral("(COALESCE(title,'') NOT LIKE ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
    EXPECT_EQ(w.bindArgs.at(0).toString(), QStringLiteral("%remix%"));
}

TEST(CrateSmartTranslator, FreeTextSpansArtistTitleAlbum) {
    const WhereClause w = one(cond(QStringLiteral("text"),
            QStringLiteral("contains"),
            QStringLiteral("acid")));
    EXPECT_EQ(w.sql, QStringLiteral("(artist LIKE ? OR title LIKE ? OR album LIKE ?)"));
    ASSERT_EQ(w.bindArgs.size(), 3);
    for (const QVariant& v : w.bindArgs) {
        EXPECT_EQ(v.toString(), QStringLiteral("%acid%"));
    }
}

// ---- Translator: malformed skip / combination / parameterization -------------

TEST(CrateSmartTranslator, MalformedConditionsAreSkipped) {
    // Unknown field, bad numeric op, non-numeric numeric value, empty key,
    // between with wrong arity -> all skipped, leaving nothing valid.
    QJsonArray conds{
            cond(QStringLiteral("energy"), QStringLiteral(">="), 5), // out-of-scope field
            cond(QStringLiteral("bpm"), QStringLiteral("contains"), 120), // bad op for numeric
            cond(QStringLiteral("bpm"), QStringLiteral(">="), QStringLiteral("fast")), // NaN
            cond(QStringLiteral("key"), QStringLiteral("is"), QStringLiteral("")), // empty
            cond(QStringLiteral("bpm"), QStringLiteral("between"), QJsonArray{120})}; // arity
    const WhereClause w = SmartCrate::translate(spec(QStringLiteral("all"), conds));
    EXPECT_FALSE(w.isValid());
    EXPECT_TRUE(w.sql.isEmpty());
    EXPECT_TRUE(w.bindArgs.isEmpty());
}

TEST(CrateSmartTranslator, ValidSurvivesAmongMalformed) {
    QJsonArray conds{
            cond(QStringLiteral("energy"), QStringLiteral(">="), 5), // skipped
            cond(QStringLiteral("bpm"), QStringLiteral(">="), 128)};  // kept
    const WhereClause w = SmartCrate::translate(spec(QStringLiteral("all"), conds));
    ASSERT_TRUE(w.isValid());
    EXPECT_EQ(w.sql, QStringLiteral("(bpm IS NOT NULL AND bpm >= ?)"));
    ASSERT_EQ(w.bindArgs.size(), 1);
}

TEST(CrateSmartTranslator, AllCombinesWithAnd) {
    QJsonArray conds{
            cond(QStringLiteral("bpm"), QStringLiteral(">="), 120),
            cond(QStringLiteral("rating"), QStringLiteral(">="), 4)};
    const WhereClause w = SmartCrate::translate(spec(QStringLiteral("all"), conds));
    EXPECT_EQ(w.sql,
            QStringLiteral("(bpm IS NOT NULL AND bpm >= ?) AND (rating IS NOT NULL AND rating >= ?)"));
    EXPECT_EQ(w.bindArgs.size(), 2);
}

TEST(CrateSmartTranslator, AnyCombinesWithOr) {
    QJsonArray conds{
            cond(QStringLiteral("bpm"), QStringLiteral(">="), 120),
            cond(QStringLiteral("rating"), QStringLiteral(">="), 4)};
    const WhereClause w = SmartCrate::translate(spec(QStringLiteral("any"), conds));
    EXPECT_EQ(w.sql,
            QStringLiteral("(bpm IS NOT NULL AND bpm >= ?) OR (rating IS NOT NULL AND rating >= ?)"));
}

TEST(CrateSmartTranslator, MissingMatchDefaultsToAll) {
    QJsonObject s;
    s.insert(QStringLiteral("conditions"),
            QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral(">="), 120),
                    cond(QStringLiteral("rating"), QStringLiteral(">="), 4)});
    const WhereClause w = SmartCrate::translate(s);
    EXPECT_TRUE(w.sql.contains(QStringLiteral(") AND (")));
}

TEST(CrateSmartTranslator, EmptySpecIsInvalid) {
    EXPECT_FALSE(SmartCrate::translate(QJsonObject{}).isValid());
    EXPECT_FALSE(SmartCrate::translate(spec(QStringLiteral("all"), QJsonArray{})).isValid());
}

TEST(CrateSmartTranslator, NoLiteralValuesInSqlText) {
    // Everything must be a "?" placeholder; no user value may leak into SQL text.
    QJsonArray conds{
            cond(QStringLiteral("bpm"), QStringLiteral("between"), QJsonArray{120, 128}),
            cond(QStringLiteral("artist"), QStringLiteral("contains"), QStringLiteral("Aphex")),
            cond(QStringLiteral("key"), QStringLiteral("harmonic"), QStringLiteral("8A")),
            cond(QStringLiteral("rating"), QStringLiteral("is"), 5)};
    const WhereClause w = SmartCrate::translate(spec(QStringLiteral("any"), conds));
    ASSERT_TRUE(w.isValid());
    EXPECT_FALSE(w.sql.contains(QStringLiteral("Aphex")));
    EXPECT_FALSE(w.sql.contains(QStringLiteral("8A")));
    EXPECT_FALSE(w.sql.contains(QStringLiteral("120")));
    EXPECT_FALSE(w.sql.contains(QStringLiteral("128")));
    // The only digit-bearing token that is legitimately in the numeric fragments
    // is the "5"? No -- rating value 5 is also a placeholder. Assert no bare 5.
    EXPECT_FALSE(w.sql.contains(QStringLiteral(" 5")));
}

// ---- Storage: round-trip -----------------------------------------------------

TEST(CrateSmartStorage, SaveLoadRoundTrip) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("smart_crates.json"));
    SmartCrate::Storage storage(path);

    SmartCrate::Def a;
    a.name = QStringLiteral("Peak time");
    a.created = 1700000000;
    a.spec = spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral("between"), QJsonArray{126, 132})});
    SmartCrate::Def b;
    b.name = QStringLiteral("Aphex only");
    b.created = 1700000001;
    b.spec = spec(QStringLiteral("any"),
            QJsonArray{cond(QStringLiteral("artist"), QStringLiteral("contains"), QStringLiteral("Aphex"))});

    ASSERT_TRUE(storage.saveAll({a, b}));
    ASSERT_TRUE(QFileInfo::exists(path));

    const QList<SmartCrate::Def> loaded = storage.loadAll();
    ASSERT_EQ(loaded.size(), 2);
    EXPECT_EQ(loaded.at(0).name, QStringLiteral("Peak time"));
    EXPECT_EQ(loaded.at(0).created, 1700000000);
    EXPECT_EQ(loaded.at(0).spec, a.spec);
    EXPECT_EQ(loaded.at(1).name, QStringLiteral("Aphex only"));
    EXPECT_EQ(loaded.at(1).spec, b.spec);
}

TEST(CrateSmartStorage, MissingFileLoadsEmpty) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    SmartCrate::Storage storage(QDir(dir.path()).filePath(QStringLiteral("nope.json")));
    EXPECT_TRUE(storage.loadAll().isEmpty());
}

TEST(CrateSmartStorage, MalformedFileLoadsEmpty) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("bad.json"));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{ not valid json ]");
    f.close();
    SmartCrate::Storage storage(path);
    EXPECT_TRUE(storage.loadAll().isEmpty());
}

TEST(CrateSmartStorage, SurvivesUnknownFutureKeys) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("future.json"));

    // Hand-write a file with unknown top-level, unknown per-entry, and unknown
    // in-spec keys, plus a future spec condition op we don't recognise yet.
    QJsonObject futureSpec;
    futureSpec.insert(QStringLiteral("match"), QStringLiteral("all"));
    futureSpec.insert(QStringLiteral("conditions"),
            QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral(">="), 120)});
    futureSpec.insert(QStringLiteral("future_spec_flag"), true); // unknown in-spec key

    QJsonObject entry;
    entry.insert(QStringLiteral("name"), QStringLiteral("Forward compatible"));
    entry.insert(QStringLiteral("created"), 1700000002.0);
    entry.insert(QStringLiteral("spec"), futureSpec);
    entry.insert(QStringLiteral("color"), QStringLiteral("#ff0000")); // unknown entry key

    QJsonObject root;
    root.insert(QStringLiteral("version"), 99); // unknown future version
    root.insert(QStringLiteral("smart_crates"), QJsonArray{entry});
    root.insert(QStringLiteral("telemetry"), QStringLiteral("whatever")); // unknown top key

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(root).toJson());
    f.close();

    SmartCrate::Storage storage(path);
    QList<SmartCrate::Def> loaded = storage.loadAll();
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_EQ(loaded.at(0).name, QStringLiteral("Forward compatible"));
    // The unknown in-spec key must survive verbatim.
    EXPECT_TRUE(loaded.at(0).spec.value(QStringLiteral("future_spec_flag")).toBool());

    // And a save/load cycle preserves that unknown in-spec key.
    ASSERT_TRUE(storage.saveAll(loaded));
    QList<SmartCrate::Def> reloaded = storage.loadAll();
    ASSERT_EQ(reloaded.size(), 1);
    EXPECT_TRUE(reloaded.at(0).spec.value(QStringLiteral("future_spec_flag")).toBool());
}

TEST(CrateSmartStorage, UpsertReplacesByNameAndRemoveDrops) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("smart_crates.json"));
    SmartCrate::Storage storage(path);

    ASSERT_TRUE(storage.upsert(QStringLiteral("Deep"),
            spec(QStringLiteral("all"),
                    QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral("<="), 122)})));
    ASSERT_TRUE(storage.upsert(QStringLiteral("Deep"),
            spec(QStringLiteral("all"),
                    QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral("<="), 120)})));
    QList<SmartCrate::Def> loaded = storage.loadAll();
    ASSERT_EQ(loaded.size(), 1); // replaced, not appended
    EXPECT_EQ(loaded.at(0).name, QStringLiteral("Deep"));

    ASSERT_TRUE(storage.remove(QStringLiteral("Deep")));
    EXPECT_TRUE(storage.loadAll().isEmpty());
}

// ---- End-to-end: translated WHERE actually resolves against a SQLite table ----

class CrateSmartResolve : public testing::Test {
  protected:
    void SetUp() override {
        m_conn = QStringLiteral("CRATE_SMART_RESOLVE_") + QUuid::createUuid().toString(QUuid::Id128);
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
        m_db.setDatabaseName(QStringLiteral(":memory:"));
        ASSERT_TRUE(m_db.open());
        QSqlQuery q(m_db);
        ASSERT_TRUE(q.exec(QStringLiteral(
                "CREATE TABLE library (id INTEGER PRIMARY KEY, artist TEXT, title TEXT, "
                "album TEXT, comment TEXT, bpm REAL, rating INTEGER, year TEXT, "
                "duration REAL, key TEXT)")))
                << qPrintable(q.lastError().text());
        insert(1, "Aphex Twin", "Windowlicker", "EP", 130, 5, "1999", "8A");
        insert(2, "Boards of Canada", "Roygbiv", "MHTRTC", 90, 4, "1998", "7A");
        insert(3, "Daft Punk", "Aerodynamic", "Discovery", 123, 3, "2001", "9A");
        insert(4, "Aphex Twin", "Xtal", "SAW 85-92", 100, 5, "1992", "8B");
    }

    void TearDown() override {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_conn);
    }

    void insert(int id,
            const char* artist,
            const char* title,
            const char* album,
            double bpm,
            int rating,
            const char* year,
            const char* key) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("INSERT INTO library "
                                 "(id, artist, title, album, comment, bpm, rating, year, duration, key) "
                                 "VALUES (?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(id);
        q.addBindValue(QString::fromUtf8(artist));
        q.addBindValue(QString::fromUtf8(title));
        q.addBindValue(QString::fromUtf8(album));
        q.addBindValue(QStringLiteral("comment"));
        q.addBindValue(bpm);
        q.addBindValue(rating);
        q.addBindValue(QString::fromUtf8(year));
        q.addBindValue(240.0);
        q.addBindValue(QString::fromUtf8(key));
        ASSERT_TRUE(q.exec()) << qPrintable(q.lastError().text());
    }

    QList<int> resolve(const QJsonObject& s) {
        const WhereClause w = SmartCrate::translate(s);
        QList<int> ids;
        if (!w.isValid()) {
            return ids; // no valid condition -> zero tracks, like v1
        }
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT id FROM library WHERE ") + w.sql +
                QStringLiteral(" ORDER BY id"));
        for (const QVariant& arg : w.bindArgs) {
            q.addBindValue(arg);
        }
        EXPECT_TRUE(q.exec()) << qPrintable(q.lastError().text()) << " sql=" << qPrintable(w.sql);
        while (q.next()) {
            ids.append(q.value(0).toInt());
        }
        return ids;
    }

    QString m_conn;
    QSqlDatabase m_db;
};

TEST_F(CrateSmartResolve, BpmBetweenSelectsRange) {
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral("between"), QJsonArray{120, 131})}));
    EXPECT_EQ(ids, (QList<int>{1, 3})); // 130 and 123
}

TEST_F(CrateSmartResolve, AllAndsConditions) {
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("artist"), QStringLiteral("contains"), QStringLiteral("Aphex")),
                    cond(QStringLiteral("rating"), QStringLiteral("is"), 5)}));
    EXPECT_EQ(ids, (QList<int>{1, 4}));
}

TEST_F(CrateSmartResolve, AnyOrsConditions) {
    const QList<int> ids = resolve(spec(QStringLiteral("any"),
            QJsonArray{cond(QStringLiteral("bpm"), QStringLiteral(">="), 129),
                    cond(QStringLiteral("artist"), QStringLiteral("is"), QStringLiteral("Daft Punk"))}));
    EXPECT_EQ(ids, (QList<int>{1, 3})); // Aphex 130 OR Daft Punk
}

TEST_F(CrateSmartResolve, HarmonicKeyMatchesNeighbors) {
    // Harmonic on 8A should match 8A (id1), 8B (id4), 7A (id2), 9A (id3) -> all four.
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("key"), QStringLiteral("harmonic"), QStringLiteral("8A"))}));
    EXPECT_EQ(ids, (QList<int>{1, 2, 3, 4}));
}

TEST_F(CrateSmartResolve, ExactKeyMatchesOne) {
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("key"), QStringLiteral("is"), QStringLiteral("8A"))}));
    EXPECT_EQ(ids, (QList<int>{1}));
}

TEST_F(CrateSmartResolve, NotContainsExcludes) {
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("title"), QStringLiteral("not_contains"), QStringLiteral("Xtal"))}));
    EXPECT_EQ(ids, (QList<int>{1, 2, 3})); // all but id4 "Xtal"
}

TEST_F(CrateSmartResolve, InvalidSpecResolvesEmpty) {
    const QList<int> ids = resolve(spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("energy"), QStringLiteral(">="), 5)}));
    EXPECT_TRUE(ids.isEmpty());
}

} // namespace
