#include "crate/data/cratesidecars.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
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

bool validateSqlite(const QString& path) {
    QSqlDatabase db = openRo(path);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    const bool ok = query.exec(QStringLiteral("PRAGMA quick_check")) &&
            query.next() && query.value(0).toString() == QStringLiteral("ok");
    closeAndRemove(&db);
    return ok;
}

} // namespace

namespace crate {

CrateSidecars::CrateSidecars(const QString& dir)
        : m_dir(dir) {
}

QString CrateSidecars::snapshotPath(const QString& fileName, bool required) {
    const QFileInfo sourceInfo(QDir(m_dir).filePath(fileName));
    const QByteArray sourceKey = QDir(m_dir).absolutePath().toUtf8();
    const QString cacheKey = QString::fromLatin1(
            QCryptographicHash::hash(sourceKey, QCryptographicHash::Sha256).toHex().left(20));
    const QString cacheDir = QDir(QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation))
                                     .filePath(QStringLiteral("crate-sidecars/") + cacheKey);
    if (!QDir().mkpath(cacheDir)) {
        m_lastError = QStringLiteral("sidecar cache directory unavailable: ") + cacheDir;
        return {};
    }
    const QString cachedPath = QDir(cacheDir).filePath(fileName);
    const QFileInfo cachedInfo(cachedPath);
    if (!sourceInfo.exists()) {
        if (cachedInfo.exists() && validateSqlite(cachedPath)) {
            return cachedPath;
        }
        if (required) {
            m_lastError = fileName + QStringLiteral(" not readable in ") + m_dir;
        }
        return {};
    }
    if (cachedInfo.exists() && cachedInfo.lastModified() == sourceInfo.lastModified() &&
            validateSqlite(cachedPath)) {
        if (required) {
            m_freshSnapshotAdopted = true;
        }
        return cachedPath;
    }

    QTemporaryFile staged(QDir(cacheDir).filePath(QStringLiteral("snapshot-XXXXXX.sqlite")));
    if (!staged.open()) {
        m_lastError = QStringLiteral("unable to stage sidecar snapshot: ") + fileName;
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }
    QFile source(sourceInfo.absoluteFilePath());
    if (!source.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("unable to copy sidecar: ") + sourceInfo.absoluteFilePath();
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() || staged.write(chunk) != chunk.size()) {
            m_lastError = QStringLiteral("incomplete sidecar snapshot: ") + fileName;
            return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
        }
    }
    staged.flush();
    staged.close();
    if (!validateSqlite(staged.fileName())) {
        m_lastError = QStringLiteral("sidecar snapshot failed validation: ") + fileName;
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }

    QSaveFile destination(cachedPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("unable to update sidecar cache: ") + cachedPath;
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }
    QFile validated(staged.fileName());
    if (!validated.open(QIODevice::ReadOnly)) {
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }
    while (!validated.atEnd()) {
        const QByteArray chunk = validated.read(1024 * 1024);
        if (chunk.isEmpty() || destination.write(chunk) != chunk.size()) {
            destination.cancelWriting();
            return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
        }
    }
    if (!destination.commit()) {
        m_lastError = QStringLiteral("unable to commit sidecar cache: ") + cachedPath;
        return cachedInfo.exists() && validateSqlite(cachedPath) ? cachedPath : QString();
    }
    QFile cached(cachedPath);
    cached.open(QIODevice::ReadWrite);
    cached.setFileTime(sourceInfo.lastModified(), QFileDevice::FileModificationTime);
    cached.close();
    if (required) {
        m_freshSnapshotAdopted = true;
    }
    return cachedPath;
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
    m_freshSnapshotAdopted = false;

    const QString umapPath = snapshotPath(QStringLiteral("umap.sqlite"), true);
    QSqlDatabase umap = openRo(umapPath);
    if (!umap.isOpen()) {
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("umap.sqlite not readable in ") + m_dir;
        }
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

    QSqlDatabase clusters = openRo(snapshotPath(QStringLiteral("clusters.sqlite"), false));
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

    QSqlDatabase features = openRo(snapshotPath(QStringLiteral("features.sqlite"), false));
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
    QSqlDatabase artists = openRo(snapshotPath(QStringLiteral("artist_umap.sqlite"), false));
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
