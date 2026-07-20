#include "crate/triage/triagetablemodel.h"

#include "crate/system/systemcrates.h"
#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_triagetablemodel.cpp"
#include "util/db/fwdsqlquery.h"

namespace {
const QString kViewName = QStringLiteral("crate_triage_view");
} // namespace

namespace crate {

TriageTableModel::TriageTableModel(QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager,
        QDateTime since)
        : TrackSetTableModel(pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.cratetriage"),
          m_since(std::move(since)) {
    refresh();
}

void TriageTableModel::refresh() {
    const QList<TrackId> ids = SystemCrates(
            m_pTrackCollectionManager->internalCollection())
                                       .unreviewedTrackIds(m_since);
    QStringList orderedIds;
    orderedIds.reserve(ids.size());
    for (const TrackId& id : ids) {
        orderedIds.append(QString::number(id.toVariant().toInt()));
    }

    QStringList columns;
    columns << LIBRARYTABLE_ID
            << "'' AS " + LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;
    QStringList orderCases;
    for (int i = 0; i < ids.size(); ++i) {
        orderCases.append(QStringLiteral("WHEN %1 THEN %2")
                                  .arg(orderedIds.at(i))
                                  .arg(i));
    }
    columns << (orderCases.isEmpty()
                    ? QStringLiteral("-1 AS %1").arg(PLAYLISTTRACKSTABLE_POSITION)
                    : QStringLiteral("CASE %1 %2 ELSE -1 END AS %3")
                              .arg(LIBRARYTABLE_ID,
                                      orderCases.join(QChar(' ')),
                                      PLAYLISTTRACKSTABLE_POSITION));
    FwdSqlQuery(m_database, QStringLiteral("DROP VIEW IF EXISTS ") + kViewName)
            .execPrepared();
    const QString idFilter = orderedIds.isEmpty()
            ? QStringLiteral("-1")
            : orderedIds.join(QChar(','));
    FwdSqlQuery(m_database,
            QStringLiteral("CREATE TEMPORARY VIEW %1 AS SELECT %2 FROM %3 "
                           "WHERE %4 IN (%5) AND %6=0")
                    .arg(kViewName,
                            columns.join(QChar(',')),
                            QString::fromUtf8(LIBRARY_TABLE),
                            LIBRARYTABLE_ID,
                            idFilter,
                            LIBRARYTABLE_MIXXXDELETED))
            .execPrepared();

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = LIBRARYTABLE_PREVIEW;
    columns[2] = LIBRARYTABLE_COVERART;
    columns[3] = PLAYLISTTRACKSTABLE_POSITION;
    setTable(kViewName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());
    setSearch(QString());
    const int positionColumn =
            fieldIndex(ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION);
    setDefaultSort(positionColumn, Qt::AscendingOrder);
    m_tableOrderBy = QStringLiteral("ORDER BY ") +
            PLAYLISTTRACKSTABLE_POSITION + QStringLiteral(" ASC");
    select();
}

TrackModel::Capabilities TriageTableModel::getCapabilities() const {
    return Capability::AddToTrackSet | Capability::AddToAutoDJ |
            Capability::LoadToDeck | Capability::LoadToSampler |
            Capability::LoadToPreviewDeck | Capability::EditMetadata |
            Capability::Analyze | Capability::Properties | Capability::Sorting;
}

QString TriageTableModel::modelKey(bool noSearch) const {
    return QStringLiteral("crate-triage") +
            (noSearch ? QString() : QChar('#') + currentSearch());
}

} // namespace crate
