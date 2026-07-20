#pragma once

#include <QPointer>

#include "library/libraryfeature.h"
#include "library/treeitemmodel.h"
#include "util/parented_ptr.h"

class TrackCollectionManager;

namespace crate {
class WTriageView;

class TriageFeature final : public LibraryFeature {
    Q_OBJECT

  public:
    TriageFeature(Library* pLibrary,
            UserSettingsPointer pConfig,
            TrackCollectionManager* pTrackCollectionManager = nullptr);

    static QDateTime ensureEpoch(const UserSettingsPointer& pConfig,
            const QDateTime& now = QDateTime::currentDateTimeUtc());
    static bool isEnabled(const UserSettingsPointer& pConfig);
    QVariant title() override;
    TreeItemModel* sidebarModel() const override;
    bool viewRegistered() const { return m_viewRegistered; }
    void bindLibraryWidget(WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;

  private:
    const QDateTime m_since;
    TrackCollectionManager* const m_pTrackCollectionManager;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    QPointer<WTriageView> m_pView;
    bool m_viewRegistered{false};
};

} // namespace crate
