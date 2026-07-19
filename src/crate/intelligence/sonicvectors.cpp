#include "crate/intelligence/sonicvectors.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace crate {

std::optional<SonicVectors::Vector> SonicVectors::centerAndNormalize(
        const QByteArray& blob, const std::optional<Vector>& mean) {
    if (blob.size() != static_cast<int>(sizeof(Vector))) {
        return std::nullopt;
    }
    Vector raw;
    std::memcpy(raw.data(), blob.constData(), sizeof(raw));
    std::array<double, kDimensions> centered;
    double squaredNorm = 0.0;
    for (int i = 0; i < kDimensions; ++i) {
        centered[i] = static_cast<double>(raw[i]) -
                (mean ? static_cast<double>((*mean)[i]) : 0.0);
        squaredNorm += centered[i] * centered[i];
    }
    const double norm = std::sqrt(squaredNorm);
    if (!std::isfinite(norm) || norm < 1e-9) {
        return std::nullopt;
    }
    Vector result;
    for (int i = 0; i < kDimensions; ++i) {
        result[i] = static_cast<float>(centered[i] / norm);
    }
    return result;
}

bool SonicVectors::load(const QString& vectorsPath) {
    m_whole.clear();
    m_sections.clear();
    m_lastError.clear();
    if (!QFileInfo::exists(vectorsPath)) {
        m_lastError = QStringLiteral("vector database does not exist: ") + vectorsPath;
        return false;
    }

    const QString connection = QStringLiteral("CRATE_VECTORS_") +
            QUuid::createUuid().toString(QUuid::Id128);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(vectorsPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        m_lastError = db.lastError().text();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
        return false;
    }

    std::optional<Vector> mean;
    QSqlQuery meanQuery(db);
    if (meanQuery.exec(QStringLiteral("SELECT v FROM vector_stats WHERE k='mean'")) &&
            meanQuery.next()) {
        const QByteArray blob = meanQuery.value(0).toByteArray();
        if (blob.size() == static_cast<int>(sizeof(Vector))) {
            Vector value;
            std::memcpy(value.data(), blob.constData(), sizeof(value));
            mean = value;
        }
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
                "SELECT relpath, vec, vec_intro, vec_outro FROM vectors"))) {
        m_lastError = query.lastError().text();
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
        return false;
    }
    while (query.next()) {
        const QString relpath = query.value(0).toString();
        if (auto vector = centerAndNormalize(query.value(1).toByteArray(), mean)) {
            m_whole.insert(relpath, *vector);
        }
        Sections sections;
        sections.intro = centerAndNormalize(query.value(2).toByteArray(), mean);
        sections.outro = centerAndNormalize(query.value(3).toByteArray(), mean);
        if (sections.intro || sections.outro) {
            m_sections.insert(relpath, sections);
        }
    }
    db.close();
    query = QSqlQuery();
    meanQuery = QSqlQuery();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
    return true;
}

const SonicVectors::Vector* SonicVectors::centered(const QString& relpath) const {
    const auto it = m_whole.constFind(relpath);
    return it == m_whole.constEnd() ? nullptr : &it.value();
}

std::optional<double> SonicVectors::dot(const Vector* a, const Vector* b) {
    if (!a || !b) {
        return std::nullopt;
    }
    // numpy.dot(float32, float32) accumulates and returns float32.
    float sum = 0.0f;
    for (int i = 0; i < kDimensions; ++i) {
        sum += (*a)[i] * (*b)[i];
    }
    if (!std::isfinite(sum)) {
        return std::nullopt;
    }
    return std::clamp(static_cast<double>(sum), 0.0, 1.0);
}

std::optional<double> SonicVectors::cosine(const QString& relA, const QString& relB) const {
    return dot(centered(relA), centered(relB));
}

std::optional<double> SonicVectors::transition(const QString& relA, const QString& relB) const {
    const auto a = m_sections.constFind(relA);
    const auto b = m_sections.constFind(relB);
    if (a == m_sections.constEnd() || b == m_sections.constEnd() ||
            !a->outro || !b->intro) {
        return std::nullopt;
    }
    return dot(&*a->outro, &*b->intro);
}

} // namespace crate
