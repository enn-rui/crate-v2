#pragma once

#include <QModelIndex>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariant>

#include "library/trackset/basetracksetfeature.h"
#include "library/trackset/smartcrate/smartcratestorage.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class Library;
class WLibrary;
class WLibrarySidebar;
class KeyboardEventFilter;
class QAction;
class QPoint;
class SmartCrateTableModel;

// "SMART CRATES" sidebar feature (wave-6). Lists stored smart-crate specs as tree
// children; New / Edit / Delete via right-click. Activating a child re-resolves
// its spec LIVE against the library table (through SmartCrateTableModel) and shows
// the matching tracks in the table area. Registered UNCONDITIONALLY -- unlike
// GRAB it needs no backend. Persistence is smart_crates.json in the settings dir.
class SmartCrateFeature : public BaseTrackSetFeature {
    Q_OBJECT

  public:
    SmartCrateFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~SmartCrateFeature() override = default;

    QVariant title() override;

    void bindLibraryWidget(WLibrary* libraryWidget,
            KeyboardEventFilter* keyboard) override;
    void bindSidebarWidget(WLibrarySidebar* pSidebarWidget) override;
    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    void onRightClick(const QPoint& globalPos) override;
    void onRightClickChild(const QPoint& globalPos, const QModelIndex& index) override;
    void deleteItem(const QModelIndex& index) override;
    void renameItem(const QModelIndex& index) override;

  private slots:
    void slotNewSmartCrate();
    void slotEditSmartCrate();
    void slotDeleteSmartCrate();
    void htmlLinkClicked(const QUrl& link);

  private:
    void initActions();
    void rebuildChildModel();
    QString nameFromIndex(const QModelIndex& index) const;
    bool findDef(const QString& name, SmartCrate::Def* pDef) const;
    void openEditor(const QString& existingName);
    QString formatRootViewHtml() const;

    SmartCrate::Storage m_storage;
    SmartCrateTableModel* m_pSmartCrateTableModel;

    QModelIndex m_lastRightClickedIndex;
    QPointer<WLibrarySidebar> m_pSidebarWidget;

    parented_ptr<QAction> m_pNewAction;
    parented_ptr<QAction> m_pEditAction;
    parented_ptr<QAction> m_pDeleteAction;
};
