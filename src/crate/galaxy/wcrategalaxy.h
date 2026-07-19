#pragma once

#include <QGraphicsView>
#include <QHash>

#include "crate/data/cratesidecars.h"
#include "preferences/usersettings.h"

class PlayerManager;
class QContextMenuEvent;
class QGraphicsScene;
class QPainter;
class QGraphicsEllipseItem;

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
    void contextMenuEvent(QContextMenuEvent* pEvent) override;
    void drawForeground(QPainter* pPainter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* pEvent) override;
    void mouseDoubleClickEvent(QMouseEvent* pEvent) override;
    void mousePressEvent(QMouseEvent* pEvent) override;
    void mouseReleaseEvent(QMouseEvent* pEvent) override;
    void mouseMoveEvent(QMouseEvent* pEvent) override;
    void leaveEvent(QEvent* pEvent) override;
    void resizeEvent(QResizeEvent* pEvent) override;
    void scrollContentsBy(int dx, int dy) override;
    void showEvent(QShowEvent* pEvent) override;

  private:
    enum class ColorMode {
        Cluster,
        Key,
        Tempo,
        Energy,
    };

    struct ValueRange {
        double low = 0.0;
        double high = 0.0;
        bool valid = false;
    };

    void populate();
    void updateColors();
    void setColorMode(ColorMode mode);
    QColor nodeColor(const GalaxyNode& node) const;
    static ValueRange percentileRange(const QVector<double>& values);
    QString resolveMusicPath(const QString& relpath) const;
    void updatePills();
    void setHoveredNode(int index);
    void set3dMode(bool enabled);
    void update3dProjection();
    int projectedNodeAt(const QPoint& viewportPos) const;

    PlayerManager* m_pPlayerManager;
    UserSettingsPointer m_pConfig;
    QGraphicsScene* m_pScene;
    QVector<GalaxyNode> m_nodes;
    QVector<QGraphicsEllipseItem*> m_dots;
    QHash<int, QGraphicsItem*> m_pills;
    QString m_musicRoot;
    ColorMode m_colorMode = ColorMode::Cluster;
    ValueRange m_tempoRange;
    ValueRange m_energyRange;
    bool m_initialFitDone = false;
    bool m_3dMode = false;
    double m_azimuth = 30.0;
    double m_elevation = 18.0;
    QPoint m_orbitLast;
    bool m_orbiting = false;
    bool m_orbitMoved = false;
    double m_fitScale = 1.0;
    int m_hoveredNode = -1;
};

} // namespace crate
