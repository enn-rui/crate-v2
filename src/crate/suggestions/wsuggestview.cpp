#include "crate/suggestions/wsuggestview.h"

#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSqlQuery>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "crate/system/systemcrates.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/crate.h"
#include "library/trackset/crate/cratetablemodel.h"
#include "widget/wlibrary.h"
#include "moc_wsuggestview.cpp"

namespace crate {
namespace {

// Root-independent match of a library location to a known sidecar relpath, so
// the same share spelled different ways (drive letter, UNC, case) still lines
// up with the vectors table. Mirrors the reverted widget's matcher.
QString matchRelpath(const QString& location, const QSet<QString>& known) {
    const QString normalized = QDir::fromNativeSeparators(location);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString suffix = parts.mid(i).join(QLatin1Char('/'));
        if (known.contains(suffix.toCaseFolded())) {
            return suffix;
        }
    }
    return {};
}

} // namespace

WSuggestView::WSuggestView(WLibrary* pParent,
        UserSettingsPointer pConfig,
        Library* pLibrary,
        TrackCollectionManager* pTrackCollectionManager,
        KeyboardEventFilter* pKeyboard)
        : QWidget(pParent),
          m_pConfig(std::move(pConfig)),
          m_pLibrary(pLibrary),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_pTitle(new QLabel(this)),
          m_pEmpty(new QLabel(tr("Visit a crate to see suggestions."), this)),
          m_pRows(new QWidget(this)),
          m_pRowsLayout(new QGridLayout(m_pRows)),
          m_pRefresh(new QToolButton(this)) {
    setObjectName(QStringLiteral("CrateSuggestView"));
    m_pTitle->setObjectName(QStringLiteral("SuggestCrateName"));
    m_pEmpty->setObjectName(QStringLiteral("SuggestEmptyState"));
    m_pRefresh->setObjectName(QStringLiteral("SuggestRefresh"));
    m_pRefresh->setText(tr("REFRESH"));
    m_pTitle->setText(tr("SUGGEST"));

    if (pKeyboard) {
        installEventFilter(pKeyboard);
    }

    // Persisted mode (shared with the galaxy suggestion rings).
    const QString configured = m_pConfig->getValue(
            ConfigKey("[Crate]", "suggest_mode"), QStringLiteral("SOUND"));
    m_mode = configured.compare(QStringLiteral("MIX"), Qt::CaseInsensitive) == 0
            ? SuggestMode::Mix
            : configured.compare(QStringLiteral("GAP"), Qt::CaseInsensitive) == 0
            ? SuggestMode::Gap
            : SuggestMode::Sound;

    auto* pRoot = new QVBoxLayout(this);
    pRoot->setContentsMargins(8, 6, 8, 6);
    pRoot->setSpacing(4);

    auto* pHeader = new QHBoxLayout();
    pHeader->addWidget(m_pTitle);
    pHeader->addStretch();
    static const SuggestMode kModes[] = {
            SuggestMode::Sound, SuggestMode::Mix, SuggestMode::Gap};
    for (SuggestMode mode : kModes) {
        auto* pButton = new QToolButton(this);
        pButton->setText(modeName(mode));
        pButton->setCheckable(true);
        pButton->setObjectName(
                QStringLiteral("SuggestMode%1").arg(modeName(mode)));
        connect(pButton, &QToolButton::clicked, this, [this, mode] {
            setMode(mode);
        });
        pHeader->addWidget(pButton);
        m_modeButtons.append(pButton);
    }
    pHeader->addWidget(m_pRefresh);
    connect(m_pRefresh, &QToolButton::clicked, this, [this] {
        scheduleCompute();
    });
    pRoot->addLayout(pHeader);
    pRoot->addWidget(m_pEmpty);
    pRoot->addWidget(m_pRows, 1);

    m_pRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_pRowsLayout->setHorizontalSpacing(10);
    m_pRowsLayout->setVerticalSpacing(1);
    syncModeButtons();

    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        applyResult(m_watcher.result());
    });

    // Track the most recently activated crate. Connecting our own slot to the
    // Library signal is safe: it never touches the stock Tracks view wiring.
    if (m_pLibrary) {
        connect(m_pLibrary,
                &Library::showTrackModel,
                this,
                &WSuggestView::loadTrackModel);
    }
}

WSuggestView::~WSuggestView() = default;

QString WSuggestView::modeName(SuggestMode mode) {
    return mode == SuggestMode::Mix ? QStringLiteral("MIX")
            : mode == SuggestMode::Gap ? QStringLiteral("GAP")
                                       : QStringLiteral("SOUND");
}

void WSuggestView::syncModeButtons() {
    for (int i = 0; i < m_modeButtons.size(); ++i) {
        m_modeButtons[i]->setChecked(i == static_cast<int>(m_mode));
    }
}

void WSuggestView::loadTrackModel(QAbstractItemModel* pModel) {
    auto* pCrateModel = qobject_cast<CrateTableModel*>(pModel);
    if (!pCrateModel || !pCrateModel->selectedCrate().isValid()) {
        // Not a crate (e.g. the stock library table): keep the last crate.
        return;
    }
    m_targetCrate = pCrateModel->selectedCrate();
    m_targetCrateName.clear();
    if (m_pTrackCollectionManager) {
        Crate crate;
        if (m_pTrackCollectionManager->internalCollection()->crates().readCrateById(
                    m_targetCrate, &crate)) {
            m_targetCrateName = crate.getName();
        }
    }
    m_pTitle->setText(m_targetCrateName.isEmpty()
                    ? tr("SUGGEST")
                    : tr("SUGGEST - %1").arg(m_targetCrateName));
    scheduleCompute();
}

void WSuggestView::setMode(SuggestMode mode) {
    m_mode = mode;
    m_pConfig->setValue(ConfigKey("[Crate]", "suggest_mode"), modeName(mode));
    syncModeButtons();
    scheduleCompute();
}

void WSuggestView::scheduleCompute() {
    syncModeButtons();
    // The async worker is a production concern: it needs the live Library graph
    // to be wired. Headless views (tests) leave m_pLibrary null and render
    // injected results through applyResult instead, keeping tests deterministic.
    if (!m_pLibrary || !m_pTrackCollectionManager) {
        return;
    }
    if (!m_targetCrate.isValid()) {
        Result result;
        result.state = State::NoCrate;
        applyResult(result);
        return;
    }

    auto* pCollection = m_pTrackCollectionManager->internalCollection();
    QSet<TrackId> memberIds;
    auto members = pCollection->crates().selectCrateTracksSorted(m_targetCrate);
    while (members.next()) {
        memberIds.insert(members.trackId());
    }
    if (memberIds.isEmpty()) {
        Result result;
        result.state = State::CrateEmpty;
        applyResult(result);
        return;
    }

    m_pEmpty->setVisible(false);
    m_pTitle->setText(tr("SUGGEST - %1 - computing").arg(m_targetCrateName));

    const QSet<TrackId> demoted = SystemCrates(pCollection).demotedTrackIds();
    const QString vectorsPath = QDir(m_pConfig->getValue(
                                             ConfigKey("[Crate]", "sidecar_dir"),
                                             QString()))
                                        .filePath(QStringLiteral("music_vectors.sqlite"));

    // Metadata is snapshotted here on the owning DB thread; vector I/O and
    // scoring happen entirely in the worker below.
    struct Raw {
        TrackId id;
        QString location;
        QString title;
        QString artist;
        QString key;
        double bpm;
    };
    QVector<Raw> raw;
    QSqlQuery query(pCollection->database());
    query.exec(QStringLiteral(
            "SELECT library.id, track_locations.location, library.title, "
            "library.artist, library.key, library.bpm FROM library INNER JOIN "
            "track_locations ON library.location=track_locations.id "
            "WHERE library.mixxx_deleted=0"));
    while (query.next()) {
        raw.append({TrackId(query.value(0)),
                query.value(1).toString(),
                query.value(2).toString(),
                query.value(3).toString(),
                query.value(4).toString(),
                query.value(5).toDouble()});
    }

    const SuggestMode mode = m_mode;
    m_watcher.setFuture(QtConcurrent::run(
            [raw, memberIds, demoted, vectorsPath, mode]() -> Result {
                Result output;
                SonicVectors vectors;
                if (!vectors.load(vectorsPath)) {
                    output.state = State::NoVectors;
                    return output;
                }
                output.state = State::Ok;
                QSet<QString> known;
                for (const QString& path : vectors.relpaths()) {
                    known.insert(path.toCaseFolded());
                }
                QVector<SuggestTrack> memberTracks;
                QVector<SuggestTrack> universe;
                for (const auto& item : raw) {
                    const QString path = matchRelpath(item.location, known);
                    const auto* vector = vectors.centered(path);
                    if (path.isEmpty() || !vector) {
                        continue;
                    }
                    SuggestTrack track{path,
                            *vector,
                            item.key,
                            item.bpm,
                            demoted.contains(item.id)};
                    universe.append(track);
                    output.meta.insert(path,
                            {item.id, item.title, item.artist, item.key, item.bpm});
                    if (memberIds.contains(item.id)) {
                        memberTracks.append(track);
                    }
                }
                output.suggestions =
                        Suggestions::rank(memberTracks, universe, mode);
                return output;
            }));
}

void WSuggestView::clearRows() {
    while (QLayoutItem* pItem = m_pRowsLayout->takeAt(0)) {
        if (pItem->widget()) {
            pItem->widget()->deleteLater();
        }
        delete pItem;
    }
}

void WSuggestView::applyResult(const Result& result) {
    m_lastResult = result;
    clearRows();

    QString emptyText;
    switch (result.state) {
    case State::NoCrate:
        emptyText = tr("Visit a crate to see suggestions.");
        break;
    case State::CrateEmpty:
        emptyText = tr("This crate is empty - add a few tracks first.");
        break;
    case State::NoVectors:
        emptyText = tr("No sonic vectors yet - analyze your library.");
        break;
    case State::Ok:
        if (result.suggestions.isEmpty()) {
            emptyText = tr("Nothing left to suggest for this crate.");
        }
        break;
    }

    if (!m_targetCrateName.isEmpty()) {
        m_pTitle->setText(tr("SUGGEST - %1").arg(m_targetCrateName));
    }

    if (!emptyText.isEmpty()) {
        m_pEmpty->setText(emptyText);
        m_pEmpty->setVisible(true);
        m_pRows->setVisible(false);
        return;
    }

    m_pEmpty->setVisible(false);
    m_pRows->setVisible(true);
    int row = 0;
    for (const Suggestion& suggestion : result.suggestions) {
        const Meta meta = result.meta.value(suggestion.relpath);
        m_pRowsLayout->addWidget(new QLabel(meta.title, m_pRows), row, 0);
        m_pRowsLayout->addWidget(new QLabel(meta.artist, m_pRows), row, 1);
        m_pRowsLayout->addWidget(new QLabel(meta.key, m_pRows), row, 2);
        m_pRowsLayout->addWidget(
                new QLabel(meta.bpm > 0 ? QString::number(meta.bpm, 'f', 1)
                                        : QString(),
                        m_pRows),
                row,
                3);
        auto* pAdd = new QToolButton(m_pRows);
        pAdd->setObjectName(QStringLiteral("SuggestAdd"));
        pAdd->setText(tr("ADD"));
        const QString relpath = suggestion.relpath;
        const TrackId id = meta.id;
        connect(pAdd, &QToolButton::clicked, this, [this, relpath, id] {
            if (!m_targetCrate.isValid() || !id.isValid() ||
                    !m_pTrackCollectionManager) {
                return;
            }
            if (!m_pTrackCollectionManager->internalCollection()->addCrateTracks(
                        m_targetCrate, {id})) {
                return;
            }
            // Drop the added row immediately, then let a fresh compute refresh
            // the rest (a no-op for headless views without a live Library).
            for (int i = 0; i < m_lastResult.suggestions.size(); ++i) {
                if (m_lastResult.suggestions[i].relpath == relpath) {
                    m_lastResult.suggestions.removeAt(i);
                    break;
                }
            }
            applyResult(m_lastResult);
            scheduleCompute();
        });
        m_pRowsLayout->addWidget(pAdd, row, 4);
        ++row;
    }
}

void WSuggestView::onShow() {
    scheduleCompute();
}

bool WSuggestView::hasFocus() const {
    return QWidget::hasFocus();
}

void WSuggestView::setFocus() {
    QWidget::setFocus();
}

} // namespace crate
