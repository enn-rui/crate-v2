#include <gtest/gtest.h>

#include <QLabel>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QToolButton>

#include "crate/suggestions/suggestfeature.h"
#include "crate/suggestions/wsuggestview.h"
#include "library/librarytablemodel.h"
#include "library/trackcollection.h"
#include "library/trackset/crate/crate.h"
#include "library/trackset/crate/cratestorage.h"
#include "library/trackset/crate/cratetablemodel.h"
#include "preferences/usersettings.h"
#include "test/librarytest.h"
#include "track/track.h"
#include "widget/wlibrary.h"

namespace {

UserSettingsPointer makeConfig(QTemporaryDir& dir) {
    return UserSettingsPointer(
            new UserSettings(dir.filePath(QStringLiteral("suggest_test.cfg"))));
}

class CrateSuggestViewTest : public LibraryTest {
  protected:
    CrateId makeCrate(const QString& name) {
        Crate crate;
        crate.setName(name);
        CrateId id;
        EXPECT_TRUE(internalCollection()->insertCrate(crate, &id));
        return id;
    }

    TrackId addTrack(const QString& file) {
        TrackPointer pTrack = getOrAddTrackByLocation(
                getTestDir().filePath(QStringLiteral("id3-test-data/") + file));
        EXPECT_TRUE(pTrack);
        return pTrack ? pTrack->getId() : TrackId();
    }
};

// The GRAB regression template: mount through the real bindLibraryWidget /
// registerView path. Unlike TRIAGE this view embeds no WTrackTableView, so the
// headless fixture does not raise the SEH crash that keeps the TRIAGE mount test
// disabled -- the registration and switch can be asserted for real here.
TEST_F(CrateSuggestViewTest, ViewMountsThroughFeatureRegistrationPath) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);

    WLibrary libraryWidget(nullptr);
    crate::SuggestFeature feature(nullptr, pConfig, trackCollectionManager());
    feature.bindLibraryWidget(&libraryWidget, nullptr);
    ASSERT_TRUE(feature.viewRegistered())
            << "SUGGEST registerView must accept the LibraryView implementation";

    libraryWidget.switchToView(crate::SuggestFeature::viewName());
    auto* pView = qobject_cast<crate::WSuggestView*>(libraryWidget.currentWidget());
    ASSERT_NE(pView, nullptr) << "SUGGEST view was not registered/mounted";
    EXPECT_EQ(libraryWidget.getActiveView(), pView);

    auto* pEmpty = pView->findChild<QLabel*>(QStringLiteral("SuggestEmptyState"));
    ASSERT_NE(pEmpty, nullptr);
    EXPECT_EQ(pEmpty->text(), QStringLiteral("Visit a crate to see suggestions."));
}

// Crate tracking: a CrateTableModel sets the target crate; any other model
// (the stock LibraryTableModel, or a plain model) must not clear it -- last
// crate wins. Library is null so no async compute fires: state only.
TEST_F(CrateSuggestViewTest, TracksMostRecentCrateAndIgnoresNonCrateModel) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);
    const CrateId crateA = makeCrate(QStringLiteral("Alpha"));

    crate::WSuggestView view(nullptr, pConfig, nullptr, trackCollectionManager(), nullptr);
    EXPECT_FALSE(view.currentCrateId().isValid());

    CrateTableModel crateModel(nullptr, trackCollectionManager());
    crateModel.selectCrate(crateA);
    view.loadTrackModel(&crateModel);
    EXPECT_EQ(view.currentCrateId(), crateA);
    EXPECT_EQ(view.currentCrateName(), QStringLiteral("Alpha"));

    // A stock library table must not clear the tracked crate.
    LibraryTableModel libModel(
            nullptr, trackCollectionManager(), "mixxx.db.model.library");
    view.loadTrackModel(&libModel);
    EXPECT_EQ(view.currentCrateId(), crateA);

    // Neither does an unrelated model.
    QStandardItemModel plainModel;
    view.loadTrackModel(&plainModel);
    EXPECT_EQ(view.currentCrateId(), crateA);
}

// ADD inserts the suggestion's track into the tracked crate and drops the row.
TEST_F(CrateSuggestViewTest, AddInsertsTrackIntoCrateAndRemovesRow) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);
    const CrateId crateId = makeCrate(QStringLiteral("Beta"));
    const TrackId first = addTrack(QStringLiteral("all.mp3"));
    const TrackId second = addTrack(QStringLiteral("artist.mp3"));
    ASSERT_TRUE(first.isValid());
    ASSERT_TRUE(second.isValid());

    crate::WSuggestView view(nullptr, pConfig, nullptr, trackCollectionManager(), nullptr);
    CrateTableModel crateModel(nullptr, trackCollectionManager());
    crateModel.selectCrate(crateId);
    view.loadTrackModel(&crateModel);
    ASSERT_EQ(view.currentCrateId(), crateId);

    crate::WSuggestView::Result result;
    result.state = crate::WSuggestView::State::Ok;
    result.suggestions = {{QStringLiteral("a.flac"), 2.0},
            {QStringLiteral("b.flac"), 1.0}};
    result.meta.insert(QStringLiteral("a.flac"),
            {first, QStringLiteral("A"), QStringLiteral("ArtA"),
                    QStringLiteral("8A"), 128.0});
    result.meta.insert(QStringLiteral("b.flac"),
            {second, QStringLiteral("B"), QStringLiteral("ArtB"),
                    QStringLiteral("9A"), 130.0});
    view.applyResult(result);
    ASSERT_EQ(view.suggestionRowCount(), 2);

    const auto addButtons =
            view.findChildren<QToolButton*>(QStringLiteral("SuggestAdd"));
    ASSERT_EQ(addButtons.size(), 2);
    addButtons[0]->click();

    // The first suggestion's track is now in the crate and its row is gone.
    EXPECT_EQ(view.suggestionRowCount(), 1);
    EXPECT_EQ(internalCollection()->crates().countCrateTracks(crateId), 1u);
    CrateTrackSelectResult members(
            internalCollection()->crates().selectCrateTracksSorted(crateId));
    ASSERT_TRUE(members.next());
    EXPECT_EQ(members.trackId(), first);
}

// Mode chips persist to [Crate],suggest_mode and a fresh view reads them back.
// (Ordering per mode is covered by crate_suggestions_test on the engine.)
TEST_F(CrateSuggestViewTest, ModeChipsPersistAndRestore) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);

    crate::WSuggestView view(nullptr, pConfig, nullptr, trackCollectionManager(), nullptr);
    EXPECT_EQ(view.currentMode(), crate::SuggestMode::Sound);

    auto* pMix = view.findChild<QToolButton*>(QStringLiteral("SuggestModeMIX"));
    auto* pGap = view.findChild<QToolButton*>(QStringLiteral("SuggestModeGAP"));
    ASSERT_NE(pMix, nullptr);
    ASSERT_NE(pGap, nullptr);

    pMix->click();
    EXPECT_EQ(view.currentMode(), crate::SuggestMode::Mix);
    EXPECT_TRUE(pMix->isChecked());
    EXPECT_FALSE(pGap->isChecked());
    EXPECT_EQ(pConfig->getValue(
                      ConfigKey("[Crate]", "suggest_mode"), QString()),
            QStringLiteral("MIX"));

    pGap->click();
    EXPECT_EQ(view.currentMode(), crate::SuggestMode::Gap);
    EXPECT_EQ(pConfig->getValue(
                      ConfigKey("[Crate]", "suggest_mode"), QString()),
            QStringLiteral("GAP"));

    // A newly constructed view restores the persisted mode.
    crate::WSuggestView restored(
            nullptr, pConfig, nullptr, trackCollectionManager(), nullptr);
    EXPECT_EQ(restored.currentMode(), crate::SuggestMode::Gap);
}

} // namespace
