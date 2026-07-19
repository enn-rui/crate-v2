#pragma once

#include <QJsonObject>
#include <QString>

#include "library/trackset/tracksettablemodel.h"

// Live, read-only table model for a smart crate (wave-6). It never stores track
// membership: every time a spec is selected it re-resolves the rule against the
// library table via the SmartCrate translator and shows the matching tracks --
// the rekordbox "intelligent playlist" model. Because a SQLite VIEW definition
// cannot carry bind parameters, resolution runs the parameterized SELECT first
// to collect the matching (integer) track ids, then builds the display view with
// a plain `id IN (<ints>)` filter (the same shape CrateTableModel uses, and
// injection-safe because only integers are inlined).
class SmartCrateTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    SmartCrateTableModel(QObject* parent, TrackCollectionManager* pTrackCollectionManager);
    ~SmartCrateTableModel() final = default;

    // Re-resolve `spec` against the library table and show the matching tracks.
    void selectSmartCrate(const QString& name, const QJsonObject& spec);

    QString smartCrateName() const {
        return m_name;
    }

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    QString m_name;
};
