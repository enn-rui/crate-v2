#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include "crate/intelligence/suggestions.h"
#include "library/libraryview.h"
#include "library/trackset/crate/crateid.h"
#include "preferences/usersettings.h"
#include "track/trackid.h"

class KeyboardEventFilter;
class Library;
class QAbstractItemModel;
class QGridLayout;
class QLabel;
class QToolButton;
class TrackCollectionManager;
class WLibrary;

namespace crate {

// SUGGEST view (crate v2, wave-11 slice 7). A STANDALONE LibraryFeature view --
// unlike the reverted WCrateSuggestionsView it does NOT embed or re-register the
// stock Tracks WTrackTableView. It renders suggestion rows for the most recently
// activated crate (tracked via Library::showTrackModel), computed off the DB
// thread by the intact suggestions engine. Being a plain QWidget (no
// WTrackTableView) it mounts cleanly in the headless test fixture, so its
// registration path can be asserted without the SEH crash that disables the
// TRIAGE mount test.
class WSuggestView final : public QWidget, public LibraryView {
    Q_OBJECT

  public:
    // Why the crate has no suggestions right now. Computed partly on the DB
    // thread (NoCrate/CrateEmpty) and partly in the worker (NoVectors), then
    // mapped to plain-language copy on the GUI thread -- the worker never calls
    // tr() or touches this view.
    enum class State {
        Ok,
        NoCrate,
        CrateEmpty,
        NoVectors,
    };

    struct Meta {
        TrackId id;
        QString title;
        QString artist;
        QString key;
        double bpm = 0.0;
    };

    struct Result {
        State state = State::NoCrate;
        QVector<Suggestion> suggestions;
        QHash<QString, Meta> meta;
    };

    WSuggestView(WLibrary* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            TrackCollectionManager* pTrackCollectionManager,
            KeyboardEventFilter* pKeyboard);
    ~WSuggestView() override;

    // LibraryView
    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;

    // Test / integration seams.
    CrateId currentCrateId() const {
        return m_targetCrate;
    }
    QString currentCrateName() const {
        return m_targetCrateName;
    }
    SuggestMode currentMode() const {
        return m_mode;
    }
    // Render an already-computed result without going through the async worker.
    // Production feeds this from the QFutureWatcher; tests feed it directly.
    void applyResult(const Result& result);
    int suggestionRowCount() const {
        return m_lastResult.suggestions.size();
    }

  public slots:
    // Track the most recently activated crate. A CrateTableModel updates the
    // target crate; any other model (e.g. the stock LibraryTableModel) is
    // ignored so the last crate wins.
    void loadTrackModel(QAbstractItemModel* pModel);

  private:
    void setMode(SuggestMode mode);
    void scheduleCompute();
    void clearRows();
    void syncModeButtons();
    static QString modeName(SuggestMode mode);

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    TrackCollectionManager* const m_pTrackCollectionManager;

    QLabel* m_pTitle;
    QLabel* m_pEmpty;
    QWidget* m_pRows;
    QGridLayout* m_pRowsLayout;
    QToolButton* m_pRefresh;
    QVector<QToolButton*> m_modeButtons;

    CrateId m_targetCrate;
    QString m_targetCrateName;
    SuggestMode m_mode = SuggestMode::Sound;
    Result m_lastResult;
    QFutureWatcher<Result> m_watcher;
};

} // namespace crate
