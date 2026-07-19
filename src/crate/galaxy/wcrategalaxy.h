#pragma once

#include <QGraphicsView>
#include <QHash>
#include <QPointer>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <memory>

#include "crate/data/cratesidecars.h"
#include "crate/intelligence/sonicvectors.h"
#include "preferences/usersettings.h"

class Library;
class PlayerManager;
class ControlObject;
class ControlProxy;
class ControlPushButton;
class QContextMenuEvent;
class QMenu;
class QGraphicsScene;
class QPainter;
class QGraphicsEllipseItem;
class QVariantAnimation;
class QAbstractItemModel;

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
            UserSettingsPointer pConfig,
            Library* pLibrary = nullptr);
    ~WCrateGalaxy() override;

    // Pure, engine-free rule for the "natural next-prep deck" (spec wave-5 S3).
    // `playing[i]` is the play state of 0-based deck i; `lastStartedIndex` is the
    // 0-based index of the most-recently-started playing deck (-1 if unknown /
    // none). Returns a 1-based deck number to prep, or 0 = do nothing (never
    // steal a playing deck). Rule: nothing playing -> deck 1; all playing -> 0;
    // otherwise the lowest-numbered stopped deck on the side (1/3 vs 2/4)
    // opposite the reference playing deck, falling back to the lowest-numbered
    // stopped deck anywhere.
    static int pickNextPrepDeck(const QVector<bool>& playing, int lastStartedIndex);

    // Pure, engine-free sensitivity mapping for the 3D orbit (spec wave-5 S4).
    // Returns the orbit angle change in DEGREES for a mouse motion of
    // `pixelDelta` pixels, scaled so a pixel of mouse motion moves the scene the
    // same number of SCREEN pixels at every zoom. `viewportExtent` is the
    // viewport span (px) a full 360-degree sweep maps to at the fitted scale;
    // `zoomRatio` is the current view scale over the fit scale (1.0 at fit, >1
    // zoomed in). Larger zoom -> smaller angle per pixel, so on-screen motion
    // stays constant. Her report: orbit "way too high when zoomed in" because
    // the old mapping ignored zoom entirely.
    static double orbitAngleDelta(int pixelDelta, int viewportExtent, double zoomRatio);

    // Test-only introspection. Cheap const accessors, no behavior; let the
    // widget test recompute the walk metric independently and assert against it.
    int testCursorNode() const {
        return m_cursorNode;
    }
    int testNodeCount() const {
        return m_nodes.size();
    }
    QPointF testNodeDisplayPos(int index) const;
    QString testNodeRelpath(int index) const;
    QString testRelpathForLocation(const QString& location) const {
        return relpathForLocation(location);
    }
    bool testNodeVisible(int index) const;
    bool testNodeGhosted(int index) const;
    bool testNodeHasHalo(int index) const;
    quintptr testDotItemIdentity(int index) const;
    int testProjectedNodeAt(const QPoint& viewportPos) const;
    void testApplySubsetByRelpaths(const QSet<QString>& relpaths);
    void testClearSubset();
    QString testLayoutMode() const;
    QString testColorMode() const;
    bool test3dMode() const { return m_3dMode; }
    bool testHalosEnabled() const { return m_halosEnabled; }
    bool testKnobFocusMap() const { return m_knobFocusMap; }
    // Deck-load context-menu labels for a node (empty if the node is not
    // selectable / ghosted). Playing decks are annotated "(playing)".
    QStringList testDeckLoadLabels(int nodeIndex) const;
    int testNextPrepDeck() const { return nextPrepDeck(); }
    // Table-jump observation seam (see requestTableJumpToCursor): every
    // map-initiated load bumps this counter and records the track's relpath,
    // so a widget test can assert the load asked the table to jump even without
    // a live Library fixture.
    int testTableJumpRequests() const { return m_tableJumpRequests; }
    QString testLastTableJumpRelpath() const { return m_lastTableJumpRelpath; }

  protected:
    bool eventFilter(QObject* pObj, QEvent* pEvent) override;
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

    enum class LayoutMode {
        Scatter,
        KeyWheel,
        BpmSerpentine,
        Artist,
    };

    struct ValueRange {
        double low = 0.0;
        double high = 0.0;
        bool valid = false;
    };

    void populate();
    void updateColors();
    void setColorMode(ColorMode mode);
    void setLayoutMode(LayoutMode mode, bool animate = true);
    QVector<QPointF> layoutTarget(LayoutMode mode) const;
    QVector<QPointF> separate(const QVector<QPointF>& positions, double shrink) const;
    void applyPositions(const QVector<QPointF>& positions);
    QColor nodeColor(const GalaxyNode& node) const;
    static ValueRange percentileRange(const QVector<double>& values);
    QString resolveMusicPath(const QString& relpath) const;
    void updatePills();
    void setHoveredNode(int index);
    void set3dMode(bool enabled);
    void setHalosEnabled(bool enabled);
    void update3dProjection();
    int projectedNodeAt(const QPoint& viewportPos) const;
    void updateMixabilityHalos();
    void applyHaloVisuals();
    QString relpathForLocation(const QString& location) const;
    void bindSubsetModel(QAbstractItemModel* pModel);
    void recomputeSubsetFromModel();
    void applySubset(const QSet<QString>& relpaths, bool active = true);
    bool nodeInSubset(int index) const;
    QColor subsetColor(int index, const QColor& color) const;

    // FLX4 browse-knob MAP navigation (wave 4). The walk mirrors v1
    // map_view.nearest_unplayed: forward = nearest node not yet visited in the
    // currently displayed coordinate space (2D layout, or 3D coords in 3D mode);
    // back = retrace the walk's own history.
    void setKnobFocusMap(bool mapFocus);
    void onKnobMove(double delta);
    void stepCursorForward();
    void stepCursorBack();
    int seedNode() const;
    int nearestUnvisited(int fromNode) const;
    double displaySqDistance(int a, int b) const;
    bool nodeSelectable(int index) const;
    void setCursorNode(int index, bool resetWalk);
    void resetWalk();
    void updateCursorVisual();
    void syncSelectionToCursor();
    void requestTableJumpToCursor();
    void loadCursorToNextPrepDeck();
    void loadCursorIntoDeck(int deckIndex);
    QVector<bool> deckPlayingStates() const;
    int nextPrepDeck() const;
    // Deck-load context-menu integration (spec wave-5 S3, item 1).
    struct DeckLoadEntry {
        int deck = 0;      // 1-based deck number
        QString label;     // menu label, annotated when the deck is playing
        bool enabled = false; // false = deck playing, load disabled
    };
    QVector<DeckLoadEntry> deckLoadEntries(int nodeIndex) const;
    void addDeckLoadActions(QMenu* pMenu, int nodeIndex);
    int nodeAtViewportPos(const QPoint& viewportPos) const;

    PlayerManager* m_pPlayerManager;
    Library* m_pLibrary;
    UserSettingsPointer m_pConfig;
    QGraphicsScene* m_pScene;
    QVector<GalaxyNode> m_nodes;
    QVector<QGraphicsEllipseItem*> m_dots;
    QVector<QGraphicsEllipseItem*> m_halos;
    QHash<int, QGraphicsItem*> m_pills;
    QString m_musicRoot;
    ColorMode m_colorMode = ColorMode::Cluster;
    ValueRange m_tempoRange;
    ValueRange m_energyRange;
    QVector<QPointF> m_scatterPositions;
    LayoutMode m_layoutMode = LayoutMode::Scatter;
    QVariantAnimation* m_pLayoutAnimation = nullptr;
    bool m_initialFitDone = false;
    bool m_debugInput = false;
    bool m_3dMode = false;
    double m_azimuth = 30.0;
    double m_elevation = 18.0;
    QPoint m_orbitLast;
    bool m_orbiting = false;
    bool m_orbitMoved = false;
    double m_fitScale = 1.0;
    int m_hoveredNode = -1;
    SonicVectors m_sonicVectors;
    bool m_vectorsLoadAttempted = false;
    bool m_halosEnabled = true;
    QString m_playingRelpath;
    QHash<int, double> m_haloScores;
    QHash<QString, int> m_nodeByRelpath;
    QSet<int> m_subsetNodes;
    bool m_subsetActive = false;
    QPointer<QAbstractItemModel> m_pSubsetModel;

    // Table-jump-on-load observation (test seam; see requestTableJumpToCursor).
    int m_tableJumpRequests = 0;
    QString m_lastTableJumpRelpath;

    // MAP-walk state.
    bool m_knobFocusMap = false;
    int m_cursorNode = -1;
    QVector<int> m_walkHistory; // ordered nodes the cursor has occupied
    QSet<int> m_walkVisited;    // nodes visited this walk (skip on forward)
    QGraphicsEllipseItem* m_pCursorRing = nullptr;

    // Controls. knob_focus + galaxy_load are owned here (new [Crate] controls);
    // MoveVertical is a proxy onto the stock [Library] encoder the browse knob
    // already drives.
    std::unique_ptr<ControlPushButton> m_pGalaxyLoadCO;
    ControlProxy* m_pLayoutProxy = nullptr;
    ControlProxy* m_pColorProxy = nullptr;
    ControlProxy* m_p3dProxy = nullptr;
    ControlProxy* m_pHaloProxy = nullptr;
    ControlProxy* m_pKnobFocusProxy = nullptr;
    ControlProxy* m_pMoveVerticalProxy = nullptr;
};

} // namespace crate
