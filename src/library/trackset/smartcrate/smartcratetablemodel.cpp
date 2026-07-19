#include "library/trackset/smartcrate/smartcratetablemodel.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

#include "library/dao/trackschema.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/smartcrate/smartcratespec.h"
#include "moc_smartcratetablemodel.cpp"
#include "util/db/fwdsqlquery.h"

namespace {

const QString kModelName = QStringLiteral("smartcrate");
const QString kViewName = QStringLiteral("smartcrate_view");

} // anonymous namespace

SmartCrateTableModel::SmartCrateTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackSetTableModel(
                  pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.smartcrate") {
}

void SmartCrateTableModel::selectSmartCrate(const QString& name, const QJsonObject& spec) {
    m_name = name;

    // 1) Resolve the rule spec LIVE. The engine hands us a parameterized WHERE
    // fragment plus ordered bind args (values never appear in SQL text). We run
    // that parameterized SELECT to collect the matching integer track ids.
    const SmartCrate::WhereClause where = SmartCrate::translate(spec);
    QStringList idList;
    if (where.isValid()) {
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE (%3) AND %4=0")
                              .arg(LIBRARYTABLE_ID,
                                      QString::fromUtf8(LIBRARY_TABLE),
                                      where.sql,
                                      LIBRARYTABLE_MIXXXDELETED));
        for (const QVariant& arg : where.bindArgs) {
            query.addBindValue(arg);
        }
        if (query.exec()) {
            while (query.next()) {
                idList.append(QString::number(query.value(0).toInt()));
            }
        } else {
            qWarning() << "SmartCrateTableModel: resolve query failed:"
                       << query.lastError().text();
        }
    }

    // 2) Build the display view from the resolved ids. An empty match must still
    // yield a syntactically valid, zero-row view (no track has id -1).
    const QString idFilter = idList.isEmpty()
            ? QStringLiteral("-1")
            : idList.join(QChar(','));

    QStringList columns;
    columns << LIBRARYTABLE_ID
            << "'' AS " + LIBRARYTABLE_PREVIEW
            // For sorting the cover art column we give LIBRARYTABLE_COVERART the
            // same value as the cover digest.
            << LIBRARYTABLE_COVERART_DIGEST + " AS " + LIBRARYTABLE_COVERART;

    // The view is re-resolved on every activation, so drop and recreate it.
    FwdSqlQuery(m_database, QStringLiteral("DROP VIEW IF EXISTS ") + kViewName)
            .execPrepared();
    const QString viewQuery =
            QStringLiteral("CREATE TEMPORARY VIEW %1 AS SELECT %2 FROM %3 "
                           "WHERE %4 IN (%5) AND %6=0")
                    .arg(kViewName,
                            columns.join(QChar(',')),
                            QString::fromUtf8(LIBRARY_TABLE),
                            LIBRARYTABLE_ID,
                            idFilter,
                            LIBRARYTABLE_MIXXXDELETED);
    FwdSqlQuery(m_database, viewQuery).execPrepared();

    columns[0] = LIBRARYTABLE_ID;
    columns[1] = LIBRARYTABLE_PREVIEW;
    columns[2] = LIBRARYTABLE_COVERART;
    setTable(kViewName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    setSearch(QString());
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), Qt::AscendingOrder);
}

TrackModel::Capabilities SmartCrateTableModel::getCapabilities() const {
    // A smart crate is a rule-based, read-only view of the library. Tracks can be
    // loaded / dragged out / analyzed, but membership is never edited directly
    // (it is derived from the spec), so no ReceiveDrops / Remove* capabilities.
    return Capability::AddToTrackSet |
            Capability::AddToAutoDJ |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            Capability::EditMetadata |
            Capability::Analyze |
            Capability::Properties |
            Capability::Sorting;
}

QString SmartCrateTableModel::modelKey(bool noSearch) const {
    if (noSearch) {
        return kModelName + QChar(':') + m_name;
    }
    return kModelName + QChar(':') + m_name + QChar('#') + currentSearch();
}
