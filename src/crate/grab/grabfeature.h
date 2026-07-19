#pragma once

#include <QPointer>
#include <QString>
#include <QVariant>

#include "library/libraryfeature.h"
#include "library/treeitemmodel.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

// GRAB sidebar feature (wave-6, slice G2). Config-gated: Library only constructs
// and registers this when [Crate],grab_service_url is non-empty, so public
// builds without a backend never see it. When activated it swaps a custom view
// (WGrabView) into the library pane, exactly like AnalysisFeature does.

namespace crate {

class WGrabView;

class GrabFeature final : public LibraryFeature {
    Q_OBJECT
  public:
    GrabFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~GrabFeature() override = default;

    // The single gate Library branches on. True iff a grab service is configured.
    static bool isConfigured(const UserSettingsPointer& pConfig);

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;

    void bindLibraryWidget(WLibrary* pLibraryWidget,
            KeyboardEventFilter* pKeyboard) override;

  public slots:
    void activate() override;

  private:
    const QString m_title;
    parented_ptr<TreeItemModel> m_pSidebarModel;
    QPointer<WGrabView> m_pView;
};

} // namespace crate
