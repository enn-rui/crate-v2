#include "crate/suggestions/suggestfeature.h"

#include "crate/suggestions/wsuggestview.h"
#include "library/library.h"
#include "moc_suggestfeature.cpp"
#include "util/assert.h"
#include "widget/wlibrary.h"

namespace {
const QString kViewName = QStringLiteral("CrateSuggest");
} // namespace

namespace crate {

SuggestFeature::SuggestFeature(Library* pLibrary,
        UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager)
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("crate")),
          m_pTrackCollectionManager(pTrackCollectionManager
                          ? pTrackCollectionManager
                          : pLibrary->trackCollectionManager()),
          m_pSidebarModel(make_parented<TreeItemModel>(this)) {
}

bool SuggestFeature::isEnabled(const UserSettingsPointer& pConfig) {
    return pConfig->getValue(ConfigKey("[Crate]", "suggest_enabled"), true);
}

QString SuggestFeature::viewName() {
    return kViewName;
}

QVariant SuggestFeature::title() {
    return QStringLiteral("SUGGEST");
}

TreeItemModel* SuggestFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void SuggestFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget, KeyboardEventFilter* pKeyboard) {
    m_pView = new WSuggestView(pLibraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pTrackCollectionManager,
            pKeyboard);
    m_viewRegistered = pLibraryWidget->registerView(kViewName, m_pView);
    // registerView rejects widgets that don't implement LibraryView; a false
    // here means the sidebar click would switch to a view that was never added.
    VERIFY_OR_DEBUG_ASSERT(m_viewRegistered) {
        qWarning() << "SuggestFeature: WLibrary refused to register" << kViewName;
    }
}

void SuggestFeature::activate() {
    emit switchToView(kViewName);
    // SUGGEST is not a track table; no cover-art column to display.
    emit enableCoverArtDisplay(false);
    emit disableSearch();
    if (m_pView) {
        m_pView->onShow();
    }
}

} // namespace crate
