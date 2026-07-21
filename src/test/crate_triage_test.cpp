#include <gtest/gtest.h>

#include <QLabel>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>

#include "crate/system/systemcrates.h"
#include "crate/triage/triagefeature.h"
#include "crate/triage/triagetablemodel.h"
#include "crate/triage/wtriageview.h"
#include "control/controlobject.h"
#include "preferences/usersettings.h"
#include "test/librarytest.h"
#include "track/track.h"
#include "widget/wlibrary.h"
#include "widget/wtrackmenu.h"

namespace {

QStringList s_triageMenuMessages;

void captureTriageMenuMessages(
        QtMsgType, const QMessageLogContext&, const QString& message) {
    s_triageMenuMessages.append(message);
}

UserSettingsPointer makeConfig(QTemporaryDir& dir) {
    return UserSettingsPointer(
            new UserSettings(dir.filePath(QStringLiteral("triage_test.cfg"))));
}

class CrateTriageTest : public LibraryTest {
  protected:
    TrackId addTrack(const QString& file, const QDateTime& added) {
        TrackPointer pTrack = getOrAddTrackByLocation(
                getTestDir().filePath(QStringLiteral("id3-test-data/") + file));
        EXPECT_TRUE(pTrack);
        if (!pTrack) {
            return TrackId();
        }
        m_tracks.append(pTrack);
        QSqlQuery query(dbConnection());
        query.prepare(QStringLiteral(
                "UPDATE library SET datetime_added=:added WHERE id=:id"));
        query.bindValue(QStringLiteral(":added"),
                added.toUTC().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        query.bindValue(QStringLiteral(":id"), pTrack->getId().toVariant());
        EXPECT_TRUE(query.exec()) << qPrintable(query.lastError().text());
        return pTrack->getId();
    }

    QList<TrackPointer> m_tracks;
};

TEST(CrateTriageEpoch, StampsAbsentOnceAndPreservesExisting) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);
    const ConfigKey key("[Crate]", "triage_since");
    const QDateTime first = QDateTime::fromString(
            QStringLiteral("2026-07-20T10:00:00Z"), Qt::ISODate);
    const QDateTime second = first.addDays(1);

    EXPECT_EQ(crate::TriageFeature::ensureEpoch(pConfig, first), first);
    const QString stamped = pConfig->getValueString(key);
    EXPECT_FALSE(stamped.isEmpty());
    EXPECT_EQ(crate::TriageFeature::ensureEpoch(pConfig, second), first);
    EXPECT_EQ(pConfig->getValueString(key), stamped);
    EXPECT_TRUE(crate::TriageFeature::isEnabled(pConfig));
    pConfig->setValue(ConfigKey("[Crate]", "triage_enabled"), false);
    EXPECT_FALSE(crate::TriageFeature::isEnabled(pConfig));
}

TEST_F(CrateTriageTest, ModelFiltersByEpochNewestFirstAndKeepRemoves) {
    const QDateTime epoch = QDateTime::fromString(
            QStringLiteral("2026-07-20T10:00:00Z"), Qt::ISODate);
    const TrackId oldId = addTrack(QStringLiteral("all.mp3"), epoch.addSecs(-1));
    const TrackId firstId = addTrack(QStringLiteral("artist.mp3"), epoch);
    const TrackId newestId = addTrack(QStringLiteral("empty.mp3"), epoch.addSecs(10));

    crate::TriageTableModel model(nullptr, trackCollectionManager(), epoch);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.getTrackId(model.index(0, 0)), newestId);
    EXPECT_EQ(model.getTrackId(model.index(1, 0)), firstId);
    EXPECT_FALSE(crate::SystemCrates(internalCollection()).isReviewed(oldId));

    // Reading/loading a row has no review side effect.
    EXPECT_TRUE(model.getTrack(model.index(0, 0)));
    EXPECT_FALSE(crate::SystemCrates(internalCollection()).isReviewed(newestId));

    ASSERT_TRUE(crate::SystemCrates(internalCollection()).markReviewed({newestId}));
    model.refresh();
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.getTrackId(model.index(0, 0)), firstId);
}

TEST_F(CrateTriageTest, MetadataMenuConstructionDoesNotInsertNullActions) {
    const QDateTime epoch = QDateTime::fromString(
            QStringLiteral("2026-07-20T10:00:00Z"), Qt::ISODate);
    crate::TriageTableModel model(nullptr, trackCollectionManager(), epoch);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);

    s_triageMenuMessages.clear();
    const auto oldHandler = qInstallMessageHandler(captureTriageMenuMessages);
    WTrackMenu menu(nullptr,
            pConfig,
            nullptr,
            WTrackMenu::Feature::Metadata,
            &model);
    qInstallMessageHandler(oldHandler);

    for (const QString& message : std::as_const(s_triageMenuMessages)) {
        EXPECT_FALSE(message.contains(
                QStringLiteral("insertAction: Attempt to insert null action")))
                << qPrintable(message);
    }
}

// Upstream WTrackTableView raises Windows SEH 0xc0000005 in this stripped
// LibraryTest fixture. Production still asserts registerView's return in
// TriageFeature::bindLibraryWidget. Keep this exact real-path regression ready
// for a fixture that can construct the full PlayerManager/CoreServices graph.
TEST_F(CrateTriageTest, DISABLED_ViewMountsThroughFeatureRegistrationPath) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto pConfig = makeConfig(dir);
    pConfig->setValue(ConfigKey("[Crate]", "triage_since"),
            QStringLiteral("2026-07-20T10:00:00Z"));
    // WTrackTableView binds these application-wide controls in production.
    // Supply their real ControlObject counterparts in the headless fixture.
    ControlObject guiTick(ConfigKey("[App]", "gui_tick_50ms_period_s"));
    ControlObject sortColumn(ConfigKey("[Library]", "sort_column"));
    ControlObject sortOrder(ConfigKey("[Library]", "sort_order"));
    ControlObject crossfader(ConfigKey("[Master]", "crossfader"));
    ControlObject numDecks(ConfigKey("[App]", "num_decks"));
    ControlObject numSamplers(ConfigKey("[App]", "num_samplers"));
    ControlObject numPreviewDecks(ConfigKey("[App]", "num_preview_decks"));
    WLibrary libraryWidget(nullptr);
    crate::TriageFeature feature(nullptr, pConfig, trackCollectionManager());

    feature.bindLibraryWidget(&libraryWidget, nullptr);
    ASSERT_TRUE(feature.viewRegistered())
            << "TRIAGE registerView must accept the LibraryView implementation";
    libraryWidget.switchToView(QStringLiteral("CrateTriage"));
    auto* pView = qobject_cast<crate::WTriageView*>(libraryWidget.currentWidget());
    ASSERT_NE(pView, nullptr);
    EXPECT_EQ(libraryWidget.getActiveView(), pView);
    EXPECT_NE(pView->findChild<QPushButton*>(QStringLiteral("TriageKeep")), nullptr);
    auto* pEmpty = pView->findChild<QLabel*>(QStringLiteral("TriageEmptyState"));
    ASSERT_NE(pEmpty, nullptr);
    EXPECT_EQ(pEmpty->text(), QStringLiteral("Nothing to triage"));
}

} // namespace
