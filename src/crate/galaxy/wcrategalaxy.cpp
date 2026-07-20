#include "crate/galaxy/wcrategalaxy.h"

#include <QDir>
#include <QFileInfo>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QInputDialog>
#include <QLineEdit>
#include <QStyle>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QVariantAnimation>
#include <QRandomGenerator>
#include <QtMath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "crate/intelligence/scores.h"
#include "library/library.h"
#include "library/trackcollectionmanager.h"
#include "library/trackmodel.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "track/track.h"
#include "track/trackid.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("WCrateGalaxy");

constexpr double kSceneSpan = 2000.0; // normalized coordinate span
constexpr double kDotRadius = 3.5;    // px, transform-independent
constexpr double kHaloRadius = 11.0;  // px, transform-independent
constexpr double kCursorRingRadius = 8.5; // px, transform-independent
constexpr double kClusterFadeStart = 1.4;
constexpr double kClusterFadeEnd = 1.8;
constexpr double kArtistFadeOutStart = 2.9;
constexpr double kArtistFadeOutEnd = 3.4;
constexpr double kArtistClumpDistance = kSceneSpan * 0.06;
constexpr double kGhostColorStrength = 0.20;
class GalaxyPillItem final : public QGraphicsItem {
  public:
    GalaxyPillItem(const crate::GalaxyNode& node,
            const QColor& accent,
            const QColor& ground,
            const QColor& ink,
            int index)
            : m_node(node), m_accent(accent), m_ground(ground), m_ink(ink) {
        setFlag(ItemIgnoresTransformations, true);
        setData(0, index);
        setZValue(10.0);
    }
    QRectF boundingRect() const override {
        return QRectF(7.0, -25.0, 172.0, 50.0);
    }
    void paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override {
        const QRectF box = boundingRect();
        p->setRenderHint(QPainter::Antialiasing, true);
        QColor border(m_ink);
        border.setAlpha(46);
        QColor fill(m_ground);
        fill.setAlpha(235);
        p->setPen(QPen(border, 1.0));
        p->setBrush(fill);
        p->drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
        QFont font(QStringLiteral("IBM Plex Mono"));
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(10);
        p->setFont(font);
        const QFontMetrics fm(font);
        const auto elide = [&fm](const QString& text) {
            return fm.elidedText(text, Qt::ElideRight, 158);
        };
        p->setPen(m_ink);
        p->drawText(QPointF(14.0, -10.0), elide(m_node.title));
        p->drawText(QPointF(14.0, 3.0), elide(m_node.artist));
        p->setPen(m_accent.lighter(120));
        QStringList details;
        if (!m_node.keyCamelot.isEmpty()) {
            details.append(m_node.keyCamelot);
        }
        if (m_node.bpm > 0.0 && std::isfinite(m_node.bpm)) {
            details.append(QString::number(m_node.bpm, 'f', 1));
        }
        if (!details.isEmpty()) {
            p->drawText(QPointF(14.0, 17.0), details.join(QStringLiteral("  \u00b7  ")));
        }
    }
  private:
    crate::GalaxyNode m_node;
    QColor m_accent;
    QColor m_ground;
    QColor m_ink;
};

// Faithful to Crate v1 map_view._cluster_color: golden-ratio hue walk,
// noise/unclustered = grey.
QColor clusterColor(int cid) {
    if (cid < 0) {
        return QColor(110, 115, 125);
    }
    return QColor::fromHsvF(std::fmod(cid * 0.61803398875, 1.0), 0.6, 1.0);
}

QColor keyColor(const QString& key) {
    static const QRegularExpression camelot(
            QStringLiteral("^\\s*(1[0-2]|[1-9])([AB])\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = camelot.match(key);
    if (!match.hasMatch()) {
        return QColor(150, 150, 160);
    }
    const int number = match.captured(1).toInt();
    const bool major = match.captured(2).compare(
                               QStringLiteral("B"), Qt::CaseInsensitive) == 0;
    return QColor::fromHsvF((number - 1) / 12.0, 0.65, major ? 1.0 : 0.8);
}

QColor rampColor(bool tempo, double fraction) {
    const double f = qBound(0.0, fraction, 1.0);
    if (tempo) {
        return QColor::fromHsvF(0.66 * (1.0 - f), 0.72, 1.0);
    }
    return QColor::fromHsvF(0.5 * (1.0 - f) + 0.08 * f, 0.7, 1.0);
}

} // namespace

namespace crate {

WCrateGalaxy::WCrateGalaxy(QWidget* pParent,
        PlayerManager* pPlayerManager,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        GalaxyPalette palette)
        : QGraphicsView(pParent),
          m_pPlayerManager(pPlayerManager),
          m_pLibrary(pLibrary),
          m_pConfig(pConfig),
          m_palette(std::move(palette)),
          m_pScene(new QGraphicsScene(this)),
          m_pLayoutAnimation(new QVariantAnimation(this)) {
    setScene(m_pScene);
    // Input diagnostics, off by default: set [Crate],debug_input 1 to log which
    // widget receives every mouse press (added for the intermittent
    // cannot-rotate-3D report 2026-07-19; presses reached the widget fine when
    // retested under instrumentation).
    m_debugInput = m_pConfig->getValue(ConfigKey("[Crate]", "debug_input"), 0) != 0;
    if (m_debugInput) {
        qApp->installEventFilter(this);
    }
    setBackgroundBrush(m_palette.ground);
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    setRenderHint(QPainter::Antialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_3dMode = m_pConfig->getValue(ConfigKey("[Crate]", "galaxy_3d"), 0) != 0;
    m_halosEnabled = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_halos"), 1) != 0;
    m_trailEnabled = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_trail"), 1) != 0;
    const QString debugOrbit = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_debug_orbit"), QString());
    const QStringList orbitParts = debugOrbit.split(',');
    if (m_3dMode && orbitParts.size() == 2) {
        bool azOk = false;
        bool elOk = false;
        const double az = orbitParts[0].trimmed().toDouble(&azOk);
        const double el = orbitParts[1].trimmed().toDouble(&elOk);
        if (azOk && elOk) {
            m_azimuth = az;
            m_elevation = qBound(-85.0, el, 85.0);
        }
    }
    if (m_3dMode) {
        setDragMode(QGraphicsView::NoDrag);
    }
    const QString savedMode = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_color_mode"), QStringLiteral("cluster"))
                                      .trimmed()
                                      .toLower();
    if (savedMode == QStringLiteral("key")) {
        m_colorMode = ColorMode::Key;
    } else if (savedMode == QStringLiteral("tempo")) {
        m_colorMode = ColorMode::Tempo;
    } else if (savedMode == QStringLiteral("energy")) {
        m_colorMode = ColorMode::Energy;
    }
    const QString savedLayout = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_layout"), QStringLiteral("scatter"))
                                        .trimmed()
                                        .toLower();
    if (savedLayout == QStringLiteral("key")) {
        m_layoutMode = LayoutMode::KeyWheel;
    } else if (savedLayout == QStringLiteral("bpm")) {
        m_layoutMode = LayoutMode::BpmSerpentine;
    } else if (savedLayout == QStringLiteral("artist")) {
        m_layoutMode = LayoutMode::Artist;
    }
    populate();
    if (m_pLibrary != nullptr) {
        connect(m_pLibrary,
                &Library::showTrackModel,
                this,
                [this](QAbstractItemModel* pModel, bool) { bindSubsetModel(pModel); });
        bindSubsetModel(m_pLibrary->currentTrackTableModel());
    }
    PlayerInfo& playerInfo = PlayerInfo::instance();
    connect(&playerInfo,
            &PlayerInfo::currentPlayingTrackChanged,
            this,
            &WCrateGalaxy::playingTrackChanged);
    connect(&playerInfo,
            &PlayerInfo::currentPlayingDeckChanged,
            this,
            [this](int) { updateMixabilityHalos(); });
    connect(&playerInfo,
            &PlayerInfo::trackChanged,
            this,
            [this](const QString&, const TrackPointer&, const TrackPointer&) {
                updateMixabilityHalos();
            });
    updateMixabilityHalos();

    m_pLabelRebuildTimer = new QTimer(this);
    m_pLabelRebuildTimer->setSingleShot(true);
    m_pLabelRebuildTimer->setInterval(120);
    connect(m_pLabelRebuildTimer, &QTimer::timeout,
            this, &WCrateGalaxy::rebuildLabelCache);

    // Cursor highlight: a hollow mono ring, distinct from the now-playing dot
    // (filled white) and the mixability halos (blue radial glow). Kept above
    // everything, transform-independent, hidden until the walk seeds a cursor.
    m_pCursorRing = new QGraphicsEllipseItem(
            -kCursorRingRadius, -kCursorRingRadius,
            kCursorRingRadius * 2, kCursorRingRadius * 2);
    m_pCursorRing->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    m_pCursorRing->setPen(QPen(m_palette.ink, 1.6));
    m_pCursorRing->setBrush(Qt::NoBrush);
    m_pCursorRing->setZValue(200.0);
    m_pCursorRing->setVisible(false);
    m_pScene->addItem(m_pCursorRing);

    // knob_focus: persisted toggle (default TABLE = stock behavior). Exposed as
    // a ControlObject so a future FLX4 mapping / other UI can read or flip it.
    const bool mapFocus = m_pConfig->getValue(
            ConfigKey("[Crate]", "knob_focus"), 0) != 0;
    m_knobFocusMap = mapFocus;

    m_pLayoutProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_layout_control"), this);
    m_pLayoutDegradedProxy = new ControlProxy(
            ConfigKey("[Crate]", "galaxy_layout_degraded_count"), this);
    m_pColorProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_color_control"), this);
    m_p3dProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_3d"), this);
    m_pHaloProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_halos"), this);
    m_pTrailProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_trail"), this);
    m_pKnobFocusProxy = new ControlProxy(ConfigKey("[Crate]", "knob_focus"), this);
    m_pLayoutProxy->connectValueChanged(this, [this](double value) {
        setLayoutMode(static_cast<LayoutMode>(qBound(0, qRound(value), 3)));
    });
    m_pColorProxy->connectValueChanged(this, [this](double value) {
        setColorMode(static_cast<ColorMode>(qBound(0, qRound(value), 3)));
    });
    m_p3dProxy->connectValueChanged(this, [this](double value) { set3dMode(value != 0.0); });
    m_pHaloProxy->connectValueChanged(this, [this](double value) { setHalosEnabled(value != 0.0); });
    m_pTrailProxy->connectValueChanged(this, [this](double value) { setTrailEnabled(value != 0.0); });
    m_pKnobFocusProxy->connectValueChanged(
            this, [this](double value) { setKnobFocusMap(value != 0.0); });
    m_pLayoutProxy->set(static_cast<int>(m_layoutMode));
    publishLayoutDegradation(m_layoutMode);
    m_pColorProxy->set(static_cast<int>(m_colorMode));
    m_p3dProxy->set(m_3dMode ? 1.0 : 0.0);
    m_pHaloProxy->set(m_halosEnabled ? 1.0 : 0.0);
    m_pTrailProxy->set(m_trailEnabled ? 1.0 : 0.0);
    m_pKnobFocusProxy->set(m_knobFocusMap ? 1.0 : 0.0);

    // galaxy_load: a push control that loads the CURSOR track into the natural
    // next-prep deck (never yanks a playing one; same never-steal rule as the
    // double-click load). Mirrors the double-click path but is reachable from a
    // controller / other UI.
    m_pGalaxyLoadCO = std::make_unique<ControlPushButton>(
            ConfigKey("[Crate]", "galaxy_load"));
    connect(m_pGalaxyLoadCO.get(),
            &ControlObject::valueChanged,
            this,
            [this](double v) {
                if (v != 0.0) {
                    loadCursorToNextPrepDeck();
                }
            });

    m_pReloadProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_reload"), this);
    m_pReloadProxy->connectValueChanged(this, [this](double value) {
        if (value != 0.0) {
            reloadSidecars();
        }
    });

    // Browse knob -> [Library],MoveVertical (stock encoder the FLX4 already
    // drives). When knob focus is MAP we step the galaxy cursor; TABLE leaves
    // the stock library scroll untouched.
    m_pMoveVerticalProxy = new ControlProxy(
            ConfigKey("[Library]", "MoveVertical"), this);
    if (m_pMoveVerticalProxy->valid()) {
        m_pMoveVerticalProxy->connectValueChanged(
                this, &WCrateGalaxy::onKnobMove);
    }

}

WCrateGalaxy::~WCrateGalaxy() = default;

void WCrateGalaxy::populate() {
    const QString sidecarDir = m_pConfig->getValue(
            ConfigKey("[Crate]", "sidecar_dir"), QString());
    m_musicRoot = m_pConfig->getValue(
            ConfigKey("[Crate]", "music_root"), QString());
    if (m_musicRoot.isEmpty() && !sidecarDir.isEmpty()) {
        m_musicRoot = QFileInfo(sidecarDir).dir().absolutePath();
    }

    if (sidecarDir.isEmpty()) {
        auto* pText = m_pScene->addSimpleText(
                QStringLiteral("galaxy: set [Crate] sidecar_dir in crate.cfg"));
        pText->setBrush(m_palette.ink);
        return;
    }

    if (!reloadSidecars()) {
        auto* pText = m_pScene->addSimpleText(
                QStringLiteral("galaxy: sidecars unavailable"));
        pText->setBrush(m_palette.ink);
    }
}

bool WCrateGalaxy::reloadSidecars() {
    const QString sidecarDir = m_pConfig->getValue(
            ConfigKey("[Crate]", "sidecar_dir"), QString());
    if (sidecarDir.isEmpty()) {
        qInfo() << "galaxy sidecar reload skipped: sidecar_dir is empty";
        return false;
    }

    CrateSidecars sidecars(sidecarDir);
    if (!sidecars.load()) {
        qInfo() << "galaxy sidecar reload kept prior scene:" << sidecars.lastError();
        return false;
    }
    rebuildScene(sidecars.nodes());
    m_lastSidecarModified = QFileInfo(
            QDir(sidecarDir).filePath(QStringLiteral("umap.sqlite"))).lastModified();
    ++m_sidecarRebuildCount;
    kLogger.info() << "galaxy populated with" << m_nodes.size() << "nodes from" << sidecarDir;
    return true;
}

void WCrateGalaxy::reloadSidecarsIfChanged() {
    const QString sidecarDir = m_pConfig->getValue(
            ConfigKey("[Crate]", "sidecar_dir"), QString());
    const QFileInfo umapInfo(QDir(sidecarDir).filePath(QStringLiteral("umap.sqlite")));
    if (umapInfo.lastModified() != m_lastSidecarModified) {
        reloadSidecars();
    }
}

void WCrateGalaxy::rebuildScene(const QVector<GalaxyNode>& nodes) {
    if (m_pLayoutAnimation->state() != QAbstractAnimation::Stopped) {
        m_pLayoutAnimation->stop();
    }
    for (QGraphicsItem* pItem : m_pScene->items()) {
        if (pItem != m_pCursorRing) {
            delete pItem;
        }
    }
    m_dots.clear();
    m_halos.clear();
    m_pills.clear();
    m_clusterLabels.clear();
    m_artistLabels.clear();
    m_trackLabels.clear();
    m_scatterPositions.clear();
    m_nodeByRelpath.clear();
    m_subsetNodes.clear();
    m_subsetActive = false;
    m_hoveredNode = -1;
    resetWalk();
    m_playTrail.clear();
    m_trailDeviceLines.clear();
    m_trailDeviceColors.clear();
    m_plexusSegments.clear();
    m_plexusDeviceLines.clear();
    m_plexusDeviceColors.clear();
    m_plexusDeviceWidths.clear();
    m_deckPlexusScores.clear();
    m_deckPlayingNodes.clear();
    m_deckIsPlaying.clear();
    m_sonicVectors = SonicVectors();
    m_vectorsLoadAttempted = false;
    m_nodes = nodes;
    if (m_nodes.isEmpty()) {
        auto* pText = m_pScene->addSimpleText(
                QStringLiteral("galaxy: no analyzed tracks yet"));
        pText->setBrush(m_palette.ink);
        m_pScene->setSceneRect(pText->boundingRect());
        viewport()->update();
        return;
    }
    m_nodeByRelpath.reserve(m_nodes.size());
    for (int i = 0; i < m_nodes.size(); ++i) {
        m_nodeByRelpath.insert(
                QDir::fromNativeSeparators(m_nodes[i].relpath).toCaseFolded(), i);
    }

    QVector<double> tempos;
    QVector<double> energies;
    for (const GalaxyNode& node : std::as_const(m_nodes)) {
        if (node.bpm > 0.0 && std::isfinite(node.bpm)) {
            tempos.append(node.bpm);
        }
        if (node.energy > 0.0 && std::isfinite(node.energy)) {
            energies.append(node.energy);
        }
    }
    m_tempoRange = percentileRange(tempos);
    m_energyRange = percentileRange(energies);

    double minX = m_nodes[0].x, maxX = m_nodes[0].x;
    double minY = m_nodes[0].y, maxY = m_nodes[0].y;
    for (const GalaxyNode& node : m_nodes) {
        minX = qMin(minX, node.x);
        maxX = qMax(maxX, node.x);
        minY = qMin(minY, node.y);
        maxY = qMax(maxY, node.y);
    }
    const double spanX = (maxX > minX) ? (maxX - minX) : 1.0;
    const double spanY = (maxY > minY) ? (maxY - minY) : 1.0;

    for (int i = 0; i < m_nodes.size(); ++i) {
        const GalaxyNode& node = m_nodes[i];
        const double sx = (node.x - minX) / spanX * kSceneSpan;
        const double sy = (node.y - minY) / spanY * kSceneSpan;
        auto* pHalo = new QGraphicsEllipseItem(
                -kHaloRadius, -kHaloRadius, kHaloRadius * 2, kHaloRadius * 2);
        pHalo->setPos(sx, sy);
        pHalo->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        pHalo->setPen(Qt::NoPen);
        pHalo->setVisible(false);
        pHalo->setZValue(-100.0);
        m_pScene->addItem(pHalo);
        m_halos.append(pHalo);
        auto* pDot = new QGraphicsEllipseItem(
                -kDotRadius, -kDotRadius, kDotRadius * 2, kDotRadius * 2);
        pDot->setPos(sx, sy);
        m_scatterPositions.append(QPointF(sx, sy));
        pDot->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        pDot->setPen(Qt::NoPen);
        pDot->setBrush(nodeColor(node));
        pDot->setData(0, i);
        m_pScene->addItem(pDot);
        m_dots.append(pDot);
    }
    m_pScene->setSceneRect(0.0, 0.0, kSceneSpan, kSceneSpan);
    if (m_3dMode) {
        update3dProjection();
    } else if (m_layoutMode != LayoutMode::Scatter) {
        setLayoutMode(m_layoutMode, false);
    }
    recomputeSubsetFromModel();
    updateMixabilityHalos();
    rebuildLabelCache();
    rebuildOverlayCache();
    applyHaloVisuals();
}

void WCrateGalaxy::bindSubsetModel(QAbstractItemModel* pModel) {
    if (m_pSubsetModel == pModel) {
        recomputeSubsetFromModel();
        return;
    }
    if (m_pSubsetModel != nullptr) {
        disconnect(m_pSubsetModel, nullptr, this, nullptr);
    }
    m_pSubsetModel = pModel;
    if (pModel == nullptr) {
        applySubset({}, false);
        return;
    }
    const auto refresh = [this] { recomputeSubsetFromModel(); };
    connect(pModel, &QAbstractItemModel::modelReset, this, refresh);
    connect(pModel, &QAbstractItemModel::rowsInserted, this, refresh);
    connect(pModel, &QAbstractItemModel::rowsRemoved, this, refresh);
    connect(pModel, &QAbstractItemModel::layoutChanged, this, refresh);
    connect(pModel, &QAbstractItemModel::dataChanged, this, refresh);
    recomputeSubsetFromModel();
}

void WCrateGalaxy::recomputeSubsetFromModel() {
    auto* pModel = m_pSubsetModel.data();
    auto* pTrackModel = pModel ? dynamic_cast<TrackModel*>(pModel) : nullptr;
    if (pTrackModel == nullptr) {
        applySubset({}, false);
        return;
    }
    QSet<QString> relpaths;
    relpaths.reserve(pModel->rowCount());
    for (int row = 0; row < pModel->rowCount(); ++row) {
        const QString relpath = relpathForLocation(
                pTrackModel->getTrackLocation(pModel->index(row, 0)));
        if (!relpath.isEmpty()) {
            relpaths.insert(relpath);
        }
    }
    applySubset(relpaths);
}

void WCrateGalaxy::applySubset(const QSet<QString>& relpaths, bool active) {
    QSet<int> nodes;
    nodes.reserve(relpaths.size());
    for (const QString& relpath : relpaths) {
        const auto it = m_nodeByRelpath.constFind(
                QDir::fromNativeSeparators(relpath).toCaseFolded());
        if (it != m_nodeByRelpath.constEnd()) {
            nodes.insert(*it);
        }
    }
    m_subsetActive = active && nodes.size() != m_nodes.size();
    m_subsetNodes = m_subsetActive ? std::move(nodes) : QSet<int>();
    if (m_hoveredNode >= 0 && !nodeInSubset(m_hoveredNode)) {
        setHoveredNode(-1);
    }
    if (m_cursorNode >= 0 && !nodeInSubset(m_cursorNode)) {
        resetWalk();
    }
    updateMixabilityHalos();
    if (m_3dMode) {
        update3dProjection();
    } else {
        applyHaloVisuals();
        rebuildLabelCache();
        updateCursorVisual();
    }
}

bool WCrateGalaxy::nodeInSubset(int index) const {
    return index >= 0 && index < m_nodes.size() &&
            (!m_subsetActive || m_subsetNodes.contains(index));
}

QColor WCrateGalaxy::subsetColor(int index, const QColor& color) const {
    if (nodeInSubset(index)) {
        return color;
    }
    return QColor(qRound(m_palette.ground.red() + (color.red() - m_palette.ground.red()) * kGhostColorStrength),
            qRound(m_palette.ground.green() + (color.green() - m_palette.ground.green()) * kGhostColorStrength),
            qRound(m_palette.ground.blue() + (color.blue() - m_palette.ground.blue()) * kGhostColorStrength),
            color.alpha());
}

QString WCrateGalaxy::relpathForLocation(const QString& location) const {
    // Root-independent matching. The same share can be spelled many ways on
    // Windows (mapped drive Z:\..., UNC \\host\share\..., differing case), and
    // the configured music_root may use yet another spelling — a prefix strip
    // against music_root silently resolved NOTHING when the library stored
    // Z:/ paths while music_root was the UNC form (everything ghosted, no
    // pills, no halo seed). Instead, walk the location's path suffixes against
    // the known node relpaths: the full relative path incl. filename makes a
    // false hit practically impossible, and any root spelling works.
    if (location.isEmpty() || m_nodeByRelpath.isEmpty()) {
        return QString();
    }
    // Backslashes are normalized EXPLICITLY, not via fromNativeSeparators: on
    // Linux/macOS that call is a no-op, but a library DB migrated from Windows
    // still carries Z:\ / UNC spellings — those must resolve on any platform
    // (caught by the first Linux CI run).
    const QString path =
            QDir::cleanPath(QFileInfo(QString(location).replace(
                                              QLatin1Char('\\'), QLatin1Char('/')))
                                    .absoluteFilePath())
                    .toCaseFolded();
    int from = 0;
    while (true) {
        const int slash = path.indexOf(QLatin1Char('/'), from);
        if (slash < 0) {
            return QString();
        }
        const auto it = m_nodeByRelpath.constFind(path.mid(slash + 1));
        if (it != m_nodeByRelpath.constEnd()) {
            // Return the node's stored relpath (original case/separators) so
            // exact-string consumers (m_playingRelpath == node.relpath) match.
            return m_nodes[*it].relpath;
        }
        from = slash + 1;
    }
}

void WCrateGalaxy::updateMixabilityHalos() {
    m_deckPlayingNodes.clear();
    m_deckIsPlaying.clear();
    m_deckPlexusScores.clear();
    m_plexusSegments.clear();
    if (!m_halosEnabled || m_nodes.isEmpty()) {
        applyHaloVisuals();
        rebuildOverlayCache();
        return;
    }
    if (!m_vectorsLoadAttempted) {
        m_vectorsLoadAttempted = true;
        const QString sidecarDir = m_pConfig->getValue(
                ConfigKey("[Crate]", "sidecar_dir"), QString());
        if (!m_sonicVectors.load(QDir(sidecarDir).filePath(
                    QStringLiteral("music_vectors.sqlite")))) {
            kLogger.warning() << "mixability vectors unavailable:"
                              << m_sonicVectors.lastError();
        }
    }
    QVector<QString> requested;
    const QString debugSeed = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_debug_seed"), QString()).trimmed();
    if (!debugSeed.isEmpty()) {
        requested.append(QDir::fromNativeSeparators(debugSeed));
    } else {
        const QVector<bool> loaded = deckLoadedStates();
        const QVector<bool> playing = deckPlayingStates();
        requested.resize(loaded.size());
        for (int deck = 0; deck < loaded.size(); ++deck) {
            if (loaded[deck]) {
                const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(
                        PlayerManager::groupForDeck(deck));
                requested[deck] = pTrack ? relpathForLocation(pTrack->getLocation()) : QString();
                m_deckIsPlaying.insert(deck, playing.value(deck, false));
            }
        }
    }
    for (int deck = 0; deck < requested.size(); ++deck) {
        const auto nodeIt = m_nodeByRelpath.constFind(
                requested[deck].replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded());
        if (nodeIt == m_nodeByRelpath.constEnd() || !nodeInSubset(*nodeIt)) {
            continue;
        }
        const int playingIndex = *nodeIt;
        const QString& playingRelpath = m_nodes[playingIndex].relpath;
        if (!m_sonicVectors.centered(playingRelpath)) {
            continue;
        }
        m_deckPlayingNodes.insert(deck, playingIndex);
        m_deckPlexusScores.insert(deck, {});
        QVector<QPair<double, int>> ranked;
        const GalaxyNode& playingNode = m_nodes[playingIndex];
        for (int i = 0; i < m_nodes.size(); ++i) {
            if (i == playingIndex || !nodeInSubset(i)) continue;
            const GalaxyNode& node = m_nodes[i];
            const auto sonic = m_sonicVectors.cosine(playingRelpath, node.relpath);
            if (!sonic) continue;
            const bool keys = !playingNode.keyCamelot.isEmpty() && !node.keyCamelot.isEmpty();
            const bool bpms = playingNode.bpm > 0.0 && node.bpm > 0.0 &&
                    std::isfinite(playingNode.bpm) && std::isfinite(node.bpm);
            const double score = scores::mixability(sonic, keys, keys,
                    keys ? scores::keyScore(playingNode.keyCamelot, node.keyCamelot) : 0.0,
                    bpms, bpms,
                    bpms ? scores::bpmScore(playingNode.bpm, node.bpm) : 0.0,
                    m_sonicVectors.transition(playingRelpath, node.relpath));
            if (score >= 0.5) ranked.append(qMakePair(score, i));
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        ranked.resize(qMin(40, ranked.size()));
        for (const auto& entry : std::as_const(ranked)) {
            m_deckPlexusScores[deck].insert(entry.second, entry.first);
            m_plexusSegments.append({deck, playingIndex, entry.second, entry.first,
                    m_deckIsPlaying.value(deck, false)});
        }
    }
    applyHaloVisuals();
    rebuildOverlayCache();
}

void WCrateGalaxy::applyHaloVisuals() {
    for (int i = 0; i < m_dots.size(); ++i) {
        int playingDeck = -1;
        for (auto it = m_deckPlayingNodes.constBegin(); it != m_deckPlayingNodes.constEnd(); ++it) {
            if (it.value() == i) { playingDeck = it.key(); break; }
        }
        const bool playing = playingDeck >= 0;
        QGraphicsEllipseItem* pDot = m_dots[i];
        const double radius = playing ? kDotRadius * 1.65 : kDotRadius;
        if (!m_3dMode) {
            pDot->setRect(-radius, -radius, radius * 2, radius * 2);
        }
        QColor deckColor = playingDeck % 2 == 0
                ? m_palette.accentDeckA : m_palette.accentDeckB;
        if (playingDeck >= 2) deckColor.setAlpha(153);
        pDot->setPen(playing ? QPen(deckColor, 1.2) : QPen(Qt::NoPen));
        if (playing) {
            pDot->setBrush(deckColor);
            pDot->setZValue(100.0);
        } else if (!m_3dMode) {
            pDot->setBrush(subsetColor(i, nodeColor(m_nodes[i])));
            pDot->setZValue(0.0);
        }

        QGraphicsEllipseItem* pHalo = m_halos.value(i, nullptr);
        if (!pHalo) {
            continue;
        }
        pHalo->setPos(pDot->pos());
        int ringDeck = playingDeck;
        double ringScore = playing ? 1.0 : 0.0;
        for (auto deckIt = m_deckPlexusScores.constBegin(); deckIt != m_deckPlexusScores.constEnd(); ++deckIt) {
            const auto scoreIt = deckIt.value().constFind(i);
            if (scoreIt != deckIt.value().constEnd() && *scoreIt > ringScore) {
                ringDeck = deckIt.key(); ringScore = *scoreIt;
            }
        }
        const bool visible = nodeInSubset(i) && ringDeck >= 0 && pDot->isVisible();
        pHalo->setVisible(visible);
        if (visible) {
            QColor color = ringDeck % 2 == 0
                    ? m_palette.accentDeckA : m_palette.accentDeckB;
            const double activityAlpha =
                    m_deckIsPlaying.value(ringDeck, false) ? 1.0 : 0.65;
            color.setAlpha(qRound(activityAlpha *
                    (ringDeck >= 2 ? 105 : qRound(80 + 110 * ringScore))));
            pHalo->setBrush(Qt::NoBrush);
            pHalo->setPen(QPen(color, 1.0 + ringScore));
        }
    }
    viewport()->update();
}

WCrateGalaxy::ValueRange WCrateGalaxy::percentileRange(const QVector<double>& values) {
    if (values.isEmpty()) {
        return {};
    }
    QVector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&sorted](double p) {
        const double position = p * (sorted.size() - 1);
        const int lower = static_cast<int>(std::floor(position));
        const int upper = static_cast<int>(std::ceil(position));
        const double blend = position - lower;
        return sorted[lower] * (1.0 - blend) + sorted[upper] * blend;
    };
    return {percentile(0.02), percentile(0.98), true};
}

QColor WCrateGalaxy::nodeColor(const GalaxyNode& node) const {
    if (m_colorMode == ColorMode::Cluster) {
        return clusterColor(node.clusterId);
    }
    if (m_colorMode == ColorMode::Key) {
        return keyColor(node.keyCamelot);
    }
    const double value = m_colorMode == ColorMode::Tempo ? node.bpm : node.energy;
    const ValueRange& range =
            m_colorMode == ColorMode::Tempo ? m_tempoRange : m_energyRange;
    if (value <= 0.0 || !std::isfinite(value) || !range.valid) {
        return QColor(120, 120, 130);
    }
    const double fraction = range.high > range.low
            ? (value - range.low) / (range.high - range.low)
            : 0.5;
    return rampColor(m_colorMode == ColorMode::Tempo, fraction);
}

void WCrateGalaxy::updateColors() {
    for (int i = 0; i < m_dots.size(); ++i) {
        m_dots[i]->setBrush(subsetColor(i, nodeColor(m_nodes[i])));
    }
    if (m_3dMode) {
        update3dProjection();
    } else {
        applyHaloVisuals();
    }
    rebuildLabelCache();
    viewport()->update();
}

void WCrateGalaxy::setColorMode(ColorMode mode) {
    if (m_colorMode == mode) {
        return;
    }
    m_colorMode = mode;
    QString value = QStringLiteral("cluster");
    if (mode == ColorMode::Key) {
        value = QStringLiteral("key");
    } else if (mode == ColorMode::Tempo) {
        value = QStringLiteral("tempo");
    } else if (mode == ColorMode::Energy) {
        value = QStringLiteral("energy");
    }
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_color_mode"), value);
    if (m_pColorProxy && qRound(m_pColorProxy->get()) != static_cast<int>(mode)) {
        m_pColorProxy->set(static_cast<int>(mode));
    }
    updateColors();
}

void WCrateGalaxy::applyPositions(const QVector<QPointF>& positions) {
    for (int i = 0; i < qMin(positions.size(), m_dots.size()); ++i) {
        m_dots[i]->setPos(positions[i]);
        m_halos[i]->setPos(positions[i]);
    }
    updateCursorVisual();
    rebuildLabelCache();
    rebuildOverlayCache();
    viewport()->update();
}

QVector<QPointF> WCrateGalaxy::separate(
        const QVector<QPointF>& positions, double shrink) const {
    QVector<QPointF> result = positions;
    constexpr double minimum = 12.0;
    constexpr double minimumSquared = minimum * minimum;
    QRandomGenerator rng(7);
    for (int iteration = 0; iteration < 50; ++iteration) {
        QHash<QPair<int, int>, QVector<int>> grid;
        for (int i = 0; i < result.size(); ++i) {
            grid[qMakePair(qFloor(result[i].x() / minimum),
                    qFloor(result[i].y() / minimum))]
                    .append(i);
        }
        QVector<QPointF> displacement(result.size());
        bool pushed = false;
        for (auto it = grid.constBegin(); it != grid.constEnd(); ++it) {
            for (int i : it.value()) {
                for (int dx = -1; dx <= 1; ++dx) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        const QVector<int> neighbours = grid.value(
                                qMakePair(it.key().first + dx, it.key().second + dy));
                        for (int j : neighbours) {
                            if (j <= i) continue;
                            QPointF delta = result[i] - result[j];
                            double distanceSquared = QPointF::dotProduct(delta, delta);
                            if (distanceSquared >= minimumSquared) continue;
                            double distance = std::sqrt(distanceSquared);
                            if (distanceSquared < 1e-9) {
                                const double angle = rng.generateDouble() * 2.0 * M_PI;
                                delta = QPointF(std::cos(angle), std::sin(angle));
                                distance = 0.0;
                            } else {
                                delta /= distance;
                            }
                            const QPointF push = delta * ((minimum - distance) * 0.7);
                            displacement[i] += push;
                            displacement[j] -= push;
                            pushed = true;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < result.size(); ++i) result[i] += displacement[i];
        if (!pushed) break;
    }
    if (shrink != 1.0 && !result.isEmpty()) {
        QPointF center;
        for (const QPointF& point : std::as_const(result)) center += point;
        center /= result.size();
        for (QPointF& point : result) point = center + (point - center) * shrink;
    }
    return result;
}

QVector<QPointF> WCrateGalaxy::layoutTarget(LayoutMode mode) const {
    if (mode == LayoutMode::Scatter) return m_scatterPositions;
    QVector<QPointF> target = m_scatterPositions;
    QRandomGenerator rng(42);
    const auto normal = [&rng] {
        const double u1 = qMax(rng.generateDouble(), 1e-12);
        return std::sqrt(-2.0 * std::log(u1)) *
                std::cos(2.0 * M_PI * rng.generateDouble());
    };
    if (mode == LayoutMode::KeyWheel) {
        static const QRegularExpression camelot(QStringLiteral("^\\s*(1[0-2]|[1-9])([AB])\\s*$"),
                QRegularExpression::CaseInsensitiveOption);
        for (int i = 0; i < m_nodes.size(); ++i) {
            const auto match = camelot.match(m_nodes[i].keyCamelot);
            if (!match.hasMatch()) {
                target[i] = QPointF(0.5 * kSceneSpan, 0.03 * kSceneSpan);
                continue;
            }
            const int number = match.captured(1).toInt();
            const bool inner = match.captured(2).compare(QStringLiteral("A"), Qt::CaseInsensitive) == 0;
            const double angle = (number - 1) / 12.0 * 2.0 * M_PI - M_PI / 2.0;
            const double radius = (inner ? 0.27 : 0.46) * kSceneSpan;
            target[i] = QPointF(0.5 * kSceneSpan + radius * std::cos(angle) + normal() * 0.008 * kSceneSpan,
                    0.5 * kSceneSpan + radius * std::sin(angle) + normal() * 0.008 * kSceneSpan);
        }
        return separate(target, 0.95);
    }
    if (mode == LayoutMode::BpmSerpentine) {
        QVector<int> order;
        for (int i = 0; i < m_nodes.size(); ++i) {
            if (m_nodes[i].bpm > 0.0 && std::isfinite(m_nodes[i].bpm)) {
                order.append(i);
            }
        }
        std::sort(order.begin(), order.end(), [this](int a, int b) { return m_nodes[a].bpm < m_nodes[b].bpm; });
        for (int rank = 0; rank < order.size(); ++rank) {
            const double f = rank / qMax(1.0, static_cast<double>(order.size() - 1));
            const double y = 0.50 * kSceneSpan + 0.24 * kSceneSpan * std::sin(f * 2.0 * M_PI * 1.25) +
                    0.07 * kSceneSpan * std::sin(f * 2.0 * M_PI * 2.6 + 1.3);
            target[order[rank]] = QPointF((0.08 + 0.84 * f) * kSceneSpan, y);
        }
        for (int i = 0; i < m_nodes.size(); ++i) {
            if (!(m_nodes[i].bpm > 0.0) || !std::isfinite(m_nodes[i].bpm)) {
                target[i] = QPointF(0.5 * kSceneSpan, 0.97 * kSceneSpan);
            }
        }
        return separate(target, 0.97);
    }
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].hasArtistPosition) {
            target[i] = QPointF((m_nodes[i].artistX + normal() * 0.018) * kSceneSpan,
                    (m_nodes[i].artistY + normal() * 0.018) * kSceneSpan);
        } else {
            target[i] = QPointF((0.04 + normal() * 0.02) * kSceneSpan,
                    (0.96 + normal() * 0.02) * kSceneSpan);
        }
    }
    return separate(target, 0.95);
}

int WCrateGalaxy::layoutDegradedCount(LayoutMode mode) const {
    static const QRegularExpression camelot(
            QStringLiteral("^\\s*(1[0-2]|[1-9])([AB])\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
    int count = 0;
    for (const GalaxyNode& node : m_nodes) {
        if ((mode == LayoutMode::KeyWheel && !camelot.match(node.keyCamelot).hasMatch()) ||
                (mode == LayoutMode::BpmSerpentine &&
                        (!(node.bpm > 0.0) || !std::isfinite(node.bpm))) ||
                (mode == LayoutMode::Artist && !node.hasArtistPosition)) {
            ++count;
        }
    }
    return count;
}

void WCrateGalaxy::publishLayoutDegradation(LayoutMode mode) {
    if (m_pLayoutDegradedProxy) {
        m_pLayoutDegradedProxy->set(layoutDegradedCount(mode));
    }
}

void WCrateGalaxy::setLayoutMode(LayoutMode mode, bool animate) {
    if (m_3dMode) {
        set3dMode(false);
    }
    // The displayed coordinate space changed, so the walk's nearest-neighbor
    // metric and history no longer apply — reset.
    resetWalk();
    m_layoutMode = mode;
    QString value = QStringLiteral("scatter");
    if (mode == LayoutMode::KeyWheel) value = QStringLiteral("key");
    else if (mode == LayoutMode::BpmSerpentine) value = QStringLiteral("bpm");
    else if (mode == LayoutMode::Artist) value = QStringLiteral("artist");
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_layout"), value);
    if (m_pLayoutProxy && qRound(m_pLayoutProxy->get()) != static_cast<int>(mode)) {
        m_pLayoutProxy->set(static_cast<int>(mode));
    }
    const QVector<QPointF> start = [&] { QVector<QPointF> p; for (auto* dot : m_dots) p.append(dot->pos()); return p; }();
    const QVector<QPointF> target = layoutTarget(mode);
    publishLayoutDegradation(mode);
    m_pLayoutAnimation->stop();
    disconnect(m_pLayoutAnimation, nullptr, this, nullptr);
    if (!animate) { applyPositions(target); return; }
    m_pLayoutAnimation->setDuration(600);
    m_pLayoutAnimation->setStartValue(0.0);
    m_pLayoutAnimation->setEndValue(1.0);
    m_pLayoutAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_pLayoutAnimation, &QVariantAnimation::valueChanged, this,
            [this, start, target](const QVariant& value) {
                const double f = value.toDouble();
                QVector<QPointF> positions; positions.reserve(target.size());
                for (int i = 0; i < target.size(); ++i) positions.append(start[i] + (target[i] - start[i]) * f);
                applyPositions(positions);
            });
    m_pLayoutAnimation->start();
}

void WCrateGalaxy::contextMenuEvent(QContextMenuEvent* pEvent) {
    QMenu menu(this);
    // A code-created QMenu pops up as a native-styled (white) top-level window
    // — the skin stylesheet never reaches it. Style it ink explicitly.
    menu.setStyleSheet(QStringLiteral(
            "QMenu { background-color: %1; color: %2;"
            "  border: 1px solid %2; padding: 4px; }"
            "QMenu::item { padding: 4px 20px 4px 24px; background: transparent; }"
            "QMenu::item:selected { background-color: %3; color: %1; }"
            "QMenu::item:disabled { color: %2; }"
            "QMenu::separator { height: 1px;"
            "  background: %2; margin: 4px 6px; }"
            "QMenu::indicator { width: 12px; height: 12px; margin-left: 6px; }")
                               .arg(m_palette.ground.name(),
                                       m_palette.ink.name(),
                                       m_palette.accentDeckA.name()));
    // Deck-choice load (spec wave-5 S3): if the right-click landed on a
    // selectable dot, offer "Load to Deck N" for each deck at the top. Ghosted /
    // non-selectable nodes get no deck-load entries.
    const int node = nodeAtViewportPos(pEvent->pos());
    if (node >= 0) {
        addDeckLoadActions(&menu, node);
        menu.addSeparator();
    }
    const int clusterId = clusterLabelAt(pEvent->pos());
    if (clusterId >= 0) {
        QAction* pRename = menu.addAction(tr("Rename area..."));
        connect(pRename, &QAction::triggered, this, [this, clusterId] {
            QVector<int> members;
            for (int i = 0; i < m_nodes.size(); ++i) {
                if (nodeInSubset(i) && m_nodes[i].clusterId == clusterId) {
                    members.append(i);
                }
            }
            bool ok = false;
            const QString name = QInputDialog::getText(this,
                    tr("Rename area"), tr("Area name:"), QLineEdit::Normal,
                    clusterName(clusterId, members), &ok).trimmed();
            if (ok && !name.isEmpty()) {
                m_pConfig->setValue(ConfigKey("[Crate]",
                        QStringLiteral("cluster_name_%1").arg(clusterId)), name);
                rebuildLabelCache();
            }
        });
        menu.addSeparator();
    }
    QActionGroup group(&menu);
    group.setExclusive(true);
    const auto addMode = [&](const QString& label, ColorMode mode) {
        QAction* pAction = menu.addAction(label);
        pAction->setCheckable(true);
        pAction->setChecked(m_colorMode == mode);
        group.addAction(pAction);
        connect(pAction, &QAction::triggered, this, [this, mode] {
            setColorMode(mode);
        });
    };
    addMode(tr("Color by Cluster"), ColorMode::Cluster);
    addMode(tr("Color by Key"), ColorMode::Key);
    addMode(tr("Color by Tempo"), ColorMode::Tempo);
    addMode(tr("Color by Energy"), ColorMode::Energy);
    menu.addSeparator();
    QMenu* pLayout = menu.addMenu(tr("Layout"));
    pLayout->setEnabled(!m_3dMode);
    QActionGroup* pLayoutGroup = new QActionGroup(pLayout);
    pLayoutGroup->setExclusive(true);
    const auto addLayout = [&](const QString& label, LayoutMode mode) {
        QAction* action = pLayout->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_layoutMode == mode);
        pLayoutGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { setLayoutMode(mode); });
    };
    addLayout(tr("Scatter"), LayoutMode::Scatter);
    addLayout(tr("Key wheel"), LayoutMode::KeyWheel);
    addLayout(tr("BPM serpentine"), LayoutMode::BpmSerpentine);
    addLayout(tr("Artist"), LayoutMode::Artist);
    menu.addSeparator();
    QAction* p3d = menu.addAction(tr("3D view"));
    p3d->setCheckable(true);
    p3d->setChecked(m_3dMode);
    connect(p3d, &QAction::triggered, this, &WCrateGalaxy::set3dMode);
    QAction* pHalos = menu.addAction(tr("Mixability halos"));
    pHalos->setCheckable(true);
    pHalos->setChecked(m_halosEnabled);
    connect(pHalos, &QAction::triggered, this, &WCrateGalaxy::setHalosEnabled);
    menu.exec(pEvent->globalPos());
    pEvent->accept();
}

void WCrateGalaxy::drawForeground(QPainter* pPainter, const QRectF& rect) {
    Q_UNUSED(rect);
    pPainter->save();
    pPainter->resetTransform();
    pPainter->setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < m_trailDeviceLines.size(); ++i) {
        pPainter->setPen(QPen(m_trailDeviceColors[i], 1.25, Qt::SolidLine, Qt::RoundCap));
        pPainter->drawLine(m_trailDeviceLines[i]);
    }
    for (int i = 0; i < m_plexusDeviceLines.size(); ++i) {
        pPainter->setPen(QPen(m_plexusDeviceColors[i], m_plexusDeviceWidths[i],
                Qt::SolidLine, Qt::RoundCap));
        pPainter->drawLine(m_plexusDeviceLines[i]);
    }
    // Leader geometry is intentionally derived from the live transform. The
    // device-space label offset is cached, but pan/zoom can move its scene
    // anchor between debounced cache rebuilds.
    QHash<QRgb, QVector<QLineF>> leaderBatches;
    if (!leadersSuppressed()) {
        for (const MapLabel& label : std::as_const(m_trackLabels)) {
            if (!label.hasLeader || label.nodeIndex < 0) {
                continue;
            }
            const QPointF dotCenter = mapFromScene(label.sceneAnchor);
            const QRectF labelRect(dotCenter + label.pixmapOffset, label.logicalSize);
            const QPointF labelEdge(qBound(labelRect.left(), dotCenter.x(), labelRect.right()),
                    qBound(labelRect.top(), dotCenter.y(), labelRect.bottom()));
            QLineF leader(dotCenter, labelEdge);
            const QRectF dotRect = mapFromScene(
                    m_dots[label.nodeIndex]->sceneBoundingRect()).boundingRect();
            const qreal dotRadius = qMax(dotRect.width(), dotRect.height()) * 0.5;
            if (leader.length() > dotRadius) {
                leader.setP1(leader.pointAt(dotRadius / leader.length()));
            }
            QColor leaderColor(label.color);
            leaderColor.setAlphaF(0.40 * label.opacity);
            leaderBatches[leaderColor.rgba()].append(leader);
        }
    }
    for (auto it = leaderBatches.constBegin(); it != leaderBatches.constEnd(); ++it) {
        pPainter->setPen(QPen(QColor::fromRgba(it.key()), 1.0));
        pPainter->drawLines(it.value());
    }
    const auto drawLabels = [pPainter, this](const QVector<MapLabel>& labels) {
        for (const MapLabel& label : labels) {
            pPainter->setOpacity(label.opacity);
            pPainter->drawPixmap(mapFromScene(label.sceneAnchor) + label.pixmapOffset,
                    label.pixmap);
        }
    };
    drawLabels(m_clusterLabels);
    drawLabels(m_artistLabels);
    drawLabels(m_trackLabels);
    pPainter->setOpacity(1.0);
    pPainter->restore();
    if (m_colorMode == ColorMode::Cluster) {
        return;
    }
    pPainter->save();
    pPainter->resetTransform();
    pPainter->setRenderHint(QPainter::Antialiasing, true);
    QFont font(QStringLiteral("IBM Plex Mono"));
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(10);
    pPainter->setFont(font);
    pPainter->setPen(m_palette.ink);

    const int margin = 10;
    const int top = margin;
    if (m_colorMode == ColorMode::Key) {
        const QRect chip(margin, top, 224, 42);
        QColor chipGround(m_palette.ground);
        chipGround.setAlpha(205);
        pPainter->setBrush(chipGround);
        pPainter->setPen(Qt::NoPen);
        pPainter->drawRoundedRect(chip, 3, 3);
        pPainter->setPen(m_palette.ink);
        for (int row = 0; row < 2; ++row) {
            const int y = top + 6 + row * 17;
            pPainter->drawText(QRect(margin + 6, y, 10, 12),
                    Qt::AlignCenter, row == 0 ? QStringLiteral("B") : QStringLiteral("A"));
            for (int number = 1; number <= 12; ++number) {
                const int x = margin + 19 + (number - 1) * 16;
                pPainter->fillRect(QRect(x, y, 13, 8),
                        QColor::fromHsvF((number - 1) / 12.0,
                                0.65,
                                row == 0 ? 1.0 : 0.8));
                pPainter->drawText(QRect(x, y + 8, 13, 8),
                        Qt::AlignHCenter | Qt::AlignTop, QString::number(number));
            }
        }
    } else {
        const ValueRange& range =
                m_colorMode == ColorMode::Tempo ? m_tempoRange : m_energyRange;
        const QRect chip(margin, top, 196, 30);
        const QRect bar(margin + 38, top + 7, 112, 8);
        QColor chipGround(m_palette.ground);
        chipGround.setAlpha(205);
        pPainter->setBrush(chipGround);
        pPainter->setPen(Qt::NoPen);
        pPainter->drawRoundedRect(chip, 3, 3);
        QLinearGradient gradient(bar.topLeft(), bar.topRight());
        for (int i = 0; i <= 10; ++i) {
            const double fraction = i / 10.0;
            gradient.setColorAt(
                    fraction, rampColor(m_colorMode == ColorMode::Tempo, fraction));
        }
        pPainter->fillRect(bar, gradient);
        pPainter->setPen(m_palette.ink);
        const QString low = range.valid ? QString::number(range.low, 'f', 1) : QStringLiteral("--");
        const QString high = range.valid ? QString::number(range.high, 'f', 1) : QStringLiteral("--");
        pPainter->drawText(QRect(margin + 5, top + 5, 30, 12), Qt::AlignRight, low);
        pPainter->drawText(QRect(margin + 154, top + 5, 37, 12), Qt::AlignLeft, high);
        pPainter->drawText(QRect(margin + 38, top + 17, 112, 10),
                Qt::AlignCenter,
                m_colorMode == ColorMode::Tempo ? QStringLiteral("BPM")
                                                : QStringLiteral("ENERGY"));
    }
    pPainter->restore();
}

QString WCrateGalaxy::resolveMusicPath(const QString& relpath) const {
    if (m_musicRoot.isEmpty()) {
        return QString();
    }
    return QDir(m_musicRoot).filePath(relpath);
}

void WCrateGalaxy::wheelEvent(QWheelEvent* pEvent) {
    // Clamp cumulative zoom relative to the fitted scale. Unclamped wheel zoom
    // could run the transform to extremes — a few notches past the content and
    // the view showed "a blank page, no points or pills" with no obvious way
    // back. The 0.5x floor guarantees zooming out always returns the whole
    // galaxy to the viewport; 40x is deep enough to isolate a single dot.
    constexpr double kMinZoomRatio = 0.5;
    constexpr double kMaxZoomRatio = 40.0;
    const double factor = (pEvent->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    const double current =
            m_fitScale > 0.0 ? transform().m11() / m_fitScale : 1.0;
    const double target = qBound(kMinZoomRatio, current * factor, kMaxZoomRatio);
    const double apply = current > 0.0 ? target / current : 1.0;
    if (!qFuzzyCompare(apply, 1.0)) {
        scale(apply, apply);
        if (m_3dMode) {
            update3dProjection();
        }
        updateLabelOpacities();
        scheduleLabelCacheRebuild();
        rebuildOverlayCache();
    }
    pEvent->accept();
}

void WCrateGalaxy::setHoveredNode(int index) {
    if (!nodeSelectable(index)) {
        index = -1;
    }
    if (m_hoveredNode != index) {
        m_hoveredNode = index;
        updateHoverCard();
    }
}

void WCrateGalaxy::set3dMode(bool enabled) {
    if (m_3dMode == enabled) {
        return;
    }
    m_3dMode = enabled;
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_3d"), enabled ? 1 : 0);
    setDragMode(enabled ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    setHoveredNode(-1);
    // 2D<->3D changes the walk's coordinate space; reset it.
    resetWalk();
    if (enabled) {
        resetTransform();
        fitInView(m_pScene->sceneRect(), Qt::KeepAspectRatio);
        m_fitScale = transform().m11();
        update3dProjection();
    } else if (!m_nodes.isEmpty()) {
        for (int i = 0; i < m_dots.size(); ++i) {
            QGraphicsEllipseItem* pDot = m_dots[i];
            pDot->setVisible(true);
            pDot->setRect(-kDotRadius, -kDotRadius, 2 * kDotRadius, 2 * kDotRadius);
            pDot->setZValue(0.0);
            pDot->setBrush(nodeColor(m_nodes[i]));
        }
        applyPositions(layoutTarget(m_layoutMode));
        resetTransform();
        fitInView(m_pScene->sceneRect(), Qt::KeepAspectRatio);
        m_fitScale = transform().m11();
    }
    if (m_p3dProxy && (m_p3dProxy->get() != 0.0) != enabled) {
        m_p3dProxy->set(enabled ? 1.0 : 0.0);
    }
    applyHaloVisuals();
    rebuildLabelCache();
    viewport()->update();
}

void WCrateGalaxy::setHalosEnabled(bool enabled) {
    if (m_halosEnabled == enabled) {
        return;
    }
    m_halosEnabled = enabled;
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_halos"), enabled ? 1 : 0);
    if (m_pHaloProxy && (m_pHaloProxy->get() != 0.0) != enabled) {
        m_pHaloProxy->set(enabled ? 1.0 : 0.0);
    }
    updateMixabilityHalos();
}

void WCrateGalaxy::setTrailEnabled(bool enabled) {
    if (m_trailEnabled == enabled) return;
    m_trailEnabled = enabled;
    m_pConfig->setValue(ConfigKey("[Crate]", "galaxy_trail"), enabled ? 1 : 0);
    if (m_pTrailProxy && (m_pTrailProxy->get() != 0.0) != enabled) {
        m_pTrailProxy->set(enabled ? 1.0 : 0.0);
    }
    rebuildOverlayCache();
}

void WCrateGalaxy::playingTrackChanged(const TrackPointer& pTrack) {
    if (pTrack) appendTrailRelpath(relpathForLocation(pTrack->getLocation()));
    updateMixabilityHalos();
}

void WCrateGalaxy::appendTrailRelpath(const QString& relpath) {
    const auto it = m_nodeByRelpath.constFind(
            QString(relpath).replace(QLatin1Char('\\'), QLatin1Char('/')).toCaseFolded());
    if (it == m_nodeByRelpath.constEnd()) return;
    if (!m_playTrail.isEmpty() && m_playTrail.constLast() == *it) return;
    m_playTrail.append(*it);
    rebuildOverlayCache();
}

void WCrateGalaxy::testPlayingTrackChanged(const QString& relpath) {
    appendTrailRelpath(relpath);
}

int WCrateGalaxy::testPlexusRingAlpha(int node) const {
    QGraphicsEllipseItem* pHalo = m_halos.value(node, nullptr);
    return pHalo && pHalo->isVisible() ? pHalo->pen().color().alpha() : 0;
}

void WCrateGalaxy::rebuildOverlayCache() {
    m_trailDeviceLines.clear();
    m_trailDeviceColors.clear();
    m_plexusDeviceLines.clear();
    m_plexusDeviceColors.clear();
    m_plexusDeviceWidths.clear();
    if (m_trailEnabled && m_playTrail.size() > 1) {
        const int segments = m_playTrail.size() - 1;
        m_trailDeviceLines.reserve(segments);
        m_trailDeviceColors.reserve(segments);
        for (int i = 0; i < segments; ++i) {
            const int age = segments - 1 - i;
            const double freshness = qBound(0.0, (12.0 - age) / 12.0, 1.0);
            m_trailDeviceLines.append(QLineF(mapFromScene(m_dots[m_playTrail[i]]->pos()),
                    mapFromScene(m_dots[m_playTrail[i + 1]]->pos())));
            QColor trailColor(m_palette.ink);
            trailColor.setAlpha(qRound(38 + 170 * freshness));
            m_trailDeviceColors.append(trailColor);
        }
    }
    if (m_halosEnabled) {
        m_plexusDeviceLines.reserve(m_plexusSegments.size());
        for (const PlexusSegment& segment : std::as_const(m_plexusSegments)) {
            if (segment.from < 0 || segment.to < 0 ||
                    !m_dots[segment.from]->isVisible() || !m_dots[segment.to]->isVisible()) continue;
            const double strength = qBound(0.0, (segment.score - 0.5) / 0.5, 1.0);
            QColor color = segment.deck % 2 == 0
                    ? m_palette.accentDeckA : m_palette.accentDeckB;
            const double activityAlpha = segment.playing ? 1.0 : 0.65;
            color.setAlpha(qRound(activityAlpha *
                    (segment.deck >= 2 ? 0.6 : 1.0) * (24 + 180 * strength)));
            m_plexusDeviceLines.append(QLineF(mapFromScene(m_dots[segment.from]->pos()),
                    mapFromScene(m_dots[segment.to]->pos())));
            m_plexusDeviceColors.append(color);
            m_plexusDeviceWidths.append(0.65 + 1.45 * strength);
        }
    }
    viewport()->update();
}

void WCrateGalaxy::update3dProjection() {
    if (!m_3dMode || m_nodes.isEmpty()) {
        return;
    }
    double cx = 0.0, cy = 0.0, cz = 0.0;
    int count = 0;
    for (const GalaxyNode& node : std::as_const(m_nodes)) {
        if (node.has3d) {
            cx += node.x3d;
            cy += node.y3d;
            cz += node.z;
            ++count;
        }
    }
    if (count == 0) {
        for (QGraphicsEllipseItem* pDot : std::as_const(m_dots)) {
            pDot->setVisible(false);
        }
        return;
    }
    cx /= count;
    cy /= count;
    cz /= count;
    double extent = 0.0;
    for (const GalaxyNode& node : std::as_const(m_nodes)) {
        if (node.has3d) {
            extent = qMax(extent, std::abs(node.x3d - cx));
            extent = qMax(extent, std::abs(node.y3d - cy));
            extent = qMax(extent, std::abs(node.z - cz));
        }
    }
    extent = qMax(extent, 1e-9);
    const double az = qDegreesToRadians(m_azimuth);
    const double el = qDegreesToRadians(m_elevation);
    const double ca = std::cos(az), sa = std::sin(az);
    const double ce = std::cos(el), se = std::sin(el);
    QVector<double> depths(m_nodes.size(), 0.0);
    double minDepth = 0.0, maxDepth = 0.0;
    bool first = true;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const GalaxyNode& node = m_nodes[i];
        if (!node.has3d) {
            m_dots[i]->setVisible(false);
            continue;
        }
        const double x = (node.x3d - cx) / extent;
        const double y = (node.y3d - cy) / extent;
        const double z = (node.z - cz) / extent;
        const double rx = ca * x + sa * z;
        const double rz = -sa * x + ca * z;
        const double ry = ce * y - se * rz;
        const double depth = se * y + ce * rz;
        depths[i] = depth;
        minDepth = first ? depth : qMin(minDepth, depth);
        maxDepth = first ? depth : qMax(maxDepth, depth);
        first = false;
        m_dots[i]->setPos(kSceneSpan * 0.5 + rx * kSceneSpan * 0.38,
                kSceneSpan * 0.5 - ry * kSceneSpan * 0.38);
        m_halos[i]->setPos(m_dots[i]->pos());
    }
    const double depthSpan = qMax(maxDepth - minDepth, 1e-9);
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i].has3d) {
            continue;
        }
        const double d = (depths[i] - minDepth) / depthSpan;
        const double radius = kDotRadius * (0.55 + 0.9 * d);
        QColor color = subsetColor(i, nodeColor(m_nodes[i]));
        color.setAlpha(qRound(70.0 + 170.0 * d));
        QGraphicsEllipseItem* pDot = m_dots[i];
        pDot->setVisible(true);
        pDot->setRect(-radius, -radius, 2 * radius, 2 * radius);
        pDot->setBrush(color);
        pDot->setZValue(depths[i]);
    }
    applyHaloVisuals();
    updateCursorVisual();
    rebuildLabelCache();
    rebuildOverlayCache();
    viewport()->update();
}

int WCrateGalaxy::projectedNodeAt(const QPoint& viewportPos) const {
    int nearest = -1;
    double bestDistance = 18.0 * 18.0;
    for (int i = 0; i < m_dots.size(); ++i) {
        if (!nodeSelectable(i) || !m_nodes[i].has3d) {
            continue;
        }
        const QPointF center = mapFromScene(m_dots[i]->pos());
        const double dx = center.x() - viewportPos.x();
        const double dy = center.y() - viewportPos.y();
        const double distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            nearest = i;
        }
    }
    return nearest;
}

bool WCrateGalaxy::eventFilter(QObject* pObj, QEvent* pEvent) {
    // [Crate],debug_input diagnostics: identify the real receiver of presses.
    if (m_debugInput && pEvent->type() == QEvent::MouseButtonPress) {
        auto* pWidget = qobject_cast<QWidget*>(pObj);
        kLogger.info() << "ORBITDBG app-press receiver="
                       << pObj->metaObject()->className()
                       << (pWidget ? pWidget->objectName() : QStringLiteral("<non-widget>"));
    }
    return QGraphicsView::eventFilter(pObj, pEvent);
}

void WCrateGalaxy::mousePressEvent(QMouseEvent* pEvent) {
    if (m_debugInput) {
        kLogger.info() << "ORBITDBG press button=" << pEvent->button()
                       << "3d=" << m_3dMode << "pos=" << pEvent->pos();
    }
    if (m_3dMode && pEvent->button() == Qt::LeftButton) {
        m_orbiting = true;
        m_orbitMoved = false;
        m_orbitLast = pEvent->pos();
        pEvent->accept();
        return;
    }
    QGraphicsView::mousePressEvent(pEvent);
}

void WCrateGalaxy::mouseReleaseEvent(QMouseEvent* pEvent) {
    if (m_3dMode && pEvent->button() == Qt::LeftButton && m_orbiting) {
        m_orbiting = false;
        pEvent->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(pEvent);
}

void WCrateGalaxy::mouseMoveEvent(QMouseEvent* pEvent) {
    if (m_debugInput && m_orbiting) {
        kLogger.info() << "ORBITDBG move orbiting pos=" << pEvent->pos();
    }
    if (m_3dMode && m_orbiting) {
        const QPoint delta = pEvent->pos() - m_orbitLast;
        if (delta.manhattanLength() > 0) {
            m_orbitMoved = m_orbitMoved || delta.manhattanLength() > 2;
            // Scale orbit by the current zoom so a screen pixel of mouse motion
            // moves the scene the same number of screen pixels at every zoom
            // (her report: sensitivity "way too high when zoomed in").
            const double zoomRatio =
                    m_fitScale > 0.0 ? transform().m11() / m_fitScale : 1.0;
            const int extent = viewport()->width();
            m_azimuth += orbitAngleDelta(delta.x(), extent, zoomRatio);
            m_elevation = qBound(-85.0,
                    m_elevation + orbitAngleDelta(delta.y(), extent, zoomRatio),
                    85.0);
            m_orbitLast = pEvent->pos();
            update3dProjection();
        }
        pEvent->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(pEvent);
    setHoveredNode(nodeAtViewportPos(pEvent->pos()));
}

void WCrateGalaxy::leaveEvent(QEvent* pEvent) {
    setHoveredNode(-1);
    QGraphicsView::leaveEvent(pEvent);
}

void WCrateGalaxy::resizeEvent(QResizeEvent* pEvent) {
    QGraphicsView::resizeEvent(pEvent);
    if (m_3dMode) {
        update3dProjection();
    }
    scheduleLabelCacheRebuild();
    rebuildOverlayCache();
}

void WCrateGalaxy::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    rebuildOverlayCache();
    if (!m_labelBuiltViewportSceneRect.isValid()) {
        return;
    }
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const QPointF motion = visible.center() - m_labelBuiltViewportSceneRect.center();
    if (qAbs(motion.x()) > m_labelBuiltViewportSceneRect.width() * 0.25 ||
            qAbs(motion.y()) > m_labelBuiltViewportSceneRect.height() * 0.25) {
        scheduleLabelCacheRebuild();
    }
}

void WCrateGalaxy::scheduleLabelCacheRebuild() {
    if (m_pLabelRebuildTimer) {
        m_pLabelRebuildTimer->start();
    }
}

void WCrateGalaxy::updateLabelOpacities() {
    const double zoom = m_fitScale > 0.0 ? transform().m11() / m_fitScale : 0.0;
    const qreal cluster = 1.0 - qBound(0.0,
            (zoom - kClusterFadeStart) / (kClusterFadeEnd - kClusterFadeStart), 1.0);
    const qreal artistIn = qBound(0.0,
            (zoom - kClusterFadeStart) / (kClusterFadeEnd - kClusterFadeStart), 1.0);
    const qreal artistOut = 1.0 - qBound(0.0,
            (zoom - kArtistFadeOutStart) / (kArtistFadeOutEnd - kArtistFadeOutStart), 1.0);
    const qreal track = qBound(0.0,
            (zoom - kArtistFadeOutStart) / (kArtistFadeOutEnd - kArtistFadeOutStart), 1.0);
    for (MapLabel& label : m_clusterLabels) label.opacity = cluster;
    for (MapLabel& label : m_artistLabels) label.opacity = 0.78 * qMin(artistIn, artistOut);
    for (MapLabel& label : m_trackLabels) label.opacity = track;
    viewport()->update();
}

QString WCrateGalaxy::artistForNode(const GalaxyNode& node) const {
    if (!node.artist.trimmed().isEmpty()) {
        return node.artist.trimmed();
    }
    const QStringList parts = QDir::fromNativeSeparators(node.relpath).split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() >= 3 && parts[0].compare(
            QStringLiteral("music"), Qt::CaseInsensitive) == 0 &&
            (parts[1].compare(QStringLiteral("dj"), Qt::CaseInsensitive) == 0 ||
                    parts[1].compare(QStringLiteral("personal"),
                            Qt::CaseInsensitive) == 0)) {
        return parts[2];
    }
    return parts.size() >= 2 ? parts[1] : QString();
}

QString WCrateGalaxy::clusterName(int clusterId, const QVector<int>& members) const {
    const QString saved = m_pConfig->getValue(ConfigKey("[Crate]",
            QStringLiteral("cluster_name_%1").arg(clusterId)), QString()).trimmed();
    if (!saved.isEmpty()) {
        return saved;
    }
    QHash<QString, int> counts;
    QHash<QString, QString> display;
    for (int index : members) {
        const QString artist = artistForNode(m_nodes[index]);
        if (!artist.isEmpty()) {
            const QString key = artist.toCaseFolded();
            ++counts[key];
            display.insert(key, artist);
        }
    }
    QVector<QString> artists = counts.keys();
    std::stable_sort(artists.begin(), artists.end(), [&counts](const QString& a,
                                                        const QString& b) {
        return counts[a] == counts[b] ? a < b : counts[a] > counts[b];
    });
    if (artists.isEmpty()) {
        return tr("cluster %1").arg(clusterId);
    }
    if (artists.size() == 1 || counts[artists[0]] * 100 > members.size() * 60) {
        return display[artists[0]];
    }
    return display[artists[0]] + QStringLiteral(" / ") + display[artists[1]];
}

void WCrateGalaxy::rebuildLabelCache() {
    ++m_labelRebuildCount;
    m_labelBuiltViewportSceneRect = mapToScene(viewport()->rect()).boundingRect();
    m_labelBuiltTransformScale = transform().m11();
    if (m_nodes.isEmpty() || m_dots.size() != m_nodes.size()) {
        return;
    }
    const double zoom = m_fitScale > 0.0 ? transform().m11() / m_fitScale : 0.0;
    m_clusterLabels.clear();
    m_artistLabels.clear();
    m_trackLabels.clear();
    // Two candidate sets: cluster/artist names are WAYFINDING — the map's
    // regions keep their names even when a crate subset ghosts most dots
    // (you don't delete city names while browsing one district). Track text
    // stays subset-gated like the pills it replaced.
    QVector<int> wayfinding;
    QVector<int> visible;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!m_3dMode || m_nodes[i].has3d) {
            wayfinding.append(i);
            if (nodeSelectable(i)) {
                visible.append(i);
            }
        }
    }
    const qreal dpr = viewport()->devicePixelRatioF();
    const auto makeLabel = [this, dpr](const QString& text, const QPointF& sceneAnchor,
                                   QColor color, int pixelSize, qreal opacity,
                                   int clusterId, int nodeIndex, QPointF offset = {}) {
        QFont font(QStringLiteral("IBM Plex Mono"));
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(pixelSize);
        font.setWeight(pixelSize >= 13 ? QFont::DemiBold : QFont::Normal);
        const QFontMetrics metrics(font);
        const QSize textSize = metrics.size(Qt::TextSingleLine, text);
        const QSize logicalSize = textSize + QSize(2, 2);
        QPixmap pixmap(qCeil(logicalSize.width() * dpr),
                qCeil(logicalSize.height() * dpr));
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setFont(font);
        QColor shadow(m_palette.ground);
        shadow.setAlphaF(0.60);
        painter.setPen(shadow);
        painter.drawText(QPointF(2.0, 1.0 + metrics.ascent()), text);
        painter.setPen(color);
        painter.drawText(QPointF(1.0, metrics.ascent()), text);
        if (offset.isNull()) {
            offset = QPointF(-logicalSize.width() * 0.5,
                    -logicalSize.height() * 0.5);
        }
        MapLabel label{text, sceneAnchor, offset, logicalSize, pixmap,
                color, pixelSize, opacity, clusterId, nodeIndex, false};
        return label;
    };

    // One device-space collision index for every layer. Keeping the stable
    // insertion order makes labels tile-like: zooming in only frees space.
    QVector<QRectF> occupied;
    const int labelCap = zoom < 8.0 ? 220 : 600;
    occupied.reserve(labelCap);
    QVector<MapLabel> pendingArtistLabels;
    const auto place = [&occupied, this, labelCap](MapLabel&& label,
                               QVector<MapLabel>* output) {
        if (occupied.size() >= labelCap) return;
        const QPointF deviceAnchor = mapFromScene(label.sceneAnchor);
        const QRectF rect(deviceAnchor + label.pixmapOffset, label.logicalSize);
        const QRectF padded = rect.adjusted(-2, -2, 2, 2);
        for (const QRectF& other : std::as_const(occupied)) {
            if (other.intersects(padded)) return;
        }
        occupied.append(padded);
        output->append(std::move(label));
    };
    const auto placeTrack = [&occupied, this, labelCap](MapLabel&& label,
                                    QVector<MapLabel>* output) {
        if (occupied.size() >= labelCap) return;
        const QPointF deviceAnchor = mapFromScene(label.sceneAnchor);
        const QPointF homeOffset = label.pixmapOffset;
        const auto tryOffset = [&occupied, &label, deviceAnchor](const QPointF& offset) {
            const QRectF rect(deviceAnchor + offset, label.logicalSize);
            const QRectF padded = rect.adjusted(-2, -2, 2, 2);
            for (const QRectF& other : std::as_const(occupied)) {
                if (other.intersects(padded)) return false;
            }
            label.pixmapOffset = offset;
            occupied.append(padded);
            return true;
        };
        if (tryOffset(homeOffset)) {
            output->append(std::move(label));
            return;
        }
        // Clockwise rings, starting at the home/right direction. Integer ring
        // radii and a fixed angle count make repeated rebuilds bit-stable.
        constexpr int kAngles = 12;
        for (int radius = 8; radius <= 56; radius += 8) {
            for (int angle = 0; angle < kAngles; ++angle) {
                const qreal radians = angle * (2.0 * M_PI / kAngles);
                const QPointF candidate = homeOffset +
                        QPointF(radius * std::cos(radians), radius * std::sin(radians));
                if (tryOffset(candidate)) {
                    label.hasLeader = radius > 10;
                    output->append(std::move(label));
                    return;
                }
            }
        }
    };

    if (zoom < kClusterFadeEnd) {
        QHash<int, QVector<int>> clusters;
        int largest = 1;
        for (int index : wayfinding) {
            if (m_nodes[index].clusterId >= 0) {
                clusters[m_nodes[index].clusterId].append(index);
            }
        }
        for (const QVector<int>& members : std::as_const(clusters)) {
            largest = qMax(largest, members.size());
        }
        const qreal opacity = 1.0 - qBound(0.0,
                (zoom - kClusterFadeStart) /
                        (kClusterFadeEnd - kClusterFadeStart), 1.0);
        for (auto it = clusters.constBegin(); it != clusters.constEnd(); ++it) {
            QPointF center;
            for (int index : it.value()) center += m_dots[index]->pos();
            center /= it.value().size();
            const double population = std::log1p(it.value().size()) /
                    std::log1p(largest);
            const int px = qRound(11.0 + 9.0 * population);
            place(makeLabel(clusterName(it.key(), it.value()), center,
                          clusterColor(it.key()), px, opacity, it.key(), -1),
                    &m_clusterLabels);
        }
    }

    if (zoom > kClusterFadeStart && zoom < kArtistFadeOutEnd) {
        const qreal fadeIn = qBound(0.0, (zoom - kClusterFadeStart) /
                (kClusterFadeEnd - kClusterFadeStart), 1.0);
        const qreal fadeOut = 1.0 - qBound(0.0,
                (zoom - kArtistFadeOutStart) /
                        (kArtistFadeOutEnd - kArtistFadeOutStart), 1.0);
        QHash<QString, QVector<int>> byArtist;
        for (int index : wayfinding) {
            const QString artist = artistForNode(m_nodes[index]);
            if (!artist.isEmpty()) byArtist[artist.toCaseFolded()].append(index);
        }
        for (const QVector<int>& tracks : std::as_const(byArtist)) {
            QVector<int> component(tracks.size());
            for (int i = 0; i < component.size(); ++i) component[i] = i;
            const auto root = [&component](int i) {
                while (component[i] != i) i = component[i];
                return i;
            };
            for (int a = 0; a < tracks.size(); ++a) {
                for (int b = a + 1; b < tracks.size(); ++b) {
                    const QPointF delta = m_dots[tracks[a]]->pos() -
                            m_dots[tracks[b]]->pos();
                    if (QPointF::dotProduct(delta, delta) <=
                            kArtistClumpDistance * kArtistClumpDistance) {
                        const int ra = root(a), rb = root(b);
                        if (ra != rb) component[rb] = ra;
                    }
                }
            }
            QHash<int, QVector<int>> clumps;
            for (int i = 0; i < tracks.size(); ++i) clumps[root(i)].append(tracks[i]);
            for (const QVector<int>& clump : std::as_const(clumps)) {
                if (clump.size() < 2) continue;
                QPointF center;
                QHash<int, int> clusterCounts;
                for (int index : clump) {
                    center += m_dots[index]->pos();
                    ++clusterCounts[m_nodes[index].clusterId];
                }
                center /= clump.size();
                int dominant = -1, count = -1;
                for (auto it = clusterCounts.constBegin(); it != clusterCounts.constEnd(); ++it) {
                    if (it.value() > count || (it.value() == count && it.key() < dominant)) {
                        dominant = it.key(); count = it.value();
                    }
                }
                pendingArtistLabels.append(makeLabel(artistForNode(m_nodes[clump[0]]),
                        center, clusterColor(dominant),
                        qBound(9, 9 + clump.size() / 3, 12),
                        0.78 * qMin(fadeIn, fadeOut), dominant, -1));
            }
        }
    }

    if (zoom > kArtistFadeOutStart) {
        const qreal opacity = qBound(0.0,
                (zoom - kArtistFadeOutStart) /
                        (kArtistFadeOutEnd - kArtistFadeOutStart), 1.0);
        QSet<int> endpoints;
        for (const PlexusSegment& segment : std::as_const(m_plexusSegments)) {
            endpoints.insert(segment.to);
        }
        std::stable_sort(visible.begin(), visible.end(), [this, &endpoints](int a, int b) {
            const auto priority = [this, &endpoints](int index) {
                if (m_deckPlayingNodes.values().contains(index)) return 0;
                if (endpoints.contains(index)) return 1;
                if (index == m_hoveredNode || index == m_cursorNode) return 2;
                return 3;
            };
            const int ap = priority(a), bp = priority(b);
            if (ap != bp) return ap < bp;
            const size_t ah = qHash(m_nodes[a].relpath.toCaseFolded(), 0);
            const size_t bh = qHash(m_nodes[b].relpath.toCaseFolded(), 0);
            return ah == bh ? m_nodes[a].relpath < m_nodes[b].relpath : ah < bh;
        });
        for (int index : std::as_const(visible)) {
            if (occupied.size() >= labelCap) break;
            QFont font(QStringLiteral("IBM Plex Mono"));
            font.setPixelSize(10);
            const QString title = QFontMetrics(font).elidedText(
                    m_nodes[index].title, Qt::ElideRight,
                    QFontMetrics(font).horizontalAdvance(QString(24, QLatin1Char('M'))));
            placeTrack(makeLabel(title, m_dots[index]->pos(),
                          clusterColor(m_nodes[index].clusterId), 10, opacity,
                          m_nodes[index].clusterId, index, QPointF(7.0, -6.0)),
                    &m_trackLabels);
        }
        std::sort(m_trackLabels.begin(), m_trackLabels.end(), [](const MapLabel& a,
                                                                  const MapLabel& b) {
            return a.nodeIndex < b.nodeIndex;
        });
    }
    // During the artist/track cross-fade, high-priority track labels reserve
    // their rectangles first. Artist wayfinding then fills the shared gaps.
    for (MapLabel& label : pendingArtistLabels) {
        place(std::move(label), &m_artistLabels);
    }
    updateHoverCard();
    viewport()->update();
}

void WCrateGalaxy::updateHoverCard() {
    QSet<int> wanted;
    if (nodeSelectable(m_hoveredNode) &&
            (!m_3dMode || m_nodes[m_hoveredNode].has3d)) {
        wanted.insert(m_hoveredNode);
    }
    for (auto it = m_pills.begin(); it != m_pills.end();) {
        if (!wanted.contains(it.key())) {
            delete it.value();
            it = m_pills.erase(it);
        } else {
            ++it;
        }
    }
    for (int index : wanted) {
        QGraphicsItem* pPill = m_pills.value(index, nullptr);
        if (pPill == nullptr) {
            pPill = new GalaxyPillItem(m_nodes[index],
                    nodeColor(m_nodes[index]),
                    m_palette.ground,
                    m_palette.ink,
                    index);
            m_pScene->addItem(pPill);
            m_pills.insert(index, pPill);
        }
        pPill->setPos(m_dots[index]->pos());
        pPill->setOpacity(1.0);
    }
    // Dots stay visible alongside their pills (interview 2026-07-19: "always
    // show both" — hiding the dot broke the map's geometry and read
    // inconsistently because the playing track kept its dot).
    for (int i = 0; i < m_dots.size(); ++i) {
        m_dots[i]->setOpacity(1.0);
    }
}

void WCrateGalaxy::setKnobFocusMap(bool mapFocus) {
    const bool changed = m_knobFocusMap != mapFocus;
    m_knobFocusMap = mapFocus;
    m_pConfig->setValue(ConfigKey("[Crate]", "knob_focus"), mapFocus ? 1 : 0);
    if (m_pKnobFocusProxy && (m_pKnobFocusProxy->get() != 0.0) != mapFocus) {
        m_pKnobFocusProxy->set(mapFocus ? 1.0 : 0.0);
    }
    Q_UNUSED(changed);
}

void WCrateGalaxy::onKnobMove(double delta) {
    // Consume the browse encoder only when knob focus is MAP; TABLE leaves the
    // stock library scroll untouched. Each tick is a relative step; interpret
    // its sign/magnitude (encoders normally send +/-1 per detent).
    if (!m_knobFocusMap || m_nodes.isEmpty()) {
        return;
    }
    int steps = static_cast<int>(std::lround(delta));
    if (steps == 0) {
        steps = delta > 0.0 ? 1 : (delta < 0.0 ? -1 : 0);
    }
    steps = qBound(-16, steps, 16);
    for (int i = 0; i < steps; ++i) {
        stepCursorForward();
    }
    for (int i = 0; i > steps; --i) {
        stepCursorBack();
    }
}

bool WCrateGalaxy::nodeSelectable(int index) const {
    if (index < 0 || index >= m_nodes.size() || index >= m_dots.size()) {
        return false;
    }
    if (!nodeInSubset(index)) {
        return false;
    }
    if (m_3dMode) {
        // In 3D the walk follows what you actually see in the orbit: only
        // projected (has3d) nodes participate.
        return m_nodes[index].has3d && m_dots[index]->isVisible();
    }
    return m_dots[index]->isVisible();
}

double WCrateGalaxy::displaySqDistance(int a, int b) const {
    if (m_3dMode) {
        // Mirror v1's 3D nearest_unplayed: squared-euclidean in coords3d space.
        const double dx = m_nodes[a].x3d - m_nodes[b].x3d;
        const double dy = m_nodes[a].y3d - m_nodes[b].y3d;
        const double dz = m_nodes[a].z - m_nodes[b].z;
        return dx * dx + dy * dy + dz * dz;
    }
    // 2D: squared-euclidean in the CURRENT layout's scene positions (what the
    // eye sees), mirroring v1's UMAP-space nearest_unplayed.
    const QPointF delta = m_dots[a]->pos() - m_dots[b]->pos();
    return QPointF::dotProduct(delta, delta);
}

int WCrateGalaxy::nearestUnvisited(int fromNode) const {
    if (fromNode < 0) {
        return -1;
    }
    int best = -1;
    double bestDistance = 0.0;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (i == fromNode || m_walkVisited.contains(i) || !nodeSelectable(i)) {
            continue;
        }
        const double distance = displaySqDistance(fromNode, i);
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

int WCrateGalaxy::seedNode() const {
    // Seed at the now-playing node if it is visible, else the visible node
    // nearest the current view center.
    for (auto it = m_deckPlayingNodes.constBegin(); it != m_deckPlayingNodes.constEnd(); ++it) {
        if (nodeSelectable(it.value())) return it.value();
    }
    const QPointF center = mapToScene(viewport()->rect().center());
    int best = -1;
    double bestDistance = 0.0;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!nodeSelectable(i)) {
            continue;
        }
        const QPointF delta = m_dots[i]->pos() - center;
        const double distance = QPointF::dotProduct(delta, delta);
        if (best < 0 || distance < bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void WCrateGalaxy::stepCursorForward() {
    if (m_cursorNode < 0) {
        setCursorNode(seedNode(), /*resetWalk=*/true);
        return;
    }
    const int next = nearestUnvisited(m_cursorNode);
    if (next < 0) {
        return; // exhausted the reachable, unvisited set; hold the cursor
    }
    m_cursorNode = next;
    m_walkVisited.insert(next);
    m_walkHistory.append(next);
    updateCursorVisual();
    syncSelectionToCursor();
}

void WCrateGalaxy::stepCursorBack() {
    if (m_walkHistory.size() <= 1) {
        return; // at the seed; nothing to retrace
    }
    const int leaving = m_walkHistory.takeLast();
    m_walkVisited.remove(leaving); // un-mark so it can be revisited going forward
    m_cursorNode = m_walkHistory.last();
    updateCursorVisual();
    syncSelectionToCursor();
}

void WCrateGalaxy::setCursorNode(int index, bool resetWalk) {
    if (index < 0) {
        return;
    }
    if (resetWalk) {
        m_walkVisited.clear();
        m_walkHistory.clear();
    }
    m_cursorNode = index;
    m_walkVisited.insert(index);
    if (m_walkHistory.isEmpty() || m_walkHistory.last() != index) {
        m_walkHistory.append(index);
    }
    updateCursorVisual();
    syncSelectionToCursor();
}

void WCrateGalaxy::resetWalk() {
    m_cursorNode = -1;
    m_walkVisited.clear();
    m_walkHistory.clear();
    updateCursorVisual();
}

void WCrateGalaxy::updateCursorVisual() {
    if (!m_pCursorRing) {
        return;
    }
    const bool show = m_cursorNode >= 0 && m_cursorNode < m_dots.size() &&
            m_dots[m_cursorNode]->isVisible();
    if (show) {
        m_pCursorRing->setPos(m_dots[m_cursorNode]->pos());
        m_pCursorRing->setData(0, m_cursorNode);
    }
    m_pCursorRing->setVisible(show);
    viewport()->update();
}

void WCrateGalaxy::syncSelectionToCursor() {
    // PREFERRED load integration (selection-sync): mirror the cursor into the
    // library table selection, so the stock deck LOAD buttons and every other
    // controller load the cursor track with zero interception. No-op without a
    // Library (e.g. widget tests) or when the track is not in the collection.
    if (m_pLibrary == nullptr || m_cursorNode < 0 || m_cursorNode >= m_nodes.size()) {
        return;
    }
    const QString location = resolveMusicPath(m_nodes[m_cursorNode].relpath);
    if (location.isEmpty()) {
        return;
    }
    TrackCollectionManager* pTcm = m_pLibrary->trackCollectionManager();
    if (pTcm == nullptr) {
        return;
    }
    const QList<TrackId> ids = pTcm->resolveTrackIdsFromLocations({location});
    if (ids.isEmpty() || !ids.first().isValid()) {
        return;
    }
    emit m_pLibrary->selectTrack(ids.first());
}

QVector<bool> WCrateGalaxy::deckPlayingStates() const {
    // Scan decks by their [ChannelN],play control. Deck count is discovered from
    // which play controls exist (works with or without a PlayerManager, so the
    // rule stays unit-testable). Index i (0-based) -> deck i+1.
    QVector<bool> states;
    const int maxDecks = m_pPlayerManager != nullptr
            ? static_cast<int>(m_pPlayerManager->numberOfDecks())
            : 8;
    for (int i = 0; i < maxDecks; ++i) {
        const ConfigKey playKey(PlayerManager::groupForDeck(i), "play");
        if (!ControlObject::exists(playKey)) {
            break;
        }
        states.append(ControlObject::get(playKey) != 0.0);
    }
    return states;
}

QVector<bool> WCrateGalaxy::deckLoadedStates() const {
    // Mirrors deckPlayingStates: [ChannelN],track_loaded is owned by each
    // deck's EngineBuffer.
    QVector<bool> states;
    const int maxDecks = m_pPlayerManager != nullptr
            ? static_cast<int>(m_pPlayerManager->numberOfDecks())
            : 8;
    for (int i = 0; i < maxDecks; ++i) {
        const ConfigKey loadedKey(PlayerManager::groupForDeck(i), "track_loaded");
        if (!ControlObject::exists(loadedKey)) {
            break;
        }
        states.append(ControlObject::get(loadedKey) != 0.0);
    }
    return states;
}

double WCrateGalaxy::orbitAngleDelta(int pixelDelta, int viewportExtent, double zoomRatio) {
    const double extent = static_cast<double>(qMax(1, viewportExtent));
    const double ratio = zoomRatio > 0.0 ? zoomRatio : 1.0;
    // Base mapping: a full viewport-width drag = 360 degrees at the fitted scale.
    // Dividing by the zoom ratio keeps the *screen-pixel* motion of a node
    // constant, because the view transform already scales scene motion by that
    // same ratio.
    return static_cast<double>(pixelDelta) * 360.0 / extent / ratio;
}

int WCrateGalaxy::pickNextPrepDeck(
        const QVector<bool>& playing, const QVector<bool>& loaded) {
    // Her rule (interview 2026-07-19, round 2): loaded-or-playing = taken.
    // Fill EMPTY decks first, in order; once every deck holds a track, replace
    // the lowest-numbered STOPPED deck; never touch a playing one. (The old
    // playing-only rule saw stopped-but-loaded decks as free and silently
    // overwrote deck 1 forever — "clicking always puts something on deck 1".)
    const int n = playing.size();
    const auto isLoaded = [&loaded](int i) {
        // A deck with no loaded-state available reads as empty.
        return i < loaded.size() && loaded[i];
    };
    for (int i = 0; i < n; ++i) {
        if (!isLoaded(i) && !playing[i]) {
            return i + 1;
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!playing[i]) {
            return i + 1;
        }
    }
    return 0; // every deck playing: never steal
}

int WCrateGalaxy::nextPrepDeck() const {
    return pickNextPrepDeck(deckPlayingStates(), deckLoadedStates());
}

int WCrateGalaxy::nodeAtViewportPos(const QPoint& viewportPos) const {
    // Geometric pick in VIEWPORT space, mirroring projectedNodeAt. itemAt()
    // returned whatever was topmost — hover pills are 172x50 screen-anchored
    // rects above the dots, so once a pill showed, clicks over a wide area
    // resolved to the pill's node (or nothing) instead of the dot under the
    // cursor ("the hitbox gets really funky when the pill shows up").
    int nearest = -1;
    if (m_3dMode) {
        nearest = projectedNodeAt(viewportPos);
    } else {
        double bestDistance = 14.0 * 14.0;
        for (int i = 0; i < m_dots.size(); ++i) {
            if (!nodeSelectable(i)) {
                continue;
            }
            const QPointF center = mapFromScene(m_dots[i]->pos());
            const double dx = center.x() - viewportPos.x();
            const double dy = center.y() - viewportPos.y();
            const double distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = i;
            }
        }
    }
    if (nearest >= 0) {
        return nearest;
    }
    // No dot under the cursor: a visible pill counts as its dot (interview
    // 2026-07-19: "pill = the dot, everywhere"). Pills are screen-anchored
    // (ItemIgnoresTransformations), so hit-test their device-space rect
    // anchored at the dot's viewport position.
    for (auto it = m_pills.constBegin(); it != m_pills.constEnd(); ++it) {
        const int index = it.key();
        if (!nodeSelectable(index) || index >= m_dots.size() ||
                !it.value()->isVisible()) {
            continue;
        }
        const QPointF anchor = mapFromScene(m_dots[index]->pos());
        const QRectF pillRect =
                it.value()->boundingRect().translated(anchor);
        if (pillRect.contains(viewportPos)) {
            return index;
        }
    }
    return -1;
}

QVector<WCrateGalaxy::DeckLoadEntry> WCrateGalaxy::deckLoadEntries(int nodeIndex) const {
    QVector<DeckLoadEntry> entries;
    if (!nodeSelectable(nodeIndex)) {
        return entries; // ghosted / off-map: no deck-load entries
    }
    const QVector<bool> playing = deckPlayingStates();
    entries.reserve(playing.size());
    for (int i = 0; i < playing.size(); ++i) {
        DeckLoadEntry entry;
        entry.deck = i + 1;
        entry.enabled = !playing[i];
        entry.label = playing[i]
                ? tr("Load to Deck %1 (playing)").arg(entry.deck)
                : tr("Load to Deck %1").arg(entry.deck);
        entries.append(entry);
    }
    return entries;
}

void WCrateGalaxy::addDeckLoadActions(QMenu* pMenu, int nodeIndex) {
    const QVector<DeckLoadEntry> entries = deckLoadEntries(nodeIndex);
    for (const DeckLoadEntry& entry : entries) {
        QAction* pAction = pMenu->addAction(entry.label);
        pAction->setEnabled(entry.enabled);
        if (!entry.enabled) {
            continue; // playing deck: annotated, not loadable from this menu
        }
        const int deck = entry.deck;
        connect(pAction, &QAction::triggered, this, [this, nodeIndex, deck] {
            // Re-seed the walk cursor on this node, then load into the chosen
            // deck; the table-jump seam keeps the library selection in sync.
            setCursorNode(nodeIndex, /*resetWalk=*/true);
            requestTableJumpToCursor();
            loadCursorIntoDeck(deck);
        });
    }
}

QStringList WCrateGalaxy::testDeckLoadLabels(int nodeIndex) const {
    QStringList labels;
    for (const DeckLoadEntry& entry : deckLoadEntries(nodeIndex)) {
        labels.append(entry.label);
    }
    return labels;
}

void WCrateGalaxy::loadCursorIntoDeck(int deckNumber) {
    // deckNumber is 1-based (matches PlayerManager::slotLoadToDeck).
    if (m_cursorNode < 0 || m_cursorNode >= m_nodes.size() ||
            m_pPlayerManager == nullptr || deckNumber < 1) {
        return;
    }
    const QString path = resolveMusicPath(m_nodes[m_cursorNode].relpath);
    if (path.isEmpty()) {
        kLogger.warning() << "no music_root; cannot load"
                          << m_nodes[m_cursorNode].relpath;
        return;
    }
    kLogger.info() << "loading" << path << "into deck" << deckNumber;
    m_pPlayerManager->slotLoadToDeck(path, deckNumber);
}

void WCrateGalaxy::requestTableJumpToCursor() {
    // Single entry point for "map load -> table jumps to the loaded track" (spec
    // wave-5 S3, item 3). Records the request (observable in widget tests without
    // a live Library) and drives the real selection seam. The seam
    // (Library::selectTrack -> WLibrary::slotSelectTrackInActiveTrackView ->
    // WTrackTableView::selectTrack) scrolls to + selects the track only when it
    // exists in the CURRENT table view, and never switches the sidebar.
    if (m_cursorNode < 0 || m_cursorNode >= m_nodes.size()) {
        return;
    }
    ++m_tableJumpRequests;
    m_lastTableJumpRelpath = m_nodes[m_cursorNode].relpath;
    syncSelectionToCursor();
}

void WCrateGalaxy::loadCursorToNextPrepDeck() {
    if (m_cursorNode < 0 || m_cursorNode >= m_nodes.size()) {
        return;
    }
    const int deck = nextPrepDeck();
    if (deck < 1) {
        kLogger.info() << "all decks playing / none present; not loading cursor";
        return;
    }
    // Table-jump on load: keep the library selection on the cursor track.
    requestTableJumpToCursor();
    if (m_pPlayerManager != nullptr) {
        // Direct path load: robust for galaxy nodes not imported into the Mixxx
        // library (mirrors the double-click load path).
        loadCursorIntoDeck(deck);
        return;
    }
    // No PlayerManager (controller-only / widget tests): trigger the stock
    // LoadSelectedTrack on the chosen deck, relying on the selection-sync above.
    const ConfigKey loadKey(PlayerManager::groupForDeck(deck - 1), "LoadSelectedTrack");
    if (ControlObject::exists(loadKey)) {
        ControlObject::set(loadKey, 1.0);
    }
}

QPointF WCrateGalaxy::testNodeDisplayPos(int index) const {
    if (index < 0 || index >= m_dots.size()) {
        return QPointF();
    }
    return m_dots[index]->pos();
}

bool WCrateGalaxy::testNodeVisible(int index) const {
    return nodeSelectable(index);
}

int WCrateGalaxy::clusterLabelAt(const QPoint& viewportPos) const {
    for (const MapLabel& label : m_clusterLabels) {
        const QRectF rect(mapFromScene(label.sceneAnchor) + label.pixmapOffset,
                label.logicalSize);
        if (rect.adjusted(-8, -8, 8, 8).contains(viewportPos)) {
            return label.clusterId;
        }
    }
    return -1;
}

QVector<QRectF> WCrateGalaxy::testLabelRects() const {
    QVector<QRectF> result;
    const auto append = [this, &result](const QVector<MapLabel>& labels) {
        for (const MapLabel& label : labels) {
            result.append(QRectF(mapFromScene(label.sceneAnchor) + label.pixmapOffset,
                    label.logicalSize));
        }
    };
    append(m_clusterLabels);
    append(m_artistLabels);
    append(m_trackLabels);
    return result;
}

QStringList WCrateGalaxy::testTrackLabelRelpaths() const {
    QStringList result;
    for (const MapLabel& label : m_trackLabels) {
        if (label.nodeIndex >= 0) result.append(m_nodes[label.nodeIndex].relpath);
    }
    result.sort();
    return result;
}

int WCrateGalaxy::testVisibleSelectableNodeCount() const {
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    int count = 0;
    for (int i = 0; i < m_dots.size(); ++i) {
        if (nodeSelectable(i) && (!m_3dMode || m_nodes[i].has3d) &&
                visible.contains(m_dots[i]->pos())) {
            ++count;
        }
    }
    return count;
}

int WCrateGalaxy::testLabelCount(int layer) const {
    if (layer == 0) return m_clusterLabels.size();
    if (layer == 1) return m_artistLabels.size();
    if (layer == 2) return m_trackLabels.size();
    return 0;
}

QStringList WCrateGalaxy::testLabelTexts(int layer) const {
    const QVector<MapLabel>* labels = layer == 0 ? &m_clusterLabels
            : layer == 1 ? &m_artistLabels : &m_trackLabels;
    QStringList result;
    for (const MapLabel& label : *labels) result.append(label.text);
    result.sort(Qt::CaseInsensitive);
    return result;
}

QColor WCrateGalaxy::testLabelColor(int layer, int index) const {
    const QVector<MapLabel>* labels = layer == 0 ? &m_clusterLabels
            : layer == 1 ? &m_artistLabels : &m_trackLabels;
    return index >= 0 && index < labels->size() ? labels->at(index).color : QColor();
}

int WCrateGalaxy::testLabelClusterId(int layer, int index) const {
    const QVector<MapLabel>* labels = layer == 0 ? &m_clusterLabels
            : layer == 1 ? &m_artistLabels : &m_trackLabels;
    return index >= 0 && index < labels->size() ? labels->at(index).clusterId : -1;
}

int WCrateGalaxy::testTrackLabelLeaderCount() const {
    int count = 0;
    for (const MapLabel& label : m_trackLabels) {
        if (label.hasLeader) ++count;
    }
    return count;
}

bool WCrateGalaxy::testTrackLabelHasLeader(int index) const {
    return index >= 0 && index < m_trackLabels.size() &&
            m_trackLabels[index].hasLeader;
}

bool WCrateGalaxy::leadersSuppressed() const {
    constexpr qreal kScaleEpsilon = 1e-6;
    return qAbs(transform().m11() - m_labelBuiltTransformScale) > kScaleEpsilon;
}

bool WCrateGalaxy::testLeaderGeometrySane() const {
    for (const MapLabel& label : m_trackLabels) {
        if (!label.hasLeader || label.nodeIndex < 0) {
            continue;
        }
        const QPointF dotCenter = mapFromScene(label.sceneAnchor);
        const QRectF labelRect(dotCenter + label.pixmapOffset, label.logicalSize);
        const QPointF labelEdge(qBound(labelRect.left(), dotCenter.x(), labelRect.right()),
                qBound(labelRect.top(), dotCenter.y(), labelRect.bottom()));
        constexpr qreal kEpsilon = 1e-6;
        return qAbs(labelEdge.x() - labelRect.left()) < kEpsilon ||
                qAbs(labelEdge.x() - labelRect.right()) < kEpsilon ||
                qAbs(labelEdge.y() - labelRect.top()) < kEpsilon ||
                qAbs(labelEdge.y() - labelRect.bottom()) < kEpsilon;
    }
    return false;
}

QColor WCrateGalaxy::testClusterColor(int clusterId) const {
    return clusterColor(clusterId);
}

QString WCrateGalaxy::testNodeArtist(int index) const {
    return index >= 0 && index < m_nodes.size() ? artistForNode(m_nodes[index])
                                                : QString();
}

void WCrateGalaxy::testSetNodeDisplayPosition(int index, const QPointF& position) {
    if (index < 0 || index >= m_dots.size()) return;
    m_dots[index]->setPos(position);
    m_halos[index]->setPos(position);
    rebuildLabelCache();
}

QString WCrateGalaxy::testClusterName(int clusterId) const {
    QVector<int> members;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (nodeInSubset(i) && m_nodes[i].clusterId == clusterId) members.append(i);
    }
    return clusterId < 0 || members.isEmpty() ? QString()
                                               : clusterName(clusterId, members);
}

void WCrateGalaxy::testSetAllNodeDisplayPositions(const QPointF& position) {
    applyPositions(QVector<QPointF>(m_nodes.size(), position));
}

QString WCrateGalaxy::testNodeRelpath(int index) const {
    return index >= 0 && index < m_nodes.size() ? m_nodes[index].relpath : QString();
}

bool WCrateGalaxy::testNodeGhosted(int index) const {
    return index >= 0 && index < m_nodes.size() && !nodeInSubset(index);
}

bool WCrateGalaxy::testNodeHasHalo(int index) const {
    return index >= 0 && index < m_halos.size() && m_halos[index]->isVisible();
}

quintptr WCrateGalaxy::testDotItemIdentity(int index) const {
    return index >= 0 && index < m_dots.size()
            ? reinterpret_cast<quintptr>(m_dots[index])
            : 0;
}

int WCrateGalaxy::testProjectedNodeAt(const QPoint& viewportPos) const {
    return projectedNodeAt(viewportPos);
}

void WCrateGalaxy::testApplySubsetByRelpaths(const QSet<QString>& relpaths) {
    applySubset(relpaths);
}

void WCrateGalaxy::testClearSubset() {
    applySubset({}, false);
}

QString WCrateGalaxy::testLayoutMode() const {
    switch (m_layoutMode) {
    case LayoutMode::Scatter: return QStringLiteral("scatter");
    case LayoutMode::KeyWheel: return QStringLiteral("key");
    case LayoutMode::BpmSerpentine: return QStringLiteral("bpm");
    case LayoutMode::Artist: return QStringLiteral("artist");
    }
    return {};
}

QString WCrateGalaxy::testColorMode() const {
    switch (m_colorMode) {
    case ColorMode::Cluster: return QStringLiteral("cluster");
    case ColorMode::Key: return QStringLiteral("key");
    case ColorMode::Tempo: return QStringLiteral("tempo");
    case ColorMode::Energy: return QStringLiteral("energy");
    }
    return {};
}

void WCrateGalaxy::mouseDoubleClickEvent(QMouseEvent* pEvent) {
    const int node = nodeAtViewportPos(pEvent->pos());
    if (node < 0 || m_pPlayerManager == nullptr) {
        QGraphicsView::mouseDoubleClickEvent(pEvent);
        return;
    }
    // Re-seed the MAP-walk cursor here: a click on a node resets the walk with
    // this node as the new origin (spec: cursor re-seeded by a mouse click).
    setCursorNode(node, /*resetWalk=*/true);
    // Smarter target (spec wave-5 S3, item 2): the natural next-prep deck, never
    // stealing a playing one. Plain double-click no longer forces first-stopped.
    const int deck = nextPrepDeck();
    if (deck < 1) {
        kLogger.info() << "all decks playing; not loading"
                       << m_nodes[node].relpath;
        return;
    }
    // Table-jump on load, then load into the chosen deck.
    requestTableJumpToCursor();
    loadCursorIntoDeck(deck);
}

void WCrateGalaxy::showEvent(QShowEvent* pEvent) {
    QGraphicsView::showEvent(pEvent);
    reloadSidecarsIfChanged();
    if (!m_initialFitDone && !m_pScene->items().isEmpty()) {
        fitInView(m_pScene->sceneRect(), Qt::KeepAspectRatio);
        m_fitScale = transform().m11();
        double debugZoom = m_pConfig->getValue(
                ConfigKey("[Crate]", "galaxy_test_zoom"), 0.0);
        if (debugZoom <= 0.0) {
            debugZoom = m_pConfig->getValue(
                    ConfigKey("[Crate]", "galaxy_debug_zoom"), 0.0);
        }
        if (debugZoom > 0.0) {
            scale(debugZoom, debugZoom);
            centerOn(m_pScene->sceneRect().center());
        }
        m_initialFitDone = true;
        // The label/overlay caches were last built before the initial fit (or
        // the debug zoom) changed the view transform — every cached device
        // position is stale at first paint without this. Wheel/resize/scroll
        // paths rebuild themselves; the initial fit must too.
        rebuildLabelCache();
        rebuildOverlayCache();
        // Proof/debug hook (matches the other galaxy_debug_* keys): when knob
        // focus is MAP, seed a cursor on show so the highlight is visible in a
        // static screenshot. No effect on normal runs (key defaults off).
        if (m_knobFocusMap && m_cursorNode < 0 &&
                m_pConfig->getValue(
                        ConfigKey("[Crate]", "galaxy_debug_cursor"), 0) != 0) {
            setCursorNode(seedNode(), /*resetWalk=*/true);
            // Take a couple of forward steps so the walk history is non-trivial.
            stepCursorForward();
            stepCursorForward();
        }
        rebuildLabelCache();
    }
}

void WCrateGalaxy::focusInEvent(QFocusEvent* pEvent) {
    QGraphicsView::focusInEvent(pEvent);
    reloadSidecarsIfChanged();
}

} // namespace crate

#include "moc_wcrategalaxy.cpp"
