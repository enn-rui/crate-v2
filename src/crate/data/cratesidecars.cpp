#include "crate/data/cratesidecars.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QUuid>

namespace {

// Opens a read-only SQLite connection with a unique name; returns an invalid
// database if the file does not exist.
QSqlDatabase openRo(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return QSqlDatabase();
    }
    const QString connName =
            QStringLiteral("CRATE_SIDECAR_") + QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        QSqlDatabase::removeDatabase(connName);
        return QSqlDatabase();
    }
    return db;
}

void closeAndRemove(QSqlDatabase* db) {
    const QString name = db->connectionName();
    db->close();
    *db = QSqlDatabase(); // drop the handle before removeDatabase
    QSqlDatabase::removeDatabase(name);
}

} // namespace

namespace crate {

CrateSidecars::CrateSidecars(const QString& dir)
        : m_dir(dir) {
}

void CrateSidecars::parseTrackName(
        const QString& relpath, QString* pTitle, QString* pArtist) {
    const QString stem = QFileInfo(relpath).completeBaseName().trimmed();
    QString cleaned = stem;
    // Common rip/export prefixes: track ("07 ", "07.", "07-"),
    // spaced track separator ("07 - "), and disc-track ("1-06 ").
    const QString stripped = cleaned
                                     .remove(QRegularExpression(QStringLiteral(
                                             "^\\s*(?:\\d+\\s*-\\s*\\d+|\\d+)(?:\\s*[.-]\\s*|\\s+)")))
                                     .trimmed();
    if (!stripped.isEmpty()) {
        cleaned = stripped;
    } else {
        cleaned = stem;
    }
    QStringList parts = cleaned.split(
            QRegularExpression(QStringLiteral("\\s+-\\s+")), Qt::KeepEmptyParts);
    const bool numbered = QRegularExpression(QStringLiteral("^\\d+[.]?$")).match(
            parts.first().trimmed()).hasMatch();
    if (parts.size() >= 3 && numbered) {
        parts.removeFirst();
    }
    if (parts.size() == 2 && numbered) {
        *pArtist = QString();
        *pTitle = parts.last().trimmed();
    } else if (parts.size() >= 2) {
        *pArtist = parts.takeFirst().trimmed();
        *pTitle = parts.join(QStringLiteral(" - ")).trimmed();
    } else {
        *pArtist = QString();
        *pTitle = cleaned;
    }
}

bool CrateSidecars::load() {
    m_nodes.clear();

    QSqlDatabase umap = openRo(QDir(m_dir).filePath(QStringLiteral("umap.sqlite")));
    if (!umap.isOpen()) {
        m_lastError = QStringLiteral("umap.sqlite not readable in ") + m_dir;
        return false;
    }

    QHash<QString, int> indexByRelpath;
    {
        QSqlQuery q(umap);
        if (!q.exec(QStringLiteral("SELECT relpath, x, y FROM coords"))) {
            m_lastError = QStringLiteral("umap.sqlite coords unreadable: ") +
                    q.lastError().text();
            closeAndRemove(&umap);
            return false;
        }
        while (q.next()) {
            GalaxyNode node;
            node.relpath = q.value(0).toString();
            parseTrackName(node.relpath, &node.title, &node.artist);
            node.x = q.value(1).toDouble();
            node.y = q.value(2).toDouble();
            indexByRelpath.insert(node.relpath, m_nodes.size());
            m_nodes.append(node);
        }
    }
    {
        QSqlQuery q(umap);
        q.exec(QStringLiteral("SELECT relpath, x, y, z FROM coords3d"));
        while (q.next()) {
            const auto it = indexByRelpath.constFind(q.value(0).toString());
            if (it != indexByRelpath.constEnd()) {
                GalaxyNode& node = m_nodes[it.value()];
                node.x3d = q.value(1).toDouble();
                node.y3d = q.value(2).toDouble();
                node.z = q.value(3).toDouble();
                node.has3d = true;
            }
        }
    }
    closeAndRemove(&umap);

    QSqlDatabase clusters = openRo(QDir(m_dir).filePath(QStringLiteral("clusters.sqlite")));
    if (clusters.isOpen()) {
        QSqlQuery q(clusters);
        q.exec(QStringLiteral("SELECT relpath, cluster_id FROM clusters"));
        while (q.next()) {
            const auto it = indexByRelpath.constFind(q.value(0).toString());
            if (it != indexByRelpath.constEnd()) {
                m_nodes[it.value()].clusterId = q.value(1).toInt();
            }
        }
        closeAndRemove(&clusters);
    }

    QSqlDatabase features = openRo(QDir(m_dir).filePath(QStringLiteral("features.sqlite")));
    if (features.isOpen()) {
        QSqlQuery q(features);
        q.exec(QStringLiteral("SELECT relpath, bpm, key_camelot, energy FROM features"));
        while (q.next()) {
            const auto it = indexByRelpath.constFind(q.value(0).toString());
            if (it != indexByRelpath.constEnd()) {
                GalaxyNode& node = m_nodes[it.value()];
                node.bpm = q.value(1).toDouble();
                node.keyCamelot = q.value(2).toString();
                node.energy = q.value(3).toDouble();
            }
        }
        closeAndRemove(&features);
    }

    QHash<QString, QPair<double, double>> artistPositions;
    QSqlDatabase artists = openRo(QDir(m_dir).filePath(QStringLiteral("artist_umap.sqlite")));
    if (artists.isOpen()) {
        QSqlQuery q(artists);
        q.exec(QStringLiteral("SELECT artist, x, y FROM artists"));
        while (q.next()) {
            artistPositions.insert(q.value(0).toString().trimmed().toLower(),
                    qMakePair(q.value(1).toDouble(), q.value(2).toDouble()));
        }
        closeAndRemove(&artists);
    }
    const QRegularExpression artistSeparator(QStringLiteral(
            "\\s*(?:;|,|/|&|\\bfeat\\.?\\b|\\bft\\.?\\b|\\bx\\b|\\bvs\\.?\\b)\\s*"),
            QRegularExpression::CaseInsensitiveOption);
    for (GalaxyNode& node : m_nodes) {
        const QString primary = node.artist.split(artistSeparator).value(0).trimmed().toLower();
        const auto it = artistPositions.constFind(primary);
        if (it != artistPositions.constEnd()) {
            node.artistX = it.value().first;
            node.artistY = it.value().second;
            node.hasArtistPosition = true;
        }
    }

    return true;
}

} // namespace crate
