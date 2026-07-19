#pragma once

#include <QAbstractTableModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

// Data model for the config-gated GRAB sidebar feature (wave-6, slice G2).
//
// These types mirror the documented box-service contract (slice G1). Everything
// here is pure/UI-free so it can be unit-tested directly without a network or a
// running service. The JSON helpers and the two table models are the seam the
// GrabClient and WGrabView build on.

namespace crate {

enum class GrabQueueState {
    Queued,
    Downloading,
    Landed,
    Failed,
    Unknown,
};

// One candidate file returned by GET /api/search/<id> while state == "done".
struct GrabResult {
    QString key;
    QString display;
    QString ext;
    qint64 size = 0;
    QString user;
    bool freeSlot = false;
    int queueLen = 0;
    int sourceCount = 0;
    QString path;
    QString artistDir;
};

// One row of GET /api/queue.
struct GrabQueueItem {
    QString key;
    QString display;
    GrabQueueState state = GrabQueueState::Unknown;
    QString detail;
    QString dest;
};

// ---- Pure helpers (unit-tested directly) ------------------------------------

// Human-readable byte size, e.g. 0 -> "0 B", 1536 -> "1.5 KB", 5*1024^2 -> "5.0 MB".
QString grabHumanSize(qint64 bytes);

// Contract state string -> enum (case-insensitive). Unknown values map to Unknown.
GrabQueueState grabParseQueueState(const QString& raw);

// enum -> the plain lowercase label shown to the user.
QString grabQueueStateLabel(GrabQueueState state);

GrabResult grabResultFromJson(const QJsonObject& obj);
GrabQueueItem grabQueueItemFromJson(const QJsonObject& obj);
QVector<GrabResult> grabParseResults(const QJsonArray& arr);
QVector<GrabQueueItem> grabParseQueue(const QJsonArray& arr);

// ---- Table models -----------------------------------------------------------

class GrabResultsModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Column {
        ColumnName = 0,
        ColumnExt,
        ColumnSize,
        ColumnFreeSlot,
        ColumnQueue,
        ColumnSources,
        ColumnCount,
    };

    explicit GrabResultsModel(QObject* pParent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setResults(const QVector<GrabResult>& results);
    void clearResults();

    QString keyAt(int row) const;
    const GrabResult* resultAt(int row) const;
    bool isEmpty() const {
        return m_results.isEmpty();
    }

  private:
    QVector<GrabResult> m_results;
};

class GrabQueueModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Column {
        ColumnName = 0,
        ColumnState,
        ColumnCount,
    };

    explicit GrabQueueModel(QObject* pParent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setQueue(const QVector<GrabQueueItem>& items);

    const GrabQueueItem* itemAt(int row) const;

  private:
    QVector<GrabQueueItem> m_items;
};

} // namespace crate
