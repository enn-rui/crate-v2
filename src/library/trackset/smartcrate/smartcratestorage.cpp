#include "library/trackset/smartcrate/smartcratestorage.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <utility>

namespace SmartCrate {

namespace {
const QString kVersionKey = QStringLiteral("version");
const QString kCratesKey = QStringLiteral("smart_crates");
const QString kNameKey = QStringLiteral("name");
const QString kCreatedKey = QStringLiteral("created");
const QString kSpecKey = QStringLiteral("spec");
constexpr int kVersion = 1;
} // namespace

Storage::Storage(QString filePath)
        : m_filePath(std::move(filePath)) {
}

QList<Def> Storage::loadAll() const {
    QList<Def> defs;
    QFile file(m_filePath);
    if (!file.exists()) {
        return defs; // no file yet -> no smart crates
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return defs;
    }
    const QByteArray raw = file.readAll();
    file.close();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return defs; // malformed -> treat as empty, never throw
    }
    const QJsonArray crates = doc.object().value(kCratesKey).toArray();
    for (const QJsonValue& raw2 : crates) {
        if (!raw2.isObject()) {
            continue;
        }
        const QJsonObject obj = raw2.toObject();
        const QString name = obj.value(kNameKey).toString();
        if (name.isEmpty()) {
            continue; // an entry with no name is unusable
        }
        Def def;
        def.name = name;
        def.created = static_cast<qint64>(obj.value(kCreatedKey).toDouble(0));
        // Preserve the spec object verbatim so unknown future keys survive.
        def.spec = obj.value(kSpecKey).toObject();
        defs.append(def);
    }
    return defs;
}

bool Storage::saveAll(const QList<Def>& defs) const {
    QJsonArray crates;
    for (const Def& def : defs) {
        QJsonObject obj;
        obj.insert(kNameKey, def.name);
        obj.insert(kCreatedKey, static_cast<double>(def.created));
        obj.insert(kSpecKey, def.spec);
        crates.append(obj);
    }
    QJsonObject root;
    root.insert(kVersionKey, kVersion);
    root.insert(kCratesKey, crates);

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool Storage::upsert(const QString& name, const QJsonObject& spec) {
    if (name.isEmpty()) {
        return false;
    }
    QList<Def> defs = loadAll();
    for (Def& def : defs) {
        if (def.name == name) {
            def.spec = spec; // keep original created + position
            return saveAll(defs);
        }
    }
    Def def;
    def.name = name;
    def.created = QDateTime::currentSecsSinceEpoch();
    def.spec = spec;
    defs.append(def);
    return saveAll(defs);
}

bool Storage::remove(const QString& name) {
    QList<Def> defs = loadAll();
    QList<Def> kept;
    kept.reserve(defs.size());
    for (const Def& def : defs) {
        if (def.name != name) {
            kept.append(def);
        }
    }
    return saveAll(kept);
}

} // namespace SmartCrate
