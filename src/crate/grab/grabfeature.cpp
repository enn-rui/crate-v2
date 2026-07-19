#include "crate/grab/grabfeature.h"

#include "controllers/keyboard/keyboardeventfilter.h"
#include "crate/grab/grabclient.h"
#include "crate/grab/wgrabview.h"
#include "moc_grabfeature.cpp"
#include "widget/wlibrary.h"

namespace {
const QString kViewName = QStringLiteral("CrateGrab");
} // namespace

namespace crate {

GrabFeature::GrabFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, std::move(pConfig), QStringLiteral("crates")),
          m_title(QStringLiteral("Grab")),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pView(nullptr) {
}

bool GrabFeature::isConfigured(const UserSettingsPointer& pConfig) {
    return !GrabClient::configuredBaseUrl(pConfig).isEmpty();
}

QVariant GrabFeature::title() {
    return m_title;
}

TreeItemModel* GrabFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void GrabFeature::bindLibraryWidget(WLibrary* pLibraryWidget,
        KeyboardEventFilter* pKeyboard) {
    m_pView = new WGrabView(pLibraryWidget, m_pConfig);
    if (pKeyboard) {
        m_pView->installEventFilter(pKeyboard);
    }
    pLibraryWidget->registerView(kViewName, m_pView);
}

void GrabFeature::activate() {
    emit switchToView(kViewName);
    // GRAB is not a track table; no cover art column to display.
    emit enableCoverArtDisplay(false);
    emit disableSearch();
    if (m_pView) {
        m_pView->onActivated();
    }
}

} // namespace crate
