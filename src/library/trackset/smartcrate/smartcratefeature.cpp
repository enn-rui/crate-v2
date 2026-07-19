#include "library/trackset/smartcrate/smartcratefeature.h"

#include <QDir>
#include <QMenu>
#include <QMessageBox>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/smartcrate/dlgsmartcrateeditor.h"
#include "library/trackset/smartcrate/smartcratetablemodel.h"
#include "library/treeitem.h"
#include "moc_smartcratefeature.cpp"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/wlibrarytextbrowser.h"

namespace {

QString storagePathFromConfig(const UserSettingsPointer& pConfig) {
    // smart_crates.json lives next to mixxxdb.sqlite in the settings dir.
    return QDir(pConfig->getSettingsPath())
            .filePath(QStringLiteral("smart_crates.json"));
}

} // anonymous namespace

SmartCrateFeature::SmartCrateFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary,
                  pConfig,
                  QStringLiteral("SMARTCRATEHOME"),
                  QStringLiteral("crates")),
          m_storage(storagePathFromConfig(pConfig)),
          m_pSmartCrateTableModel(pLibrary
                          ? new SmartCrateTableModel(this, pLibrary->trackCollectionManager())
                          : nullptr) {
    initActions();

    m_pSidebarModel->setRootItem(TreeItem::newRoot(this));
    rebuildChildModel();
}

void SmartCrateFeature::initActions() {
    m_pNewAction = make_parented<QAction>(tr("New Smart Crate"), this);
    connect(m_pNewAction.get(),
            &QAction::triggered,
            this,
            &SmartCrateFeature::slotNewSmartCrate);

    m_pEditAction = make_parented<QAction>(tr("Edit"), this);
    connect(m_pEditAction.get(),
            &QAction::triggered,
            this,
            &SmartCrateFeature::slotEditSmartCrate);

    m_pDeleteAction = make_parented<QAction>(tr("Remove"), this);
    connect(m_pDeleteAction.get(),
            &QAction::triggered,
            this,
            &SmartCrateFeature::slotDeleteSmartCrate);
}

QVariant SmartCrateFeature::title() {
    return tr("Smart Crates");
}

TreeItemModel* SmartCrateFeature::sidebarModel() const {
    return m_pSidebarModel;
}

QString SmartCrateFeature::formatRootViewHtml() const {
    QString html;
    html.append(QStringLiteral("<h2>%1</h2>").arg(tr("Smart Crates")));
    html.append(QStringLiteral("<p>%1</p>").arg(
            tr("A smart crate is a saved rule, not a folder. It fills itself "
               "live from your library every time you open it.")));
    html.append(QStringLiteral("<p>%1</p>").arg(
            tr("Build one from BPM, key (incl. harmonic), rating, year, "
               "duration, or artist / title / album / comment.")));
    html.append(
            QStringLiteral("<a style=\"color:#b4d2ff;\" href=\"create\">%1</a>")
                    .arg(tr("New Smart Crate")));
    return html;
}

void SmartCrateFeature::bindLibraryWidget(
        WLibrary* libraryWidget, KeyboardEventFilter* keyboard) {
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(formatRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit,
            &WLibraryTextBrowser::anchorClicked,
            this,
            &SmartCrateFeature::htmlLinkClicked);
    libraryWidget->registerView(m_rootViewName, edit);
}

void SmartCrateFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    m_pSidebarWidget = pSidebarWidget;
}

void SmartCrateFeature::activate() {
    BaseTrackSetFeature::activate();
}

QString SmartCrateFeature::nameFromIndex(const QModelIndex& index) const {
    if (!index.isValid()) {
        return QString();
    }
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (pItem == nullptr) {
        return QString();
    }
    return pItem->getData().toString();
}

bool SmartCrateFeature::findDef(const QString& name, SmartCrate::Def* pDef) const {
    const QList<SmartCrate::Def> defs = m_storage.loadAll();
    for (const SmartCrate::Def& def : defs) {
        if (def.name == name) {
            if (pDef != nullptr) {
                *pDef = def;
            }
            return true;
        }
    }
    return false;
}

void SmartCrateFeature::activateChild(const QModelIndex& index) {
    const QString name = nameFromIndex(index);
    if (name.isEmpty() || m_pSmartCrateTableModel == nullptr) {
        return;
    }
    SmartCrate::Def def;
    if (!findDef(name, &def)) {
        return;
    }
    m_lastRightClickedIndex = QModelIndex();
    emit saveModelState();
    m_pSmartCrateTableModel->selectSmartCrate(def.name, def.spec);
    emit showTrackModel(m_pSmartCrateTableModel);
    emit enableCoverArtDisplay(true);
}

void SmartCrateFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pNewAction.get());
    menu.exec(globalPos);
}

void SmartCrateFeature::onRightClickChild(
        const QPoint& globalPos, const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    if (nameFromIndex(index).isEmpty()) {
        return;
    }
    QMenu menu(m_pSidebarWidget);
    menu.addAction(m_pNewAction.get());
    menu.addSeparator();
    menu.addAction(m_pEditAction.get());
    menu.addAction(m_pDeleteAction.get());
    menu.exec(globalPos);
}

void SmartCrateFeature::deleteItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotDeleteSmartCrate();
}

void SmartCrateFeature::renameItem(const QModelIndex& index) {
    m_lastRightClickedIndex = index;
    slotEditSmartCrate();
}

void SmartCrateFeature::slotNewSmartCrate() {
    openEditor(QString());
}

void SmartCrateFeature::slotEditSmartCrate() {
    const QString name = nameFromIndex(m_lastRightClickedIndex);
    if (name.isEmpty()) {
        return;
    }
    openEditor(name);
}

void SmartCrateFeature::slotDeleteSmartCrate() {
    const QString name = nameFromIndex(m_lastRightClickedIndex);
    if (name.isEmpty()) {
        return;
    }
    const QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
            tr("Confirm Deletion"),
            tr("Do you really want to delete smart crate <b>%1</b>?").arg(name),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        return;
    }
    m_storage.remove(name);
    rebuildChildModel();
}

void SmartCrateFeature::openEditor(const QString& existingName) {
    DlgSmartCrateEditor dlg(nullptr);
    if (!existingName.isEmpty()) {
        SmartCrate::Def def;
        if (findDef(existingName, &def)) {
            dlg.loadSpec(def.name, def.spec);
        }
    }
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString newName = dlg.crateName().trimmed();
    if (newName.isEmpty()) {
        QMessageBox::warning(nullptr,
                tr("Smart Crate"),
                tr("A smart crate needs a name."));
        return;
    }

    const QJsonObject spec = dlg.buildSpec();

    // Renaming: drop the old entry, but never clobber a different existing crate.
    if (!existingName.isEmpty() && existingName != newName) {
        if (findDef(newName, nullptr)) {
            QMessageBox::warning(nullptr,
                    tr("Smart Crate"),
                    tr("A smart crate named \"%1\" already exists.").arg(newName));
            return;
        }
        m_storage.remove(existingName);
    }

    m_storage.upsert(newName, spec);
    rebuildChildModel();
}

void SmartCrateFeature::htmlLinkClicked(const QUrl& link) {
    if (link.path() == QLatin1String("create")) {
        slotNewSmartCrate();
    }
}

void SmartCrateFeature::rebuildChildModel() {
    m_lastRightClickedIndex = QModelIndex();

    TreeItem* pRootItem = m_pSidebarModel->getRootItem();
    VERIFY_OR_DEBUG_ASSERT(pRootItem != nullptr) {
        return;
    }
    m_pSidebarModel->removeRows(0, pRootItem->childRows());

    const QList<SmartCrate::Def> defs = m_storage.loadAll();
    std::vector<std::unique_ptr<TreeItem>> rows;
    rows.reserve(defs.size());
    for (const SmartCrate::Def& def : defs) {
        auto pItem = TreeItem::newRoot(this);
        pItem->setLabel(def.name);
        pItem->setData(def.name);
        rows.push_back(std::move(pItem));
    }
    m_pSidebarModel->insertTreeItemRows(std::move(rows), 0);
}
