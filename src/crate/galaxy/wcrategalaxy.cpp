#include "crate/galaxy/wcrategalaxy.h"

#include <QDir>
#include <QFileInfo>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
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
constexpr double kPillFadeStart = 2.55;
constexpr double kPillFadeEnd = 3.15;
constexpr int kPillCellWidth = 180;
constexpr int kPillCellHeight = 52;
constexpr double kGhostColorStrength = 0.20;
const QColor kGalaxyInk(0x05, 0x06, 0x0a);

class GalaxyPillItem final : public QGraphicsItem {
  public:
    GalaxyPillItem(const crate::GalaxyNode& node, const QColor& accent, int index)
            : m_node(node), m_accent(accent) {
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
        p->setPen(QPen(QColor(244, 247, 251, 46), 1.0));
        p->setBrush(QColor(5, 6, 10, 235));
        p->drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);
        QFont font(QStringLiteral("monospace"));
        font.setStyleHint(QFont::Monospace);
        font.setPixelSize(10);
        p->setFont(font);
        const QFontMetrics fm(font);
        const auto elide = [&fm](const QString& text) {
            return fm.elidedText(text, Qt::ElideRight, 158);
        };
        p->setPen(QColor(244, 247, 251));
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
        Library* pLibrary)
        : QGraphicsView(pParent),
          m_pPlayerManager(pPlayerManager),
          m_pLibrary(pLibrary),
          m_pConfig(pConfig),
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
    setBackgroundBrush(kGalaxyInk);
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
            [this](const TrackPointer&) { updateMixabilityHalos(); });
    connect(&playerInfo,
            &PlayerInfo::currentPlayingDeckChanged,
            this,
            [this](int) { updateMixabilityHalos(); });
    updateMixabilityHalos();

    // Cursor highlight: a hollow mono ring, distinct from the now-playing dot
    // (filled white) and the mixability halos (blue radial glow). Kept above
    // everything, transform-independent, hidden until the walk seeds a cursor.
    m_pCursorRing = new QGraphicsEllipseItem(
            -kCursorRingRadius, -kCursorRingRadius,
            kCursorRingRadius * 2, kCursorRingRadius * 2);
    m_pCursorRing->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    m_pCursorRing->setPen(QPen(QColor(0xf4, 0xf7, 0xfb), 1.6));
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
    m_pColorProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_color_control"), this);
    m_p3dProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_3d"), this);
    m_pHaloProxy = new ControlProxy(ConfigKey("[Crate]", "galaxy_halos"), this);
    m_pKnobFocusProxy = new ControlProxy(ConfigKey("[Crate]", "knob_focus"), this);
    m_pLayoutProxy->connectValueChanged(this, [this](double value) {
        setLayoutMode(static_cast<LayoutMode>(qBound(0, qRound(value), 3)));
    });
    m_pColorProxy->connectValueChanged(this, [this](double value) {
        setColorMode(static_cast<ColorMode>(qBound(0, qRound(value), 3)));
    });
    m_p3dProxy->connectValueChanged(this, [this](double value) { set3dMode(value != 0.0); });
    m_pHaloProxy->connectValueChanged(this, [this](double value) { setHalosEnabled(value != 0.0); });
    m_pKnobFocusProxy->connectValueChanged(
            this, [this](double value) { setKnobFocusMap(value != 0.0); });
    m_pLayoutProxy->set(static_cast<int>(m_layoutMode));
    m_pColorProxy->set(static_cast<int>(m_colorMode));
    m_p3dProxy->set(m_3dMode ? 1.0 : 0.0);
    m_pHaloProxy->set(m_halosEnabled ? 1.0 : 0.0);
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
        pText->setBrush(QColor(0xf4, 0xf7, 0xfb));
        return;
    }

    CrateSidecars sidecars(sidecarDir);
    if (!sidecars.load()) {
        auto* pText = m_pScene->addSimpleText(
                QStringLiteral("galaxy: ") + sidecars.lastError());
        pText->setBrush(QColor(0xf4, 0xf7, 0xfb));
        kLogger.warning() << "sidecar load failed:" << sidecars.lastError();
        return;
    }
    m_nodes = sidecars.nodes();
    if (m_nodes.isEmpty()) {
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
    kLogger.info() << "galaxy populated with" << m_nodes.size() << "nodes from" << sidecarDir;
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
        updatePills();
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
    return QColor(qRound(kGalaxyInk.red() + (color.red() - kGalaxyInk.red()) * kGhostColorStrength),
            qRound(kGalaxyInk.green() + (color.green() - kGalaxyInk.green()) * kGhostColorStrength),
            qRound(kGalaxyInk.blue() + (color.blue() - kGalaxyInk.blue()) * kGhostColorStrength),
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
    const QString path = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(location).absoluteFilePath()))
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
    m_playingRelpath.clear();
    m_haloScores.clear();
    if (!m_halosEnabled || m_nodes.isEmpty()) {
        applyHaloVisuals();
        return;
    }

    const QString debugSeed = m_pConfig->getValue(
            ConfigKey("[Crate]", "galaxy_debug_seed"), QString()).trimmed();
    QString requested = QDir::fromNativeSeparators(debugSeed);
    if (requested.isEmpty()) {
        const TrackPointer pTrack = PlayerInfo::instance().getCurrentPlayingTrack();
        requested = pTrack ? relpathForLocation(pTrack->getLocation()) : QString();
    }
    int playingIndex = -1;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].relpath.compare(requested, Qt::CaseInsensitive) == 0) {
            playingIndex = i;
            m_playingRelpath = m_nodes[i].relpath;
            break;
        }
    }
    if (playingIndex < 0 || !nodeInSubset(playingIndex)) {
        applyHaloVisuals();
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
    if (!m_sonicVectors.centered(m_playingRelpath)) {
        applyHaloVisuals();
        return;
    }

    QVector<QPair<double, int>> ranked;
    ranked.reserve(m_nodes.size());
    const GalaxyNode& playing = m_nodes[playingIndex];
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (i == playingIndex || !nodeInSubset(i)) {
            continue;
        }
        const GalaxyNode& node = m_nodes[i];
        const auto sonic = m_sonicVectors.cosine(m_playingRelpath, node.relpath);
        if (!sonic) {
            continue;
        }
        const bool keysPresent = !playing.keyCamelot.isEmpty() && !node.keyCamelot.isEmpty();
        const bool bpmsPresent = playing.bpm > 0.0 && node.bpm > 0.0 &&
                std::isfinite(playing.bpm) && std::isfinite(node.bpm);
        const double score = scores::mixability(sonic,
                keysPresent,
                keysPresent,
                keysPresent ? scores::keyScore(playing.keyCamelot, node.keyCamelot) : 0.0,
                bpmsPresent,
                bpmsPresent,
                bpmsPresent ? scores::bpmScore(playing.bpm, node.bpm) : 0.0,
                m_sonicVectors.transition(m_playingRelpath, node.relpath));
        if (score >= 0.5) {
            ranked.append(qMakePair(score, i));
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    ranked.resize(qMin(40, ranked.size()));
    for (const auto& entry : std::as_const(ranked)) {
        m_haloScores.insert(entry.second, entry.first);
    }
    applyHaloVisuals();
}

void WCrateGalaxy::applyHaloVisuals() {
    for (int i = 0; i < m_dots.size(); ++i) {
        const bool playing = nodeInSubset(i) && !m_playingRelpath.isEmpty() &&
                m_nodes[i].relpath == m_playingRelpath;
        QGraphicsEllipseItem* pDot = m_dots[i];
        const double radius = playing ? kDotRadius * 1.65 : kDotRadius;
        if (!m_3dMode) {
            pDot->setRect(-radius, -radius, radius * 2, radius * 2);
        }
        pDot->setPen(playing ? QPen(QColor(0xf4, 0xf7, 0xfb), 1.0) : QPen(Qt::NoPen));
        if (playing) {
            pDot->setBrush(QColor(0xf4, 0xf7, 0xfb));
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
        const auto score = m_haloScores.constFind(i);
        const bool visible = nodeInSubset(i) && score != m_haloScores.constEnd() && pDot->isVisible();
        pHalo->setVisible(visible);
        if (visible) {
            const double strength = qBound(0.0, (*score - 0.5) / 0.5, 1.0);
            QRadialGradient gradient(QPointF(0.0, 0.0), kHaloRadius);
            gradient.setColorAt(0.0, QColor(180, 210, 255, qRound(105 + 80 * strength)));
            gradient.setColorAt(0.42, QColor(180, 210, 255, qRound(65 + 65 * strength)));
            gradient.setColorAt(1.0, QColor(180, 210, 255, 0));
            pHalo->setBrush(gradient);
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
    updatePills();
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
        for (int i = 0; i < m_nodes.size(); ++i) if (m_nodes[i].bpm > 0.0) order.append(i);
        std::sort(order.begin(), order.end(), [this](int a, int b) { return m_nodes[a].bpm < m_nodes[b].bpm; });
        for (int rank = 0; rank < order.size(); ++rank) {
            const double f = rank / qMax(1.0, static_cast<double>(order.size() - 1));
            const double y = 0.50 * kSceneSpan + 0.24 * kSceneSpan * std::sin(f * 2.0 * M_PI * 1.25) +
                    0.07 * kSceneSpan * std::sin(f * 2.0 * M_PI * 2.6 + 1.3);
            target[order[rank]] = QPointF((0.08 + 0.84 * f) * kSceneSpan, y);
        }
        for (int i = 0; i < m_nodes.size(); ++i) if (!(m_nodes[i].bpm > 0.0)) target[i] = QPointF(0.5 * kSceneSpan, 0.97 * kSceneSpan);
        return separate(target, 0.97);
    }
    int hits = 0;
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].hasArtistPosition) {
            target[i] = QPointF((m_nodes[i].artistX + normal() * 0.018) * kSceneSpan,
                    (m_nodes[i].artistY + normal() * 0.018) * kSceneSpan);
            ++hits;
        } else {
            target[i] = QPointF((0.04 + normal() * 0.02) * kSceneSpan,
                    (0.96 + normal() * 0.02) * kSceneSpan);
        }
    }
    return hits >= qMax(3, m_nodes.size() / 4) ? separate(target, 0.95) : m_scatterPositions;
}

void WCrateGalaxy::setLayoutMode(LayoutMode mode, bool animate) {
    if (m_3dMode) return;
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
    // Deck-choice load (spec wave-5 S3): if the right-click landed on a
    // selectable dot, offer "Load to Deck N" for each deck at the top. Ghosted /
    // non-selectable nodes get no deck-load entries.
    const int node = nodeAtViewportPos(pEvent->pos());
    if (node >= 0) {
        addDeckLoadActions(&menu, node);
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
    if (m_colorMode == ColorMode::Cluster) {
        return;
    }
    pPainter->save();
    pPainter->resetTransform();
    pPainter->setRenderHint(QPainter::Antialiasing, true);
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(10);
    pPainter->setFont(font);
    pPainter->setPen(QColor(0xf4, 0xf7, 0xfb));

    const int margin = 10;
    const int top = margin;
    if (m_colorMode == ColorMode::Key) {
        const QRect chip(margin, top, 224, 42);
        pPainter->setBrush(QColor(0x05, 0x06, 0x0a, 205));
        pPainter->setPen(Qt::NoPen);
        pPainter->drawRoundedRect(chip, 3, 3);
        pPainter->setPen(QColor(0xf4, 0xf7, 0xfb));
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
        pPainter->setBrush(QColor(0x05, 0x06, 0x0a, 205));
        pPainter->setPen(Qt::NoPen);
        pPainter->drawRoundedRect(chip, 3, 3);
        QLinearGradient gradient(bar.topLeft(), bar.topRight());
        for (int i = 0; i <= 10; ++i) {
            const double fraction = i / 10.0;
            gradient.setColorAt(
                    fraction, rampColor(m_colorMode == ColorMode::Tempo, fraction));
        }
        pPainter->fillRect(bar, gradient);
        pPainter->setPen(QColor(0xf4, 0xf7, 0xfb));
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
    const double factor = (pEvent->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    if (m_3dMode) {
        update3dProjection();
    }
    updatePills();
    pEvent->accept();
}

void WCrateGalaxy::setHoveredNode(int index) {
    if (!nodeSelectable(index)) {
        index = -1;
    }
    if (m_hoveredNode != index) {
        m_hoveredNode = index;
        updatePills();
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
    updatePills();
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
    updatePills();
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
    QGraphicsItem* pItem = itemAt(pEvent->pos());
    setHoveredNode(m_3dMode ? projectedNodeAt(pEvent->pos())
                           : (pItem != nullptr && pItem->data(0).isValid()
                    ? pItem->data(0).toInt()
                    : -1));
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
    updatePills();
}

void WCrateGalaxy::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    updatePills();
}

void WCrateGalaxy::updatePills() {
    if (m_nodes.isEmpty() || m_dots.size() != m_nodes.size()) {
        return;
    }
    const double zoom = m_fitScale > 0.0 ? transform().m11() / m_fitScale : 0.0;
    const double lodOpacity = qBound(0.0,
            (zoom - kPillFadeStart) / (kPillFadeEnd - kPillFadeStart), 1.0);
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const double margin = 190.0 / qMax(transform().m11(), 0.001);
    const QRectF nearby = visible.adjusted(-margin, -margin, margin, margin);
    QVector<int> candidates;
    if (lodOpacity > 0.0) {
        for (int i = 0; i < m_dots.size(); ++i) {
            if (nodeSelectable(i) && (!m_3dMode || m_nodes[i].has3d) &&
                    nearby.contains(m_dots[i]->pos())) {
                candidates.append(i);
            }
        }
        if (m_3dMode) {
            std::stable_sort(candidates.begin(), candidates.end(), [this](int a, int b) {
                return m_dots[a]->zValue() > m_dots[b]->zValue();
            });
        }
    }

    // Cull in viewport pixels because pills ignore the view transform. Reserve
    // the hovered node first, then use front-to-back depth in 3D (or ascending
    // node index in 2D) as stable priority.
    // The rectangle check also prevents overlap across adjacent cell edges.
    QSet<int> wanted;
    QSet<QPair<int, int>> occupiedCells;
    QVector<QRect> occupiedRects;
    const auto tryAdd = [&](int index) {
        if (index < 0 || index >= m_dots.size() || wanted.contains(index)) {
            return;
        }
        const QPoint anchor = mapFromScene(m_dots[index]->pos());
        const QPair<int, int> cell(
                qFloor(static_cast<double>(anchor.x()) / kPillCellWidth),
                qFloor(static_cast<double>(anchor.y()) / kPillCellHeight));
        const QRect pillRect(anchor.x() + 7, anchor.y() - 25, 172, 50);
        if (occupiedCells.contains(cell)) {
            return;
        }
        for (const QRect& occupied : std::as_const(occupiedRects)) {
            if (occupied.intersects(pillRect)) {
                return;
            }
        }
        wanted.insert(index);
        occupiedCells.insert(cell);
        occupiedRects.append(pillRect);
    };
    if (nodeSelectable(m_hoveredNode) && (!m_3dMode || m_nodes[m_hoveredNode].has3d)) {
        tryAdd(m_hoveredNode);
    }
    for (int index : std::as_const(candidates)) {
        tryAdd(index);
    }
    for (auto it = m_pills.begin(); it != m_pills.end();) {
        if (!wanted.contains(it.key())) {
            delete it.value();
            it = m_pills.erase(it);
        } else {
            ++it;
        }
    }
    for (int index : std::as_const(wanted)) {
        QGraphicsItem* pPill = m_pills.value(index, nullptr);
        if (pPill == nullptr) {
            pPill = new GalaxyPillItem(m_nodes[index], nodeColor(m_nodes[index]), index);
            m_pScene->addItem(pPill);
            m_pills.insert(index, pPill);
        }
        pPill->setPos(m_dots[index]->pos());
        pPill->setOpacity(index == m_hoveredNode ? 1.0 : lodOpacity);
    }
    for (int i = 0; i < m_dots.size(); ++i) {
        const bool playing = nodeInSubset(i) && !m_playingRelpath.isEmpty() &&
                m_nodes[i].relpath == m_playingRelpath;
        m_dots[i]->setOpacity(wanted.contains(i) && !playing ? 0.0 : 1.0);
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
    if (!m_playingRelpath.isEmpty()) {
        for (int i = 0; i < m_nodes.size(); ++i) {
            if (nodeSelectable(i) &&
                    m_nodes[i].relpath == m_playingRelpath) {
                return i;
            }
        }
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

double WCrateGalaxy::orbitAngleDelta(int pixelDelta, int viewportExtent, double zoomRatio) {
    const double extent = static_cast<double>(qMax(1, viewportExtent));
    const double ratio = zoomRatio > 0.0 ? zoomRatio : 1.0;
    // Base mapping: a full viewport-width drag = 360 degrees at the fitted scale.
    // Dividing by the zoom ratio keeps the *screen-pixel* motion of a node
    // constant, because the view transform already scales scene motion by that
    // same ratio.
    return static_cast<double>(pixelDelta) * 360.0 / extent / ratio;
}

int WCrateGalaxy::pickNextPrepDeck(const QVector<bool>& playing, int lastStartedIndex) {
    const int n = playing.size();
    if (n == 0) {
        return 0;
    }
    bool anyPlaying = false;
    bool anyStopped = false;
    for (bool p : playing) {
        anyPlaying = anyPlaying || p;
        anyStopped = anyStopped || !p;
    }
    if (!anyPlaying) {
        return 1; // fresh session: prep deck 1
    }
    if (!anyStopped) {
        return 0; // every deck busy: never steal
    }
    // Reference playing deck = the most-recently-started one when known and still
    // playing; otherwise the lowest-numbered playing deck.
    int refIndex = lastStartedIndex;
    if (refIndex < 0 || refIndex >= n || !playing[refIndex]) {
        refIndex = -1;
        for (int i = 0; i < n; ++i) {
            if (playing[i]) {
                refIndex = i;
                break;
            }
        }
    }
    // Sides mirror the physical layout: decks 1/3 (even 0-based index) vs 2/4
    // (odd 0-based index). Prep the opposite side of the reference deck.
    const int oppositeSide = 1 - (refIndex % 2);
    for (int i = 0; i < n; ++i) {
        if (!playing[i] && (i % 2) == oppositeSide) {
            return i + 1;
        }
    }
    // Opposite side is full; fall back to the lowest-numbered stopped deck.
    for (int i = 0; i < n; ++i) {
        if (!playing[i]) {
            return i + 1;
        }
    }
    return 0;
}

int WCrateGalaxy::nextPrepDeck() const {
    // Live "most-recently-started" is approximated by PlayerInfo's current
    // (loudest audible) playing deck; for the standard two-deck battle these
    // coincide. Verified live (S5): full 4-deck last-started ordering.
    return pickNextPrepDeck(
            deckPlayingStates(), PlayerInfo::instance().getCurrentPlayingDeck());
}

int WCrateGalaxy::nodeAtViewportPos(const QPoint& viewportPos) const {
    if (m_3dMode) {
        return projectedNodeAt(viewportPos);
    }
    QGraphicsItem* pItem = itemAt(viewportPos);
    if (pItem != nullptr && pItem->data(0).isValid()) {
        const int index = pItem->data(0).toInt();
        return nodeSelectable(index) ? index : -1;
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
    QGraphicsItem* pItem = itemAt(pEvent->pos());
    const int projectedIndex = m_3dMode ? projectedNodeAt(pEvent->pos()) : -1;
    if ((m_3dMode ? projectedIndex < 0 : pItem == nullptr) || m_pPlayerManager == nullptr) {
        QGraphicsView::mouseDoubleClickEvent(pEvent);
        return;
    }
    const QVariant idx = m_3dMode ? QVariant(projectedIndex) : pItem->data(0);
    if (!idx.isValid() || !nodeSelectable(idx.toInt())) {
        return;
    }
    // Re-seed the MAP-walk cursor here: a click on a node resets the walk with
    // this node as the new origin (spec: cursor re-seeded by a mouse click).
    setCursorNode(idx.toInt(), /*resetWalk=*/true);
    // Smarter target (spec wave-5 S3, item 2): the natural next-prep deck, never
    // stealing a playing one. Plain double-click no longer forces first-stopped.
    const int deck = nextPrepDeck();
    if (deck < 1) {
        kLogger.info() << "all decks playing; not loading"
                       << m_nodes[idx.toInt()].relpath;
        return;
    }
    // Table-jump on load, then load into the chosen deck.
    requestTableJumpToCursor();
    loadCursorIntoDeck(deck);
}

void WCrateGalaxy::showEvent(QShowEvent* pEvent) {
    QGraphicsView::showEvent(pEvent);
    if (!m_initialFitDone && !m_pScene->items().isEmpty()) {
        fitInView(m_pScene->sceneRect(), Qt::KeepAspectRatio);
        m_fitScale = transform().m11();
        const double debugZoom = m_pConfig->getValue(
                ConfigKey("[Crate]", "galaxy_debug_zoom"), 0.0);
        if (debugZoom > 0.0) {
            scale(debugZoom, debugZoom);
        }
        m_initialFitDone = true;
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
        updatePills();
    }
}

} // namespace crate

#include "moc_wcrategalaxy.cpp"
