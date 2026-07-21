#include "crate/triage/wtriageview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "crate/system/systemcrates.h"
#include "crate/triage/triagetablemodel.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_wtriageview.cpp"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"

namespace crate {

WTriageView::WTriageView(WLibrary* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        TrackCollectionManager* pTrackCollectionManager,
        KeyboardEventFilter* pKeyboard,
        const QDateTime& since)
        : QWidget(pParent),
          m_pTable(new WTrackTableView(this,
                  pConfig,
                  pLibrary,
                  pParent->getTrackTableBackgroundColorOpacity())),
          m_pModel(new TriageTableModel(this, pTrackCollectionManager, since)),
          m_pEmpty(new QLabel(tr("Nothing to triage"), this)),
          m_pKeep(new QPushButton(tr("KEEP (MARK REVIEWED)"), this)),
          m_pTrackCollectionManager(pTrackCollectionManager) {
    setObjectName(QStringLiteral("CrateTriageView"));
    m_pEmpty->setObjectName(QStringLiteral("TriageEmptyState"));
    m_pKeep->setObjectName(QStringLiteral("TriageKeep"));
    m_pTable->setObjectName(QStringLiteral("TriageTrackTable"));
    if (pKeyboard) {
        m_pTable->installEventFilter(pKeyboard);
    }
    m_pTable->loadTrackModel(m_pModel);

    auto* pToolbar = new QHBoxLayout();
    pToolbar->addStretch();
    pToolbar->addWidget(m_pKeep);
    auto* pLayout = new QVBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addLayout(pToolbar);
    pLayout->addWidget(m_pEmpty);
    pLayout->addWidget(m_pTable, 1);

    connect(m_pKeep, &QPushButton::clicked,
            this, &WTriageView::markSelectedReviewed);
    connect(m_pTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &WTriageView::updateKeepButton);
    connect(m_pModel, &QAbstractItemModel::modelReset,
            this, &WTriageView::updateEmptyState);
    if (pLibrary) {
        connect(m_pTable, &WTrackTableView::loadTrack,
                pLibrary, &Library::slotLoadTrack);
        connect(m_pTable, &WTrackTableView::loadTrackToPlayer,
                pLibrary, &Library::slotLoadTrackToPlayer);
        connect(pLibrary, &Library::setTrackTableFont,
                m_pTable, &WTrackTableView::setTrackTableFont);
        connect(pLibrary, &Library::setTrackTableRowHeight,
                m_pTable, &WTrackTableView::setTrackTableRowHeight);
        connect(pLibrary, &Library::setSelectedClick,
                m_pTable, &WTrackTableView::setSelectedClick);
    }
    m_pKeep->setEnabled(false);
    updateEmptyState();
}

WTriageView::~WTriageView() {
    // WTrackTableView saves header state through its model while destructing.
    // Delete it before the model, matching the established DlgHidden pattern.
    delete m_pTable;
    m_pTable = nullptr;
    delete m_pModel;
    m_pModel = nullptr;
}

void WTriageView::onShow() {
    if (m_hasShown) {
        m_pModel->refresh();
    }
    m_hasShown = true;
    updateEmptyState();
}

bool WTriageView::hasFocus() const { return m_pTable->hasFocus(); }
void WTriageView::setFocus() { m_pTable->setFocus(); }
void WTriageView::onSearch(const QString& text) { m_pModel->search(text); }
void WTriageView::saveCurrentViewState() { m_pTable->saveCurrentViewState(); }
bool WTriageView::restoreCurrentViewState() { return m_pTable->restoreCurrentViewState(); }

void WTriageView::markSelectedReviewed() {
    const QList<TrackId> ids = m_pTable->getSelectedTrackIds();
    if (ids.isEmpty()) {
        return;
    }
    if (SystemCrates(m_pTrackCollectionManager->internalCollection()).markReviewed(ids)) {
        m_pModel->refresh();
        updateEmptyState();
    }
}

void WTriageView::updateEmptyState() {
    const bool empty = m_pModel->rowCount() == 0;
    m_pEmpty->setVisible(empty);
    m_pTable->setVisible(!empty);
    updateKeepButton();
}

void WTriageView::updateKeepButton() {
    // The button acts on the whole selection; say so, with the live count, so
    // multi-select KEEP is discoverable ("KEEP 12 SELECTED" beats a mystery).
    const int count = m_pModel->rowCount() == 0
            ? 0
            : m_pTable->getSelectedTrackIds().size();
    m_pKeep->setEnabled(count > 0);
    if (count > 1) {
        m_pKeep->setText(tr("KEEP %1 SELECTED").arg(count));
    } else {
        m_pKeep->setText(tr("KEEP (MARK REVIEWED)"));
    }
}

} // namespace crate
