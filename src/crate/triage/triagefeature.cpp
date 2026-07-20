#include "crate/triage/triagefeature.h"

#include "crate/triage/wtriageview.h"
#include "library/library.h"
#include "moc_triagefeature.cpp"
#include "util/assert.h"
#include "widget/wlibrary.h"

namespace {
const ConfigKey kEpochKey("[Crate]", "triage_since");
const QString kViewName = QStringLiteral("CrateTriage");
} // namespace

namespace crate {

TriageFeature::TriageFeature(Library* pLibrary,
        UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("crate")),
          m_since(ensureEpoch(pConfig)),
          m_pTrackCollectionManager(pTrackCollectionManager
                          ? pTrackCollectionManager
                          : pLibrary->trackCollectionManager()),
          m_pSidebarModel(make_parented<TreeItemModel>(this)) {
}

QDateTime TriageFeature::ensureEpoch(
        const UserSettingsPointer& pConfig, const QDateTime& now) {
    if (!pConfig->exists(kEpochKey)) {
        pConfig->setValue(kEpochKey, now.toUTC().toString(Qt::ISODate));
        return now.toUTC();
    }
    return QDateTime::fromString(pConfig->getValueString(kEpochKey), Qt::ISODate);
}

bool TriageFeature::isEnabled(const UserSettingsPointer& pConfig) {
    return pConfig->getValue(ConfigKey("[Crate]", "triage_enabled"), true);
}

QVariant TriageFeature::title() { return QStringLiteral("TRIAGE"); }
TreeItemModel* TriageFeature::sidebarModel() const { return m_pSidebarModel; }

void TriageFeature::bindLibraryWidget(
        WLibrary* pLibraryWidget, KeyboardEventFilter* pKeyboard) {
    m_pView = new WTriageView(pLibraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pTrackCollectionManager,
            pKeyboard,
            m_since);
    m_viewRegistered = pLibraryWidget->registerView(kViewName, m_pView);
    VERIFY_OR_DEBUG_ASSERT(m_viewRegistered) {
        qWarning() << "TriageFeature: WLibrary refused to register" << kViewName;
    }
}

void TriageFeature::activate() {
    emit switchToView(kViewName);
    emit enableCoverArtDisplay(true);
}

} // namespace crate
