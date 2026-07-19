#pragma once

#include <QString>
#include <QVector>

namespace crate {

// One track node of the galaxy map, joined across the box-produced sidecars
// (umap.sqlite coords + clusters.sqlite + features.sqlite), keyed by relpath
// with forward slashes. The sidecar files are the data contract with the
// unchanged Python analysis pipeline — read-only here, never written.
struct GalaxyNode {
    QString relpath;
    QString title;
    QString artist;
    double artistX = 0.0;
    double artistY = 0.0;
    bool hasArtistPosition = false;
    double x = 0.0;
    double y = 0.0;
    double x3d = 0.0;
    double y3d = 0.0;
    double z = 0.0;
    bool has3d = false;
    int clusterId = -1; // -1 = noise per HDBSCAN convention
    double bpm = 0.0;
    QString keyCamelot;
    double energy = 0.0;
};

class CrateSidecars {
  public:
    // dir: the .crate sidecar directory (config [Crate],sidecar_dir).
    explicit CrateSidecars(const QString& dir);

    // Loads a joined in-memory snapshot. Returns false if umap.sqlite is
    // missing/unreadable (the galaxy then renders an empty-state hint).
    bool load();
    static void parseTrackName(
            const QString& relpath, QString* pTitle, QString* pArtist);

    const QVector<GalaxyNode>& nodes() const {
        return m_nodes;
    }
    QString lastError() const {
        return m_lastError;
    }

  private:
    QString m_dir;
    QString m_lastError;
    QVector<GalaxyNode> m_nodes;
};

} // namespace crate
