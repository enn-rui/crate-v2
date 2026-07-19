#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Persistence for smart-crate rule specs.
//
// STORAGE CHOICE (this slice): specs live in a plain JSON file (smart_crates.json)
// in the settings dir, next to mixxxdb.sqlite -- NOT a mixxxdb schema migration.
// Mixxx's schema.xml versioning is deliberately out of scope here; a future slice
// can migrate these into the DB. The on-disk shape mirrors v1's spec JSON
// (apps/crate/library.py save_smart_crate) so a v1 importer is trivial: each
// entry carries {name, created, spec} where spec is the exact
// {"match","conditions"} object v1 writes.
namespace SmartCrate {

struct Def {
    QString name;
    qint64 created = 0; // epoch seconds, like v1's time.time()
    QJsonObject spec;   // {"match":"all"|"any","conditions":[...]}
};

class Storage {
  public:
    explicit Storage(QString filePath);

    const QString& filePath() const {
        return m_filePath;
    }

    // Read every def. A missing file yields an empty list; a malformed file also
    // yields an empty list (never throws). Unknown top-level / per-entry keys are
    // ignored, and each `spec` object is preserved verbatim, so unknown future
    // keys inside a spec survive a load/save round-trip.
    QList<Def> loadAll() const;

    // Atomically overwrite the file with `defs`. Returns false on IO error.
    bool saveAll(const QList<Def>& defs) const;

    // Insert-or-replace by exact name (mirrors v1's ON CONFLICT(name)); keeps the
    // original position when replacing, appends when new. Stamps `created` for
    // new entries. Returns false on IO error.
    bool upsert(const QString& name, const QJsonObject& spec);

    // Remove the entry with this exact name. Returns false on IO error.
    bool remove(const QString& name);

  private:
    QString m_filePath;
};

} // namespace SmartCrate
