#include "crate/grab/grabmodels.h"

#include <QLocale>

#include "moc_grabmodels.cpp"

namespace crate {

QString grabHumanSize(qint64 bytes) {
    if (bytes < 0) {
        return QStringLiteral("?");
    }
    if (bytes < 1024) {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    static const char* const kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', 1) + QLatin1Char(' ') +
            QLatin1String(kUnits[unit]);
}

GrabQueueState grabParseQueueState(const QString& raw) {
    const QString value = raw.trimmed().toLower();
    if (value == QLatin1String("queued")) {
        return GrabQueueState::Queued;
    }
    if (value == QLatin1String("downloading")) {
        return GrabQueueState::Downloading;
    }
    if (value == QLatin1String("landed")) {
        return GrabQueueState::Landed;
    }
    if (value == QLatin1String("failed")) {
        return GrabQueueState::Failed;
    }
    return GrabQueueState::Unknown;
}

QString grabQueueStateLabel(GrabQueueState state) {
    switch (state) {
    case GrabQueueState::Queued:
        return QStringLiteral("queued");
    case GrabQueueState::Downloading:
        return QStringLiteral("downloading");
    case GrabQueueState::Landed:
        return QStringLiteral("landed");
    case GrabQueueState::Failed:
        return QStringLiteral("failed");
    case GrabQueueState::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

GrabResult grabResultFromJson(const QJsonObject& obj) {
    GrabResult result;
    result.key = obj.value(QStringLiteral("key")).toString();
    result.display = obj.value(QStringLiteral("display")).toString();
    result.ext = obj.value(QStringLiteral("ext")).toString();
    // size may arrive as a JSON number (possibly > 2^31) or as a string.
    const QJsonValue sizeValue = obj.value(QStringLiteral("size"));
    if (sizeValue.isString()) {
        result.size = sizeValue.toString().toLongLong();
    } else {
        result.size = static_cast<qint64>(sizeValue.toDouble());
    }
    result.user = obj.value(QStringLiteral("user")).toString();
    result.freeSlot = obj.value(QStringLiteral("free_slot")).toBool();
    result.queueLen = obj.value(QStringLiteral("queue_len")).toInt();
    result.sourceCount = obj.value(QStringLiteral("source_count")).toInt();
    result.path = obj.value(QStringLiteral("path")).toString();
    result.artistDir = obj.value(QStringLiteral("artist_dir")).toString();
    return result;
}

GrabQueueItem grabQueueItemFromJson(const QJsonObject& obj) {
    GrabQueueItem item;
    item.key = obj.value(QStringLiteral("key")).toString();
    item.display = obj.value(QStringLiteral("display")).toString();
    item.state = grabParseQueueState(obj.value(QStringLiteral("state")).toString());
    item.detail = obj.value(QStringLiteral("detail")).toString();
    item.dest = obj.value(QStringLiteral("dest")).toString();
    return item;
}

QVector<GrabResult> grabParseResults(const QJsonArray& arr) {
    QVector<GrabResult> results;
    results.reserve(arr.size());
    for (const QJsonValue& value : arr) {
        if (value.isObject()) {
            results.append(grabResultFromJson(value.toObject()));
        }
    }
    return results;
}

QVector<GrabQueueItem> grabParseQueue(const QJsonArray& arr) {
    QVector<GrabQueueItem> items;
    items.reserve(arr.size());
    for (const QJsonValue& value : arr) {
        if (value.isObject()) {
            items.append(grabQueueItemFromJson(value.toObject()));
        }
    }
    return items;
}

// ---- GrabResultsModel -------------------------------------------------------

GrabResultsModel::GrabResultsModel(QObject* pParent)
        : QAbstractTableModel(pParent) {
}

int GrabResultsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_results.size();
}

int GrabResultsModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant GrabResultsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return QVariant();
    }
    const GrabResult& result = m_results.at(index.row());
    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColumnSize:
        case ColumnQueue:
        case ColumnSources:
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        case ColumnFreeSlot:
            return static_cast<int>(Qt::AlignCenter);
        default:
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    if (role == Qt::ToolTipRole) {
        if (!result.user.isEmpty()) {
            return QStringLiteral("%1\nfrom user %2").arg(result.display, result.user);
        }
        return result.display;
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    switch (index.column()) {
    case ColumnName:
        return result.display;
    case ColumnExt:
        return result.ext;
    case ColumnSize:
        return grabHumanSize(result.size);
    case ColumnFreeSlot:
        return result.freeSlot ? QStringLiteral("yes") : QStringLiteral("no");
    case ColumnQueue:
        return result.queueLen;
    case ColumnSources:
        return result.sourceCount;
    default:
        return QVariant();
    }
}

QVariant GrabResultsModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QVariant();
    }
    switch (section) {
    case ColumnName:
        return QStringLiteral("track");
    case ColumnExt:
        return QStringLiteral("type");
    case ColumnSize:
        return QStringLiteral("size");
    case ColumnFreeSlot:
        return QStringLiteral("free slot");
    case ColumnQueue:
        return QStringLiteral("queue");
    case ColumnSources:
        return QStringLiteral("sources");
    default:
        return QVariant();
    }
}

void GrabResultsModel::setResults(const QVector<GrabResult>& results) {
    beginResetModel();
    m_results = results;
    endResetModel();
}

void GrabResultsModel::clearResults() {
    beginResetModel();
    m_results.clear();
    endResetModel();
}

QString GrabResultsModel::keyAt(int row) const {
    if (row < 0 || row >= m_results.size()) {
        return QString();
    }
    return m_results.at(row).key;
}

const GrabResult* GrabResultsModel::resultAt(int row) const {
    if (row < 0 || row >= m_results.size()) {
        return nullptr;
    }
    return &m_results.at(row);
}

// ---- GrabQueueModel ---------------------------------------------------------

GrabQueueModel::GrabQueueModel(QObject* pParent)
        : QAbstractTableModel(pParent) {
}

int GrabQueueModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_items.size();
}

int GrabQueueModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant GrabQueueModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return QVariant();
    }
    const GrabQueueItem& item = m_items.at(index.row());
    if (role == Qt::ToolTipRole) {
        // "landed" is the moment the file exists on disk; the honest note is
        // that the library scan folds it in a little later.
        if (item.state == GrabQueueState::Landed) {
            QString note = QStringLiteral(
                    "landed on disk — it shows up in your library within about "
                    "15 minutes");
            if (!item.dest.isEmpty()) {
                note.append(QLatin1Char('\n'));
                note.append(item.dest);
            }
            return note;
        }
        if (!item.detail.isEmpty()) {
            return item.detail;
        }
        return item.display;
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return QVariant();
    }
    switch (index.column()) {
    case ColumnName:
        return item.display;
    case ColumnState: {
        const QString label = grabQueueStateLabel(item.state);
        if (item.state == GrabQueueState::Failed && !item.detail.isEmpty()) {
            return QString(label + QStringLiteral(" — ") + item.detail);
        }
        return label;
    }
    default:
        return QVariant();
    }
}

QVariant GrabQueueModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QVariant();
    }
    switch (section) {
    case ColumnName:
        return QStringLiteral("download");
    case ColumnState:
        return QStringLiteral("state");
    default:
        return QVariant();
    }
}

void GrabQueueModel::setQueue(const QVector<GrabQueueItem>& items) {
    beginResetModel();
    m_items = items;
    endResetModel();
}

const GrabQueueItem* GrabQueueModel::itemAt(int row) const {
    if (row < 0 || row >= m_items.size()) {
        return nullptr;
    }
    return &m_items.at(row);
}

} // namespace crate
