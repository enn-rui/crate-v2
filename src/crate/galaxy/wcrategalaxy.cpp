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
#include <QRegularExpression>
#include <QtMath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

#include "control/controlobject.h"
#include "mixer/playermanager.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("WCrateGalaxy");

constexpr double kSceneSpan = 2000.0; // normalized coordinate span
constexpr double kDotRadius = 3.5;    // px, transform-independent
constexpr double kPillFadeStart = 2.55;
constexpr double kPillFadeEnd = 3.15;
constexpr int kPillCellWidth = 180;
constexpr int kPillCellHeight = 52;

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
        UserSettingsPointer pConfig)
        : QGraphicsView(pParent),
          m_pPlayerManager(pPlayerManager),
          m_pConfig(pConfig),
          m_pScene(new QGraphicsScene(this)) {
    setScene(m_pScene);
    setBackgroundBrush(QColor(0x05, 0x06, 0x0a));
    setFrameShape(QFrame::NoFrame);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    setRenderHint(QPainter::Antialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_3dMode = m_pConfig->getValue(ConfigKey("[Crate]", "galaxy_3d"), 0) != 0;
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
    populate();
}

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
        auto* pDot = new QGraphicsEllipseItem(
                -kDotRadius, -kDotRadius, kDotRadius * 2, kDotRadius * 2);
        pDot->setPos(sx, sy);
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
    }
    kLogger.info() << "galaxy populated with" << m_nodes.size() << "nodes from" << sidecarDir;
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
        m_dots[i]->setBrush(nodeColor(m_nodes[i]));
    }
    if (m_3dMode) {
        update3dProjection();
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
    updateColors();
}

void WCrateGalaxy::contextMenuEvent(QContextMenuEvent* pEvent) {
    QMenu menu(this);
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
    QAction* p3d = menu.addAction(tr("3D view"));
    p3d->setCheckable(true);
    p3d->setChecked(m_3dMode);
    connect(p3d, &QAction::toggled, this, &WCrateGalaxy::set3dMode);
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
    if (m_colorMode == ColorMode::Key) {
        const QRect chip(margin, margin, 224, 42);
        pPainter->setBrush(QColor(0x05, 0x06, 0x0a, 205));
        pPainter->setPen(Qt::NoPen);
        pPainter->drawRoundedRect(chip, 3, 3);
        pPainter->setPen(QColor(0xf4, 0xf7, 0xfb));
        for (int row = 0; row < 2; ++row) {
            const int y = margin + 6 + row * 17;
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
        const QRect chip(margin, margin, 196, 30);
        const QRect bar(margin + 38, margin + 7, 112, 8);
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
        pPainter->drawText(QRect(margin + 5, margin + 5, 30, 12), Qt::AlignRight, low);
        pPainter->drawText(QRect(margin + 154, margin + 5, 37, 12), Qt::AlignLeft, high);
        pPainter->drawText(QRect(margin + 38, margin + 17, 112, 10),
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
    if (enabled) {
        update3dProjection();
    } else if (!m_nodes.isEmpty()) {
        double minX = m_nodes[0].x, maxX = m_nodes[0].x;
        double minY = m_nodes[0].y, maxY = m_nodes[0].y;
        for (const GalaxyNode& node : std::as_const(m_nodes)) {
            minX = qMin(minX, node.x);
            maxX = qMax(maxX, node.x);
            minY = qMin(minY, node.y);
            maxY = qMax(maxY, node.y);
        }
        const double spanX = qMax(maxX - minX, 1e-9);
        const double spanY = qMax(maxY - minY, 1e-9);
        for (int i = 0; i < m_dots.size(); ++i) {
            QGraphicsEllipseItem* pDot = m_dots[i];
            pDot->setVisible(true);
            pDot->setRect(-kDotRadius, -kDotRadius, 2 * kDotRadius, 2 * kDotRadius);
            pDot->setPos((m_nodes[i].x - minX) / spanX * kSceneSpan,
                    (m_nodes[i].y - minY) / spanY * kSceneSpan);
            pDot->setZValue(0.0);
            pDot->setBrush(nodeColor(m_nodes[i]));
        }
    }
    updatePills();
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
    }
    const double depthSpan = qMax(maxDepth - minDepth, 1e-9);
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i].has3d) {
            continue;
        }
        const double d = (depths[i] - minDepth) / depthSpan;
        const double radius = kDotRadius * (0.55 + 0.9 * d);
        QColor color = nodeColor(m_nodes[i]);
        color.setAlpha(qRound(70.0 + 170.0 * d));
        QGraphicsEllipseItem* pDot = m_dots[i];
        pDot->setVisible(true);
        pDot->setRect(-radius, -radius, 2 * radius, 2 * radius);
        pDot->setBrush(color);
        pDot->setZValue(depths[i]);
    }
    updatePills();
    viewport()->update();
}

int WCrateGalaxy::projectedNodeAt(const QPoint& viewportPos) const {
    int nearest = -1;
    double bestDistance = 18.0 * 18.0;
    for (int i = 0; i < m_dots.size(); ++i) {
        if (!m_dots[i]->isVisible() || !m_nodes[i].has3d) {
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

void WCrateGalaxy::mousePressEvent(QMouseEvent* pEvent) {
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
    if (m_3dMode && m_orbiting) {
        const QPoint delta = pEvent->pos() - m_orbitLast;
        if (delta.manhattanLength() > 0) {
            m_orbitMoved = m_orbitMoved || delta.manhattanLength() > 2;
            m_azimuth += delta.x() * 360.0 / qMax(1, viewport()->width());
            m_elevation = qBound(-85.0,
                    m_elevation + delta.y() * 360.0 / qMax(1, viewport()->width()),
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
    if (!m_3dMode && lodOpacity > 0.0) {
        for (int i = 0; i < m_dots.size(); ++i) {
            if (nearby.contains(m_dots[i]->pos())) {
                candidates.append(i);
            }
        }
    }

    // Cull in viewport pixels because pills ignore the view transform. Reserve
    // the hovered node first, then use ascending node index as stable priority.
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
    if (m_hoveredNode >= 0 && (!m_3dMode || m_nodes[m_hoveredNode].has3d)) {
        tryAdd(m_hoveredNode);
    }
    if (!m_3dMode) {
        for (int index : std::as_const(candidates)) {
            tryAdd(index);
        }
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
            pPill->setPos(m_dots[index]->pos());
            m_pScene->addItem(pPill);
            m_pills.insert(index, pPill);
        }
        pPill->setOpacity(index == m_hoveredNode ? 1.0 : lodOpacity);
    }
    for (int i = 0; i < m_dots.size(); ++i) {
        m_dots[i]->setOpacity(wanted.contains(i) ? 0.0 : 1.0);
    }
}

void WCrateGalaxy::mouseDoubleClickEvent(QMouseEvent* pEvent) {
    QGraphicsItem* pItem = itemAt(pEvent->pos());
    const int projectedIndex = m_3dMode ? projectedNodeAt(pEvent->pos()) : -1;
    if ((m_3dMode ? projectedIndex < 0 : pItem == nullptr) || m_pPlayerManager == nullptr) {
        QGraphicsView::mouseDoubleClickEvent(pEvent);
        return;
    }
    const QVariant idx = m_3dMode ? QVariant(projectedIndex) : pItem->data(0);
    if (!idx.isValid()) {
        return;
    }
    const GalaxyNode& node = m_nodes[idx.toInt()];
    const QString path = resolveMusicPath(node.relpath);
    if (path.isEmpty()) {
        kLogger.warning() << "no music_root; cannot load" << node.relpath;
        return;
    }
    // Load into the first stopped deck; never yank a playing one.
    const int deckCount = static_cast<int>(m_pPlayerManager->numberOfDecks());
    for (int i = 1; i <= deckCount; ++i) {
        const QString group = PlayerManager::groupForDeck(i - 1);
        if (ControlObject::get(ConfigKey(group, "play")) == 0.0) {
            kLogger.info() << "loading" << path << "into deck" << i;
            m_pPlayerManager->slotLoadToDeck(path, i);
            return;
        }
    }
    kLogger.info() << "all decks playing; not loading" << path;
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
        updatePills();
    }
}

} // namespace crate

#include "moc_wcrategalaxy.cpp"
