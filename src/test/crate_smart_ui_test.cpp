// Tests for the smart-crate UI (wave-6): the SMART CRATES sidebar feature, the
// rule-editor dialog, and the live table model that re-resolves a spec against a
// real library DB. The pure spec/storage translator has its own suite in
// crate_smart_test.cpp; this file exercises the UI that consumes it.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

#include "library/treeitemmodel.h"
#include "library/trackset/smartcrate/dlgsmartcrateeditor.h"
#include "library/trackset/smartcrate/smartcratefeature.h"
#include "library/trackset/smartcrate/smartcratespec.h"
#include "library/trackset/smartcrate/smartcratestorage.h"
#include "library/trackset/smartcrate/smartcratetablemodel.h"
#include "preferences/usersettings.h"
#include "test/librarytest.h"
#include "track/track.h"

namespace {

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

// ---- Feature: lists the stored smart crates as sidebar children --------------

TEST(CrateSmartFeature, ListsStoredCratesFromStorageFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // A config whose settings dir IS the temp dir -> the feature reads
    // smart_crates.json from here.
    UserSettingsPointer pConfig(
            new UserSettings(dir.filePath(QStringLiteral("smart_test.cfg"))));

    // Seed two stored smart crates before the feature is constructed.
    SmartCrate::Storage storage(
            QDir(dir.path()).filePath(QStringLiteral("smart_crates.json")));
    SmartCrate::Def a;
    a.name = QStringLiteral("Peak time");
    a.spec = spec(QStringLiteral("all"),
            QJsonArray{cond(QStringLiteral("bpm"),
                    QStringLiteral("between"),
                    QJsonArray{126, 132})});
    SmartCrate::Def b;
    b.name = QStringLiteral("Aphex only");
    b.spec = spec(QStringLiteral("any"),
            QJsonArray{cond(QStringLiteral("artist"),
                    QStringLiteral("contains"),
                    QStringLiteral("Aphex"))});
    ASSERT_TRUE(storage.saveAll({a, b}));

    // No live Library needed to test the sidebar listing.
    SmartCrateFeature feature(nullptr, pConfig);
    TreeItemModel* pModel = feature.sidebarModel();
    ASSERT_NE(pModel, nullptr);
    ASSERT_EQ(pModel->rowCount(), 2);
    EXPECT_EQ(pModel->index(0, 0).data().toString(), QStringLiteral("Peak time"));
    EXPECT_EQ(pModel->index(1, 0).data().toString(), QStringLiteral("Aphex only"));
    EXPECT_FALSE(feature.title().toString().isEmpty());
}

// ---- Editor: build -> save -> reload reproduces the same spec JSON -----------

TEST(CrateSmartEditor, SpecRoundTrips) {
    DlgSmartCrateEditor dlg;
    dlg.setCrateName(QStringLiteral("Peak"));
    dlg.setMatchMode(QStringLiteral("any"));

    // The dialog starts with one blank row; configure it and add two more.
    dlg.configureRow(0,
            QStringLiteral("bpm"),
            QStringLiteral("between"),
            QStringLiteral("126, 132"));
    dlg.addConditionRow();
    dlg.configureRow(1,
            QStringLiteral("artist"),
            QStringLiteral("contains"),
            QStringLiteral("Aphex"));
    dlg.addConditionRow();
    dlg.configureRow(2,
            QStringLiteral("key"),
            QStringLiteral("harmonic"),
            QStringLiteral("8A"));

    const QJsonObject spec1 = dlg.buildSpec();
    EXPECT_EQ(spec1.value(QStringLiteral("match")).toString(), QStringLiteral("any"));
    EXPECT_EQ(spec1.value(QStringLiteral("conditions")).toArray().size(), 3);
    // The editor must only emit ops the translator actually supports.
    EXPECT_TRUE(SmartCrate::translate(spec1).isValid());

    // Reload into a fresh dialog and rebuild -> identical JSON.
    DlgSmartCrateEditor dlg2;
    dlg2.loadSpec(QStringLiteral("Peak"), spec1);
    const QJsonObject spec2 = dlg2.buildSpec();
    EXPECT_EQ(spec1, spec2);
    EXPECT_EQ(dlg2.crateName(), QStringLiteral("Peak"));
    EXPECT_EQ(dlg2.matchMode(), QStringLiteral("any"));
}

// ---- Editor: real control clicks for add-row / remove-row / save -------------

TEST(CrateSmartEditor, AddRemoveRowAndSaveClicks) {
    DlgSmartCrateEditor dlg;
    dlg.show();
    QApplication::processEvents();

    auto* pAdd = dlg.findChild<QPushButton*>(QStringLiteral("SmartCrateAddRow"));
    auto* pSave = dlg.findChild<QPushButton*>(QStringLiteral("SmartCrateSave"));
    ASSERT_NE(pAdd, nullptr);
    ASSERT_NE(pSave, nullptr);

    // One blank row to start; two clicks -> three rows.
    EXPECT_EQ(dlg.conditionRowCount(), 1);
    QTest::mouseClick(pAdd, Qt::LeftButton);
    QTest::mouseClick(pAdd, Qt::LeftButton);
    EXPECT_EQ(dlg.conditionRowCount(), 3);

    // Click a real per-row remove button -> two rows.
    QList<QPushButton*> removes =
            dlg.findChildren<QPushButton*>(QStringLiteral("SmartCrateRemoveRow"));
    ASSERT_EQ(removes.size(), 3);
    QTest::mouseClick(removes.last(), Qt::LeftButton);
    QApplication::processEvents();
    EXPECT_EQ(dlg.conditionRowCount(), 2);

    // Fill the two survivors and click Save -> accepted, spec has two rules.
    dlg.setCrateName(QStringLiteral("Loud & liked"));
    dlg.configureRow(0, QStringLiteral("bpm"), QStringLiteral(">="), QStringLiteral("120"));
    dlg.configureRow(1, QStringLiteral("rating"), QStringLiteral("is"), QStringLiteral("5"));
    QTest::mouseClick(pSave, Qt::LeftButton);
    EXPECT_EQ(dlg.result(), static_cast<int>(QDialog::Accepted));

    const QJsonObject built = dlg.buildSpec();
    EXPECT_EQ(built.value(QStringLiteral("conditions")).toArray().size(), 2);
    EXPECT_TRUE(SmartCrate::translate(built).isValid());
}

// ---- Model: a spec resolved against a real library returns exactly the matches -

class CrateSmartResolveModel : public LibraryTest {
  protected:
    // Add a real (supported) test file to the collection, then overwrite its
    // library-row fields to controlled values via direct SQL. The TrackPointer is
    // held for the fixture's lifetime so the cache never flushes over our values.
    int addTrack(const QString& file,
            const QString& artist,
            const QString& title,
            const QString& album,
            double bpm,
            int rating,
            const QString& year,
            const QString& key) {
        TrackPointer pTrack = getOrAddTrackByLocation(
                getTestDir().filePath(QStringLiteral("id3-test-data/") + file));
        VERIFY_OR_DEBUG_ASSERT(pTrack) {
            return -1;
        }
        m_held.append(pTrack);
        const TrackId id = pTrack->getId();

        QSqlQuery q(dbConnection());
        q.prepare(QStringLiteral(
                "UPDATE library SET artist=?, title=?, album=?, comment=?, "
                "bpm=?, rating=?, year=?, duration=?, key=? WHERE id=?"));
        q.addBindValue(artist);
        q.addBindValue(title);
        q.addBindValue(album);
        q.addBindValue(QStringLiteral("comment"));
        q.addBindValue(bpm);
        q.addBindValue(rating);
        q.addBindValue(year);
        q.addBindValue(240.0);
        q.addBindValue(key);
        q.addBindValue(id.toVariant());
        EXPECT_TRUE(q.exec()) << qPrintable(q.lastError().text());
        return id.toVariant().toInt();
    }

    void SetUp() override {
        m_a = addTrack(QStringLiteral("TOAL_TPE2.mp3"),
                QStringLiteral("Aphex Twin"), QStringLiteral("Windowlicker"),
                QStringLiteral("EP"), 130.0, 5, QStringLiteral("1999"), QStringLiteral("8A"));
        m_b = addTrack(QStringLiteral("all.mp3"),
                QStringLiteral("Boards of Canada"), QStringLiteral("Roygbiv"),
                QStringLiteral("MHTRTC"), 90.0, 4, QStringLiteral("1998"), QStringLiteral("7A"));
        m_c = addTrack(QStringLiteral("artist.mp3"),
                QStringLiteral("Daft Punk"), QStringLiteral("Aerodynamic"),
                QStringLiteral("Discovery"), 123.0, 3, QStringLiteral("2001"), QStringLiteral("9A"));
        m_d = addTrack(QStringLiteral("empty.mp3"),
                QStringLiteral("Aphex Twin"), QStringLiteral("Xtal"),
                QStringLiteral("SAW 85-92"), 100.0, 5, QStringLiteral("1992"), QStringLiteral("8B"));
        ASSERT_GT(m_a, 0);
        ASSERT_GT(m_b, 0);
        ASSERT_GT(m_c, 0);
        ASSERT_GT(m_d, 0);
    }

    QList<int> resolve(const QJsonObject& s) {
        SmartCrateTableModel model(nullptr, trackCollectionManager());
        model.selectSmartCrate(QStringLiteral("t"), s);
        model.select();
        QList<int> ids;
        for (int row = 0; row < model.rowCount(); ++row) {
            ids.append(model.getTrackId(model.index(row, 0)).toVariant().toInt());
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    QList<TrackPointer> m_held;
    int m_a = -1;
    int m_b = -1;
    int m_c = -1;
    int m_d = -1;
};

TEST_F(CrateSmartResolveModel, BpmBetweenSelectsRange) {
    QList<int> expected{m_a, m_c}; // 130 and 123
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(resolve(spec(QStringLiteral("all"),
                      QJsonArray{cond(QStringLiteral("bpm"),
                              QStringLiteral("between"),
                              QJsonArray{120, 131})})),
            expected);
}

TEST_F(CrateSmartResolveModel, AllAndsArtistAndRating) {
    QList<int> expected{m_a, m_d}; // both Aphex, both rating 5
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(resolve(spec(QStringLiteral("all"),
                      QJsonArray{cond(QStringLiteral("artist"),
                                         QStringLiteral("contains"),
                                         QStringLiteral("Aphex")),
                              cond(QStringLiteral("rating"), QStringLiteral("is"), 5)})),
            expected);
}

TEST_F(CrateSmartResolveModel, HarmonicKeyMatchesNeighbors) {
    QList<int> expected{m_a, m_b, m_c, m_d}; // 8A neighbours: 8A,8B,7A,9A
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(resolve(spec(QStringLiteral("all"),
                      QJsonArray{cond(QStringLiteral("key"),
                              QStringLiteral("harmonic"),
                              QStringLiteral("8A"))})),
            expected);
}

TEST_F(CrateSmartResolveModel, InvalidSpecResolvesEmpty) {
    EXPECT_TRUE(resolve(spec(QStringLiteral("all"),
                        QJsonArray{cond(QStringLiteral("energy"),
                                QStringLiteral(">="),
                                5)}))
                        .isEmpty());
}

} // namespace
