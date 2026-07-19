#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "crate/intelligence/scores.h"
#include "crate/intelligence/sonicvectors.h"

namespace {

struct Feature {
    double bpm = 0.0;
    QString key;
    bool hasBpm = false;
    bool hasKey = false;
};

QString goldenDir() {
    return QDir(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
            .absoluteFilePath(QStringLiteral("../../golden"));
}

double decodeDouble(const QString& hex) {
    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    EXPECT_EQ(bytes.size(), static_cast<int>(sizeof(double)));
    double value = 0.0;
    if (bytes.size() == static_cast<int>(sizeof(double))) {
        std::memcpy(&value, bytes.constData(), sizeof(value));
    }
    return value;
}

class CrateGoldenTest : public testing::Test {
  protected:
    static void SetUpTestSuite() {
        QFile file(QDir(goldenDir()).filePath(QStringLiteral("golden.json")));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << qPrintable(file.errorString());
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        ASSERT_EQ(error.error, QJsonParseError::NoError) << qPrintable(error.errorString());
        golden = document.object();

        const QString dbPath = QDir(goldenDir()).filePath(
                QStringLiteral("fixture_lib/.crate/features.sqlite"));
        const QString connection = QStringLiteral("CRATE_GOLDEN_FEATURES_") +
                QUuid::createUuid().toString(QUuid::Id128);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        ASSERT_TRUE(db.open()) << qPrintable(db.lastError().text());
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec(QStringLiteral(
                "SELECT relpath, bpm, key_camelot FROM features")));
        while (query.next()) {
            Feature feature;
            feature.hasBpm = !query.isNull(1) && query.value(1).toDouble() != 0.0;
            feature.bpm = query.value(1).toDouble();
            feature.hasKey = !query.isNull(2) && !query.value(2).toString().isEmpty();
            feature.key = query.value(2).toString();
            features.insert(query.value(0).toString(), feature);
        }
        query = QSqlQuery();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);

        ASSERT_TRUE(vectors.load(QDir(goldenDir()).filePath(
                QStringLiteral("fixture_lib/.crate/music_vectors.sqlite"))))
                << qPrintable(vectors.lastError());
    }

    static QJsonObject golden;
    static QHash<QString, Feature> features;
    static crate::SonicVectors vectors;
};

QJsonObject CrateGoldenTest::golden;
QHash<QString, Feature> CrateGoldenTest::features;
crate::SonicVectors CrateGoldenTest::vectors;

TEST_F(CrateGoldenTest, CenterVectorsAreBitExact) {
    const QJsonObject centers = golden.value(QStringLiteral("center")).toObject();
    ASSERT_EQ(centers.size(), 30);
    for (auto it = centers.begin(); it != centers.end(); ++it) {
        const crate::SonicVectors::Vector* actual = vectors.centered(it.key());
        ASSERT_NE(actual, nullptr) << qPrintable(it.key());
        const QByteArray expected = QByteArray::fromHex(it.value().toString().toLatin1());
        ASSERT_EQ(expected.size(), static_cast<int>(sizeof(*actual)));
        if (std::memcmp(actual->data(), expected.constData(), sizeof(*actual)) != 0) {
            int index = 0;
            while (index < crate::SonicVectors::kDimensions &&
                    std::memcmp(actual->data() + index,
                            expected.constData() + index * sizeof(float),
                            sizeof(float)) == 0) {
                ++index;
            }
            float wanted = 0.0f;
            std::memcpy(&wanted, expected.constData() + index * sizeof(float), sizeof(float));
            FAIL() << "first centered-vector mismatch: " << qPrintable(it.key())
                   << " index " << index << " expected " << wanted
                   << " actual " << (*actual)[index];
        }
    }
}

TEST_F(CrateGoldenTest, PairScoresMatchReference) {
    const QJsonObject sonic = golden.value(QStringLiteral("sonic")).toObject();
    const QJsonObject keys = golden.value(QStringLiteral("key_score")).toObject();
    const QJsonObject bpms = golden.value(QStringLiteral("bpm_score")).toObject();
    const QJsonObject transitions = golden.value(QStringLiteral("transition")).toObject();
    const QJsonObject mixes = golden.value(QStringLiteral("mixability")).toObject();
    ASSERT_EQ(sonic.size(), 870);
    for (auto it = sonic.begin(); it != sonic.end(); ++it) {
        const QStringList pair = it.key().split(QLatin1Char('|'));
        ASSERT_EQ(pair.size(), 2) << qPrintable(it.key());
        const Feature a = features.value(pair[0]);
        const Feature b = features.value(pair[1]);
        const auto sonicValue = vectors.cosine(pair[0], pair[1]);
        const auto transitionValue = vectors.transition(pair[0], pair[1]);
        const double keyValue = crate::scores::keyScore(a.key, b.key);
        const double bpmValue = crate::scores::bpmScore(a.bpm, b.bpm);

        if (it.value().isNull()) {
            EXPECT_FALSE(sonicValue) << qPrintable(it.key());
        } else {
            ASSERT_TRUE(sonicValue) << qPrintable(it.key());
            EXPECT_NEAR(*sonicValue, decodeDouble(it.value().toString()), 1e-6)
                    << qPrintable(it.key());
        }
        const QJsonValue transitionGolden = transitions.value(it.key());
        if (transitionGolden.isNull()) {
            EXPECT_FALSE(transitionValue) << qPrintable(it.key());
        } else {
            ASSERT_TRUE(transitionValue) << qPrintable(it.key());
            EXPECT_NEAR(*transitionValue, decodeDouble(transitionGolden.toString()), 1e-6)
                    << qPrintable(it.key());
        }
        EXPECT_NEAR(keyValue, decodeDouble(keys.value(it.key()).toString()), 1e-6)
                << qPrintable(it.key());
        EXPECT_NEAR(bpmValue, decodeDouble(bpms.value(it.key()).toString()), 1e-6)
                << qPrintable(it.key());
        const double mix = crate::scores::mixability(sonicValue,
                a.hasKey,
                b.hasKey,
                keyValue,
                a.hasBpm,
                b.hasBpm,
                bpmValue,
                transitionValue);
        EXPECT_NEAR(mix, decodeDouble(mixes.value(it.key()).toString()), 1e-6)
                << qPrintable(it.key());
    }
}

TEST_F(CrateGoldenTest, CamelotNeighborTablesAreExact) {
    const QJsonObject tables = golden.value(QStringLiteral("camelot")).toObject();
    for (auto table = tables.begin(); table != tables.end(); ++table) {
        const QString input = table.key() == QStringLiteral("<empty>") ? QString() : table.key();
        const QMap<QString, QString> actual = crate::scores::camelotNeighbors(input);
        const QJsonObject expected = table.value().toObject();
        ASSERT_EQ(actual.size(), expected.size()) << qPrintable(table.key());
        for (auto entry = expected.begin(); entry != expected.end(); ++entry) {
            EXPECT_EQ(actual.value(entry.key()), entry.value().toString())
                    << qPrintable(table.key()) << " -> " << qPrintable(entry.key());
        }
    }
}

} // namespace
