#include "crate/grab/wgrabview.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "crate/grab/grabclient.h"
#include "moc_wgrabview.cpp"

namespace {
constexpr int kQueuePollMs = 12000;
} // namespace

namespace crate {

WGrabView::WGrabView(QWidget* pParent, UserSettingsPointer pConfig)
        : QWidget(pParent),
          m_pConfig(std::move(pConfig)),
          m_pClient(nullptr),
          m_pResultsModel(new GrabResultsModel(this)),
          m_pQueueModel(new GrabQueueModel(this)),
          m_pQuery(new QLineEdit(this)),
          m_pArtistMode(new QPushButton(QStringLiteral("ARTIST"), this)),
          m_pTrackMode(new QPushButton(QStringLiteral("TRACK"), this)),
          m_pSearch(new QPushButton(QStringLiteral("SEARCH"), this)),
          m_pDownload(new QPushButton(QStringLiteral("GRAB"), this)),
          m_pStatus(new QLabel(this)),
          m_pHint(new QLabel(this)),
          m_pQueueHeader(new QLabel(QStringLiteral("QUEUE"), this)),
          m_pResults(new QTableView(this)),
          m_pQueue(new QTableView(this)),
          m_pElapsedTimer(new QTimer(this)),
          m_pQueuePoll(new QTimer(this)),
          m_reachable(true),
          m_searching(false) {
    setObjectName(QStringLiteral("GrabView"));

    m_pClient = new GrabClient(GrabClient::configuredBaseUrl(m_pConfig),
            GrabClient::configuredToken(m_pConfig),
            this);

    // --- Query row -----------------------------------------------------------
    m_pQuery->setObjectName(QStringLiteral("GrabQuery"));
    m_pQuery->setPlaceholderText(QStringLiteral("artist or track name"));
    m_pQuery->setClearButtonEnabled(true);
    m_pArtistMode->setObjectName(QStringLiteral("GrabModeArtist"));
    m_pTrackMode->setObjectName(QStringLiteral("GrabModeTrack"));
    m_pSearch->setObjectName(QStringLiteral("GrabSearch"));
    for (QPushButton* pButton : {m_pArtistMode, m_pTrackMode}) {
        pButton->setCheckable(true);
        pButton->setFocusPolicy(Qt::NoFocus);
    }
    m_pArtistMode->setChecked(true);
    m_pSearch->setFocusPolicy(Qt::NoFocus);

    auto* pQueryRow = new QHBoxLayout();
    pQueryRow->setContentsMargins(0, 0, 0, 0);
    pQueryRow->setSpacing(6);
    pQueryRow->addWidget(m_pQuery, 1);
    pQueryRow->addWidget(m_pArtistMode);
    pQueryRow->addWidget(m_pTrackMode);
    pQueryRow->addWidget(m_pSearch);

    // --- Status + hint -------------------------------------------------------
    m_pStatus->setObjectName(QStringLiteral("GrabStatus"));
    m_pHint->setObjectName(QStringLiteral("GrabHint"));
    m_pHint->setWordWrap(true);
    m_pHint->hide();

    // --- Results table -------------------------------------------------------
    m_pResults->setObjectName(QStringLiteral("GrabResults"));
    m_pResults->setModel(m_pResultsModel);
    m_pResults->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pResults->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pResults->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pResults->setAlternatingRowColors(true);
    m_pResults->verticalHeader()->hide();
    m_pResults->horizontalHeader()->setStretchLastSection(false);
    m_pResults->horizontalHeader()->setSectionResizeMode(
            GrabResultsModel::ColumnName, QHeaderView::Stretch);

    // --- Queue strip ---------------------------------------------------------
    m_pQueueHeader->setObjectName(QStringLiteral("GrabQueueHeader"));
    m_pDownload->setObjectName(QStringLiteral("GrabDownload"));
    m_pDownload->setFocusPolicy(Qt::NoFocus);
    m_pQueue->setObjectName(QStringLiteral("GrabQueue"));
    m_pQueue->setModel(m_pQueueModel);
    m_pQueue->setSelectionMode(QAbstractItemView::NoSelection);
    m_pQueue->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_pQueue->verticalHeader()->hide();
    m_pQueue->horizontalHeader()->setStretchLastSection(true);
    m_pQueue->horizontalHeader()->setSectionResizeMode(
            GrabQueueModel::ColumnName, QHeaderView::Stretch);
    m_pQueue->setMaximumHeight(150);

    auto* pQueueHeaderRow = new QHBoxLayout();
    pQueueHeaderRow->setContentsMargins(0, 0, 0, 0);
    pQueueHeaderRow->addWidget(m_pQueueHeader, 1);
    pQueueHeaderRow->addWidget(m_pDownload);

    // --- Assemble ------------------------------------------------------------
    auto* pMain = new QVBoxLayout(this);
    pMain->setContentsMargins(10, 10, 10, 10);
    pMain->setSpacing(6);
    pMain->addLayout(pQueryRow);
    pMain->addWidget(m_pStatus);
    pMain->addWidget(m_pHint);
    pMain->addWidget(m_pResults, 1);
    pMain->addLayout(pQueueHeaderRow);
    pMain->addWidget(m_pQueue);

    m_pElapsedTimer->setInterval(1000);
    m_pQueuePoll->setInterval(kQueuePollMs);

    // --- Wiring --------------------------------------------------------------
    connect(m_pSearch, &QPushButton::clicked, this, &WGrabView::onSearchClicked);
    connect(m_pQuery, &QLineEdit::returnPressed, this, &WGrabView::onSearchClicked);
    connect(m_pArtistMode, &QPushButton::clicked, this, [this]() {
        m_pArtistMode->setChecked(true);
        m_pTrackMode->setChecked(false);
        onModeChanged();
    });
    connect(m_pTrackMode, &QPushButton::clicked, this, [this]() {
        m_pTrackMode->setChecked(true);
        m_pArtistMode->setChecked(false);
        onModeChanged();
    });
    connect(m_pDownload, &QPushButton::clicked, this, &WGrabView::onDownloadClicked);
    connect(m_pResults, &QTableView::doubleClicked, this, &WGrabView::onResultActivated);
    connect(m_pElapsedTimer, &QTimer::timeout, this, &WGrabView::tickElapsed);
    connect(m_pQueuePoll, &QTimer::timeout, this, [this]() { m_pClient->refreshQueue(); });

    connect(m_pClient, &GrabClient::searchStarted, this, &WGrabView::onSearchStarted);
    connect(m_pClient, &GrabClient::searchStillRunning, this, &WGrabView::onSearchStillRunning);
    connect(m_pClient, &GrabClient::resultsReady, this, &WGrabView::onResultsReady);
    connect(m_pClient, &GrabClient::searchFailed, this, &WGrabView::onSearchFailed);
    connect(m_pClient, &GrabClient::downloadResult, this, &WGrabView::onDownloadResult);
    connect(m_pClient, &GrabClient::downloadFailed, this, &WGrabView::onDownloadFailed);
    connect(m_pClient, &GrabClient::queueReady, this, &WGrabView::onQueueReady);
    connect(m_pClient, &GrabClient::queueFailed, this, &WGrabView::onQueueFailed);
    connect(m_pClient, &GrabClient::pingResult, this, &WGrabView::onPingResult);

    setStatus(QStringLiteral("type an artist and hit SEARCH."));
    // Check reachability up front so a dead service is obvious immediately.
    m_pClient->ping();
}

WGrabView::~WGrabView() = default;

QString WGrabView::currentMode() const {
    return m_pTrackMode->isChecked() ? QStringLiteral("track")
                                     : QStringLiteral("artist");
}

void WGrabView::onActivated() {
    m_pClient->ping();
    if (m_reachable) {
        m_pClient->refreshQueue();
    }
}

void WGrabView::onModeChanged() {
    if (m_pTrackMode->isChecked()) {
        setHint(QStringLiteral(
                "heads up: searching for one exact track on Soulseek often "
                "comes up thin — searching by artist usually finds far more."));
    } else {
        m_pHint->hide();
    }
}

void WGrabView::onSearchClicked() {
    if (m_searching) {
        return;
    }
    if (!m_reachable) {
        setStatus(QStringLiteral(
                "grab service not reachable — nothing to search yet."));
        return;
    }
    const QString query = m_pQuery->text().trimmed();
    if (query.isEmpty()) {
        setStatus(QStringLiteral("type an artist or a track name first."));
        return;
    }
    beginSearch();
    m_pClient->startSearch(query, currentMode());
}

void WGrabView::beginSearch() {
    m_searching = true;
    m_pResultsModel->clearResults();
    m_pHint->hide();
    m_pSearch->setEnabled(false);
    m_pSearch->setText(QStringLiteral("…"));
    m_elapsed.restart();
    m_pElapsedTimer->start();
    setStatus(QStringLiteral(
            "searching… the box is asking Soulseek, this takes about a minute."));
}

void WGrabView::tickElapsed() {
    if (!m_searching) {
        return;
    }
    const qint64 seconds = m_elapsed.elapsed() / 1000;
    setStatus(QStringLiteral("searching… %1s — the box is asking Soulseek, "
                             "this takes about a minute.")
                      .arg(seconds));
}

void WGrabView::onSearchStarted(const QString& id) {
    Q_UNUSED(id);
    // The searching state is already shown; nothing more to do.
}

void WGrabView::onSearchStillRunning() {
    // Keep the elapsed ticker going; no per-poll UI change needed.
}

void WGrabView::onResultsReady(const QVector<crate::GrabResult>& results) {
    m_searching = false;
    m_pElapsedTimer->stop();
    m_pSearch->setEnabled(true);
    m_pSearch->setText(QStringLiteral("SEARCH"));
    m_pResultsModel->setResults(results);
    if (results.isEmpty()) {
        if (currentMode() == QLatin1String("track")) {
            setStatus(QStringLiteral("no sources found."));
            setHint(QStringLiteral(
                    "exact-track searches on Soulseek are often thin — try "
                    "searching by the artist instead, then pick the track."));
        } else {
            setStatus(QStringLiteral(
                    "no sources found — try a different spelling or a related "
                    "artist."));
        }
        return;
    }
    setStatus(QStringLiteral("%1 source(s). double-click a row, or select and "
                             "hit GRAB.")
                      .arg(results.size()));
}

void WGrabView::onSearchFailed(const QString& error) {
    m_searching = false;
    m_pElapsedTimer->stop();
    m_pSearch->setEnabled(true);
    m_pSearch->setText(QStringLiteral("SEARCH"));
    setStatus(QStringLiteral("search didn't work: %1").arg(error));
}

void WGrabView::onResultActivated(const QModelIndex& index) {
    if (!index.isValid()) {
        return;
    }
    const QString key = m_pResultsModel->keyAt(index.row());
    if (key.isEmpty()) {
        return;
    }
    m_pClient->requestDownload({key});
    setStatus(QStringLiteral("queuing 1 download…"));
}

void WGrabView::onDownloadClicked() {
    downloadSelectedRows();
}

void WGrabView::downloadSelectedRows() {
    const QModelIndexList selected =
            m_pResults->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        setStatus(QStringLiteral("pick one or more results first, then GRAB."));
        return;
    }
    QStringList keys;
    for (const QModelIndex& index : selected) {
        const QString key = m_pResultsModel->keyAt(index.row());
        if (!key.isEmpty()) {
            keys.append(key);
        }
    }
    if (keys.isEmpty()) {
        return;
    }
    m_pClient->requestDownload(keys);
    setStatus(QStringLiteral("queuing %1 download(s)…").arg(keys.size()));
}

void WGrabView::onDownloadResult(const QStringList& accepted, const QStringList& rejected) {
    if (rejected.isEmpty()) {
        setStatus(QStringLiteral("queued %1 download(s).").arg(accepted.size()));
    } else {
        setStatus(QStringLiteral("queued %1, skipped %2 (already queued or gone).")
                          .arg(accepted.size())
                          .arg(rejected.size()));
    }
    m_pClient->refreshQueue();
}

void WGrabView::onDownloadFailed(const QString& error) {
    setStatus(QStringLiteral("couldn't queue that: %1").arg(error));
}

void WGrabView::onQueueReady(const QVector<crate::GrabQueueItem>& items) {
    m_pQueueModel->setQueue(items);
}

void WGrabView::onQueueFailed(const QString& error) {
    Q_UNUSED(error);
    // Queue is a background nicety; a failed refresh must never shout.
}

void WGrabView::onPingResult(bool reachable, bool slskdOnline) {
    m_reachable = reachable;
    setControlsEnabled(reachable);
    if (!reachable) {
        m_pQueuePoll->stop();
        setStatus(QStringLiteral(
                "grab service not reachable — the box may be off or the "
                "address is wrong."));
        return;
    }
    if (!slskdOnline) {
        setStatus(QStringLiteral(
                "connected, but Soulseek is offline on the box — searches "
                "won't find anything until it's back."));
    }
    if (!m_pQueuePoll->isActive()) {
        m_pQueuePoll->start();
    }
    m_pClient->refreshQueue();
}

void WGrabView::setControlsEnabled(bool enabled) {
    m_pQuery->setEnabled(enabled);
    m_pArtistMode->setEnabled(enabled);
    m_pTrackMode->setEnabled(enabled);
    m_pSearch->setEnabled(enabled && !m_searching);
    m_pDownload->setEnabled(enabled);
}

void WGrabView::setStatus(const QString& text) {
    m_pStatus->setText(text);
}

void WGrabView::setHint(const QString& text) {
    m_pHint->setText(text);
    m_pHint->setVisible(!text.isEmpty());
}

} // namespace crate
