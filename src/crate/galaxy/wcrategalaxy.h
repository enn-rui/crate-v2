#pragma once

#include <QColor>
#include <QDateTime>
#include <QGraphicsView>
#include <QHash>
#include <QLineF>
#include <QPointer>
#include <QPointF>
#include <QPixmap>
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
class QTimer;
class QAbstractItemModel;

namespace crate {

struct GalaxyPalette {
    QColor ground = QColor(QStringLiteral("#05060a"));
    QColor ink = QColor(QStringLiteral("#f4f7fb"));
    QColor accentDeckA = QColor(QStringLiteral("#b4d2ff"));
    QColor accentDeckB = QColor(QStringLiteral("#ffb454"));
};

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
            Library* pLibrary = nullptr,
            GalaxyPalette palette = {});
    ~WCrateGalaxy() override;

    // Pure, engine-free rule for the "natural next-prep deck" (spec wave-5 S3).
    // `playing[i]` is the play state of 0-based deck i; `lastStartedIndex` is the
    // 0-based index of the most-recently-started playing deck (-1 if unknown /
    // none). Returns a 1-based deck number to prep, or 0 = do nothing (never
    // steal a playing deck). Rule: nothing playing -> deck 1; all playing -> 0;
    // otherwise the lowest-numbered stopped deck on the side (1/3 vs 2/4)
    // opposite the reference playing deck, falling back to the lowest-numbered
    // stopped deck anywhere.
    static int pickNextPrepDeck(
            const QVector<bool>& playing, const QVector<bool>& loaded);

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
    int testNodeAtViewportPos(const QPoint& viewportPos) const {
        return nodeAtViewportPos(viewportPos);
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
    GalaxyPalette testPalette() const { return m_palette; }
    bool test3dMode() const { return m_3dMode; }
    bool testHalosEnabled() const { return m_halosEnabled; }
    bool testTrailEnabled() const { return m_trailEnabled; }
    int testTrailSegmentCount() const { return m_trailDeviceLines.size(); }
    QVector<int> testPlayTrail() const { return m_playTrail; }
    int testPlexusSegmentCount() const { return m_plexusDeviceLines.size(); }
    int testPlexusLineAlpha(int index) const {
        return m_plexusDeviceColors.value(index).alpha();
    }
    int testPlexusRingAlpha(int node) const;
    int testPlexusDeckCount() const { return m_deckPlexusScores.size(); }
    QHash<int, double> testPlexusScores(int deck) const {
        return m_deckPlexusScores.value(deck);
    }
    void testPlayingTrackChanged(const QString& relpath);
    void testSetTrailEnabled(bool enabled) { setTrailEnabled(enabled); }
    void testRefreshPlexus() { updateMixabilityHalos(); }
    bool testKnobFocusMap() const { return m_knobFocusMap; }
    int testVisibleSelectableNodeCount() const;
    int testLabelCount(int layer) const;
    QStringList testLabelTexts(int layer) const;
    QColor testLabelColor(int layer, int index) const;
    int testLabelClusterId(int layer, int index) const;
    int testTrackLabelLeaderCount() const;
    bool testTrackLabelHasLeader(int index) const;
    QColor testClusterColor(int clusterId) const;
    QString testClusterName(int clusterId) const;
    void testRebuildLabels() { rebuildLabelCache(); }
    void testScaleWithoutRebuild(double factor) { scale(factor, factor); }
    bool testLeadersSuppressed() const { return leadersSuppressed(); }
    bool testLeaderGeometrySane() const;
    int testLabelRebuildCount() const { return m_labelRebuildCount; }
    int testSidecarRebuildCount() const { return m_sidecarRebuildCount; }
    void testPanBy(int dx, int dy) { scrollContentsBy(dx, dy); }
    QVector<QRectF> testLabelRects() const;
    QStringList testTrackLabelRelpaths() const;
    void testScaleAndRebuild(double factor) { scale(factor, factor); rebuildLabelCache(); }
    QString testNodeArtist(int index) const;
    void testSetNodeDisplayPosition(int index, const QPointF& position);
    void testSetAllNodeDisplayPositions(const QPointF& position);
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
    void focusInEvent(QFocusEvent* pEvent) override;

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
    bool reloadSidecars();
    void reloadSidecarsIfChanged();
    void rebuildScene(const QVector<GalaxyNode>& nodes);
    void updateColors();
    void setColorMode(ColorMode mode);
    void setLayoutMode(LayoutMode mode, bool animate = true);
    QVector<QPointF> layoutTarget(LayoutMode mode) const;
    int layoutDegradedCount(LayoutMode mode) const;
    void publishLayoutDegradation(LayoutMode mode);
    QVector<QPointF> separate(const QVector<QPointF>& positions, double shrink) const;
    void applyPositions(const QVector<QPointF>& positions);
    QColor nodeColor(const GalaxyNode& node) const;
    static ValueRange percentileRange(const QVector<double>& values);
    QString resolveMusicPath(const QString& relpath) const;
    void rebuildLabelCache();
    bool leadersSuppressed() const;
    void scheduleLabelCacheRebuild();
    void updateLabelOpacities();
    void updateHoverCard();
    QString clusterName(int clusterId, const QVector<int>& members) const;
    QString artistForNode(const GalaxyNode& node) const;
    int clusterLabelAt(const QPoint& viewportPos) const;
    void setHoveredNode(int index);
    void set3dMode(bool enabled);
    void setHalosEnabled(bool enabled);
    void setTrailEnabled(bool enabled);
    void update3dProjection();
    int projectedNodeAt(const QPoint& viewportPos) const;
    void updateMixabilityHalos();
    void applyHaloVisuals();
    void playingTrackChanged(const TrackPointer& pTrack);
    void appendTrailRelpath(const QString& relpath);
    void rebuildOverlayCache();
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
    QVector<bool> deckLoadedStates() const;
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
    GalaxyPalette m_palette;
    QGraphicsScene* m_pScene;
    QVector<GalaxyNode> m_nodes;
    QVector<QGraphicsEllipseItem*> m_dots;
    QVector<QGraphicsEllipseItem*> m_halos;
    QHash<int, QGraphicsItem*> m_pills;
    struct MapLabel {
        QString text;
        QPointF sceneAnchor;
        QPointF pixmapOffset;
        QSizeF logicalSize;
        QPixmap pixmap;
        QColor color;
        int pixelSize = 10;
        qreal opacity = 1.0;
        int clusterId = -1;
        int nodeIndex = -1;
        bool hasLeader = false;
    };
    QVector<MapLabel> m_clusterLabels;
    QVector<MapLabel> m_artistLabels;
    QVector<MapLabel> m_trackLabels;
    QTimer* m_pLabelRebuildTimer = nullptr;
    int m_labelRebuildCount = 0;
    int m_sidecarRebuildCount = 0;
    QDateTime m_lastSidecarModified;
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
    bool m_trailEnabled = true;
    QVector<int> m_playTrail;
    QVector<QLineF> m_trailDeviceLines;
    QVector<QColor> m_trailDeviceColors;
    struct PlexusSegment {
        int deck = 0;
        int from = -1;
        int to = -1;
        double score = 0.0;
        bool playing = false;
    };
    QVector<PlexusSegment> m_plexusSegments;
    QVector<QLineF> m_plexusDeviceLines;
    QVector<QColor> m_plexusDeviceColors;
    QVector<qreal> m_plexusDeviceWidths;
    QHash<int, QHash<int, double>> m_deckPlexusScores;
    QHash<int, int> m_deckPlayingNodes;
    QHash<int, bool> m_deckIsPlaying;
    QHash<QString, int> m_nodeByRelpath;
    QSet<int> m_subsetNodes;
    bool m_subsetActive = false;
    QPointer<QAbstractItemModel> m_pSubsetModel;
    QRectF m_labelBuiltViewportSceneRect;
    qreal m_labelBuiltTransformScale = 0.0;

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
    ControlProxy* m_pLayoutDegradedProxy = nullptr;
    ControlProxy* m_pColorProxy = nullptr;
    ControlProxy* m_p3dProxy = nullptr;
    ControlProxy* m_pHaloProxy = nullptr;
    ControlProxy* m_pTrailProxy = nullptr;
    ControlProxy* m_pKnobFocusProxy = nullptr;
    ControlProxy* m_pReloadProxy = nullptr;
    ControlProxy* m_pMoveVerticalProxy = nullptr;
};

} // namespace crate
