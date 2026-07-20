#pragma once

#include <QWidget>
#include <QDateTime>

#include "library/libraryview.h"
#include "preferences/usersettings.h"

class KeyboardEventFilter;
class Library;
class QLabel;
class QPushButton;
class TrackCollectionManager;
class WLibrary;
class WTrackTableView;

namespace crate {
class TriageTableModel;

class WTriageView final : public QWidget, public LibraryView {
    Q_OBJECT

  public:
    WTriageView(WLibrary* pParent,
            UserSettingsPointer pConfig,
            Library* pLibrary,
            TrackCollectionManager* pTrackCollectionManager,
            KeyboardEventFilter* pKeyboard,
            const QDateTime& since);
    ~WTriageView() override;

    void onShow() override;
    bool hasFocus() const override;
    void setFocus() override;
    void onSearch(const QString& text) override;
    void saveCurrentViewState() override;
    bool restoreCurrentViewState() override;

    TriageTableModel* model() const { return m_pModel; }

  private slots:
    void markSelectedReviewed();
    void updateEmptyState();

  private:
    WTrackTableView* m_pTable;
    TriageTableModel* m_pModel;
    QLabel* m_pEmpty;
    QPushButton* m_pKeep;
    TrackCollectionManager* const m_pTrackCollectionManager;
    bool m_hasShown{false};
};

} // namespace crate
