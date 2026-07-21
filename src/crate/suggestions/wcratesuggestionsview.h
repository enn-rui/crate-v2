#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QWidget>

#include "crate/intelligence/suggestions.h"
#include "library/libraryview.h"
#include "preferences/usersettings.h"
#include "track/trackid.h"

class KeyboardEventFilter;
class Library;
class QAbstractItemModel;
class QGridLayout;
class QLabel;
class QToolButton;
class WLibrary;
class WTrackTableView;

namespace crate {

class WCrateSuggestionsView final : public QWidget, public LibraryView {
    Q_OBJECT
  public:
    WCrateSuggestionsView(WLibrary* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            KeyboardEventFilter* pKeyboard);
    WTrackTableView* trackTable() const { return m_pTrackTable; }

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void onSearch(const QString& text) override;
    void pasteFromSidebar() override;
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;
    TrackModel::SortColumnId getColumnIdFromCurrentIndex() override;
    void assignPreviousTrackColor() override;
    void assignNextTrackColor() override;

  public slots:
    void loadTrackModel(QAbstractItemModel* pModel, bool restoreState = false);

  private:
    struct Meta {
        TrackId id;
        QString title;
        QString artist;
        QString key;
        double bpm = 0.0;
    };
    struct Result {
        QVector<Suggestion> suggestions;
        QHash<QString, Meta> meta;
        QString error;
    };
    void setMode(SuggestMode mode);
    void scheduleCompute();
    void render(const Result& result);
    void clearRows();
    static QString modeName(SuggestMode mode);

    UserSettingsPointer m_pConfig;
    Library* m_pLibrary;
    WTrackTableView* m_pTrackTable;
    QWidget* m_pSection;
    QLabel* m_pHeader;
    QWidget* m_pRows;
    QGridLayout* m_pRowsLayout;
    QVector<QToolButton*> m_modeButtons;
    QPointer<QAbstractItemModel> m_pModel;
    QHash<QString, Meta> m_meta;
    QFutureWatcher<Result> m_watcher;
    SuggestMode m_mode = SuggestMode::Sound;
};

} // namespace crate
