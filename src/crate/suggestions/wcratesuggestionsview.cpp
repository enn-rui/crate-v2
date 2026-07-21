#include "crate/suggestions/wcratesuggestionsview.h"

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
#include "library/trackset/crate/cratetablemodel.h"
#include "widget/wlibrary.h"
#include "widget/wtracktableview.h"
#include "moc_wcratesuggestionsview.cpp"

namespace crate {
namespace {
QString matchRelpath(const QString& location, const QSet<QString>& known) {
    const QString normalized = QDir::fromNativeSeparators(location);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString suffix = parts.mid(i).join(QLatin1Char('/'));
        if (known.contains(suffix.toCaseFolded())) return suffix;
    }
    return {};
}
} // namespace

WCrateSuggestionsView::WCrateSuggestionsView(WLibrary* pParent,
        UserSettingsPointer pConfig, Library* pLibrary, KeyboardEventFilter* pKeyboard)
        : QWidget(pParent),
          m_pConfig(std::move(pConfig)),
          m_pLibrary(pLibrary),
          m_pTrackTable(new WTrackTableView(this, m_pConfig, pLibrary,
                  pParent->getTrackTableBackgroundColorOpacity())),
          m_pSection(new QWidget(this)),
          m_pHeader(new QLabel(m_pSection)),
          m_pRows(new QWidget(m_pSection)),
          m_pRowsLayout(new QGridLayout(m_pRows)) {
    m_pTrackTable->installEventFilter(pKeyboard);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    root->addWidget(m_pTrackTable, 1); root->addWidget(m_pSection);
    auto* section = new QVBoxLayout(m_pSection);
    section->setContentsMargins(8, 5, 8, 6); section->setSpacing(3);
    auto* header = new QHBoxLayout;
    header->addWidget(m_pHeader); header->addStretch();
    for (SuggestMode mode : {SuggestMode::Sound, SuggestMode::Mix, SuggestMode::Gap}) {
        auto* button = new QToolButton(m_pSection);
        button->setText(modeName(mode)); button->setCheckable(true);
        connect(button, &QToolButton::clicked, this, [this, mode] { setMode(mode); });
        header->addWidget(button); m_modeButtons.append(button);
    }
    section->addLayout(header); section->addWidget(m_pRows);
    m_pRowsLayout->setContentsMargins(0, 0, 0, 0);
    m_pRowsLayout->setHorizontalSpacing(10); m_pRowsLayout->setVerticalSpacing(1);
    const QString configured = m_pConfig->getValue(
            ConfigKey("[Crate]", "suggest_mode"), QStringLiteral("SOUND"));
    m_mode = configured.compare(QStringLiteral("MIX"), Qt::CaseInsensitive) == 0
            ? SuggestMode::Mix : configured.compare(QStringLiteral("GAP"), Qt::CaseInsensitive) == 0
            ? SuggestMode::Gap : SuggestMode::Sound;
    connect(&m_watcher, &QFutureWatcher<Result>::finished, this, [this] {
        render(m_watcher.result());
    });
    m_pSection->hide();
}

QString WCrateSuggestionsView::modeName(SuggestMode mode) {
    return mode == SuggestMode::Mix ? QStringLiteral("MIX")
            : mode == SuggestMode::Gap ? QStringLiteral("GAP") : QStringLiteral("SOUND");
}

void WCrateSuggestionsView::loadTrackModel(QAbstractItemModel* pModel, bool restoreState) {
    m_pModel = pModel;
    m_pTrackTable->loadTrackModel(pModel, restoreState);
    m_pSection->setVisible(qobject_cast<CrateTableModel*>(pModel) != nullptr);
    scheduleCompute();
}

void WCrateSuggestionsView::setMode(SuggestMode mode) {
    m_mode = mode;
    m_pConfig->setValue(ConfigKey("[Crate]", "suggest_mode"), modeName(mode));
    scheduleCompute();
}

void WCrateSuggestionsView::scheduleCompute() {
    auto* crateModel = qobject_cast<CrateTableModel*>(m_pModel.data());
    if (!crateModel || !crateModel->selectedCrate().isValid()) return;
    for (int i = 0; i < m_modeButtons.size(); ++i)
        m_modeButtons[i]->setChecked(i == static_cast<int>(m_mode));
    clearRows(); m_pHeader->setText(tr("SUGGESTED - %1 - computing").arg(modeName(m_mode)));

    auto* collection = m_pLibrary->trackCollectionManager()->internalCollection();
    QSet<TrackId> memberIds;
    auto members = collection->crates().selectCrateTracksSorted(crateModel->selectedCrate());
    while (members.next()) memberIds.insert(members.trackId());
    if (memberIds.isEmpty()) { m_pHeader->setText(tr("SUGGESTED - crate is empty")); return; }
    const QSet<TrackId> demoted = SystemCrates(collection).demotedTrackIds();
    SonicVectors probe;
    const QString vectorsPath = QDir(m_pConfig->getValue(
            ConfigKey("[Crate]", "sidecar_dir"), QString())).filePath(
            QStringLiteral("music_vectors.sqlite"));
    // Metadata is snapshotted on the owning DB thread; vector I/O and scoring
    // happen entirely in the worker below.
    QSqlQuery query(collection->database());
    query.exec(QStringLiteral("SELECT library.id, track_locations.location, library.title, "
                              "library.artist, library.key, library.bpm FROM library INNER JOIN "
                              "track_locations ON library.location=track_locations.id "
                              "WHERE library.mixxx_deleted=0"));
    struct Raw { TrackId id; QString location, title, artist, key; double bpm; };
    QVector<Raw> raw;
    while (query.next()) raw.append({TrackId(query.value(0)), query.value(1).toString(),
            query.value(2).toString(), query.value(3).toString(), query.value(4).toString(),
            query.value(5).toDouble()});
    const SuggestMode mode = m_mode;
    m_watcher.setFuture(QtConcurrent::run([this, raw, memberIds, demoted, vectorsPath, mode] {
        Result output; SonicVectors vectors;
        if (!vectors.load(vectorsPath)) { output.error = tr("no vectors yet"); return output; }
        QSet<QString> known;
        for (const QString& path : vectors.relpaths()) known.insert(path.toCaseFolded());
        QVector<SuggestTrack> memberTracks, universe;
        for (const auto& item : raw) {
            const QString path = matchRelpath(item.location, known);
            const auto* vector = vectors.centered(path);
            if (path.isEmpty() || !vector) continue;
            SuggestTrack track{path, *vector, item.key, item.bpm, demoted.contains(item.id)};
            universe.append(track);
            output.meta.insert(path, {item.id, item.title, item.artist, item.key, item.bpm});
            if (memberIds.contains(item.id)) memberTracks.append(track);
        }
        output.suggestions = Suggestions::rank(memberTracks, universe, mode);
        return output;
    }));
}

void WCrateSuggestionsView::clearRows() {
    while (QLayoutItem* item = m_pRowsLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void WCrateSuggestionsView::render(const Result& result) {
    clearRows();
    m_meta = result.meta;
    if (!result.error.isEmpty()) { m_pHeader->setText(tr("SUGGESTED - %1").arg(result.error)); return; }
    m_pHeader->setText(tr("SUGGESTED - %1").arg(modeName(m_mode)));
    if (result.suggestions.isEmpty()) { m_pRowsLayout->addWidget(new QLabel(tr("Nothing left to suggest.")), 0, 0); return; }
    int row = 0;
    for (const auto& suggestion : result.suggestions) {
        const Meta meta = m_meta.value(suggestion.relpath);
        m_pRowsLayout->addWidget(new QLabel(meta.title), row, 0);
        m_pRowsLayout->addWidget(new QLabel(meta.artist), row, 1);
        m_pRowsLayout->addWidget(new QLabel(meta.key), row, 2);
        m_pRowsLayout->addWidget(new QLabel(meta.bpm > 0 ? QString::number(meta.bpm, 'f', 1) : QString()), row, 3);
        auto* add = new QToolButton; add->setText(tr("ADD"));
        connect(add, &QToolButton::clicked, this, [this, meta] {
            auto* model = qobject_cast<CrateTableModel*>(m_pModel.data());
            if (model && m_pLibrary->trackCollectionManager()->internalCollection()
                    ->addCrateTracks(model->selectedCrate(), {meta.id})) scheduleCompute();
        });
        m_pRowsLayout->addWidget(add, row++, 4);
    }
}

void WCrateSuggestionsView::onShow() { m_pTrackTable->onShow(); }
bool WCrateSuggestionsView::hasFocus() const { return m_pTrackTable->hasFocus(); }
void WCrateSuggestionsView::setFocus() { m_pTrackTable->setFocus(); }
void WCrateSuggestionsView::onSearch(const QString& text) { m_pTrackTable->onSearch(text); }
void WCrateSuggestionsView::pasteFromSidebar() { m_pTrackTable->pasteFromSidebar(); }
void WCrateSuggestionsView::saveCurrentViewState() { m_pTrackTable->saveCurrentViewState(); }
bool WCrateSuggestionsView::restoreCurrentViewState() { return m_pTrackTable->restoreCurrentViewState(); }
TrackModel::SortColumnId WCrateSuggestionsView::getColumnIdFromCurrentIndex() { return m_pTrackTable->getColumnIdFromCurrentIndex(); }
void WCrateSuggestionsView::assignPreviousTrackColor() { m_pTrackTable->assignPreviousTrackColor(); }
void WCrateSuggestionsView::assignNextTrackColor() { m_pTrackTable->assignNextTrackColor(); }

} // namespace crate
