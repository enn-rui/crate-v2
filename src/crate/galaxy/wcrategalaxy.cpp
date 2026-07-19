#include "crate/galaxy/wcrategalaxy.h"

#include <QDir>
#include <QFileInfo>
#include <QActionGroup>
#include <QContextMenuEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
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
    setRenderHint(QPainter::Antialiasing, true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
        const QString stem = QFileInfo(node.relpath).completeBaseName();
        pDot->setToolTip(QStringLiteral("%1\n%2 BPM · %3")
                        .arg(stem,
                                QString::number(node.bpm, 'f', 1),
                                node.keyCamelot));
        m_pScene->addItem(pDot);
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
    for (QGraphicsItem* pItem : m_pScene->items()) {
        auto* pDot = qgraphicsitem_cast<QGraphicsEllipseItem*>(pItem);
        if (pDot == nullptr || !pDot->data(0).isValid()) {
            continue;
        }
        const int index = pDot->data(0).toInt();
        if (index >= 0 && index < m_nodes.size()) {
            pDot->setBrush(nodeColor(m_nodes[index]));
        }
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
    pEvent->accept();
}

void WCrateGalaxy::mouseDoubleClickEvent(QMouseEvent* pEvent) {
    QGraphicsItem* pItem = itemAt(pEvent->pos());
    if (pItem == nullptr || m_pPlayerManager == nullptr) {
        QGraphicsView::mouseDoubleClickEvent(pEvent);
        return;
    }
    const QVariant idx = pItem->data(0);
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
        fitInView(m_pScene->itemsBoundingRect(), Qt::KeepAspectRatio);
        m_initialFitDone = true;
    }
}

} // namespace crate

#include "moc_wcrategalaxy.cpp"
