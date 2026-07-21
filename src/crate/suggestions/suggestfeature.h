#pragma once

#include <QPointer>

#include "library/libraryfeature.h"
#include "library/treeitemmodel.h"
#include "util/parented_ptr.h"

class TrackCollectionManager;

namespace crate {
class WSuggestView;

// SUGGEST feature (crate v2, wave-11 slice 7). Registers a standalone SUGGEST
// sidebar row whose view proposes additions for the most recently activated
// crate. It follows the GRAB/TRIAGE registration shape exactly and never touches
// the stock Tracks view / WTrackTableView wiring (the reverted panel's mistake).
class SuggestFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    SuggestFeature(Library* pLibrary,
            UserSettingsPointer pConfig,
            TrackCollectionManager* pTrackCollectionManager = nullptr);

    static bool isEnabled(const UserSettingsPointer& pConfig);
    static QString viewName();

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;
    bool viewRegistered() const {
        return m_viewRegistered;
    }
    void bindLibraryWidget(WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;

  private:
    TrackCollectionManager* const m_pTrackCollectionManager;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    QPointer<WSuggestView> m_pView;
    bool m_viewRegistered{false};
};

} // namespace crate
