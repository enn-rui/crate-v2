#pragma once

#include <QDateTime>

#include "library/trackset/tracksettablemodel.h"

namespace crate {

class TriageTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    TriageTableModel(QObject* pParent,
            TrackCollectionManager* pTrackCollectionManager,
            QDateTime since);

    void refresh();
    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    const QDateTime m_since;
};

} // namespace crate
