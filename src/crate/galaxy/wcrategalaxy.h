#pragma once

#include <QGraphicsView>

#include "crate/data/cratesidecars.h"
#include "preferences/usersettings.h"

class PlayerManager;
class QGraphicsScene;

namespace crate {

// The galaxy: a 2D scatter of the library's PaCMAP coordinates, colored by
// HDBSCAN cluster (golden-ratio hue walk, faithful to Crate v1 map_view.py).
// Double-click loads the track into the first stopped deck. Data comes
// read-only from the box-produced sidecars ([Crate],sidecar_dir); audio paths
// resolve against [Crate],music_root (default: parent of sidecar_dir).
class WCrateGalaxy : public QGraphicsView {
    Q_OBJECT
  public:
    WCrateGalaxy(QWidget* pParent,
            PlayerManager* pPlayerManager,
            UserSettingsPointer pConfig);

  protected:
    void wheelEvent(QWheelEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
    void showEvent(QShowEvent* pEvent) override;

  private:
    void populate();
    QString resolveMusicPath(const QString& relpath) const;

    PlayerManager* m_pPlayerManager;
    UserSettingsPointer m_pConfig;
    QGraphicsScene* m_pScene;
    QVector<GalaxyNode> m_nodes;
    QString m_musicRoot;
    bool m_initialFitDone = false;
};

} // namespace crate
