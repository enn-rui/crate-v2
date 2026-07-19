#include "crate/galaxy/wcrategalaxy.h"

#include <QDir>
#include <QFileInfo>
#include <QGraphicsEllipseItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QWheelEvent>

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
        pDot->setBrush(clusterColor(node.clusterId));
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
